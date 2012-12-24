#include "compiler.hpp"

#include "parser.hpp"
#include "output.hpp"
#include "typecheck.hpp"

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

struct Context
{
	llvm::LLVMContext* context;
	llvm::Module* module;

	std::map<BindingTarget*, llvm::Value*> values;
	std::map<Type*, llvm::Type*> types;
};

llvm::Type* compileType(Context& context, Type* type, const Location& location)
{
	if (context.types.count(type) > 0)
		return context.types[type];

	if (CASE(TypeGeneric, type))
	{
		// this'll be an error in the future
		return context.types[type] = llvm::Type::getInt32Ty(*context.context);
	}

	if (CASE(TypeUnit, type))
	{
		// this might be void in the future
		return context.types[type] = llvm::Type::getInt32Ty(*context.context);
	}

	if (CASE(TypeInt, type))
	{
		return context.types[type] = llvm::Type::getInt32Ty(*context.context);
	}

	if (CASE(TypeFloat, type))
	{
		return context.types[type] = llvm::Type::getFloatTy(*context.context);
	}

	if (CASE(TypeFunction, type))
	{
		std::vector<llvm::Type*> args;

		for (size_t i = 0; i < _->args.size(); ++i)
			args.push_back(compileType(context, _->args[i], location));

		return context.types[type] = llvm::FunctionType::get(compileType(context, _->result, location), args, false);
	}

	errorf(location, "Unrecognized type");
}

llvm::Value* compileBinding(Context& context, BindingBase* binding, const Location& location)
{
	if (CASE(BindingLocal, binding))
	{
		if (context.values.count(_->target) > 0)
			return context.values[_->target];

		errorf(location, "Variable %s has not been computed", _->target->name.c_str());
	}

	errorf(location, "Variable binding has not been resolved");
}

llvm::Value* compileExpr(Context& context, llvm::IRBuilder<>& builder, Expr* node)
{
	if (CASE(ExprUnit, node))
	{
		// since we only have int type right now, unit should be int :)
		return builder.getInt32(0);
	}

	if (CASE(ExprLiteralNumber, node))
	{
		return builder.getInt32(uint32_t(_->value));
	}

	if (CASE(ExprBinding, node))
	{
		return compileBinding(context, _->binding, _->location);
	}

	if (CASE(ExprUnaryOp, node))
	{
		llvm::Value* ev = compileExpr(context, builder, _->expr);

		switch (_->op)
		{
		case SynUnaryOpPlus: return ev;
		case SynUnaryOpMinus: return builder.CreateNeg(ev);
		case SynUnaryOpNot: return builder.CreateNot(ev);
		default: assert(!"Unknown unary operation"); return 0;
		}
	}

	if (CASE(ExprBinaryOp, node))
	{
		llvm::Value* lv = compileExpr(context, builder, _->left);
		llvm::Value* rv = compileExpr(context, builder, _->right);

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

	if (CASE(ExprCall, node))
	{
		llvm::Value* func = compileExpr(context, builder, _->expr);

		std::vector<llvm::Value*> args;

		for (size_t i = 0; i < _->args.size(); ++i)
			args.push_back(compileExpr(context, builder, _->args[i]));

		return builder.CreateCall(func, args);
	}

	if (CASE(ExprLetVar, node))
	{
		llvm::Value* value = compileExpr(context, builder, _->body);

		assert(context.values.count(_->target) == 0);
		context.values[_->target] = value;

		return value;
	}

	if (CASE(ExprLetFunc, node))
	{
		llvm::FunctionType* funty = llvm::cast<llvm::FunctionType>(compileType(context, _->type, _->location));

		llvm::Function* func = llvm::cast<llvm::Function>(context.module->getOrInsertFunction(_->target->name, funty));

		llvm::Function::arg_iterator argi = func->arg_begin();

		for (size_t i = 0; i < func->arg_size(); ++i, ++argi)
		{
			argi->setName(_->args[i]->name);

			assert(context.values.count(_->args[i]) == 0);
			context.values[_->args[i]] = argi;
		}

		assert(context.values.count(_->target) == 0);
		context.values[_->target] = func;

		llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context.context, "entry", func);

		llvm::IRBuilder<> funcbuilder(bb);

		llvm::Value* value = compileExpr(context,  funcbuilder, _->body);

		funcbuilder.CreateRet(value);

		return func;
	}

	if (CASE(ExprLLVM, node))
	{
		llvm::Function* func = builder.GetInsertBlock()->getParent();

		std::string name = "autogen_" + func->getName().str();

		std::stringstream stream;
		llvm::raw_os_ostream os_stream(stream);

		os_stream << "define " << *func->getReturnType() << " @" << name << "(";

		for (llvm::Function::arg_iterator argi = func->arg_begin(), arge = func->arg_end(); argi != arge; ++argi)
			os_stream << (argi != func->arg_begin() ? ", " : "") << *argi->getType() << " %" << argi->getName().str();

		os_stream << "){ %out = " + _->body + " ret " << *func->getReturnType() << " %out }";
		os_stream.flush();

		llvm::SMDiagnostic err;
		if(!llvm::ParseAssemblyString(stream.str().c_str(), context.module, err, *context.context))
			errorf(_->location, "Failed to parse llvm inline code: %s", err.getMessage().c_str());

		std::vector<llvm::Value*> arguments;
		for (llvm::Function::arg_iterator argi = func->arg_begin(), arge = func->arg_end(); argi != arge; ++argi)
			arguments.push_back(argi);

		return builder.CreateCall(context.module->getFunction(name.c_str()), arguments);
	}

	if (CASE(ExprIfThenElse, node))
	{
		llvm::Function* func = builder.GetInsertBlock()->getParent();

		llvm::Value* cond = compileExpr(context, builder, _->cond);

		llvm::BasicBlock* thenbb = llvm::BasicBlock::Create(*context.context, "then", func);
		llvm::BasicBlock* elsebb = llvm::BasicBlock::Create(*context.context, "else");
		llvm::BasicBlock* ifendbb = llvm::BasicBlock::Create(*context.context, "ifend");

		builder.CreateCondBr(cond, thenbb, elsebb);

		builder.SetInsertPoint(thenbb);
		llvm::Value* thenbody = compileExpr(context, builder, _->thenbody);
		builder.CreateBr(ifendbb);
		thenbb = builder.GetInsertBlock();

		func->getBasicBlockList().push_back(elsebb);

		builder.SetInsertPoint(elsebb);
		llvm::Value* elsebody = compileExpr(context, builder, _->elsebody);
		builder.CreateBr(ifendbb);
		elsebb = builder.GetInsertBlock();

		func->getBasicBlockList().push_back(ifendbb);
		builder.SetInsertPoint(ifendbb);
		llvm::PHINode* pn = builder.CreatePHI(compileType(context, _->type, _->location), 2);

		pn->addIncoming(thenbody, thenbb);
		pn->addIncoming(elsebody, elsebb);

		return pn;
	}

	if (CASE(ExprBlock, node))
	{
		llvm::Value *value = 0;

		for (size_t i = 0; i < _->expressions.size(); ++i)
			value = compileExpr(context, builder, _->expressions[i]);

		return value;
	}

	assert(!"Unknown AST node type");
	return 0;
}

void compile(llvm::LLVMContext& context, llvm::Module* module, Expr* root)
{
	Context ctx;
	ctx.context = &context;
	ctx.module = module;

	llvm::Function* entryf =
		llvm::cast<llvm::Function>(module->getOrInsertFunction("entrypoint", llvm::Type::getInt32Ty(context),
		(Type *)0));

	llvm::BasicBlock* bb = llvm::BasicBlock::Create(context, "entry", entryf);

	llvm::IRBuilder<> builder(bb);

	llvm::Value* result = compileExpr(ctx, builder, root);

	builder.CreateRet(result);
}
