#include "common.hpp"
#include "codegen.hpp"

#include "ast.hpp"
#include "output.hpp"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

using namespace llvm;

struct Codegen
{
	Output* output;
	LLVMContext* context;
	Module* module;

	IRBuilder<>* builder;
	unordered_map<Variable*, Value*> vars;

	vector<Ast::FnDecl*> pendingFunctions;
};

static Type* getType(Codegen& cg, Ty* type)
{
	if (UNION_CASE(String, t, type))
	{
		Type* fields[] = { Type::getInt8PtrTy(*cg.context), Type::getInt32Ty(*cg.context) };

		return StructType::get(*cg.context, { fields, 2 });
	}

	if (UNION_CASE(Integer, t, type))
	{
		return Type::getInt32Ty(*cg.context);
	}

	if (UNION_CASE(Void, t, type))
	{
		return Type::getVoidTy(*cg.context);
	}

	if (UNION_CASE(Function, t, type))
	{
		Type* ret = getType(cg, t->ret);

		vector<Type*> args;

		for (auto& a: t->args)
			args.push_back(getType(cg, a));

		return FunctionType::get(ret, args, false);
	}

	ICE("Unknown Ty kind %d", type->kind);
}

static Value* codegenFunctionValue(Codegen& cg, Variable* var)
{
	FunctionType* funty = cast<FunctionType>(getType(cg, var->type));

	return cg.module->getOrInsertFunction(var->name.str(), funty);
}

static Value* codegenExpr(Codegen& cg, Ast* node)
{
	if (UNION_CASE(LiteralString, n, node))
	{
		Type* type = getType(cg, UNION_NEW(Ty, String, {}));

		Value* result = UndefValue::get(type);

		auto constant = ConstantDataArray::getString(*cg.context, StringRef(n->value.data, n->value.size));
		auto constantPtr = new GlobalVariable(*cg.module, constant->getType(), true, GlobalValue::InternalLinkage, constant);

		Constant* zero_32 = Constant::getNullValue(IntegerType::getInt32Ty(*cg.context));
		Constant *gep_params[] = { zero_32, zero_32 };

		Constant *msgptr = ConstantExpr::getGetElementPtr(constantPtr, gep_params);

		result = cg.builder->CreateInsertValue(result, msgptr, 0);
		result = cg.builder->CreateInsertValue(result, ConstantInt::get(Type::getInt32Ty(*cg.context), n->value.size), 1);

		return result;
	}

	if (UNION_CASE(LiteralNumber, n, node))
	{
		return ConstantInt::get(Type::getInt32Ty(*cg.context), atoi(n->value.str().c_str()));
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

	if (UNION_CASE(FnDecl, n, node))
	{
		cg.pendingFunctions.push_back(n);

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

static void codegenFunction(Codegen& cg, Ast::FnDecl* decl)
{
	Function* fun = cast<Function>(codegenFunctionValue(cg, decl->var));

	if (!decl->body)
		return;

	cg.vars[decl->var] = fun;

	size_t argindex = 0;

	for (Function::arg_iterator ait = fun->arg_begin(); ait != fun->arg_end(); ++ait, ++argindex)
		cg.vars[decl->args[argindex]] = ait;

	BasicBlock* bb = BasicBlock::Create(*cg.context, "entry", fun);
	cg.builder->SetInsertPoint(bb);

	Value* ret = codegenExpr(cg, decl->body);

	if (ret)
		cg.builder->CreateRet(ret);
	else
		cg.builder->CreateRetVoid();
}

static bool codegenGatherToplevel(Codegen& cg, Ast* node)
{
	if (UNION_CASE(FnDecl, n, node))
	{
		cg.pendingFunctions.push_back(n);

		return true;
	}

	return false;
}

void codegen(Output& output, Ast* root, llvm::LLVMContext* context, llvm::Module* module)
{
	IRBuilder<> builder(*context);

	Codegen cg = { &output, context, module, &builder };

	visitAst(root, codegenGatherToplevel, cg);

	while (!cg.pendingFunctions.empty())
	{
		Ast::FnDecl* decl = cg.pendingFunctions.back();

		cg.pendingFunctions.pop_back();

		codegenFunction(cg, decl);
	}
}