#include "common.hpp"
#include "codegen.hpp"

#include "ast.hpp"
#include "visit.hpp"
#include "output.hpp"
#include "mangle.hpp"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Intrinsics.h"

using namespace llvm;

struct FunctionInstance
{
	Function* value;

	Ast::FnDecl* decl;

	FunctionInstance* parent;
	vector<pair<Ty*, Ty*>> generics;
};

struct Codegen
{
	Output* output;
	CodegenOptions options;

	LLVMContext* context;
	Module* module;

	IRBuilder<>* ir;
	DIBuilder* di;

	Constant* builtinTrap;
	Constant* builtinDebugTrap;
	Constant* builtinAddOverflow;
	Constant* builtinSubOverflow;
	Constant* builtinMulOverflow;

	Constant* runtimeNew;
	Constant* runtimeNewArray;

	unordered_map<Variable*, Value*> vars;

	vector<FunctionInstance*> pendingFunctions;
	FunctionInstance* currentFunction;

	vector<DIScope*> debugBlocks;
};

enum CodegenKind
{
	KindValue,
	KindRef,
};

static Ty* getGenericInstance(Codegen& cg, Ty* type)
{
	FunctionInstance* fn = cg.currentFunction;

	while (fn)
	{
		for (auto& g: fn->generics)
			if (g.first == type)
				return g.second;

		fn = fn->parent;
	}

	UNION_CASE(Generic, t, type);
	assert(t);

	ICE("Generic type %s was not instantiated", t->name.str().c_str());
}

static FunctionInstance* getFunctionInstance(Codegen& cg, Ast::FnDecl* decl)
{
	FunctionInstance* fn = cg.currentFunction;

	while (fn)
	{
		if (fn->decl == decl)
			return fn;

		fn = fn->parent;
	}

	return nullptr;
}

static Type* codegenType(Codegen& cg, Ty* type);

static Type* codegenTypeDef(Codegen& cg, Ty* type, TyDef* def)
{
	if (UNION_CASE(Struct, d, def))
	{
		string name = mangleType(type);

		if (StructType* st = cg.module->getTypeByName(name))
			return st;

		vector<Type*> fields;
		for (size_t i = 0; i < d->fields.size; ++i)
			fields.push_back(codegenType(cg, typeMember(type, i)));

		return StructType::create(*cg.context, fields, name);
	}

	ICE("Unknown TyDef kind %d", def->kind);
}

static Type* codegenType(Codegen& cg, Ty* type)
{
	if (UNION_CASE(Void, t, type))
	{
		return Type::getVoidTy(*cg.context);
	}

	if (UNION_CASE(Bool, t, type))
	{
		return Type::getInt1Ty(*cg.context);
	}

	if (UNION_CASE(Integer, t, type))
	{
		return Type::getInt32Ty(*cg.context);
	}

	if (UNION_CASE(String, t, type))
	{
		Type* fields[] = { Type::getInt8PtrTy(*cg.context), Type::getInt32Ty(*cg.context) };

		return StructType::get(*cg.context, { fields, 2 });
	}

	if (UNION_CASE(Array, t, type))
	{
		Type* element = codegenType(cg, t->element);
		Type* fields[] = { PointerType::get(element, 0), Type::getInt32Ty(*cg.context) };

		return StructType::get(*cg.context, { fields, 2 });
	}

	if (UNION_CASE(Pointer, t, type))
	{
		Type* element = codegenType(cg, t->element);

		return PointerType::get(element, 0);
	}

	if (UNION_CASE(Function, t, type))
	{
		Type* ret = codegenType(cg, t->ret);

		vector<Type*> args;

		for (auto& a: t->args)
			args.push_back(codegenType(cg, a));

		return PointerType::get(FunctionType::get(ret, args, false), 0);
	}

	if (UNION_CASE(Instance, t, type))
	{
		if (t->generic)
		{
			Ty* inst = getGenericInstance(cg, t->generic);

			return codegenType(cg, inst);
		}
		else
		{
			assert(t->def);

			return codegenTypeDef(cg, type, t->def);
		}
	}

	ICE("Unknown Ty kind %d", type->kind);
}

static Ty* finalType(Codegen& cg, Ty* type)
{
	return typeInstantiate(type, [&](Ty* ty) -> Ty* {
		return getGenericInstance(cg, ty);
	});
}

static void codegenTrapIf(Codegen& cg, Value* cond, bool debug = false)
{
	Function* func = cg.ir->GetInsertBlock()->getParent();

	BasicBlock* trapbb = BasicBlock::Create(*cg.context, "trap");
	BasicBlock* afterbb = BasicBlock::Create(*cg.context, "after");

	cg.ir->CreateCondBr(cond, trapbb, afterbb);

	func->getBasicBlockList().push_back(trapbb);
	cg.ir->SetInsertPoint(trapbb);

	cg.ir->CreateCall(debug ? cg.builtinDebugTrap : cg.builtinTrap);
	cg.ir->CreateUnreachable();

	func->getBasicBlockList().push_back(afterbb);
	cg.ir->SetInsertPoint(afterbb);
}

static Value* codegenArithOverflow(Codegen& cg, Value* left, Value* right, Constant* overflowOp)
{
	Value* result = cg.ir->CreateCall(overflowOp, { left, right });
	Value* cond = cg.ir->CreateExtractValue(result, 1);

	codegenTrapIf(cg, cond);

	return cg.ir->CreateExtractValue(result, 0);
}

static Value* codegenNew(Codegen& cg, Type* elementType)
{
	Type* pointerType = PointerType::get(elementType, 0);

	// TODO: refactor + fix int32/size_t
	Value* elementSize = cg.ir->CreateIntCast(ConstantExpr::getSizeOf(elementType), cg.ir->getInt32Ty(), false);
	Value* rawPtr = cg.ir->CreateCall(cg.runtimeNew, elementSize);
	Value* ptr = cg.ir->CreateBitCast(rawPtr, pointerType);

	return ptr;
}

static Value* codegenNewArr(Codegen& cg, Type* type, Value* count)
{
	Type* pointerType = cast<StructType>(type)->getElementType(0);
	Type* elementType = cast<PointerType>(pointerType)->getElementType();

	// TODO: refactor + fix int32/size_t
	Value* elementSize = cg.ir->CreateIntCast(ConstantExpr::getSizeOf(elementType), cg.ir->getInt32Ty(), false);
	Value* rawPtr = cg.ir->CreateCall(cg.runtimeNewArray, { count, elementSize });
	Value* ptr = cg.ir->CreateBitCast(rawPtr, pointerType);

	return ptr;
}

static Value* codegenDebugLocation(Codegen& cg, Value* value, const Location& location)
{
	if (cg.di && isa<Instruction>(value))
		cast<Instruction>(value)->setDebugLoc(DebugLoc::get(location.line + 1, location.column + 1, cg.debugBlocks.back()));

	return value;
}

static Value* codegenExpr(Codegen& cg, Ast* node, CodegenKind kind = KindValue);

static Value* codegenLiteralString(Codegen& cg, Ast::LiteralString* n)
{
	Type* type = codegenType(cg, UNION_NEW(Ty, String, {}));

	Value* result = UndefValue::get(type);

	Value* string = cg.ir->CreateGlobalStringPtr(StringRef(n->value.data, n->value.size));

	result = cg.ir->CreateInsertValue(result, string, 0);
	result = cg.ir->CreateInsertValue(result, cg.ir->getInt32(n->value.size), 1);

	return result;
}

static Value* codegenLiteralArray(Codegen& cg, Ast::LiteralArray* n)
{
	Type* type = codegenType(cg, n->type);

	Value* ptr = codegenNewArr(cg, type, cg.ir->getInt32(n->elements.size));

	for (size_t i = 0; i < n->elements.size; ++i)
	{
		Value* expr = codegenExpr(cg, n->elements[i]);

		cg.ir->CreateStore(expr, cg.ir->CreateConstInBoundsGEP1_32(type, ptr, i));
	}

	Value* result = UndefValue::get(type);

	result = cg.ir->CreateInsertValue(result, ptr, 0);
	result = cg.ir->CreateInsertValue(result, cg.ir->getInt32(n->elements.size), 1);

	return result;
}

static Value* codegenLiteralStruct(Codegen& cg, Ast::LiteralStruct* n)
{
	UNION_CASE(Instance, ti, n->type);
	assert(ti);

	UNION_CASE(Struct, td, ti->def);
	assert(td);

	Type* type = codegenType(cg, n->type);

	Value* result = UndefValue::get(type);

	vector<bool> fields(td->fields.size);

	for (auto& f: n->fields)
	{
		assert(f.first.index >= 0);

		fields[f.first.index] = true;
	}

	for (size_t i = 0; i < fields.size(); ++i)
		if (!fields[i])
		{
			assert(td->fields[i].expr);

			Value* expr = codegenExpr(cg, td->fields[i].expr);

			result = cg.ir->CreateInsertValue(result, expr, i);
		}

	for (auto& f: n->fields)
	{
		assert(f.first.index >= 0);

		Value* expr = codegenExpr(cg, f.second);

		result = cg.ir->CreateInsertValue(result, expr, f.first.index);
	}

	return result;
}

static Value* codegenFunctionDecl(Codegen& cg, Ast::FnDecl* decl, int id, Ty* ty, const Arr<Ty*>& tyargs)
{
	FunctionInstance* parent = getFunctionInstance(cg, decl->parent);

	Ty* type = finalType(cg, ty);

	Arr<Ty*> ftyargs;
	for (auto& a: tyargs)
		ftyargs.push(finalType(cg, a));

	string parentName = parent ? string(parent->value->getName()) : decl->module ? mangleModule(decl->module->name) : "";
	string name = mangleFn(decl->var->name, id, type, ftyargs, parentName);

	if (Function* fun = cg.module->getFunction(name))
		return fun;

	FunctionType* funty = cast<FunctionType>(cast<PointerType>(codegenType(cg, ty))->getElementType());
	Function* fun = Function::Create(funty, GlobalValue::InternalLinkage, name, cg.module);

	vector<pair<Ty*, Ty*>> inst;
	assert(tyargs.size == decl->tyargs.size);

	for (size_t i = 0; i < tyargs.size; ++i)
		inst.push_back(make_pair(decl->tyargs[i], ftyargs[i]));

	cg.pendingFunctions.push_back(new FunctionInstance { fun, decl, parent, inst });

	return fun;
}

static Value* codegenIdent(Codegen& cg, Ast::Ident* n, CodegenKind kind)
{
	Variable* target = n->target;

	if (target->kind == Variable::KindFunction)
	{
		UNION_CASE(FnDecl, decl, target->fn);
		assert(decl && decl->var == target);

		return codegenFunctionDecl(cg, decl, 0, n->type, n->tyargs);
	}
	else
	{
		auto it = cg.vars.find(target);
		assert(it != cg.vars.end());

		if (target->kind == Variable::KindVariable && kind != KindRef)
			return cg.ir->CreateLoad(it->second);
		else
			return it->second;
	}
}

static Value* codegenMember(Codegen& cg, Ast::Member* n, CodegenKind kind)
{
	assert(n->field.index >= 0);

	Value* expr = codegenExpr(cg, n->expr, kind);

	if (kind == KindRef)
	{
		Type* type = codegenType(cg, n->exprty);

		return cg.ir->CreateStructGEP(PointerType::get(type, 0), expr, n->field.index);
	}
	else
		return cg.ir->CreateExtractValue(expr, n->field.index);
}

static Value* codegenBlock(Codegen& cg, Ast::Block* n)
{
	Value* result = nullptr;

	for (auto& e: n->body)
		result = codegenExpr(cg, e);

	return result;
}

static Value* codegenCall(Codegen& cg, Ast::Call* n)
{
	Value* expr = codegenExpr(cg, n->expr);

	vector<Value*> args;
	for (auto& a: n->args)
		args.push_back(codegenExpr(cg, a));

	return codegenDebugLocation(cg, cg.ir->CreateCall(expr, args), n->location);
}

static Value* codegenIndex(Codegen& cg, Ast::Index* n, CodegenKind kind)
{
	Value* expr = codegenExpr(cg, n->expr);
	Value* index = codegenExpr(cg, n->index);

	Value* ptr = cg.ir->CreateExtractValue(expr, 0);
	Value* size = cg.ir->CreateExtractValue(expr, 1);

	Value* cond = cg.ir->CreateICmpUGE(index, size);

	codegenTrapIf(cg, cond);

	if (kind == KindRef)
		return cg.ir->CreateInBoundsGEP(ptr, index);
	else
		return cg.ir->CreateLoad(cg.ir->CreateInBoundsGEP(ptr, index));
}

static Value* codegenAssign(Codegen& cg, Ast::Assign* n)
{
	Value* left = codegenExpr(cg, n->left, KindRef);
	Value* right = codegenExpr(cg, n->right);

	return cg.ir->CreateStore(right, left);
}

static Value* codegenUnary(Codegen& cg, Ast::Unary* n, CodegenKind kind)
{
	Value* expr = codegenExpr(cg, n->expr);

	switch (n->op)
	{
	case UnaryOpPlus:
		return expr;

	case UnaryOpMinus:
		return cg.ir->CreateNeg(expr);

	case UnaryOpNot:
		return cg.ir->CreateNot(expr);

	case UnaryOpDeref:
		if (kind == KindRef)
			return expr;
		else
			return cg.ir->CreateLoad(expr);

	case UnaryOpNew:
	{
		Value* ptr = codegenNew(cg, expr->getType());

		cg.ir->CreateStore(expr, ptr);

		return ptr;
	}

	default:
		ICE("Unknown UnaryOp %d", n->op);
	}
}

static Value* codegenBinaryAndOr(Codegen& cg, Ast::Binary* n)
{
	Function* func = cg.ir->GetInsertBlock()->getParent();

	Value* left = codegenExpr(cg, n->left);

	BasicBlock* currentbb = cg.ir->GetInsertBlock();
	BasicBlock* nextbb = BasicBlock::Create(*cg.context, "next");
	BasicBlock* afterbb = BasicBlock::Create(*cg.context, "after");

	if (n->op == BinaryOpAnd)
		cg.ir->CreateCondBr(left, nextbb, afterbb);
	else
		cg.ir->CreateCondBr(left, afterbb, nextbb);

	func->getBasicBlockList().push_back(nextbb);
	cg.ir->SetInsertPoint(nextbb);

	Value* right = codegenExpr(cg, n->right);
	cg.ir->CreateBr(afterbb);
	nextbb = cg.ir->GetInsertBlock();

	func->getBasicBlockList().push_back(afterbb);
	cg.ir->SetInsertPoint(afterbb);

	PHINode* pn = cg.ir->CreatePHI(left->getType(), 2);

	pn->addIncoming(right, nextbb);
	pn->addIncoming(left, currentbb);

	return pn;
}

static Value* codegenBinary(Codegen& cg, Ast::Binary* n)
{
	Value* left = codegenExpr(cg, n->left);
	Value* right = codegenExpr(cg, n->right);

	switch (n->op)
	{
		case BinaryOpAddWrap: return cg.ir->CreateAdd(left, right);
		case BinaryOpSubtractWrap: return cg.ir->CreateSub(left, right);
		case BinaryOpMultiplyWrap: return cg.ir->CreateMul(left, right);
		case BinaryOpAdd: return codegenArithOverflow(cg, left, right, cg.builtinAddOverflow);
		case BinaryOpSubtract: return codegenArithOverflow(cg, left, right, cg.builtinSubOverflow);
		case BinaryOpMultiply: return codegenArithOverflow(cg, left, right, cg.builtinMulOverflow);
		case BinaryOpDivide: return cg.ir->CreateSDiv(left, right);
		case BinaryOpModulo: return cg.ir->CreateSRem(left, right);
		case BinaryOpLess: return cg.ir->CreateICmpSLT(left, right);
		case BinaryOpLessEqual: return cg.ir->CreateICmpSLE(left, right);
		case BinaryOpGreater: return cg.ir->CreateICmpSGT(left, right);
		case BinaryOpGreaterEqual: return cg.ir->CreateICmpSGE(left, right);
		case BinaryOpEqual: return cg.ir->CreateICmpEQ(left, right);
		case BinaryOpNotEqual: return cg.ir->CreateICmpNE(left, right);
		default:
			ICE("Unknown BinaryOp %d", n->op);
	}
}

static Value* codegenIf(Codegen& cg, Ast::If* n)
{
	Value* cond = codegenExpr(cg, n->cond);

	Function* func = cg.ir->GetInsertBlock()->getParent();

	if (n->elsebody)
	{
		BasicBlock* thenbb = BasicBlock::Create(*cg.context, "then");
		BasicBlock* elsebb = BasicBlock::Create(*cg.context, "else");
		BasicBlock* endbb = BasicBlock::Create(*cg.context, "ifend");

		cg.ir->CreateCondBr(cond, thenbb, elsebb);

		func->getBasicBlockList().push_back(thenbb);
		cg.ir->SetInsertPoint(thenbb);

		Value* thenbody = codegenExpr(cg, n->thenbody);
		cg.ir->CreateBr(endbb);
		thenbb = cg.ir->GetInsertBlock();

		func->getBasicBlockList().push_back(elsebb);
		cg.ir->SetInsertPoint(elsebb);

		Value* elsebody = codegenExpr(cg, n->elsebody);
		cg.ir->CreateBr(endbb);
		elsebb = cg.ir->GetInsertBlock();

		func->getBasicBlockList().push_back(endbb);
		cg.ir->SetInsertPoint(endbb);

		if (thenbody->getType()->isVoidTy())
		{
			return nullptr;
		}
		else
		{
			PHINode* pn = cg.ir->CreatePHI(thenbody->getType(), 2);

			pn->addIncoming(thenbody, thenbb);
			pn->addIncoming(elsebody, elsebb);

			return pn;
		}
	}
	else
	{
		BasicBlock* thenbb = BasicBlock::Create(*cg.context, "then");
		BasicBlock* endbb = BasicBlock::Create(*cg.context, "ifend");

		cg.ir->CreateCondBr(cond, thenbb, endbb);

		func->getBasicBlockList().push_back(thenbb);
		cg.ir->SetInsertPoint(thenbb);

		Value* thenbody = codegenExpr(cg, n->thenbody);
		cg.ir->CreateBr(endbb);

		func->getBasicBlockList().push_back(endbb);
		cg.ir->SetInsertPoint(endbb);

		return nullptr;
	}
}

static Value* codegenFor(Codegen& cg, Ast::For* n)
{
	Function* func = cg.ir->GetInsertBlock()->getParent();

	BasicBlock* entrybb = cg.ir->GetInsertBlock();
	BasicBlock* loopbb = BasicBlock::Create(*cg.context, "loop");
	BasicBlock* endbb = BasicBlock::Create(*cg.context, "forend");

	Value* expr = codegenExpr(cg, n->expr);

	Value* size = cg.ir->CreateExtractValue(expr, 1);

	cg.ir->CreateCondBr(cg.ir->CreateICmpSGT(size, cg.ir->getInt32(0)), loopbb, endbb);

	func->getBasicBlockList().push_back(loopbb);
	cg.ir->SetInsertPoint(loopbb);

	PHINode* index = cg.ir->CreatePHI(Type::getInt32Ty(*cg.context), 2);

	index->addIncoming(cg.ir->getInt32(0), entrybb);

	Value* var = cg.ir->CreateInBoundsGEP(cg.ir->CreateExtractValue(expr, 0), index);

	cg.vars[n->var] = var;

	if (n->index)
		cg.vars[n->index] = index;

	codegenExpr(cg, n->body);

	Value* next = cg.ir->CreateAdd(index, cg.ir->getInt32(1));

	BasicBlock* loopendbb = cg.ir->GetInsertBlock();

	cg.ir->CreateCondBr(cg.ir->CreateICmpSLT(next, size), loopbb, endbb);

	index->addIncoming(next, loopendbb);

	func->getBasicBlockList().push_back(endbb);
	cg.ir->SetInsertPoint(endbb);

	return nullptr;
}

static Value* codegenWhile(Codegen& cg, Ast::While* n)
{
	Function* func = cg.ir->GetInsertBlock()->getParent();

	BasicBlock* entrybb = cg.ir->GetInsertBlock();
	BasicBlock* loopbb = BasicBlock::Create(*cg.context, "loop");
	BasicBlock* bodybb = BasicBlock::Create(*cg.context, "whilebody");
	BasicBlock* endbb = BasicBlock::Create(*cg.context, "whileend");

	cg.ir->CreateBr(loopbb);

	func->getBasicBlockList().push_back(loopbb);
	cg.ir->SetInsertPoint(loopbb);

	Value* expr = codegenExpr(cg, n->expr);

	cg.ir->CreateCondBr(expr, bodybb, endbb);

	func->getBasicBlockList().push_back(bodybb);
	cg.ir->SetInsertPoint(bodybb);

	codegenExpr(cg, n->body);

	cg.ir->CreateBr(loopbb);

	func->getBasicBlockList().push_back(endbb);
	cg.ir->SetInsertPoint(endbb);

	return nullptr;
}

static Value* codegenFn(Codegen& cg, Ast::Fn* n)
{
	UNION_CASE(FnDecl, decl, n->decl);

	return codegenFunctionDecl(cg, decl, n->id, decl->var->type, Arr<Ty*>());
}

static Value* codegenVarDecl(Codegen& cg, Ast::VarDecl* n)
{
	Value* expr = codegenExpr(cg, n->expr);

	Value* storage = cg.ir->CreateAlloca(expr->getType());
	cg.ir->CreateStore(expr, storage);

	cg.vars[n->var] = storage;

	return storage;
}

static Value* codegenExpr(Codegen& cg, Ast* node, CodegenKind kind)
{
	if (UNION_CASE(LiteralBool, n, node))
		return cg.ir->getInt1(n->value);

	if (UNION_CASE(LiteralNumber, n, node))
		return cg.ir->getInt32(atoi(n->value.str().c_str()));

	if (UNION_CASE(LiteralString, n, node))
		return codegenLiteralString(cg, n);

	if (UNION_CASE(LiteralArray, n, node))
		return codegenLiteralArray(cg, n);

	if (UNION_CASE(LiteralStruct, n, node))
		return codegenLiteralStruct(cg, n);

	if (UNION_CASE(Ident, n, node))
		return codegenIdent(cg, n, kind);

	if (UNION_CASE(Member, n, node))
		return codegenMember(cg, n, kind);

	if (UNION_CASE(Block, n, node))
		return codegenBlock(cg, n);

	if (UNION_CASE(Module, n, node))
		return codegenExpr(cg, n->body, kind);

	if (UNION_CASE(Call, n, node))
		return codegenCall(cg, n);

	if (UNION_CASE(Index, n, node))
		return codegenIndex(cg, n, kind);

	if (UNION_CASE(Assign, n, node))
		return codegenAssign(cg, n);

	if (UNION_CASE(Unary, n, node))
		return codegenUnary(cg, n, kind);

	if (UNION_CASE(Binary, n, node))
	{
		if (n->op == BinaryOpAnd || n->op == BinaryOpOr)
			return codegenBinaryAndOr(cg, n);
		else
			return codegenBinary(cg, n);
	}

	if (UNION_CASE(If, n, node))
		return codegenIf(cg, n);

	if (UNION_CASE(For, n, node))
		return codegenFor(cg, n);

	if (UNION_CASE(While, n, node))
		return codegenWhile(cg, n);

	if (UNION_CASE(Fn, n, node))
		return codegenFn(cg, n);

	if (UNION_CASE(FnDecl, n, node))
		return nullptr;

	if (UNION_CASE(VarDecl, n, node))
		return codegenVarDecl(cg, n);

	if (UNION_CASE(TyDecl, n, node))
		return nullptr;

	ICE("Unknown Ast kind %d", node->kind);
}

static void codegenFunctionExtern(Codegen& cg, const FunctionInstance& inst)
{
	assert(!inst.decl->body);

	Constant* external = cg.module->getOrInsertFunction(inst.decl->var->name.str(), inst.value->getFunctionType());

	vector<Value*> args;

	for (Function::arg_iterator ait = inst.value->arg_begin(); ait != inst.value->arg_end(); ++ait)
		args.push_back(ait);

	BasicBlock* bb = BasicBlock::Create(*cg.context, "entry", inst.value);
	cg.ir->SetInsertPoint(bb);

	Value* ret = cg.ir->CreateCall(external, args);

	if (ret->getType()->isVoidTy())
		cg.ir->CreateRetVoid();
	else
		cg.ir->CreateRet(ret);
}

static void codegenFunctionBuiltin(Codegen& cg, const FunctionInstance& inst)
{
	assert(!inst.decl->body);

	BasicBlock* bb = BasicBlock::Create(*cg.context, "entry", inst.value);
	cg.ir->SetInsertPoint(bb);

	Str name = inst.decl->var->name;

	if (name == "sizeof" && inst.generics.size() == 1 && inst.value->arg_size() == 0)
	{
		Type* type = codegenType(cg, inst.generics[0].second);
		Value* ret = cg.ir->CreateIntCast(ConstantExpr::getSizeOf(type), cg.ir->getInt32Ty(), false);

		cg.ir->CreateRet(ret);
	}
	else if (name == "newarr" && inst.generics.size() == 1 && inst.value->arg_size() == 1)
	{
		Type* type = codegenType(cg, UNION_NEW(Ty, Array, { inst.generics[0].second }));

		Value* count = inst.value->arg_begin();

		Value* ret = UndefValue::get(type);

		ret = cg.ir->CreateInsertValue(ret, codegenNewArr(cg, type, count), 0);
		ret = cg.ir->CreateInsertValue(ret, count, 1);

		cg.ir->CreateRet(ret);
	}
	else if (name == "length" && inst.generics.size() == 1 && inst.value->arg_size() == 1)
	{
		Value* array = inst.value->arg_begin();
		Value* ret = cg.ir->CreateExtractValue(array, 1);

		cg.ir->CreateRet(ret);
	}
	else if (name == "assert" && inst.value->arg_size() == 1)
	{
		Value* expr = inst.value->arg_begin();

		codegenTrapIf(cg, cg.ir->CreateNot(expr), /* debug= */ true);

		cg.ir->CreateRetVoid();
	}
	else
		// TODO Location
		cg.output->panic(Location(), "Unknown builtin function %s", name.str().c_str());
}

static void codegenFunctionImpl(Codegen& cg, const FunctionInstance& inst)
{
	assert(inst.decl->body);

	size_t argindex = 0;

	for (Function::arg_iterator ait = inst.value->arg_begin(); ait != inst.value->arg_end(); ++ait, ++argindex)
		cg.vars[inst.decl->args[argindex]] = ait;

	BasicBlock* bb = BasicBlock::Create(*cg.context, "entry", inst.value);
	cg.ir->SetInsertPoint(bb);

	Value* ret = codegenExpr(cg, inst.decl->body);

	if (!inst.value->getFunctionType()->getReturnType()->isVoidTy())
		cg.ir->CreateRet(ret);
	else
		cg.ir->CreateRetVoid();
}

static void codegenFunction(Codegen& cg, const FunctionInstance& inst)
{
	assert(inst.value->empty());

	if (inst.decl->var && cg.di)
	{
		const Location& loc = inst.decl->var->location;

		auto file = cg.di->createFile(loc.source, StringRef());

		auto fty = cg.di->createSubroutineType(file, cg.di->getOrCreateTypeArray({}));

		auto func = cg.di->createFunction(
			cg.debugBlocks.back(), StringRef(), inst.value->getName(), file, loc.line + 1, fty,
			/* isLocalToUnit= */ false, /* isDefinition= */ true, loc.line + 1,
			DINode::FlagPrototyped, /* isOptimized= */ false, inst.value);

		cg.debugBlocks.push_back(func);
	}

	if (inst.decl->attributes & FnAttributeExtern)
		codegenFunctionExtern(cg, inst);
	else if (inst.decl->attributes & FnAttributeBuiltin)
		codegenFunctionBuiltin(cg, inst);
	else
		codegenFunctionImpl(cg, inst);

	if (inst.decl->var && cg.di)
		cg.debugBlocks.pop_back();
}

static void codegenPrepare(Codegen& cg)
{
	cg.builtinTrap = Intrinsic::getDeclaration(cg.module, Intrinsic::trap);
	cg.builtinDebugTrap = Intrinsic::getDeclaration(cg.module, Intrinsic::debugtrap);

	cg.builtinAddOverflow = Intrinsic::getDeclaration(cg.module, Intrinsic::sadd_with_overflow, Type::getInt32Ty(*cg.context));
	cg.builtinSubOverflow = Intrinsic::getDeclaration(cg.module, Intrinsic::ssub_with_overflow, Type::getInt32Ty(*cg.context));
	cg.builtinMulOverflow = Intrinsic::getDeclaration(cg.module, Intrinsic::smul_with_overflow, Type::getInt32Ty(*cg.context));

	cg.runtimeNew = cg.module->getOrInsertFunction("aikeNew", Type::getInt8PtrTy(*cg.context), Type::getInt32Ty(*cg.context), nullptr);
	cg.runtimeNewArray = cg.module->getOrInsertFunction("aikeNewArray", Type::getInt8PtrTy(*cg.context), Type::getInt32Ty(*cg.context), Type::getInt32Ty(*cg.context), nullptr);
}

llvm::Value* codegen(Output& output, Ast* root, llvm::Module* module, const CodegenOptions& options)
{
	llvm::LLVMContext* context = &module->getContext();

	IRBuilder<> ir(*context);
	DIBuilder di(*module);

	Codegen cg = { &output, options, context, module, &ir, options.debugInfo ? &di : nullptr };

	codegenPrepare(cg);

	if (cg.di)
	{
		auto cu = cg.di->createCompileUnit(dwarf::DW_LANG_C, "?", ".", "aikec", false, "", 0);
		cg.debugBlocks.push_back(cu);
	}

	FunctionType* entryType = FunctionType::get(Type::getVoidTy(*cg.context), false);
	Function* entry = Function::Create(entryType, GlobalValue::InternalLinkage, "entry", module);

	Ast::FnDecl* entryDecl = new Ast::FnDecl { nullptr, Arr<Ty*>(), Arr<Variable*>(), 0, root };

	cg.pendingFunctions.push_back(new FunctionInstance { entry, entryDecl });

	while (!cg.pendingFunctions.empty())
	{
		FunctionInstance* inst = cg.pendingFunctions.back();

		cg.pendingFunctions.pop_back();

		cg.currentFunction = inst;

		codegenFunction(cg, *inst);

		cg.currentFunction = nullptr;
	}

	if (cg.di)
		di.finalize();

	return entry;
}

void codegenMain(llvm::Module* module, const vector<llvm::Value*>& entries)
{
	LLVMContext& context = module->getContext();
	IRBuilder<> ir(context);

	Function* main = cast<Function>(module->getOrInsertFunction("aikeMain", Type::getVoidTy(context), nullptr));

	BasicBlock* mainbb = BasicBlock::Create(context, "entry", main);
	ir.SetInsertPoint(mainbb);

	for (auto& e: entries)
		ir.CreateCall(e);

	ir.CreateRetVoid();

	Function* entry = cast<Function>(module->getOrInsertFunction("main", Type::getInt32Ty(context), nullptr));

	BasicBlock* entrybb = BasicBlock::Create(context, "entry", entry);
	ir.SetInsertPoint(entrybb);

	Constant* runtimeEntry = module->getOrInsertFunction("aikeEntry", Type::getInt32Ty(context), main->getType(), nullptr);

	Value* ret = ir.CreateCall(runtimeEntry, main);

	ir.CreateRet(ret);
}