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

			if (UNION_CASE(Struct, d, t->def))
			{
				if (StructType* st = cg.module->getTypeByName(t->name.str()))
					return st;

				vector<Type*> fields;
				for (auto& f: d->fields)
					fields.push_back(codegenType(cg, f.type));

				return StructType::create(*cg.context, fields, t->name.str());
			}

			ICE("Unknown TyDef kind %d", t->def->kind);
		}
	}

	ICE("Unknown Ty kind %d", type->kind);
}

static Ty* getType(Codegen& cg, Ty* type)
{
	// TODO: We need a general type rewriting facility
	if (UNION_CASE(Array, t, type))
	{
		Ty* element = getType(cg, t->element);

		return UNION_NEW(Ty, Array, { element });
	}

	if (UNION_CASE(Function, t, type))
	{
		Arr<Ty*> args;

		for (Ty* arg: t->args)
			args.push(getType(cg, arg));

		Ty* ret = getType(cg, t->ret);

		return UNION_NEW(Ty, Function, { args, ret });
	}

	if (UNION_CASE(Instance, t, type))
	{
		if (t->generic)
			return getGenericInstance(cg, t->generic);

		return type;
	}

	return type;
}

static Value* codegenFunctionValue(Codegen& cg, const string& name, Ty* type)
{
	if (Function* fun = cg.module->getFunction(name))
		return fun;

	FunctionType* funty = cast<FunctionType>(cast<PointerType>(codegenType(cg, type))->getElementType());

	return Function::Create(funty, GlobalValue::InternalLinkage, name, cg.module);
}

static Value* codegenFunctionValue(Codegen& cg, Variable* var, Ty* type, const Arr<Ty*>& tyargs)
{
	// TODO: remove hack
	string name = var->name == "main" ? "main" : mangle(mangleFn(var->name, type, tyargs));

	return codegenFunctionValue(cg, name, type);
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

static Value* codegenExpr(Codegen& cg, Ast* node)
{
	if (UNION_CASE(LiteralBool, n, node))
	{
		return cg.builder->getInt1(n->value);
	}

	if (UNION_CASE(LiteralNumber, n, node))
	{
		return cg.builder->getInt32(atoi(n->value.str().c_str()));
	}

	if (UNION_CASE(LiteralString, n, node))
	{
		Type* type = codegenType(cg, UNION_NEW(Ty, String, {}));

		Value* result = UndefValue::get(type);

		Value* string = cg.builder->CreateGlobalStringPtr(StringRef(n->value.data, n->value.size));

		result = cg.builder->CreateInsertValue(result, string, 0);
		result = cg.builder->CreateInsertValue(result, cg.builder->getInt32(n->value.size), 1);

		return result;
	}

	if (UNION_CASE(LiteralArray, n, node))
	{
		Type* type = codegenType(cg, n->type);

		Type* pointerType = cast<StructType>(type)->getElementType(0);
		Type* elementType = cast<PointerType>(pointerType)->getElementType();

		// TODO: refactor + fix int32/size_t
		Value* elementSize = cg.builder->CreateIntCast(ConstantExpr::getSizeOf(elementType), cg.builder->getInt32Ty(), false);
		Value* rawPtr = cg.builder->CreateCall2(cg.runtimeNewArr, cg.builder->getInt32(n->elements.size), elementSize);
		Value* ptr = cg.builder->CreateBitCast(rawPtr, pointerType);

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

	if (UNION_CASE(LiteralStruct, n, node))
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

	if (UNION_CASE(SizeOf, n, node))
	{
		Type* type = codegenType(cg, n->type);

		return cg.builder->CreateIntCast(ConstantExpr::getSizeOf(type), cg.builder->getInt32Ty(), false);
	}

	if (UNION_CASE(Ident, n, node))
	{
		if (n->target->kind == Variable::KindFunction)
		{
			UNION_CASE(FnDecl, decl, n->target->fn);
			assert(decl && decl->var == n->target);

			Ty* type = getType(cg, n->type);

			Arr<Ty*> tyargs;
			for (auto& a: n->tyargs)
				tyargs.push(getType(cg, a));

			Function* fun = cast<Function>(codegenFunctionValue(cg, n->target, type, tyargs));

			// TODO: there might be a better way?
			if (fun->empty())
			{
				vector<pair<Ty*, Ty*>> tyargs;
				assert(n->tyargs.size == decl->tyargs.size);

				// TODO: we're computing the final type here again
				for (size_t i = 0; i < n->tyargs.size; ++i)
					tyargs.push_back(make_pair(decl->tyargs[i], getType(cg, n->tyargs[i])));

				// TODO: parent=current is wrong: need to pick lexical parent
				cg.pendingFunctions.push_back(new FunctionInstance { fun, decl->args, decl->body, decl->attributes, decl->var->name, cg.currentFunction, tyargs });
			}

			return fun;
		}
		else
		{
			auto it = cg.vars.find(n->target);
			assert(it != cg.vars.end());

			if (n->target->kind == Variable::KindVariable)
				return cg.builder->CreateLoad(it->second);
			else
				return it->second;
		}
	}

	if (UNION_CASE(Member, n, node))
	{
		assert(n->field.index >= 0);

		Value* expr = codegenExpr(cg, n->expr);

		return cg.builder->CreateExtractValue(expr, n->field.index);
	}

	if (UNION_CASE(Block, n, node))
	{
		if (n->body.size == 0)
			return nullptr;

		for (size_t i = 0; i < n->body.size - 1; ++i)
			codegenExpr(cg, n->body[i]);

		return codegenExpr(cg, n->body[n->body.size - 1]);
	}

	if (UNION_CASE(Call, n, node))
	{
		Value* expr = codegenExpr(cg, n->expr);

		vector<Value*> args;
		for (auto& a: n->args)
			args.push_back(codegenExpr(cg, a));

		return cg.builder->CreateCall(expr, args);
	}

	if (UNION_CASE(Index, n, node))
	{
		Value* expr = codegenExpr(cg, n->expr);
		Value* index = codegenExpr(cg, n->index);

		Value* ptr = cg.builder->CreateExtractValue(expr, 0);
		Value* size = cg.builder->CreateExtractValue(expr, 1);

		Value* cond = cg.builder->CreateICmpUGE(index, size);

		codegenTrapIf(cg, cond);

		return cg.builder->CreateLoad(cg.builder->CreateInBoundsGEP(ptr, index));
	}

	if (UNION_CASE(Unary, n, node))
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

	if (UNION_CASE(Binary, n, node))
	{
		if (n->op == BinaryOpAnd || n->op == BinaryOpOr)
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
		else
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
	}

	if (UNION_CASE(If, n, node))
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

	if (UNION_CASE(Fn, n, node))
	{
		Ty* type = getType(cg, n->type);

		Function* fun = cast<Function>(codegenFunctionValue(cg, mangle(mangleFn(n->id, type)), type));

		cg.pendingFunctions.push_back(new FunctionInstance { fun, n->args, n->body, 0, Str(), cg.currentFunction });

		return fun;
	}

	if (UNION_CASE(FnDecl, n, node))
	{
		return nullptr;
	}

	if (UNION_CASE(VarDecl, n, node))
	{
		Value* expr = codegenExpr(cg, n->expr);

		Value* storage = cg.builder->CreateAlloca(expr->getType());
		cg.builder->CreateStore(expr, storage);

		cg.vars[n->var] = storage;

		return storage;
	}

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

	if (inst.external == "sizeof" && inst.generics.size() == 1)
	{
		Type* type = codegenType(cg, inst.generics[0].second);
		Value* ret = cg.builder->CreateIntCast(ConstantExpr::getSizeOf(type), cg.builder->getInt32Ty(), false);

		cg.builder->CreateRet(ret);
	}
	else
		// TODO Location
		cg.output->panic(Location(), "Unknown builtin function %s", inst.external.str().c_str());
}

static void codegenFunction(Codegen& cg, const FunctionInstance& inst)
{
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

	if (ret && !ret->getType()->isVoidTy())
		cg.builder->CreateRet(ret);
	else
		cg.builder->CreateRetVoid();
}

static bool codegenGatherToplevel(Codegen& cg, Ast* node)
{
	if (UNION_CASE(FnDecl, n, node))
	{
		if (n->tyargs.size == 0)
		{
			Function* fun = cast<Function>(codegenFunctionValue(cg, n->var, n->var->type, Arr<Ty*>()));

			cg.pendingFunctions.push_back(new FunctionInstance { fun, n->args, n->body, n->attributes, n->var->name });
		}

		return true;
	}

	return false;
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

void codegen(Output& output, Ast* root, llvm::Module* module)
{
	llvm::LLVMContext* context = &module->getContext();

	IRBuilder<> builder(*context);

	Codegen cg = { &output, context, module, &builder };

	visitAst(root, codegenGatherToplevel, cg);

	codegenPrepare(cg);

	while (!cg.pendingFunctions.empty())
	{
		FunctionInstance* inst = cg.pendingFunctions.back();

		cg.pendingFunctions.pop_back();

		cg.currentFunction = inst;

		codegenFunction(cg, *inst);

		cg.currentFunction = nullptr;
	}
}
