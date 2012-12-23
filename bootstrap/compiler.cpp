#include "compiler.hpp"

#include "parser.hpp"
#include "output.hpp"

#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/IRBuilder.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/Assembly/Parser.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_os_ostream.h"

#include <exception>
#include <cassert>
#include <sstream>

using namespace llvm;

struct Binding
{
	std::string name;
	Value* value;
};

Value* compileExpr(LLVMContext& context, Module* module, IRBuilder<>& builder, SynBase* node, std::vector<Binding>& bindings)
{
	if (CASE(SynUnit, node))
	{
		// since we only have int type right now, unit should be int :)
		return builder.getInt32(0);
	}

	if (CASE(SynLiteralNumber, node))
	{
		return builder.getInt32(uint32_t(_->value));
	}

	if (CASE(SynVariableReference, node))
	{
		for (size_t i = 0; i < bindings.size(); ++i)
			if (bindings[i].name == _->name)
				return bindings[i].value;

		errorf("Unresolved variable reference %s", _->name.c_str());
	}

	if (CASE(SynUnaryOp, node))
	{
		Value* ev = compileExpr(context, module, builder, _->expr, bindings);

		switch (_->op)
		{
		case SynUnaryOpPlus: return ev;
		case SynUnaryOpMinus: return builder.CreateNeg(ev);
		case SynUnaryOpNot: return builder.CreateNot(ev);
		default: assert(!"Unknown unary operation"); return 0;
		}
	}

	if (CASE(SynBinaryOp, node))
	{
		Value* lv = compileExpr(context, module, builder, _->left, bindings);
		Value* rv = compileExpr(context, module, builder, _->right, bindings);

		switch (_->op)
		{
		case SynBinaryOpAdd: return builder.CreateAdd(lv, rv);
		case SynBinaryOpSubtract: return builder.CreateSub(lv, rv);
		case SynBinaryOpMultiply: return builder.CreateMul(lv, rv);
		case SynBinaryOpDivide: return builder.CreateSDiv(lv, rv);
		case SynBinaryOpLess: return builder.CreateICmpSLT(lv, rv);
		case SynBinaryOpLessEqual: return builder.CreateICmpSLE(lv, rv);
		case SynBinaryOpGreater: return builder.CreateICmpSGT(lv, rv);
		case SynBinaryOpGreaterEqual: return builder.CreateICmpSGE(lv, rv);
		case SynBinaryOpEqual: return builder.CreateICmpEQ(lv, rv);
		case SynBinaryOpNotEqual: return builder.CreateICmpNE(lv, rv);
		default: assert(!"Unknown binary operation"); return 0;
		}
	}

	if (CASE(SynCall, node))
	{
		SynVariableReference* var = dynamic_cast<SynVariableReference*>(_->expr);
		if (!var) errorf("Dynamic function calls are not supported");

		Function* func = module->getFunction(var->name);
		if (!func) errorf("Unresolved function reference %s", var->name.c_str());

		std::vector<Value*> args;

		for (size_t i = 0; i < _->args.size(); ++i)
			args.push_back(compileExpr(context, module, builder, _->args[i], bindings));

		return builder.CreateCall(func, args);
	}

	if (CASE(SynLetVar, node))
	{
		Value* value = compileExpr(context, module, builder, _->body, bindings);

		Binding bind = {_->var.name, value};
		bindings.push_back(bind);

		return value;
	}

	if (CASE(SynLetFunc, node))
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

		return func;
	}

	if (CASE(SynLLVM, node))
	{
		Function* func = builder.GetInsertBlock()->getParent();

		std::string name = "autogen_" + func->getName().str();

		std::stringstream stream;
		llvm::raw_os_ostream os_stream(stream);

		os_stream << "define " << *func->getReturnType() << " @" << name << "(";

		for (Function::arg_iterator argi = func->arg_begin(), arge = func->arg_end(); argi != arge; ++argi)
			os_stream << (argi != func->arg_begin() ? ", " : "") << *argi->getType() << " %" << argi->getName().str();

		os_stream << "){ %out = " + _->body + " ret " << *func->getReturnType() << " %out }";
		os_stream.flush();

		llvm::SMDiagnostic err;
		if(!llvm::ParseAssemblyString(stream.str().c_str(), module, err, context))
			errorf("Failed to parse llvm inline code: %s", err.getMessage().c_str());

		std::vector<llvm::Value*> arguments;
		for (Function::arg_iterator argi = func->arg_begin(), arge = func->arg_end(); argi != arge; ++argi)
			arguments.push_back(argi);

		return builder.CreateCall(module->getFunction(name.c_str()), arguments);
	}

	if (CASE(SynIfThenElse, node))
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

	if (CASE(SynSequence, node))
	{
		compileExpr(context, module, builder, _->head, bindings);

		return compileExpr(context, module, builder, _->tail, bindings);
	}

	if (CASE(SynBlock, node))
	{
		Value *value = 0;

		size_t bind_count = bindings.size();

		for (size_t i = 0; i < _->expressions.size(); ++i)
			value = compileExpr(context, module, builder, _->expressions[i], bindings);

		while(bindings.size() > bind_count)
			bindings.pop_back();

		return value;
	}

	assert(!"Unknown AST node type");
	return 0;
}

void compile(LLVMContext& context, Module* module, SynBase* root)
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
