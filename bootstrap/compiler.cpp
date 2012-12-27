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
#include "llvm/Intrinsics.h"

#include <exception>
#include <cassert>
#include <sstream>

struct Context
{
	llvm::LLVMContext* context;
	llvm::Module* module;
	llvm::DataLayout* layout;

	std::map<BindingTarget*, llvm::Value*> values;
	std::map<Type*, llvm::Type*> types;
	std::vector<llvm::Type*> function_context_type;
};

llvm::Type* compileType(Context& context, Type* type, const Location& location)
{
	type = finalType(type);

	if (context.types.count(type) > 0)
		return context.types[type];

	if (CASE(TypeGeneric, type))
	{
		// this'll be an error in the future
		errorf(location, "Generic types not supported");
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

	if (CASE(TypeBool, type))
	{
		return context.types[type] = llvm::Type::getInt1Ty(*context.context);
	}
	
	if (CASE(TypeReference, type))
	{
		return context.types[type] = llvm::PointerType::getUnqual(compileType(context, _->contained, location));
	}

	if (CASE(TypeArray, type))
	{
		return context.types[type] = llvm::StructType::get(llvm::PointerType::getUnqual(compileType(context, _->contained, location)), llvm::Type::getInt32Ty(*context.context), (llvm::Type*)NULL);
	}

	if (CASE(TypeFunction, type))
	{
		std::vector<llvm::Type*> args;

		for (size_t i = 0; i < _->args.size(); ++i)
			args.push_back(compileType(context, _->args[i], location));

		args.push_back(llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(*context.context)));

		llvm::Type* function_type = llvm::FunctionType::get(compileType(context, _->result, location), args, false);

		llvm::StructType* holder_type = llvm::StructType::get(llvm::PointerType::getUnqual(function_type), llvm::Type::getInt8PtrTy(*context.context), (llvm::Type*)NULL);

		return context.types[type] = holder_type;
	}

	if (CASE(TypeStructure, type))
	{
		std::vector<llvm::Type*> members;

		for (size_t i = 0; i < _->members.size(); ++i)
			members.push_back(compileType(context, _->members[i], location));

		return context.types[type] = llvm::StructType::get(*context.context, members, false);
	}

	errorf(location, "Unrecognized type");
}

llvm::FunctionType* compileFunctionType(Context& context, Type* type, const Location& location, Type* context_type)
{
	type = finalType(type);

	if (CASE(TypeFunction, type))
	{
		std::vector<llvm::Type*> args;

		for (size_t i = 0; i < _->args.size(); ++i)
			args.push_back(compileType(context, _->args[i], location));

		if (context_type)
			args.push_back(compileType(context, context_type, location));

		return llvm::FunctionType::get(compileType(context, _->result, location), args, false);
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
	else if (CASE(BindingFreeFunction, binding))
	{
		if (context.values.count(_->target) > 0)
			return context.values[_->target];

		errorf(location, "Function %s has not been computed", _->target->name.c_str());
	}

	errorf(location, "Variable binding has not been resolved");
}

llvm::Function* compileFunctionThunk(Context& context, Location location, llvm::Function* target, llvm::Type* general_holder_type, llvm::Type* context_ref_type)
{
	llvm::FunctionType* thunk_function_type = llvm::cast<llvm::FunctionType>(general_holder_type->getContainedType(0)->getContainedType(0));

	llvm::Function* thunk_func = llvm::Function::Create(thunk_function_type, llvm::Function::InternalLinkage, target->getName(), context.module);

	llvm::BasicBlock* basic_block = llvm::BasicBlock::Create(*context.context, "entry", thunk_func);

	llvm::IRBuilder<> funcbuilder(basic_block);

	std::vector<llvm::Value*> args;

	llvm::Function::arg_iterator argi = thunk_func->arg_begin();

	for (size_t i = 0; i < target->arg_size(); ++i, ++argi)
		args.push_back(argi);

	if (context_ref_type)
		args.back() = funcbuilder.CreatePointerCast(args.back(), context_ref_type);

	funcbuilder.CreateRet(funcbuilder.CreateCall(target, args));

	return thunk_func;
}

llvm::Value* compileExpr(Context& context, llvm::IRBuilder<>& builder, Expr* node)
{
	assert(node);

	if (CASE(ExprUnit, node))
	{
		// since we only have int type right now, unit should be int :)
		return builder.getInt32(0);
	}

	if (CASE(ExprNumberLiteral, node))
	{
		return builder.getInt32(uint32_t(_->value));
	}

	if (CASE(ExprBooleanLiteral, node))
	{
		return builder.getInt1(_->value);
	}

	if (CASE(ExprArrayLiteral, node))
	{
		if (!dynamic_cast<TypeArray*>(finalType(_->type)))
			errorf(_->location, "array type is unknown");

		llvm::Type* array_type = compileType(context, _->type, _->location);

		llvm::Type* element_type = array_type->getContainedType(0)->getContainedType(0);

		llvm::Value* arr = llvm::ConstantAggregateZero::get(array_type);

		llvm::Value* data = builder.CreateBitCast(builder.CreateCall(context.module->getFunction("malloc"), builder.getInt32(uint32_t(_->elements.size() * context.layout->getTypeAllocSize(element_type)))), array_type->getContainedType(0));

		arr = builder.CreateInsertValue(arr, data, 0);

		for (size_t i = 0; i < _->elements.size(); i++)
		{
			llvm::Value* target = builder.CreateGEP(data, builder.getInt32(uint32_t(i)));

			builder.CreateStore(compileExpr(context, builder, _->elements[i]), target);
		}

		return builder.CreateInsertValue(arr, builder.getInt32(uint32_t(_->elements.size())), 1);
	}

	if (CASE(ExprBinding, node))
	{
		return compileBinding(context, _->binding, _->location);
	}

	if (CASE(ExprBindingExternal, node))
	{
		llvm::Value *value = compileBinding(context, _->context, _->location);

		value = builder.CreatePointerCast(value, context.function_context_type.back());

		return builder.CreateLoad(builder.CreateStructGEP(value, _->member_index));
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
		llvm::Value* holder = compileExpr(context, builder, _->expr);

		std::vector<llvm::Value*> args;

		for (size_t i = 0; i < _->args.size(); ++i)
			args.push_back(compileExpr(context, builder, _->args[i]));

		args.push_back(builder.CreateExtractValue(holder, 1));

		return builder.CreateCall(builder.CreateExtractValue(holder, 0), args);
	}

	if (CASE(ExprArrayIndex, node))
	{
		llvm::Function* function = builder.GetInsertBlock()->getParent();

		llvm::Value* arr = compileExpr(context, builder, _->arr);
		llvm::Value* index = compileExpr(context, builder, _->index);

		llvm::Value* data = builder.CreateExtractValue(arr, 0);
		llvm::Value* size = builder.CreateExtractValue(arr, 1);

		llvm::BasicBlock* trap_basic_block = llvm::BasicBlock::Create(*context.context, "trap");
		llvm::BasicBlock* after_basic_block = llvm::BasicBlock::Create(*context.context, "after");

		builder.CreateCondBr(builder.CreateICmpULT(index, size), after_basic_block, trap_basic_block);

		function->getBasicBlockList().push_back(trap_basic_block);
		builder.SetInsertPoint(trap_basic_block);

		builder.CreateCall(llvm::Intrinsic::getDeclaration(context.module, llvm::Intrinsic::trap));
		builder.CreateUnreachable();

		function->getBasicBlockList().push_back(after_basic_block);
		builder.SetInsertPoint(after_basic_block);

		return builder.CreateLoad(builder.CreateGEP(data, index), false);
	}

	if (CASE(ExprLetVar, node))
	{
		llvm::Value* value = compileExpr(context, builder, _->body);

		value->setName(_->target->name);

		assert(context.values.count(_->target) == 0);
		context.values[_->target] = value;

		return value;
	}

	if (CASE(ExprLetFunc, node))
	{
		llvm::FunctionType* function_type = compileFunctionType(context, _->type, _->location, _->context_target ? _->context_target->type : 0);

		llvm::Type* general_holder_type = compileType(context, _->type, _->location);

		llvm::Function* func = llvm::cast<llvm::Function>(context.module->getOrInsertFunction(_->target->name, function_type));

		llvm::Function::arg_iterator argi = func->arg_begin();

		llvm::Value* context_value = 0;

		for (size_t i = 0; i < func->arg_size(); ++i, ++argi)
		{
			if (i < _->args.size())
			{
				argi->setName(_->args[i]->name);

				assert(context.values.count(_->args[i]) == 0);
				context.values[_->args[i]] = argi;
			}
			else
			{
				argi->setName("extern");
				context_value = context.values[_->context_target] = argi;
			}
		}

		llvm::Type* context_ref_type = 0;
		llvm::Value* context_raw_data = 0;

		if (!_->externals.empty())
		{
			context_ref_type = compileType(context, _->context_target->type, _->location);
			llvm::Type* context_type = context_ref_type->getContainedType(0);

			context_raw_data = builder.CreateBitCast(builder.CreateCall(context.module->getFunction("malloc"), builder.getInt32(uint32_t(context.layout->getTypeAllocSize(context_type)))), llvm::Type::getInt8PtrTy(*context.context));

			llvm::Value* context_data = builder.CreateBitCast(context_raw_data, context_ref_type);

			for (size_t i = 0; i < _->externals.size(); i++)
				builder.CreateStore(compileExpr(context, builder, _->externals[i]), builder.CreateStructGEP(context_data, i));
		}

		// Create thunk for a function to be called through the function pointer
		llvm::Function* thunk_function = compileFunctionThunk(context, _->location, func, general_holder_type, context_ref_type);

		llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context.context, "entry", func);

		llvm::IRBuilder<> funcbuilder(bb);

		// Create holder for a function to be used inside the function
		llvm::Value* internal_holder = funcbuilder.CreateInsertValue(llvm::ConstantAggregateZero::get(general_holder_type), thunk_function, 0);
		
		if (context_raw_data)
			internal_holder = funcbuilder.CreateInsertValue(internal_holder, funcbuilder.CreateBitCast(context_value, llvm::Type::getInt8PtrTy(*context.context)), 1);
		
		assert(context.values.count(_->target) == 0);
		context.values[_->target] = internal_holder;

		// Compile function body
		context.function_context_type.push_back(context_ref_type);

		llvm::Value* value = compileExpr(context, funcbuilder, _->body);

		context.function_context_type.pop_back();

		funcbuilder.CreateRet(value);

		// Create holder for a function to be used outside the function
		llvm::Value* holder = builder.CreateInsertValue(llvm::ConstantAggregateZero::get(general_holder_type), thunk_function, 0);

		if (context_raw_data)
			holder = builder.CreateInsertValue(holder, context_raw_data, 1);

		context.values[_->target] = holder;

		return holder;
	}

	if (CASE(ExprExternFunc, node))
	{
		llvm::FunctionType* function_type = compileFunctionType(context, _->type, _->location, 0);

		llvm::Type* general_holder_type = compileType(context, _->type, _->location);

		llvm::Function* func = llvm::cast<llvm::Function>(context.module->getOrInsertFunction(_->target->name, function_type));

		llvm::Function::arg_iterator argi = func->arg_begin();

		for (size_t i = 0; i < func->arg_size(); ++i, ++argi)
		{
			if (i < _->args.size())
				argi->setName(_->args[i]->name);
		}

		llvm::Value* holder = llvm::ConstantAggregateZero::get(general_holder_type);

		holder = builder.CreateInsertValue(holder, compileFunctionThunk(context, _->location, func, general_holder_type, 0), 0);
		holder = builder.CreateInsertValue(holder, llvm::Constant::getNullValue(llvm::Type::getInt8PtrTy(*context.context)), 1);

		assert(context.values.count(_->target) == 0);
		context.values[_->target] = holder;

		return holder;
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
		if (!llvm::ParseAssemblyString(stream.str().c_str(), context.module, err, *context.context))
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

	if (CASE(ExprForInDo, node))
	{
		llvm::Function* function = builder.GetInsertBlock()->getParent();

		llvm::Value* arr = compileExpr(context, builder, _->arr);

		llvm::Value* data = builder.CreateExtractValue(arr, 0);
		llvm::Value* size = builder.CreateExtractValue(arr, 1);

		llvm::Value* index = builder.CreateAlloca(builder.getInt32Ty());
		builder.CreateStore(builder.getInt32(0), index);

		llvm::BasicBlock* step_basic_block = llvm::BasicBlock::Create(*context.context, "for_step", function);
		llvm::BasicBlock* body_basic_block = llvm::BasicBlock::Create(*context.context, "for_body");
		llvm::BasicBlock* end_basic_block = llvm::BasicBlock::Create(*context.context, "for_end");

		builder.CreateBr(step_basic_block);

		builder.SetInsertPoint(step_basic_block);

		builder.CreateCondBr(builder.CreateICmpULT(builder.CreateLoad(index), size), body_basic_block, end_basic_block);

		function->getBasicBlockList().push_back(body_basic_block);
		builder.SetInsertPoint(body_basic_block);

		context.values[_->target] = builder.CreateLoad(builder.CreateGEP(data, builder.CreateLoad(index)));

		compileExpr(context, builder, _->body);

		builder.CreateStore(builder.CreateAdd(builder.CreateLoad(index), builder.getInt32(1)), index, false);

		builder.CreateBr(step_basic_block);

		function->getBasicBlockList().push_back(end_basic_block);
		builder.SetInsertPoint(end_basic_block);

		return 0;
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
	ctx.layout = new llvm::DataLayout(module);

	llvm::Function* entryf =
		llvm::cast<llvm::Function>(module->getOrInsertFunction("entrypoint", llvm::Type::getInt32Ty(context),
		(Type *)0));

	llvm::BasicBlock* bb = llvm::BasicBlock::Create(context, "entry", entryf);

	llvm::IRBuilder<> builder(bb);

	llvm::Value* result = compileExpr(ctx, builder, root);

	builder.CreateRet(result);
}
