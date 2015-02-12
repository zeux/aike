#include "common.hpp"
#include "codegen.hpp"

#include "ast.hpp"
#include "visit.hpp"
#include "output.hpp"
#include "mangle.hpp"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

using namespace llvm;

struct FunctionInstance
{
	Function* value;

	Arr<Variable*> args;
	Ast* body;

	Str external;
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

	vector<FunctionInstance> pendingFunctions;
};

static Type* getType(Codegen& cg, Ty* type)
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
		Type* element = getType(cg, t->element);
		Type* fields[] = { PointerType::get(element, 0), Type::getInt32Ty(*cg.context) };

		return StructType::get(*cg.context, { fields, 2 });
	}

	if (UNION_CASE(Function, t, type))
	{
		Type* ret = getType(cg, t->ret);

		vector<Type*> args;

		for (auto& a: t->args)
			args.push_back(getType(cg, a));

		return PointerType::get(FunctionType::get(ret, args, false), 0);
	}

	if (UNION_CASE(Instance, t, type))
	{
		assert(t->def);

		if (UNION_CASE(Struct, d, t->def))
		{
			if (StructType* st = cg.module->getTypeByName(t->name.str()))
				return st;

			vector<Type*> fields;
			for (auto& f: d->fields)
				fields.push_back(getType(cg, f.second));

			return StructType::create(*cg.context, fields, t->name.str());
		}

		ICE("Unknown TyDef kind %d", t->def->kind);
	}

	ICE("Unknown Ty kind %d", type->kind);
}

static Value* codegenFunctionValue(Codegen& cg, const string& name, Ty* type)
{
	if (Function* fun = cg.module->getFunction(name))
		return fun;

	FunctionType* funty = cast<FunctionType>(cast<PointerType>(getType(cg, type))->getElementType());

	return Function::Create(funty, GlobalValue::InternalLinkage, name, cg.module);
}

static Value* codegenFunctionValue(Codegen& cg, Variable* var)
{
	// TODO: remove hack
	string name = var->name == "main" ? "main" : mangle(mangleFn(var->name, var->type));

	return codegenFunctionValue(cg, name, var->type);
}

static Value* codegenArithOverflow(Codegen& cg, Value* left, Value* right, Constant* overflowOp)
{
	Function* func = cg.builder->GetInsertBlock()->getParent();

	Value* result = cg.builder->CreateCall2(overflowOp, left, right);
	Value* cond = cg.builder->CreateExtractValue(result, 1);

	BasicBlock* thenbb = BasicBlock::Create(*cg.context, "then");
	BasicBlock* endbb = BasicBlock::Create(*cg.context, "ifend");

	cg.builder->CreateCondBr(cond, thenbb, endbb);

	func->getBasicBlockList().push_back(thenbb);
	cg.builder->SetInsertPoint(thenbb);

	cg.builder->CreateCall(cg.builtinTrap);
	cg.builder->CreateBr(endbb);

	func->getBasicBlockList().push_back(endbb);
	cg.builder->SetInsertPoint(endbb);

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
		Type* type = getType(cg, UNION_NEW(Ty, String, {}));

		Value* result = UndefValue::get(type);

		Value* string = cg.builder->CreateGlobalStringPtr(StringRef(n->value.data, n->value.size));

		result = cg.builder->CreateInsertValue(result, string, 0);
		result = cg.builder->CreateInsertValue(result, cg.builder->getInt32(n->value.size), 1);

		return result;
	}

	if (UNION_CASE(LiteralArray, n, node))
	{
		Type* type = getType(cg, n->type);

		Type* pointerType = cast<StructType>(type)->getElementType(0);
		Type* elementType = cast<PointerType>(pointerType)->getElementType();

		// TODO: refactor + fix int32/size_t
		Value* rawPtr = cg.builder->CreateCall2(cg.runtimeNewArr, cg.builder->getInt32(n->elements.size), cg.builder->CreateIntCast(ConstantExpr::getSizeOf(elementType), cg.builder->getInt32Ty(), false));
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
		Type* type = getType(cg, n->type);

		Value* result = UndefValue::get(type);

		for (auto& f: n->fields)
		{
			assert(f.first.index >= 0);

			Value* expr = codegenExpr(cg, f.second);

			result = cg.builder->CreateInsertValue(result, expr, f.first.index);
		}

		return result;
	}

	if (UNION_CASE(Ident, n, node))
	{
		auto it = cg.vars.find(n->target);

		if (it != cg.vars.end())
		{
			Value* storage = it->second;

			if (isa<AllocaInst>(storage))
				return cg.builder->CreateLoad(storage);
			else
				return it->second;
		}

		// Delayed function compilation
		if (n->target->type->kind == Ty::KindFunction)
			return cg.vars[n->target] = codegenFunctionValue(cg, n->target);

		ICE("No code generated for identifier %s", n->target->name.str().c_str());
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
		Function* fun = cast<Function>(codegenFunctionValue(cg, mangle(mangleFn(n->id, n->type)), n->type));

		cg.pendingFunctions.push_back({ fun, n->args, n->body });

		return fun;
	}

	if (UNION_CASE(FnDecl, n, node))
	{
		Function* fun = cast<Function>(codegenFunctionValue(cg, n->var));

		cg.pendingFunctions.push_back({ fun, n->args, n->body, n->var->name });

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

static void codegenFunction(Codegen& cg, const FunctionInstance& inst)
{
	if (!inst.body)
		return codegenFunctionExtern(cg, inst);

	size_t argindex = 0;

	for (Function::arg_iterator ait = inst.value->arg_begin(); ait != inst.value->arg_end(); ++ait, ++argindex)
		cg.vars[inst.args[argindex]] = ait;

	BasicBlock* bb = BasicBlock::Create(*cg.context, "entry", inst.value);
	cg.builder->SetInsertPoint(bb);

	Value* ret = codegenExpr(cg, inst.body);

	if (ret)
		cg.builder->CreateRet(ret);
	else
		cg.builder->CreateRetVoid();
}

static bool codegenGatherToplevel(Codegen& cg, Ast* node)
{
	if (UNION_CASE(FnDecl, n, node))
	{
		Function* fun = cast<Function>(codegenFunctionValue(cg, n->var));

		cg.pendingFunctions.push_back({ fun, n->args, n->body, n->var->name });

		return true;
	}

	return false;
}

static void codegenPrepare(Codegen& cg)
{
	cg.builtinTrap = cg.module->getOrInsertFunction("llvm.trap", FunctionType::get(Type::getVoidTy(*cg.context), false));

	Type* overflowArgs[] = { Type::getInt32Ty(*cg.context), Type::getInt32Ty(*cg.context) };
	Type* overflowRets[] = { Type::getInt32Ty(*cg.context), Type::getInt1Ty(*cg.context) };
	FunctionType* overflowFunTy = FunctionType::get(StructType::get(*cg.context, overflowRets, 2), overflowArgs, false);

	cg.builtinAddOverflow = cg.module->getOrInsertFunction("llvm.sadd.with.overflow.i32", overflowFunTy);
	cg.builtinSubOverflow = cg.module->getOrInsertFunction("llvm.ssub.with.overflow.i32", overflowFunTy);
	cg.builtinMulOverflow = cg.module->getOrInsertFunction("llvm.smul.with.overflow.i32", overflowFunTy);

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
		FunctionInstance inst = cg.pendingFunctions.back();

		cg.pendingFunctions.pop_back();

		codegenFunction(cg, inst);
	}
}
