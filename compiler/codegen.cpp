#include "common.hpp"
#include "codegen.hpp"

#include "ast.hpp"
#include "visit.hpp"
#include "output.hpp"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

using namespace llvm;

struct FunctionInstance
{
	Function* value;

	Array<Variable*> args;
	Ast* body;
};

struct Codegen
{
	Output* output;
	LLVMContext* context;
	Module* module;

	IRBuilder<>* builder;
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

static Value* codegenFunctionValue(Codegen& cg, Variable* var)
{
	FunctionType* funty = cast<FunctionType>(cast<PointerType>(getType(cg, var->type))->getElementType());

	return cg.module->getOrInsertFunction(var->name.str(), funty);
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

	if (UNION_CASE(LiteralStruct, n, node))
	{
		Type* type = getType(cg, n->type);

		Value* result = UndefValue::get(type);

		for (auto& f: n->fields)
		{
			pair<int, Ty*> p = typeIndex(n->type, f.first);
			assert(p.first >= 0);

			Value* expr = codegenExpr(cg, f.second);

			result = cg.builder->CreateInsertValue(result, expr, p.first);
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

	if (UNION_CASE(Index, n, node))
	{
		assert(n->field >= 0);

		Value* expr = codegenExpr(cg, n->expr);

		return cg.builder->CreateExtractValue(expr, n->field);
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

	if (UNION_CASE(If, n, node))
	{
		Value* cond = codegenExpr(cg, n->cond);

		Function* func = cg.builder->GetInsertBlock()->getParent();

		if (n->elsebody)
		{
			BasicBlock* thenbb = BasicBlock::Create(*cg.context, "then", func);
			BasicBlock* elsebb = BasicBlock::Create(*cg.context, "else");
			BasicBlock* endbb = BasicBlock::Create(*cg.context, "ifend");

			cg.builder->CreateCondBr(cond, thenbb, elsebb);

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
			BasicBlock* thenbb = BasicBlock::Create(*cg.context, "then", func);
			BasicBlock* endbb = BasicBlock::Create(*cg.context, "ifend");

			cg.builder->CreateCondBr(cond, thenbb, endbb);

			cg.builder->SetInsertPoint(thenbb);

			Value* thenbody = codegenExpr(cg, n->thenbody);
			cg.builder->CreateBr(endbb);
			thenbb = cg.builder->GetInsertBlock();

			func->getBasicBlockList().push_back(endbb);
			cg.builder->SetInsertPoint(endbb);

			return nullptr;
		}
	}

	if (UNION_CASE(Fn, n, node))
	{
		FunctionType* funty = cast<FunctionType>(cast<PointerType>(getType(cg, n->type))->getElementType());

		Function* fun = Function::Create(funty, GlobalValue::InternalLinkage, "anonymous", cg.module);

		cg.pendingFunctions.push_back({ fun, n->args, n->body });

		return fun;
	}

	if (UNION_CASE(FnDecl, n, node))
	{
		Function* fun = cast<Function>(codegenFunctionValue(cg, n->var));

		cg.pendingFunctions.push_back({ fun, n->args, n->body });

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

static void codegenFunction(Codegen& cg, const FunctionInstance& inst)
{
	if (!inst.body)
		return;

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

		cg.vars[n->var] = fun;

		cg.pendingFunctions.push_back({ fun, n->args, n->body });

		return true;
	}

	return false;
}

void codegen(Output& output, Ast* root, llvm::Module* module)
{
	llvm::LLVMContext* context = &module->getContext();

	IRBuilder<> builder(*context);

	Codegen cg = { &output, context, module, &builder };

	visitAst(root, codegenGatherToplevel, cg);

	while (!cg.pendingFunctions.empty())
	{
		FunctionInstance inst = cg.pendingFunctions.back();

		cg.pendingFunctions.pop_back();

		codegenFunction(cg, inst);
	}
}
