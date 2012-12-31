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

		for (size_t i = 0; i < _->member_types.size(); ++i)
			members.push_back(compileType(context, _->member_types[i], location));

		if (!_->name.empty())
			return context.types[type] = llvm::StructType::create(*context.context, members, _->name);
		else
			return context.types[type] = llvm::StructType::get(*context.context, members, false);
	}

	if (CASE(TypeUnion, type))
	{
		std::vector<llvm::Type*> members;

		members.push_back(llvm::Type::getInt32Ty(*context.context)); // Current type ID
		members.push_back(llvm::Type::getInt8PtrTy(*context.context)); // Pointer to union data

		return context.types[type] = llvm::StructType::create(*context.context, members, _->name);
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

llvm::Value* findFunctionArgument(llvm::Function* func, const std::string& name)
{
	for (llvm::Function::arg_iterator argi = func->arg_begin(); argi != func->arg_end(); ++argi)
		if (argi->getName() == name)
			return argi;

	return NULL;
}

std::string compileInlineLLVM(const std::string& name, const std::string& body, llvm::Function* func, const Location& location)
{
	std::ostringstream declare;
	std::ostringstream oss;
	llvm::raw_os_ostream os(oss);

	os << "define " << *func->getReturnType() << " @" << name << "(";

	for (llvm::Function::arg_iterator argi = func->arg_begin(); argi != func->arg_end(); ++argi)
		os << (argi != func->arg_begin() ? ", " : "") << *argi->getType() << " %" << argi->getName().str();

	os << ") alwaysinline {\n";

	// if %out is not used, assume single expression
	if (body.find("%out") == std::string::npos)
	{
		os << "%out = ";
	}

	// append body to stream, replacing typeof(%arg) with actual types
	for (size_t i = 0; i < body.size(); )
	{
		if (body[i] == 't' && body.compare(i, 8, "typeof(%") == 0)
		{
			std::string::size_type end = body.find(')', i);

			if (end == std::string::npos)
				errorf(location, "Incorrect typeof expression: closing brace expected");

			std::string var(body.begin() + i + 8, body.begin() + end);

			if (llvm::Value* arg = findFunctionArgument(func, var))
			{
				os << *arg->getType();
				i = end + 1;
			}
			else
			{
				errorf(location, "Incorrect typeof expression: unknown variable %s", var.c_str());
			}
		}
		else if (body[i] == 'd' && body.compare(i, 8, "declare ") == 0)
		{
			std::string::size_type end = body.find('\n', i);

			if (end == std::string::npos)
				errorf(location, "Incorrect declare expression: newline expected");
			
			declare << std::string(body.begin() + i, body.begin() + end + 1);
			i = end + 1;
		}
		else
		{
			os << body[i];
			i++;
		}
	}

	os << "\nret " << *func->getReturnType() << " %out }";
	os.flush();

	return declare.str() + oss.str();
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

	if (CASE(ExprArraySlice, node))
	{
		llvm::Function* function = builder.GetInsertBlock()->getParent();

		llvm::Value* arr = compileExpr(context, builder, _->arr);

		llvm::Value* arr_data = builder.CreateExtractValue(arr, 0);
		llvm::Value* arr_size = builder.CreateExtractValue(arr, 1);

		llvm::Value* index_start = compileExpr(context, builder, _->index_start);
		llvm::Value* index_end = _->index_end ? builder.CreateAdd(compileExpr(context, builder, _->index_end), builder.getInt32(1)) : arr_size;

		llvm::Type* array_type = compileType(context, _->type, _->location);
		llvm::Type* element_type = array_type->getContainedType(0)->getContainedType(0);

		llvm::Value* length = builder.CreateSub(index_end, index_start);
		llvm::Value* byte_length = builder.CreateMul(length, builder.getInt32(context.layout->getTypeAllocSize(element_type)));

		llvm::Value* arr_slice_data = builder.CreateBitCast(builder.CreateCall(context.module->getFunction("malloc"), byte_length), array_type->getContainedType(0));

		builder.CreateMemCpy(arr_slice_data, builder.CreateGEP(arr_data, index_start), byte_length, 8);

		llvm::Value* arr_slice = llvm::ConstantAggregateZero::get(array_type);
		arr_slice = builder.CreateInsertValue(arr_slice, arr_slice_data, 0);
		arr_slice = builder.CreateInsertValue(arr_slice, length, 1);

		return arr_slice;
	}

	if (CASE(ExprMemberAccess, node))
	{
		llvm::Value *aggr = compileExpr(context, builder, _->aggr);

		if (TypeStructure* struct_type = dynamic_cast<TypeStructure*>(finalType(_->aggr->type)))
		{
			assert(struct_type->member_names.size() == struct_type->member_types.size());
			for (size_t i = 0; i < struct_type->member_names.size(); i++)
			{
				if (struct_type->member_names[i] == _->member_name)
					return builder.CreateExtractValue(aggr, i);
			}

			errorf(_->location, "Type doesn't have a member named '%s'", _->member_name.c_str());
		}

		errorf(_->location, "Cannot access members of a type that is not a structure");
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

	if (CASE(ExprStructConstructorFunc, node))
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

		llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context.context, "entry", func);

		llvm::IRBuilder<> funcbuilder(bb);

		llvm::Value* aggr = llvm::ConstantAggregateZero::get(function_type->getReturnType());

		argi = func->arg_begin();

		for (size_t i = 0; i < func->arg_size(); ++i, ++argi)
			aggr = funcbuilder.CreateInsertValue(aggr, argi, i);

		funcbuilder.CreateRet(aggr);

		llvm::Value* holder = llvm::ConstantAggregateZero::get(general_holder_type);

		holder = builder.CreateInsertValue(holder, compileFunctionThunk(context, _->location, func, general_holder_type, 0), 0);
		holder = builder.CreateInsertValue(holder, llvm::Constant::getNullValue(llvm::Type::getInt8PtrTy(*context.context)), 1);

		assert(context.values.count(_->target) == 0);
		context.values[_->target] = holder;

		return holder;
	}

	if (CASE(ExprUnionConstructorFunc, node))
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

		llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context.context, "entry", func);

		llvm::IRBuilder<> funcbuilder(bb);

		llvm::Value* aggr = llvm::ConstantAggregateZero::get(function_type->getReturnType());

		// Save type ID
		aggr = funcbuilder.CreateInsertValue(aggr, funcbuilder.getInt32(_->member_id), 0);

		// Create union storage
		llvm::Type* member_type = compileType(context, _->member_type, _->location);
		llvm::Type* member_ref_type = llvm::PointerType::getUnqual(member_type);
		llvm::Value* data = funcbuilder.CreateCall(context.module->getFunction("malloc"), funcbuilder.getInt32(uint32_t(context.layout->getTypeAllocSize(member_type))));

		argi = func->arg_begin();

		if (!_->args.empty())
		{
			llvm::Value* typed_data = funcbuilder.CreateBitCast(data, member_ref_type);

			if (_->args.size() > 1)
			{
				for (size_t i = 0; i < _->args.size(); ++i, ++argi)
					funcbuilder.CreateStore(argi, funcbuilder.CreateStructGEP(typed_data, i));
			}
			else
			{
				funcbuilder.CreateStore(argi, typed_data);
			}
		}
		
		aggr = funcbuilder.CreateInsertValue(aggr, data, 1);
		
		funcbuilder.CreateRet(aggr);

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
		std::string body = compileInlineLLVM(name, _->body, func, _->location);

		llvm::SMDiagnostic err;
		if (!llvm::ParseAssemblyString(body.c_str(), context.module, err, *context.context))
			errorf(_->location, "Failed to parse llvm inline code: %s at '%s'", err.getMessage().c_str(), err.getLineContents().c_str());

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

		return builder.getInt32(0); // Same as ExprUnit
	}

	if (CASE(ExprMatchWith, node))
	{
		llvm::Function* function = builder.GetInsertBlock()->getParent();

		llvm::Value* variable = compileExpr(context, builder, _->variable);

		llvm::Value* type_id = builder.CreateExtractValue(variable, 0);
		llvm::Value* type_ptr = builder.CreateExtractValue(variable, 1);

		TypeUnion* union_type = dynamic_cast<TypeUnion*>(finalType(_->variable->type));

		// All union type variants must be covered
		if (_->cases.size() < union_type->member_names.size())
		{
			for (size_t i = 0; i < union_type->member_names.size(); ++i)
			{
				bool found = false;
				for (size_t k = 0; k < _->cases.size() && !found; ++k)
				{
					if (dynamic_cast<MatchCaseUnion*>(_->cases[k])->tag == i)
						found = true;
				}
				if (!found)
					errorf(_->location, "Incomplete pattern matches: missing case for tag '%s'", union_type->member_names[i].c_str());
			}
		}

		// Expression result
		llvm::Value* value = builder.CreateAlloca(compileType(context, _->type, _->location));
		
		// Create a switch by type ID
		std::vector<llvm::BasicBlock*> case_blocks;
		llvm::BasicBlock* finish_block = llvm::BasicBlock::Create(*context.context, "finish");

		llvm::SwitchInst* switch_inst = builder.CreateSwitch(type_id, finish_block, _->cases.size());

		for (size_t i = 0; i < _->cases.size(); ++i)
		{
			MatchCaseUnion* casei = dynamic_cast<MatchCaseUnion*>(_->cases[i]);

			uint32_t index = casei->tag;

			case_blocks.push_back(llvm::BasicBlock::Create(*context.context, "case_" + union_type->member_names[index]));

			switch_inst->addCase(builder.getInt32(index), case_blocks.back());

			function->getBasicBlockList().push_back(case_blocks.back());
			builder.SetInsertPoint(case_blocks.back());

			context.values[casei->alias] = builder.CreateLoad(builder.CreateBitCast(type_ptr, llvm::PointerType::getUnqual(compileType(context, union_type->member_types[index], Location()))));

			builder.CreateStore(compileExpr(context, builder, _->expressions[i]), value);
			builder.CreateBr(finish_block);
		}

		function->getBasicBlockList().push_back(finish_block);
		builder.SetInsertPoint(finish_block);

		return builder.CreateLoad(value);
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
