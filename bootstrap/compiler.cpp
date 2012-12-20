#include "compiler.hpp"

#include "parser.hpp"
#include "output.hpp"

#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/IRBuilder.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"

#include <exception>
#include <cassert>

using namespace llvm;

struct Binding
{
	std::string name;
	Value* value;
};

Value* compileExpr(LLVMContext& context, Module* module, IRBuilder<>& builder, AstBase* node, std::vector<Binding>& bindings)
{
	if (ASTCASE(AstUnit, node))
	{
		// since we only have int type right now, unit should be int :)
		return builder.getInt32(0);
	}

	if (ASTCASE(AstLiteralNumber, node))
	{
		return builder.getInt32(_->value);
	}

	if (ASTCASE(AstVariableReference, node))
	{
		for (size_t i = 0; i < bindings.size(); ++i)
			if (bindings[i].name == _->name)
				return bindings[i].value;

		errorf("Unresolved variable reference %s", _->name.c_str());
	}

	if (ASTCASE(AstUnaryOp, node))
	{
		Value* ev = compileExpr(context, module, builder, _->expr, bindings);

		switch (_->type)
		{
		case AstUnaryOpPlus: return ev;
		case AstUnaryOpMinus: return builder.CreateNeg(ev);
		case AstUnaryOpNot: return builder.CreateNot(ev);
		default: assert(!"Unknown unary operation"); return 0;
		}
	}

	if (ASTCASE(AstBinaryOp, node))
	{
		Value* lv = compileExpr(context, module, builder, _->left, bindings);
		Value* rv = compileExpr(context, module, builder, _->right, bindings);

		switch (_->type)
		{
		case AstBinaryOpAdd: return builder.CreateAdd(lv, rv);
		case AstBinaryOpSubtract: return builder.CreateSub(lv, rv);
		case AstBinaryOpMultiply: return builder.CreateMul(lv, rv);
		case AstBinaryOpDivide: return builder.CreateSDiv(lv, rv);
		case AstBinaryOpLess: return builder.CreateICmpSLT(lv, rv);
		case AstBinaryOpLessEqual: return builder.CreateICmpSLE(lv, rv);
		case AstBinaryOpGreater: return builder.CreateICmpSGT(lv, rv);
		case AstBinaryOpGreaterEqual: return builder.CreateICmpSGE(lv, rv);
		case AstBinaryOpEqual: return builder.CreateICmpEQ(lv, rv);
		case AstBinaryOpNotEqual: return builder.CreateICmpNE(lv, rv);
		default: assert(!"Unknown binary operation"); return 0;
		}
	}

	if (ASTCASE(AstCall, node))
	{
		AstVariableReference* var = dynamic_cast<AstVariableReference*>(_->expr);
		if (!var) errorf("Dynamic function calls are not supported");

		Function* func = module->getFunction(var->name);
		if (!func) errorf("Unresolved function reference %s", var->name.c_str());

		std::vector<Value*> args;

		for (size_t i = 0; i < _->args.size(); ++i)
			args.push_back(compileExpr(context, module, builder, _->args[i], bindings));

		return builder.CreateCall(func, args);
	}

	if (ASTCASE(AstLetVar, node))
	{
		Value* value = compileExpr(context, module, builder, _->body, bindings);

		Binding bind = {_->var.name, value};
		bindings.push_back(bind);

		Value* result = compileExpr(context, module, builder, _->expr, bindings);

		bindings.pop_back();

		return result;
	}

	if (ASTCASE(AstLetFunc, node))
	{
		std::vector<Type*> args;

		for (size_t i = 0; i < _->args.size(); ++i)
			args.push_back(Type::getInt32Ty(context));

		FunctionType* functy = FunctionType::get(Type::getInt32Ty(context), args, false);

		Function* func = cast<Function>(module->getOrInsertFunction(_->var.name, functy));

		BasicBlock* bb = BasicBlock::Create(context, "entry", func);

		IRBuilder<> funcbuilder(bb);

		Function::arg_iterator argi = func->arg_begin();

		for (size_t i = 0; i < _->args.size(); ++i, ++argi)
		{
			argi->setName(_->args[i].name);

			Binding bind = {_->args[i].name, argi};
			bindings.push_back(bind);
		}

		Value* value = compileExpr(context, module, funcbuilder, _->body, bindings);

		funcbuilder.CreateRet(value);

		for (size_t i = 0; i < _->args.size(); ++i)
			bindings.pop_back();

		return compileExpr(context, module, builder, _->expr, bindings);
	}

	if (ASTCASE(AstIfThenElse, node))
	{
		Function* func = builder.GetInsertBlock()->getParent();

		Value* cond = compileExpr(context, module, builder, _->cond, bindings);

		BasicBlock* thenbb = BasicBlock::Create(context, "then", func);
		BasicBlock* elsebb = BasicBlock::Create(context, "else");
		BasicBlock* ifendbb = BasicBlock::Create(context, "ifend");

		builder.CreateCondBr(cond, thenbb, elsebb);

		builder.SetInsertPoint(thenbb);
		Value* thenbody = compileExpr(context, module, builder, _->thenbody, bindings);
		builder.CreateBr(ifendbb);
		thenbb = builder.GetInsertBlock();

		func->getBasicBlockList().push_back(elsebb);

		builder.SetInsertPoint(elsebb);
		Value* elsebody = compileExpr(context, module, builder, _->elsebody, bindings);
		builder.CreateBr(ifendbb);
		elsebb = builder.GetInsertBlock();

		func->getBasicBlockList().push_back(ifendbb);
		builder.SetInsertPoint(ifendbb);
		PHINode* pn = builder.CreatePHI(Type::getInt32Ty(context), 2);

		pn->addIncoming(thenbody, thenbb);
		pn->addIncoming(elsebody, elsebb);

		return pn;
	}

	assert(!"Unknown AST node type");
	return 0;
}

void compile(LLVMContext& context, Module* module, AstBase* root)
{
	Function* entryf =
		cast<Function>(module->getOrInsertFunction("entrypoint", Type::getInt32Ty(context),
		(Type *)0));

	BasicBlock* bb = BasicBlock::Create(context, "entry", entryf);

	IRBuilder<> builder(bb);

	std::vector<Binding> bindings;
	Value* result = compileExpr(context, module, builder, root, bindings);

	builder.CreateRet(result);
}