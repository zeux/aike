#include "common.hpp"
#include "codegen.hpp"

#include "ast.hpp"
#include "visit.hpp"
#include "output.hpp"
#include "mangle.hpp"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Intrinsics.h"

using namespace llvm;

struct FunctionInstance
{
	Function* value;

	Arr<Variable*> args;
	Ast* body;

	unsigned int attributes;

	Str external;

	FunctionInstance* parent;
	vector<pair<Ty*, Ty*>> generics;
};

struct Codegen
{
	Output* output;
	LLVMContext* context;
	Module* module;

	IRBuilder<>* builder;

	Constant* builtinTrap;
	Constant* builtinAddOverflow;
	Constant* builtinSubOverflow;
	Constant* builtinMulOverflow;

	Constant* runtimeNew;
	Constant* runtimeNewArr;

	unordered_map<Variable*, Value*> vars;

	vector<FunctionInstance*> pendingFunctions;
	FunctionInstance* currentFunction;
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

static pair<bool, Function*> codegenFunctionDecl(Codegen& cg, const string& name, Ty* type)
{
	if (Function* fun = cg.module->getFunction(name))
		return make_pair(false, fun);

	FunctionType* funty = cast<FunctionType>(cast<PointerType>(codegenType(cg, type))->getElementType());

	return make_pair(true, Function::Create(funty, GlobalValue::InternalLinkage, name, cg.module));
}

static void codegenTrapIf(Codegen& cg, Value* cond)
{
	Function* func = cg.builder->GetInsertBlock()->getParent();

	BasicBlock* trapbb = BasicBlock::Create(*cg.context, "trap");
	BasicBlock* afterbb = BasicBlock::Create(*cg.context, "after");

	cg.builder->CreateCondBr(cond, trapbb, afterbb);

	func->getBasicBlockList().push_back(trapbb);
	cg.builder->SetInsertPoint(trapbb);

	cg.builder->CreateCall(cg.builtinTrap);
	cg.builder->CreateUnreachable();

	func->getBasicBlockList().push_back(afterbb);
	cg.builder->SetInsertPoint(afterbb);
}

static Value* codegenArithOverflow(Codegen& cg, Value* left, Value* right, Constant* overflowOp)
{
	Value* result = cg.builder->CreateCall2(overflowOp, left, right);
	Value* cond = cg.builder->CreateExtractValue(result, 1);

	codegenTrapIf(cg, cond);

	return cg.builder->CreateExtractValue(result, 0);
}

static Value* codegenNewArr(Codegen& cg, Type* type, Value* count)
{
	Type* pointerType = cast<StructType>(type)->getElementType(0);
	Type* elementType = cast<PointerType>(pointerType)->getElementType();

	// TODO: refactor + fix int32/size_t
	Value* elementSize = cg.builder->CreateIntCast(ConstantExpr::getSizeOf(elementType), cg.builder->getInt32Ty(), false);
	Value* rawPtr = cg.builder->CreateCall2(cg.runtimeNewArr, count, elementSize);
	Value* ptr = cg.builder->CreateBitCast(rawPtr, pointerType);

	return ptr;
}

static Value* codegenExpr(Codegen& cg, Ast* node, CodegenKind kind = KindValue);

static Value* codegenLiteralString(Codegen& cg, Ast::LiteralString* n)
{
	Type* type = codegenType(cg, UNION_NEW(Ty, String, {}));

	Value* result = UndefValue::get(type);

	Value* string = cg.builder->CreateGlobalStringPtr(StringRef(n->value.data, n->value.size));

	result = cg.builder->CreateInsertValue(result, string, 0);
	result = cg.builder->CreateInsertValue(result, cg.builder->getInt32(n->value.size), 1);

	return result;
}

static Value* codegenLiteralArray(Codegen& cg, Ast::LiteralArray* n)
{
	Type* type = codegenType(cg, n->type);

	Value* ptr = codegenNewArr(cg, type, cg.builder->getInt32(n->elements.size));

	for (size_t i = 0; i < n->elements.size; ++i)
	{
		Value* expr = codegenExpr(cg, n->elements[i]);

		cg.builder->CreateStore(expr, cg.builder->CreateConstInBoundsGEP1_32(ptr, i));
	}

	Value* result = UndefValue::get(type);

	result = cg.builder->CreateInsertValue(result, ptr, 0);
	result = cg.builder->CreateInsertValue(result, cg.builder->getInt32(n->elements.size), 1);

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

			result = cg.builder->CreateInsertValue(result, expr, i);
		}

	for (auto& f: n->fields)
	{
		assert(f.first.index >= 0);

		Value* expr = codegenExpr(cg, f.second);

		result = cg.builder->CreateInsertValue(result, expr, f.first.index);
	}

	return result;
}

static Value* codegenIdent(Codegen& cg, Ast::Ident* n, CodegenKind kind)
{
	Variable* target = n->target;

	if (target->kind == Variable::KindFunction)
	{
		UNION_CASE(FnDecl, decl, target->fn);
		assert(decl && decl->var == target);

		Ty* type = finalType(cg, n->type);

		Arr<Ty*> tyargs;
		for (auto& a: n->tyargs)
			tyargs.push(finalType(cg, a));

		auto fd = codegenFunctionDecl(cg, mangle(mangleFn(target->name, type, tyargs)), type);

		if (fd.first)
		{
			vector<pair<Ty*, Ty*>> inst;
			assert(n->tyargs.size == decl->tyargs.size);

			for (size_t i = 0; i < n->tyargs.size; ++i)
				inst.push_back(make_pair(decl->tyargs[i], tyargs[i]));

			// TODO: parent=current is wrong: need to pick lexical parent
			cg.pendingFunctions.push_back(new FunctionInstance { fd.second, decl->args, decl->body, decl->attributes, decl->var->name, cg.currentFunction, inst });
		}

		return fd.second;
	}
	else
	{
		auto it = cg.vars.find(target);
		assert(it != cg.vars.end());

		if (target->kind == Variable::KindVariable && kind != KindRef)
			return cg.builder->CreateLoad(it->second);
		else
			return it->second;
	}
}

static Value* codegenMember(Codegen& cg, Ast::Member* n, CodegenKind kind)
{
	assert(n->field.index >= 0);

	Value* expr = codegenExpr(cg, n->expr, kind);

	if (kind == KindRef)
		return cg.builder->CreateStructGEP(expr, n->field.index);
	else
		return cg.builder->CreateExtractValue(expr, n->field.index);
}

static Value* codegenBlock(Codegen& cg, Ast::Block* n)
{
	if (n->body.size == 0)
		return nullptr;

	for (size_t i = 0; i < n->body.size - 1; ++i)
		codegenExpr(cg, n->body[i]);

	return codegenExpr(cg, n->body[n->body.size - 1]);

}

static Value* codegenCall(Codegen& cg, Ast::Call* n)
{
	Value* expr = codegenExpr(cg, n->expr);

	vector<Value*> args;
	for (auto& a: n->args)
		args.push_back(codegenExpr(cg, a));

	return cg.builder->CreateCall(expr, args);
}

static Value* codegenIndex(Codegen& cg, Ast::Index* n, CodegenKind kind)
{
	Value* expr = codegenExpr(cg, n->expr);
	Value* index = codegenExpr(cg, n->index);

	Value* ptr = cg.builder->CreateExtractValue(expr, 0);
	Value* size = cg.builder->CreateExtractValue(expr, 1);

	Value* cond = cg.builder->CreateICmpUGE(index, size);

	codegenTrapIf(cg, cond);

	if (kind == KindRef)
		return cg.builder->CreateInBoundsGEP(ptr, index);
	else
		return cg.builder->CreateLoad(cg.builder->CreateInBoundsGEP(ptr, index));
}

static Value* codegenAssign(Codegen& cg, Ast::Assign* n)
{
	Value* left = codegenExpr(cg, n->left, KindRef);
	Value* right = codegenExpr(cg, n->right);

	return cg.builder->CreateStore(right, left);
}

static Value* codegenUnary(Codegen& cg, Ast::Unary* n)
{
	Value* expr = codegenExpr(cg, n->expr);

	switch (n->op)
	{
	case UnaryOpPlus:
		return expr;

	case UnaryOpMinus:
		return cg.builder->CreateNeg(expr);

	case UnaryOpNot:
		return cg.builder->CreateNot(expr);

	case UnaryOpSize:
		return cg.builder->CreateExtractValue(expr, 1);

	default:
		ICE("Unknown UnaryOp %d", n->op);
	}
}

static Value* codegenBinaryAndOr(Codegen& cg, Ast::Binary* n)
{
	Function* func = cg.builder->GetInsertBlock()->getParent();

	Value* left = codegenExpr(cg, n->left);

	BasicBlock* currentbb = cg.builder->GetInsertBlock();
	BasicBlock* nextbb = BasicBlock::Create(*cg.context, "next");
	BasicBlock* afterbb = BasicBlock::Create(*cg.context, "after");

	if (n->op == BinaryOpAnd)
		cg.builder->CreateCondBr(left, nextbb, afterbb);
	else
		cg.builder->CreateCondBr(left, afterbb, nextbb);

	func->getBasicBlockList().push_back(nextbb);
	cg.builder->SetInsertPoint(nextbb);

	Value* right = codegenExpr(cg, n->right);
	cg.builder->CreateBr(afterbb);
	nextbb = cg.builder->GetInsertBlock();

	func->getBasicBlockList().push_back(afterbb);
	cg.builder->SetInsertPoint(afterbb);

	PHINode* pn = cg.builder->CreatePHI(left->getType(), 2);

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
		case BinaryOpAddWrap: return cg.builder->CreateAdd(left, right);
		case BinaryOpSubtractWrap: return cg.builder->CreateSub(left, right);
		case BinaryOpMultiplyWrap: return cg.builder->CreateMul(left, right);
		case BinaryOpAdd: return codegenArithOverflow(cg, left, right, cg.builtinAddOverflow);
		case BinaryOpSubtract: return codegenArithOverflow(cg, left, right, cg.builtinSubOverflow);
		case BinaryOpMultiply: return codegenArithOverflow(cg, left, right, cg.builtinMulOverflow);
		case BinaryOpDivide: return cg.builder->CreateSDiv(left, right);
		case BinaryOpModulo: return cg.builder->CreateSRem(left, right);
		case BinaryOpLess: return cg.builder->CreateICmpSLT(left, right);
		case BinaryOpLessEqual: return cg.builder->CreateICmpSLE(left, right);
		case BinaryOpGreater: return cg.builder->CreateICmpSGT(left, right);
		case BinaryOpGreaterEqual: return cg.builder->CreateICmpSGE(left, right);
		case BinaryOpEqual: return cg.builder->CreateICmpEQ(left, right);
		case BinaryOpNotEqual: return cg.builder->CreateICmpNE(left, right);
		default:
			ICE("Unknown BinaryOp %d", n->op);
	}
}

static Value* codegenIf(Codegen& cg, Ast::If* n)
{
	Value* cond = codegenExpr(cg, n->cond);

	Function* func = cg.builder->GetInsertBlock()->getParent();

	if (n->elsebody)
	{
		BasicBlock* thenbb = BasicBlock::Create(*cg.context, "then");
		BasicBlock* elsebb = BasicBlock::Create(*cg.context, "else");
		BasicBlock* endbb = BasicBlock::Create(*cg.context, "ifend");

		cg.builder->CreateCondBr(cond, thenbb, elsebb);

		func->getBasicBlockList().push_back(thenbb);
		cg.builder->SetInsertPoint(thenbb);

		Value* thenbody = codegenExpr(cg, n->thenbody);
		cg.builder->CreateBr(endbb);
		thenbb = cg.builder->GetInsertBlock();

		func->getBasicBlockList().push_back(elsebb);
		cg.builder->SetInsertPoint(elsebb);

		Value* elsebody = codegenExpr(cg, n->elsebody);
		cg.builder->CreateBr(endbb);
		elsebb = cg.builder->GetInsertBlock();

		func->getBasicBlockList().push_back(endbb);
		cg.builder->SetInsertPoint(endbb);

		if (thenbody->getType()->isVoidTy())
		{
			return nullptr;
		}
		else
		{
			PHINode* pn = cg.builder->CreatePHI(thenbody->getType(), 2);

			pn->addIncoming(thenbody, thenbb);
			pn->addIncoming(elsebody, elsebb);

			return pn;
		}
	}
	else
	{
		BasicBlock* thenbb = BasicBlock::Create(*cg.context, "then");
		BasicBlock* endbb = BasicBlock::Create(*cg.context, "ifend");

		cg.builder->CreateCondBr(cond, thenbb, endbb);

		func->getBasicBlockList().push_back(thenbb);
		cg.builder->SetInsertPoint(thenbb);

		Value* thenbody = codegenExpr(cg, n->thenbody);
		cg.builder->CreateBr(endbb);

		func->getBasicBlockList().push_back(endbb);
		cg.builder->SetInsertPoint(endbb);

		return nullptr;
	}
}

static Value* codegenFor(Codegen& cg, Ast::For* n)
{
	Function* func = cg.builder->GetInsertBlock()->getParent();

	BasicBlock* entrybb = cg.builder->GetInsertBlock();
	BasicBlock* loopbb = BasicBlock::Create(*cg.context, "loop");
	BasicBlock* endbb = BasicBlock::Create(*cg.context, "forend");

	Value* expr = codegenExpr(cg, n->expr);

	Value* ptr = cg.builder->CreateExtractValue(expr, 0);
	Value* size = cg.builder->CreateExtractValue(expr, 1);

	cg.builder->CreateCondBr(cg.builder->CreateICmpSGT(size, cg.builder->getInt32(0)), loopbb, endbb);

	func->getBasicBlockList().push_back(loopbb);
	cg.builder->SetInsertPoint(loopbb);

	PHINode* index = cg.builder->CreatePHI(Type::getInt32Ty(*cg.context), 2);

	index->addIncoming(cg.builder->getInt32(0), entrybb);

	Value* var = cg.builder->CreateInBoundsGEP(ptr, index);

	cg.vars[n->var] = var;

	if (n->index)
		cg.vars[n->index] = index;

	codegenExpr(cg, n->body);

	Value* next = cg.builder->CreateAdd(index, cg.builder->getInt32(1));

	BasicBlock* loopendbb = cg.builder->GetInsertBlock();

	cg.builder->CreateCondBr(cg.builder->CreateICmpSLT(next, size), loopbb, endbb);

	index->addIncoming(next, loopendbb);

	func->getBasicBlockList().push_back(endbb);
	cg.builder->SetInsertPoint(endbb);

	return nullptr;
}

static Value* codegenFn(Codegen& cg, Ast::Fn* n)
{
	Ty* type = finalType(cg, n->type);

	auto fd = codegenFunctionDecl(cg, mangle(mangleFn(n->id, type)), type);
	assert(fd.first);

	cg.pendingFunctions.push_back(new FunctionInstance { fd.second, n->args, n->body, 0, Str(), cg.currentFunction });

	return fd.second;
}

static Value* codegenVarDecl(Codegen& cg, Ast::VarDecl* n)
{
	Value* expr = codegenExpr(cg, n->expr);

	Value* storage = cg.builder->CreateAlloca(expr->getType());
	cg.builder->CreateStore(expr, storage);

	cg.vars[n->var] = storage;

	return storage;
}

static Value* codegenExpr(Codegen& cg, Ast* node, CodegenKind kind)
{
	if (UNION_CASE(LiteralBool, n, node))
		return cg.builder->getInt1(n->value);

	if (UNION_CASE(LiteralNumber, n, node))
		return cg.builder->getInt32(atoi(n->value.str().c_str()));

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

	if (UNION_CASE(Call, n, node))
		return codegenCall(cg, n);

	if (UNION_CASE(Index, n, node))
		return codegenIndex(cg, n, kind);

	if (UNION_CASE(Assign, n, node))
		return codegenAssign(cg, n);

	if (UNION_CASE(Unary, n, node))
		return codegenUnary(cg, n);

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
	assert(!inst.body);
	assert(inst.external.size > 0);

	Constant* external = cg.module->getOrInsertFunction(inst.external.str(), inst.value->getFunctionType());

	vector<Value*> args;

	for (Function::arg_iterator ait = inst.value->arg_begin(); ait != inst.value->arg_end(); ++ait)
		args.push_back(ait);

	BasicBlock* bb = BasicBlock::Create(*cg.context, "entry", inst.value);
	cg.builder->SetInsertPoint(bb);

	Value* ret = cg.builder->CreateCall(external, args);

	if (ret->getType()->isVoidTy())
		cg.builder->CreateRetVoid();
	else
		cg.builder->CreateRet(ret);
}

static void codegenFunctionBuiltin(Codegen& cg, const FunctionInstance& inst)
{
	assert(!inst.body);
	assert(inst.external.size > 0);

	BasicBlock* bb = BasicBlock::Create(*cg.context, "entry", inst.value);
	cg.builder->SetInsertPoint(bb);

	if (inst.external == "sizeof" && inst.generics.size() == 1 && inst.value->arg_size() == 0)
	{
		Type* type = codegenType(cg, inst.generics[0].second);
		Value* ret = cg.builder->CreateIntCast(ConstantExpr::getSizeOf(type), cg.builder->getInt32Ty(), false);

		cg.builder->CreateRet(ret);
	}
	else if (inst.external == "newarr" && inst.generics.size() == 1 && inst.value->arg_size() == 1)
	{
		Type* type = codegenType(cg, UNION_NEW(Ty, Array, { inst.generics[0].second }));

		Value* count = inst.value->arg_begin();

		Value* ret = UndefValue::get(type);

		ret = cg.builder->CreateInsertValue(ret, codegenNewArr(cg, type, count), 0);
		ret = cg.builder->CreateInsertValue(ret, count, 1);

		cg.builder->CreateRet(ret);
	}
	else
		// TODO Location
		cg.output->panic(Location(), "Unknown builtin function %s", inst.external.str().c_str());
}

static void codegenFunction(Codegen& cg, const FunctionInstance& inst)
{
	assert(inst.value->empty());

	if (inst.attributes & FnAttributeExtern)
		return codegenFunctionExtern(cg, inst);

	if (inst.attributes & FnAttributeBuiltin)
		return codegenFunctionBuiltin(cg, inst);

	assert(inst.body);

	size_t argindex = 0;

	for (Function::arg_iterator ait = inst.value->arg_begin(); ait != inst.value->arg_end(); ++ait, ++argindex)
		cg.vars[inst.args[argindex]] = ait;

	BasicBlock* bb = BasicBlock::Create(*cg.context, "entry", inst.value);
	cg.builder->SetInsertPoint(bb);

	Value* ret = codegenExpr(cg, inst.body);

	if (!inst.value->getFunctionType()->getReturnType()->isVoidTy())
		cg.builder->CreateRet(ret);
	else
		cg.builder->CreateRetVoid();
}

static void codegenPrepare(Codegen& cg)
{
	cg.builtinTrap = Intrinsic::getDeclaration(cg.module, Intrinsic::trap);

	cg.builtinAddOverflow = Intrinsic::getDeclaration(cg.module, Intrinsic::sadd_with_overflow, Type::getInt32Ty(*cg.context));
	cg.builtinSubOverflow = Intrinsic::getDeclaration(cg.module, Intrinsic::ssub_with_overflow, Type::getInt32Ty(*cg.context));
	cg.builtinMulOverflow = Intrinsic::getDeclaration(cg.module, Intrinsic::smul_with_overflow, Type::getInt32Ty(*cg.context));

	cg.runtimeNew = cg.module->getOrInsertFunction("aike_new", Type::getInt8PtrTy(*cg.context), Type::getInt32Ty(*cg.context), nullptr);
	cg.runtimeNewArr = cg.module->getOrInsertFunction("aike_newarr", Type::getInt8PtrTy(*cg.context), Type::getInt32Ty(*cg.context), Type::getInt32Ty(*cg.context), nullptr);
}

llvm::Value* codegen(Output& output, Ast* root, llvm::Module* module)
{
	llvm::LLVMContext* context = &module->getContext();

	IRBuilder<> builder(*context);

	Codegen cg = { &output, context, module, &builder };

	codegenPrepare(cg);

	FunctionType* entryType = FunctionType::get(Type::getVoidTy(*cg.context), false);
	Function* entry = Function::Create(entryType, GlobalValue::InternalLinkage, "entry", module);

	cg.pendingFunctions.push_back(new FunctionInstance { entry, Arr<Variable*>(), root });

	while (!cg.pendingFunctions.empty())
	{
		FunctionInstance* inst = cg.pendingFunctions.back();

		cg.pendingFunctions.pop_back();

		cg.currentFunction = inst;

		codegenFunction(cg, *inst);

		cg.currentFunction = nullptr;
	}

	return entry;
}

void codegenMain(llvm::Module* module, const vector<llvm::Value*>& entries)
{
	llvm::LLVMContext& context = module->getContext();
	IRBuilder<> builder(context);

	Function* main = cast<Function>(module->getOrInsertFunction("main", Type::getInt32Ty(context), nullptr));

	BasicBlock* bb = BasicBlock::Create(context, "entry", main);
	builder.SetInsertPoint(bb);

	Constant* runtimeInit = module->getOrInsertFunction("aike_init", Type::getVoidTy(context), nullptr);

	builder.CreateCall(runtimeInit);

	for (auto& e: entries)
		builder.CreateCall(e);

	builder.CreateRet(builder.getInt32(0));
}