#include "compiler.hpp"

#include "parser.hpp"
#include "output.hpp"
#include "typecheck.hpp"
#include "match.hpp"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Assembly/Parser.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/IR/Intrinsics.h"

#include <exception>
#include <cassert>
#include <sstream>

struct Context
{
	llvm::LLVMContext* context;
	llvm::Module* module;
	llvm::DataLayout* layout;

	std::map<BindingTarget*, llvm::Value*> values;
	std::map<BindingTarget*, std::pair<Expr*, llvm::Value*> > functions;
	std::map<std::pair<Expr*, std::string>, llvm::Function*> function_instances;
	std::map<llvm::Function*, llvm::Function*> function_thunks;
	std::map<std::string, llvm::Type*> types;

	std::vector<llvm::Type*> function_context_type;
	std::vector<std::string> function_mangled_type;

	std::vector<std::pair<Type*, std::pair<Type*, llvm::Type*> > > generic_instances;
};

Type* getTypeInstance(Context& context, Type* type, const Location& location)
{
	type = finalType(type);

	if (CASE(TypeGeneric, type))
	{
		for (size_t i = 0; i < context.generic_instances.size(); ++i)
			if (type == context.generic_instances[i].first)
				return context.generic_instances[i].second.first;

		errorf(location, "No instance of the generic type '%s found", _->name.empty() ? "a" : _->name.c_str());
	}

	return type;
}

llvm::Type* compileType(Context& context, Type* type, const Location& location);

llvm::Value* compileEqualityOperator(const Location& location, Context& context, llvm::IRBuilder<>& builder, llvm::Value* left, llvm::Value* right, Type* type);

llvm::Type* compileTypePrototype(Context& context, TypePrototype* proto, const Location& location, const std::string& mtype)
{
	if (CASE(TypePrototypeRecord, proto))
	{
		std::vector<llvm::Type*> members;

		llvm::StructType* struct_type = llvm::StructType::create(*context.context, _->name + ".." + mtype);

		context.types[mtype] = struct_type;

		for (size_t i = 0; i < _->member_types.size(); ++i)
			members.push_back(compileType(context, _->member_types[i], location));

		struct_type->setBody(members);

		return struct_type;
	}

	if (CASE(TypePrototypeUnion, proto))
	{
		std::vector<llvm::Type*> members;

		members.push_back(llvm::Type::getInt32Ty(*context.context)); // Current type ID
		members.push_back(llvm::Type::getInt8PtrTy(*context.context)); // Pointer to union data

		return context.types[mtype] = llvm::StructType::create(*context.context, members, _->name + ".." + mtype);
	}

	assert(!"Unknown prototype type");
	return 0;
}

llvm::Type* compileType(Context& context, Type* type, const Location& location)
{
	type = finalType(type);

	if (CASE(TypeGeneric, type))
	{
		for (size_t i = 0; i < context.generic_instances.size(); ++i)
			if (type == context.generic_instances[i].first)
				return context.generic_instances[i].second.second;

		errorf(location, "No instance of the generic type '%s found", _->name.empty() ? "a" : _->name.c_str());
	}

	if (CASE(TypeUnit, type))
	{
		// this might be void in the future
		return llvm::Type::getInt32Ty(*context.context);
	}

	if (CASE(TypeInt, type))
	{
		return llvm::Type::getInt32Ty(*context.context);
	}

	if (CASE(TypeFloat, type))
	{
		return llvm::Type::getFloatTy(*context.context);
	}

	if (CASE(TypeBool, type))
	{
		return llvm::Type::getInt1Ty(*context.context);
	}
	
	if (CASE(TypeClosureContext, type))
	{
		std::vector<llvm::Type*> members;

		for (size_t i = 0; i < _->member_types.size(); ++i)
			members.push_back(compileType(context, _->member_types[i], location));

		return llvm::PointerType::getUnqual(llvm::StructType::get(*context.context, members));
	}

	if (CASE(TypeTuple, type))
	{
		std::vector<llvm::Type*> members;

		for (size_t i = 0; i < _->members.size(); ++i)
			members.push_back(compileType(context, _->members[i], location));

		return llvm::StructType::get(*context.context, members);
	}

	if (CASE(TypeArray, type))
	{
		return llvm::StructType::get(llvm::PointerType::getUnqual(compileType(context, _->contained, location)), llvm::Type::getInt32Ty(*context.context), (llvm::Type*)NULL);
	}

	if (CASE(TypeFunction, type))
	{
		std::vector<llvm::Type*> args;

		for (size_t i = 0; i < _->args.size(); ++i)
			args.push_back(compileType(context, _->args[i], location));

		args.push_back(llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(*context.context)));

		llvm::Type* function_type = llvm::FunctionType::get(compileType(context, _->result, location), args, false);

		llvm::StructType* holder_type = llvm::StructType::get(llvm::PointerType::getUnqual(function_type), llvm::Type::getInt8PtrTy(*context.context), (llvm::Type*)NULL);

		return holder_type;
	}

	if (CASE(TypeInstance, type))
	{
		// compute mangled instance type name
		std::string mtype = typeNameMangled(type, [&](TypeGeneric* tg) { return getTypeInstance(context, tg, location); } );

		if (context.types.count(mtype) > 0)
			return context.types[mtype];

		size_t generic_type_count = context.generic_instances.size();

		const std::vector<Type*>& generics = getGenericTypes(_->prototype);
		assert(generics.size() == _->generics.size());

		for (size_t i = 0; i < _->generics.size(); ++i)
			context.generic_instances.push_back(std::make_pair(generics[i], std::make_pair(_->generics[i], compileType(context, _->generics[i], location))));

		llvm::Type* result = compileTypePrototype(context, _->prototype, location, mtype);

		context.generic_instances.resize(generic_type_count);

		return result;
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

llvm::Function* compileFunctionThunk(Context& context, llvm::Function* target, llvm::Type* funcptr_type)
{
	llvm::FunctionType* thunk_type = llvm::cast<llvm::FunctionType>(funcptr_type->getContainedType(0)->getContainedType(0));

	if (context.function_thunks.count(target) > 0)
	{
		llvm::Function* result = context.function_thunks[target];
		assert(result->getFunctionType() == thunk_type);
		return result;
	}

	llvm::Function* thunk_func = llvm::Function::Create(thunk_type, llvm::Function::InternalLinkage, target->getName() + "..thunk", context.module);
	assert(thunk_func->arg_size() == target->arg_size() || thunk_func->arg_size() == target->arg_size() + 1);

	llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context.context, "entry", thunk_func);
	llvm::IRBuilder<> builder(bb);

	std::vector<llvm::Value*> args;

	// add all target arguments
	assert(thunk_func->arg_size() >= target->arg_size());

	llvm::Function::arg_iterator argi = thunk_func->arg_begin();

	for (size_t i = 0; i < target->arg_size(); ++i, ++argi)
		args.push_back(argi);

	// cast context argument to correct type
	if (thunk_func->arg_size() == target->arg_size())
	{
		args.back() = builder.CreatePointerCast(args.back(), target->getArgumentList().back().getType());
	}

	builder.CreateRet(builder.CreateCall(target, args));

	return context.function_thunks[target] = thunk_func;
}

llvm::Value* compileFunctionValue(Context& context, llvm::IRBuilder<>& builder, llvm::Function* target, Type* type, llvm::Value* context_ref, const Location& location)
{
	llvm::Type* funcptr_type = compileType(context, type, location);

	llvm::Function* thunk = compileFunctionThunk(context, target, funcptr_type);

	llvm::Value* result = llvm::ConstantAggregateZero::get(funcptr_type);
	
	result = builder.CreateInsertValue(result, thunk, 0);

	if (context_ref)
	{
		llvm::Value* context_ref_opaque = builder.CreateBitCast(context_ref, llvm::Type::getInt8PtrTy(*context.context));

		result = builder.CreateInsertValue(result, context_ref_opaque, 1);
	}

	return result;
}

void instantiateGenericTypes(Context& context, Type* generic, Type* instance, const Location& location)
{
	generic = finalType(generic);
	instance = finalType(instance);

	if (CASE(TypeGeneric, generic))
	{
		context.generic_instances.push_back(std::make_pair(generic, std::make_pair(instance, compileType(context, instance, location))));
	}

	if (CASE(TypeArray, generic))
	{
		TypeArray* inst = dynamic_cast<TypeArray*>(instance);

		instantiateGenericTypes(context, _->contained, inst->contained, location);
	}

	if (CASE(TypeFunction, generic))
	{
		TypeFunction* inst = dynamic_cast<TypeFunction*>(instance);

		instantiateGenericTypes(context, _->result, inst->result, location);

		assert(_->args.size() == inst->args.size());

		for (size_t i = 0; i < _->args.size(); ++i)
			instantiateGenericTypes(context, _->args[i], inst->args[i], location);
	}

	if (CASE(TypeInstance, generic))
	{
		TypeInstance* inst = dynamic_cast<TypeInstance*>(instance);

		assert(_->prototype == inst->prototype);
		assert(_->generics.size() == inst->generics.size());

		for (size_t i = 0; i < _->generics.size(); ++i)
			instantiateGenericTypes(context, _->generics[i], inst->generics[i], location);
	}

	if (CASE(TypeTuple, generic))
	{
		TypeTuple* inst = dynamic_cast<TypeTuple*>(instance);

		assert(_->members.size() == inst->members.size());

		for (size_t i = 0; i < _->members.size(); ++i)
			instantiateGenericTypes(context, _->members[i], inst->members[i], location);
	}
}

llvm::Value* compileExpr(Context& context, llvm::IRBuilder<>& builder, Expr* node);

llvm::Function* compileRegularFunction(Context& context, ExprLetFunc* node, const std::string& mtype)
{
	llvm::FunctionType* function_type = compileFunctionType(context, node->type, node->location, node->context_target ? node->context_target->type : 0);

	llvm::Function* func = llvm::Function::Create(function_type, llvm::GlobalValue::InternalLinkage, node->target->name + ".." + mtype, context.module);

	llvm::Function::arg_iterator argi = func->arg_begin();

	llvm::Value* context_value = 0;

	for (size_t i = 0; i < func->arg_size(); ++i, ++argi)
	{
		if (i < node->args.size())
		{
			argi->setName(node->args[i]->name);
			context.values[node->args[i]] = argi;
		}
		else
		{
			argi->setName("extern");
			context_value = context.values[node->context_target] = argi;
		}
	}

	llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context.context, "entry", func);
	llvm::IRBuilder<> builder(bb);

	// Create function value for use inside the function
	llvm::Value* funcptr = compileFunctionValue(context, builder, func, node->type, context_value, node->location);

	context.values[node->target] = funcptr;

	// Compile function body
	context.function_context_type.push_back(context_value ? context_value->getType() : NULL);
	context.function_mangled_type.push_back(mtype);

	llvm::Value* value = compileExpr(context, builder, node->body);

	context.function_mangled_type.pop_back();
	context.function_context_type.pop_back();

	builder.CreateRet(value);

	context.values.erase(node->target);

	return func;
}

llvm::Function* compileStructConstructor(Context& context, ExprStructConstructorFunc* node, const std::string& mtype)
{
	llvm::FunctionType* function_type = compileFunctionType(context, node->type, node->location, 0);

	llvm::Function* func = llvm::Function::Create(function_type, llvm::GlobalValue::InternalLinkage, node->target->name + ".." + mtype, context.module);

	llvm::Function::arg_iterator argi = func->arg_begin();

	for (size_t i = 0; i < func->arg_size(); ++i, ++argi)
	{
		if (i < node->args.size())
			argi->setName(node->args[i]->name);
	}

	llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context.context, "entry", func);
	llvm::IRBuilder<> builder(bb);

	llvm::Value* aggr = llvm::ConstantAggregateZero::get(function_type->getReturnType());

	argi = func->arg_begin();

	for (size_t i = 0; i < func->arg_size(); ++i, ++argi)
		aggr = builder.CreateInsertValue(aggr, argi, i);

	builder.CreateRet(aggr);

	return func;
}

llvm::Function* compileUnionConstructor(Context& context, ExprUnionConstructorFunc* node, const std::string& mtype)
{
	llvm::FunctionType* function_type = compileFunctionType(context, node->type, node->location, 0);

	llvm::Function* func = llvm::Function::Create(function_type, llvm::GlobalValue::InternalLinkage, node->target->name + ".." + mtype, context.module);

	llvm::Function::arg_iterator argi = func->arg_begin();

	for (size_t i = 0; i < func->arg_size(); ++i, ++argi)
	{
		if (i < node->args.size())
			argi->setName(node->args[i]->name);
	}

	llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context.context, "entry", func);
	llvm::IRBuilder<> builder(bb);

	llvm::Value* aggr = llvm::ConstantAggregateZero::get(function_type->getReturnType());

	// Save type ID
	aggr = builder.CreateInsertValue(aggr, builder.getInt32(node->member_id), 0);

	// Create union storage
	llvm::Type* member_type = compileType(context, node->member_type, node->location);
	llvm::Type* member_ref_type = llvm::PointerType::getUnqual(member_type);

	argi = func->arg_begin();

	if (!node->args.empty())
	{
		llvm::Value* data = builder.CreateCall(context.module->getFunction("malloc"), builder.getInt32(uint32_t(context.layout->getTypeAllocSize(member_type))));
		llvm::Value* typed_data = builder.CreateBitCast(data, member_ref_type);

		if (node->args.size() > 1)
		{
			for (size_t i = 0; i < node->args.size(); ++i, ++argi)
				builder.CreateStore(argi, builder.CreateStructGEP(typed_data, i));
		}
		else
		{
			builder.CreateStore(argi, typed_data);
		}

		aggr = builder.CreateInsertValue(aggr, data, 1);
	}

	builder.CreateRet(aggr);

	return func;
}

llvm::Function* compileFunction(Context& context, Expr* node, const std::string& mtype)
{
	if (CASE(ExprLetFunc, node))
	{
		return compileRegularFunction(context, _, mtype);
	}

	if (CASE(ExprStructConstructorFunc, node))
	{
		return compileStructConstructor(context, _, mtype);
	}

	if (CASE(ExprUnionConstructorFunc, node))
	{
		return compileUnionConstructor(context, _, mtype);
	}

	assert(!"Unknown node type");
	return 0;
}

llvm::Function* compileFunctionInstance(Context& context, Expr* node, Type* instance_type, const Location& location)
{
	// compute mangled instance type name
	// note that it depends on the full mangled type of the parent function to account for type variables that do not
	// affect the signature of this function but do affect the compilation result
	// alternatively we could just mangle together all currently defined type variables
	std::string mtype =
		(context.function_mangled_type.empty() ? "" : context.function_mangled_type.back() + ".")
		+ typeNameMangled(instance_type, [&](TypeGeneric* tg) { return getTypeInstance(context, tg, location); } );

	if (context.function_instances.count(std::make_pair(node, mtype)))
		return context.function_instances[std::make_pair(node, mtype)];

	// node->type is the generic type, and type is the instance. Instantiate all types into context, following the shape of the type.
	size_t generic_type_count = context.generic_instances.size();
	
	instantiateGenericTypes(context, node->type, instance_type, location);

	// compile function body given a non-generic type
	llvm::Function* func = compileFunction(context, node, mtype);

	// remove generic type instantiations
	context.generic_instances.resize(generic_type_count);

	return context.function_instances[std::make_pair(node, mtype)] = func;
}

llvm::Value* compileBinding(Context& context, llvm::IRBuilder<>& builder, BindingBase* binding, Type* type, const Location& location)
{
	if (CASE(BindingFunction, binding))
	{
		if (context.values.count(_->target) > 0)
			return context.values[_->target];

		if (context.functions.count(_->target) > 0)
		{
			// Compile function instantiation
			std::pair<Expr*, llvm::Value*> p = context.functions[_->target];
			Expr* node = p.first;
			llvm::Value* context_data = p.second;

			llvm::Function* func = compileFunctionInstance(context, node, type, location);

			// Create function value for use outside the function
			llvm::Value* funcptr = compileFunctionValue(context, builder, func, type, context_data, node->location);

			return funcptr;
		}

		errorf(location, "Variable %s has not been computed", _->target->name.c_str());
	}

	if (CASE(BindingLocal, binding))
	{
		if (context.values.count(_->target) > 0)
			return context.values[_->target];

		errorf(location, "Variable %s has not been computed", _->target->name.c_str());
	}

	errorf(location, "Variable binding has not been resolved");
}

llvm::Value* findFunctionArgument(llvm::Function* func, const std::string& name)
{
	for (llvm::Function::arg_iterator argi = func->arg_begin(); argi != func->arg_end(); ++argi)
		if (argi->getName() == name)
			return argi;

	return NULL;
}

llvm::Type* parseInlineLLVMType(Context& context, const std::string& name, llvm::Function* func, const Location& location)
{
	if (name[0] == '%')
	{
		if (llvm::Value* arg = findFunctionArgument(func, name.substr(1)))
		{
			return arg->getType();
		}

		errorf(location, "Incorrect type expression %s: unknown variable", name.c_str());
	}

	if (name[0] == '\'')
	{
		for (size_t i = context.generic_instances.size(); i > 0; --i)
		{
			TypeGeneric* type = dynamic_cast<TypeGeneric*>(context.generic_instances[i - 1].first);

			if (!type->name.empty() && type->name == name.substr(1))
				return context.generic_instances[i - 1].second.second;
		}

		errorf(location, "Incorrect type expression %s: unknown type variable", name.c_str());
	}

	errorf(location, "Incorrect type expression %s: expected %% or '", name.c_str());
}

std::string compileInlineLLVM(Context& context, const std::string& name, const std::string& body, llvm::Function* func, const Location& location)
{
	std::ostringstream declare;
	std::ostringstream oss;
	llvm::raw_os_ostream os(oss);

	os << "define internal " << *func->getReturnType() << " @" << name << "(";

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
		if (body[i] == 't' && body.compare(i, 7, "typeof(") == 0)
		{
			std::string::size_type end = body.find(')', i);

			if (end == std::string::npos)
				errorf(location, "Incorrect typeof expression: closing brace expected");

			std::string var(body.begin() + i + 7, body.begin() + end);

			i = end + 1;

			os << *parseInlineLLVMType(context, var, func, location);
		}
		else if (body[i] == 's' && body.compare(i, 7, "sizeof(") == 0)
		{
			std::string::size_type end = body.find(')', i);

			if (end == std::string::npos)
				errorf(location, "Incorrect sizeof expression: closing brace expected");

			std::string var(body.begin() + i + 7, body.begin() + end);

			i = end + 1;

			os << context.layout->getTypeAllocSize(parseInlineLLVMType(context, var, func, location));
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

llvm::Value* compileExpr(Context& context, llvm::IRBuilder<>& builder, Expr* node);

void compileMatch(Context& context, llvm::IRBuilder<>& builder, MatchCase* case_, llvm::Value* value, llvm::Value* target, Expr* rhs, llvm::BasicBlock* on_fail, llvm::BasicBlock* on_success)
{
	if (CASE(MatchCaseAny, case_))
	{
		if (_->alias)
			context.values[_->alias] = value;

		if (target)
			builder.CreateStore(compileExpr(context, builder, rhs), target);

		builder.CreateBr(on_success);
	}
	else if (CASE(MatchCaseNumber, case_))
	{
		llvm::Function* function = builder.GetInsertBlock()->getParent();

		llvm::Value* cond = builder.CreateICmpEQ(value, builder.CreateIntCast(builder.getInt32(uint32_t(_->number)), compileType(context, _->type, _->location), false));

		llvm::BasicBlock* success = llvm::BasicBlock::Create(*context.context, "success", function);

		builder.CreateCondBr(cond, success, on_fail);

		builder.SetInsertPoint(success);

		if (target)
			builder.CreateStore(compileExpr(context, builder, rhs), target);

		builder.CreateBr(on_success);
	}
	else if (CASE(MatchCaseValue, case_))
	{
		llvm::Function* function = builder.GetInsertBlock()->getParent();

		llvm::Value* cond = compileEqualityOperator(_->location, context, builder, compileBinding(context, builder, _->value, finalType(_->type), _->location), value, finalType(_->type));

		llvm::BasicBlock* success = llvm::BasicBlock::Create(*context.context, "success", function);

		builder.CreateCondBr(cond, success, on_fail);

		builder.SetInsertPoint(success);

		if (target)
			builder.CreateStore(compileExpr(context, builder, rhs), target);

		builder.CreateBr(on_success);
	}
	else if (CASE(MatchCaseArray, case_))
	{
		TypeArray* arr_type = dynamic_cast<TypeArray*>(finalType(_->type));
		if (!arr_type)
			errorf(_->location, "array type is unknown");

		llvm::Function* function = builder.GetInsertBlock()->getParent();

		llvm::Value* size = builder.CreateExtractValue(value, 1);

		llvm::Value* cond = builder.CreateICmpEQ(size, builder.getInt32(uint32_t(_->elements.size())));

		llvm::BasicBlock* success_size = llvm::BasicBlock::Create(*context.context, "success_size", function);
		llvm::BasicBlock* success_all = llvm::BasicBlock::Create(*context.context, "success_all");

		builder.CreateCondBr(cond, success_size, on_fail);
		builder.SetInsertPoint(success_size);

		for (size_t i = 0; i < _->elements.size(); ++i)
		{
			llvm::Value* element = builder.CreateLoad(builder.CreateGEP(builder.CreateExtractValue(value, 0), builder.getInt32(uint32_t(i))), false);

			llvm::BasicBlock* next_check = i != _->elements.size() - 1 ? llvm::BasicBlock::Create(*context.context, "next_check") : success_all;

			compileMatch(context, builder, _->elements[i], element, 0, 0, on_fail, next_check);

			function->getBasicBlockList().push_back(next_check);
			builder.SetInsertPoint(next_check);
		}

		if (target)
			builder.CreateStore(compileExpr(context, builder, rhs), target);

		builder.CreateBr(on_success);
	}
	else if (CASE(MatchCaseMembers, case_))
	{
		llvm::Function* function = builder.GetInsertBlock()->getParent();

		llvm::BasicBlock* success_all = llvm::BasicBlock::Create(*context.context, "success_all");

		TypeInstance* inst_type = dynamic_cast<TypeInstance*>(finalType(_->type));
		TypePrototypeRecord* record_type = inst_type ? dynamic_cast<TypePrototypeRecord*>(inst_type->prototype) : 0;

		if (record_type)
		{
			for (size_t i = 0; i < _->member_values.size(); ++i)
			{
				llvm::BasicBlock* next_check = i != _->member_values.size() - 1 ? llvm::BasicBlock::Create(*context.context, "next_check") : success_all;

				size_t id = ~0u;

				if (!_->member_names.empty())
					id = getMemberIndexByName(record_type, _->member_names[i], _->location);

				llvm::Value* element = builder.CreateExtractValue(value, id == ~0u ? i : id);

				compileMatch(context, builder, _->member_values[i], element, 0, 0, on_fail, next_check);

				function->getBasicBlockList().push_back(next_check);
				builder.SetInsertPoint(next_check);
			}
		}
		else if(TypeTuple* tuple_type = dynamic_cast<TypeTuple*>(finalType(_->type)))
		{
			for (size_t i = 0; i < _->member_values.size(); ++i)
			{
				llvm::BasicBlock* next_check = i != _->member_values.size() - 1 ? llvm::BasicBlock::Create(*context.context, "next_check") : success_all;

				llvm::Value* element = builder.CreateExtractValue(value, i);

				compileMatch(context, builder, _->member_values[i], element, 0, 0, on_fail, next_check);

				function->getBasicBlockList().push_back(next_check);
				builder.SetInsertPoint(next_check);
			}
		}
		else
		{
			if (_->member_values.size() > 1)
			{
				PrettyPrintContext context;
				std::string name = typeName(finalType(_->type), context);

				errorf(_->member_values[1]->location, "Type %s has no members", name.c_str());
			}

			// This must be a union tag that is a type alias
			assert(_->member_values.size() == 1);

			compileMatch(context, builder, _->member_values[0], value, 0, 0, on_fail, success_all);

			function->getBasicBlockList().push_back(success_all);
			builder.SetInsertPoint(success_all);
		}

		if (target)
			builder.CreateStore(compileExpr(context, builder, rhs), target);

		builder.CreateBr(on_success);
	}
	else if (CASE(MatchCaseUnion, case_))
	{
		TypeInstance* inst_type = dynamic_cast<TypeInstance*>(finalType(_->type));
		TypePrototypeUnion* union_type = inst_type ? dynamic_cast<TypePrototypeUnion*>(inst_type->prototype) : 0;

		if (!union_type)
			errorf(_->location, "union type is unknown");

		llvm::Function* function = builder.GetInsertBlock()->getParent();

		llvm::Value* type_id = builder.CreateExtractValue(value, 0);
		llvm::Value* type_ptr = builder.CreateExtractValue(value, 1);

		llvm::Value* cond = builder.CreateICmpEQ(type_id, builder.getInt32(uint32_t(_->tag)));

		llvm::BasicBlock* success_tag = llvm::BasicBlock::Create(*context.context, "success_tag", function);
		llvm::BasicBlock* success_all = llvm::BasicBlock::Create(*context.context, "success_all");

		builder.CreateCondBr(cond, success_tag, on_fail);
		builder.SetInsertPoint(success_tag);

		Type* type = getMemberTypeByIndex(inst_type, union_type, _->tag, _->location);

		llvm::Value* element = builder.CreateLoad(builder.CreateBitCast(type_ptr, llvm::PointerType::getUnqual(compileType(context, type, _->location))));

		compileMatch(context, builder, _->pattern, element, 0, 0, on_fail, success_all);

		function->getBasicBlockList().push_back(success_all);
		builder.SetInsertPoint(success_all);

		if (target)
			builder.CreateStore(compileExpr(context, builder, rhs), target);

		builder.CreateBr(on_success);
	}
	else if (CASE(MatchCaseOr, case_))
	{
		llvm::Function* function = builder.GetInsertBlock()->getParent();

		llvm::BasicBlock* success_any = llvm::BasicBlock::Create(*context.context, "success_any");

		std::vector<llvm::BasicBlock*> incoming;

		for (size_t i = 0; i < _->options.size(); ++i)
		{
			llvm::BasicBlock* next_check = i != _->options.size() - 1 ? llvm::BasicBlock::Create(*context.context, "next_check") : on_fail;

			compileMatch(context, builder, _->options[i], value, 0, 0, next_check, success_any);

			incoming.push_back(&function->getBasicBlockList().back());

			if (next_check != on_fail)
			{
				function->getBasicBlockList().push_back(next_check);
				builder.SetInsertPoint(next_check);
			}
		}

		function->getBasicBlockList().push_back(success_any);
		builder.SetInsertPoint(success_any);

		// Merge all variants for binding into actual binding used in the expression
		for (size_t i = 0; i < _->binding_actual.size(); ++i)
		{
			llvm::PHINode* pn = builder.CreatePHI(compileType(context, _->binding_actual[i]->type, Location()), _->binding_alternatives.size());

			for (size_t k = 0; k < _->binding_alternatives.size(); ++k)
				pn->addIncoming(compileBinding(context, builder, new BindingLocal(_->binding_alternatives[k][i]), _->binding_alternatives[k][i]->type, Location()), incoming[k]);

			context.values[_->binding_actual[i]] = pn;
		}

		if (target)
			builder.CreateStore(compileExpr(context, builder, rhs), target);

		builder.CreateBr(on_success);
	}
	else if (CASE(MatchCaseIf, case_))
	{
		llvm::Function* function = builder.GetInsertBlock()->getParent();

		llvm::BasicBlock* success_pattern = llvm::BasicBlock::Create(*context.context, "success_pattern");
		llvm::BasicBlock* success_cond = llvm::BasicBlock::Create(*context.context, "success_cond");

		compileMatch(context, builder, _->match, value, 0, 0, on_fail, success_pattern);

		function->getBasicBlockList().push_back(success_pattern);
		builder.SetInsertPoint(success_pattern);

		llvm::Value* condition = compileExpr(context, builder, _->condition);

		builder.CreateCondBr(condition, success_cond, on_fail);

		function->getBasicBlockList().push_back(success_cond);
		builder.SetInsertPoint(success_cond);

		if (target)
			builder.CreateStore(compileExpr(context, builder, rhs), target);

		builder.CreateBr(on_success);
	}
	else
	{
		assert(!"Unknown MatchCase node");
	}
}

llvm::Value* compileStructEqualityOperator(const Location& location, Context& context, llvm::IRBuilder<>& builder, llvm::Value* left, llvm::Value* right, std::vector<Type*> types)
{
	llvm::Function* function = builder.GetInsertBlock()->getParent();

	llvm::BasicBlock* compare_last = builder.GetInsertBlock();
	llvm::BasicBlock* compare_end = llvm::BasicBlock::Create(*context.context, "compare_end");

	std::vector<llvm::BasicBlock*> fail_block;

	for (size_t i = 0; i < types.size(); ++i)
	{
		llvm::BasicBlock* compare_next = llvm::BasicBlock::Create(*context.context, "compare_next");

		builder.CreateCondBr(compileEqualityOperator(location, context, builder, builder.CreateExtractValue(left, i), builder.CreateExtractValue(right, i), types[i]), compare_next, compare_end);
				
		fail_block.push_back(builder.GetInsertBlock());
		compare_last = compare_next;
		function->getBasicBlockList().push_back(compare_next);
		builder.SetInsertPoint(compare_next);
	}

	builder.CreateBr(compare_end);

	// Result computation node
	function->getBasicBlockList().push_back(compare_end);
	builder.SetInsertPoint(compare_end);

	llvm::PHINode* result = builder.CreatePHI(builder.getInt1Ty(), 1 + fail_block.size());

	// Comparison is only successful if we came here from the last node
	result->addIncoming(builder.getInt1(true), compare_last);
	// If we came from any other block, it was from a member comparison failure
	for (size_t i = 0; i < fail_block.size(); ++i)
		result->addIncoming(builder.getInt1(false), fail_block[i]);

	return result;
}

llvm::Value* compileUnionEqualityOperator(const Location& location, Context& context, llvm::IRBuilder<>& builder, llvm::Value* left, llvm::Value* right, TypePrototypeUnion* proto)
{
	llvm::Function* function = builder.GetInsertBlock()->getParent();

	llvm::Value* left_tag = builder.CreateExtractValue(left, 0);
	llvm::Value* left_ptr = builder.CreateExtractValue(left, 1);

	llvm::Value* right_tag = builder.CreateExtractValue(right, 0);
	llvm::Value* right_ptr = builder.CreateExtractValue(right, 1);

	llvm::BasicBlock* compare_start = builder.GetInsertBlock();
	llvm::BasicBlock* compare_data = llvm::BasicBlock::Create(*context.context, "compare_data");
	llvm::BasicBlock* compare_end = llvm::BasicBlock::Create(*context.context, "compare_end");

	// Check tag equality
	builder.CreateCondBr(builder.CreateICmpEQ(left_tag, right_tag), compare_data, compare_end);

	// Switch by union tag
	function->getBasicBlockList().push_back(compare_data);
	builder.SetInsertPoint(compare_data);

	llvm::SwitchInst *swichInst = builder.CreateSwitch(left_tag, compare_end, proto->member_types.size());

	std::vector<llvm::Value*> case_results;
	std::vector<llvm::BasicBlock*> case_blocks;

	for (size_t i = 0; i < proto->member_types.size(); ++i)
	{
		llvm::BasicBlock* compare_tag = llvm::BasicBlock::Create(*context.context, "compare_tag", function);
		builder.SetInsertPoint(compare_tag);

		llvm::Value* left_elem = builder.CreateLoad(builder.CreateBitCast(left_ptr, llvm::PointerType::getUnqual(compileType(context, proto->member_types[i], location))));
		llvm::Value* right_elem = builder.CreateLoad(builder.CreateBitCast(right_ptr, llvm::PointerType::getUnqual(compileType(context, proto->member_types[i], location))));

		case_results.push_back(compileEqualityOperator(location, context, builder, left_elem, right_elem, proto->member_types[i]));
		builder.CreateBr(compare_end);
		
		case_blocks.push_back(builder.GetInsertBlock());

		swichInst->addCase(builder.getInt32(i), compare_tag);
	}

	// Result computation node
	function->getBasicBlockList().push_back(compare_end);
	builder.SetInsertPoint(compare_end);

	llvm::PHINode* result = builder.CreatePHI(builder.getInt1Ty(), 2 + proto->member_types.size());

	// Tags are not equal
	result->addIncoming(builder.getInt1(false), compare_start);
	// Tag is invalid
	result->addIncoming(builder.getInt1(false), compare_data);
	// For other cases, take the result of the other comparisons
	for (size_t i = 0; i < proto->member_types.size(); ++i)
		result->addIncoming(case_results[i], case_blocks[i]);

	return result;
}

llvm::Value* compileArrayEqualityOperator(const Location& location, Context& context, llvm::IRBuilder<>& builder, llvm::Value* left, llvm::Value* right, TypeArray* type)
{
	llvm::Function* function = builder.GetInsertBlock()->getParent();

	llvm::Value* left_arr = builder.CreateExtractValue(left, 0);
	llvm::Value* left_size = builder.CreateExtractValue(left, 1);

	llvm::Value* right_arr = builder.CreateExtractValue(right, 0);
	llvm::Value* right_size = builder.CreateExtractValue(right, 1);

	llvm::BasicBlock* compare_size = builder.GetInsertBlock();
	llvm::BasicBlock* compare_content = llvm::BasicBlock::Create(*context.context, "compare_content");
	llvm::BasicBlock* compare_end = llvm::BasicBlock::Create(*context.context, "compare_end");
	llvm::BasicBlock* compare_elem = llvm::BasicBlock::Create(*context.context, "compare_elem");
	llvm::BasicBlock* compare_content_end = llvm::BasicBlock::Create(*context.context, "compare_content_end");

	// TODO: compare array pointers to determine equality immediately

	builder.CreateCondBr(builder.CreateICmpEQ(left_size, right_size), compare_content, compare_end);

	// Content computation node
	function->getBasicBlockList().push_back(compare_content);
	builder.SetInsertPoint(compare_content);

	llvm::PHINode* index = builder.CreatePHI(builder.getInt32Ty(), 2);
	index->addIncoming(builder.getInt32(0), compare_size);

	builder.CreateCondBr(builder.CreateICmpULT(index, left_size), compare_elem, compare_end);

	// Element computation node
	function->getBasicBlockList().push_back(compare_elem);
	builder.SetInsertPoint(compare_elem);

	llvm::Value* left_elem = builder.CreateLoad(builder.CreateGEP(left_arr, index));
	llvm::Value* right_elem = builder.CreateLoad(builder.CreateGEP(right_arr, index));

	builder.CreateCondBr(compileEqualityOperator(location, context, builder, left_elem, right_elem, type->contained), compare_content_end, compare_end);

	// Index increment node
	function->getBasicBlockList().push_back(compare_content_end);
	builder.SetInsertPoint(compare_content_end);

	llvm::Value *next_index = builder.CreateAdd(index, builder.getInt32(1), "", true, true);
	index->addIncoming(next_index, compare_content_end);

	builder.CreateBr(compare_content);

	// Result computation node
	function->getBasicBlockList().push_back(compare_end);
	builder.SetInsertPoint(compare_end);

	llvm::PHINode* result = builder.CreatePHI(builder.getInt1Ty(), 3);

	// If we got here after we checked for equality of all array elements and succedeed, that means arrays are equal
	result->addIncoming(builder.getInt1(true), compare_content);
	// If we got here from the block where the size comparison happened and failed, that means arrays were not equal
	result->addIncoming(builder.getInt1(false), compare_size);
	// If we got here from the block where the array elements are compared and failed, that means arrays were not equal
	result->addIncoming(builder.getInt1(false), compare_elem);

	return result;
}

llvm::Value* compileEqualityOperator(const Location& location, Context& context, llvm::IRBuilder<>& builder, llvm::Value* left, llvm::Value* right, Type* type)
{
	if (CASE(TypeUnit, type))
		return builder.getInt1(true);

	if (CASE(TypeInt, type))
		return builder.CreateICmpEQ(left, right);

	if (CASE(TypeFloat, type))
		return builder.CreateFCmpOEQ(left, right);

	if (CASE(TypeBool, type))
		return builder.CreateICmpEQ(left, right);

	if (CASE(TypeArray, type))
		return compileArrayEqualityOperator(location, context, builder, left, right, _);

	if (CASE(TypeFunction, type))
		errorf(location, "Cannot compare functions"); // Feel free to implement

	if (CASE(TypeTuple, type))
		return compileStructEqualityOperator(location, context, builder, left, right, _->members);

	if (CASE(TypeInstance, type))
	{
		auto instance = _;

		if (CASE(TypePrototypeRecord, instance->prototype))
			return compileStructEqualityOperator(location, context, builder, left, right, _->member_types);

		if (CASE(TypePrototypeUnion, instance->prototype))
			return compileUnionEqualityOperator(location, context, builder, left, right, _);

		assert(!"Unknown type prototype");
		return 0;
	}

	assert(!"Unknown type in comparison");
	return 0;
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

	if (CASE(ExprTupleLiteral, node))
	{
		if (!dynamic_cast<TypeTuple*>(finalType(_->type)))
			errorf(_->location, "tuple type is unknown");

		llvm::Type* tuple_type = compileType(context, _->type, _->location);

		llvm::Value* tuple = llvm::ConstantAggregateZero::get(tuple_type);

		for (size_t i = 0; i < _->elements.size(); i++)
			tuple = builder.CreateInsertValue(tuple, compileExpr(context, builder, _->elements[i]), uint32_t(i));

		return tuple;
	}

	if (CASE(ExprBinding, node))
	{
		return compileBinding(context, builder, _->binding, _->type, _->location);
	}

	if (CASE(ExprBindingExternal, node))
	{
		llvm::Value *value = compileBinding(context, builder, _->context, _->type, _->location);

		value = builder.CreatePointerCast(value, context.function_context_type.back());

		llvm::Value *result = builder.CreateLoad(builder.CreateStructGEP(value, _->member_index));
		
		if (llvm::LoadInst *load = llvm::dyn_cast_or_null<llvm::LoadInst>(result))
		{
			llvm::SmallVector<llvm::Value *, 1> Elts;
			load->setMetadata("invariant.load", llvm::MDNode::get(*context.context, Elts));
		}

		return result;
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
		case SynBinaryOpEqual: return compileEqualityOperator(_->location, context, builder, lv, rv, finalType(_->left->type));
		case SynBinaryOpNotEqual: return builder.CreateNot(compileEqualityOperator(_->location, context, builder, lv, rv, finalType(_->left->type)));
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

		TypeInstance* inst_type = dynamic_cast<TypeInstance*>(getTypeInstance(context, _->aggr->type, _->aggr->location));
		TypePrototypeRecord* record_type = inst_type ? dynamic_cast<TypePrototypeRecord*>(inst_type->prototype) : 0;

		if (record_type)
			return builder.CreateExtractValue(aggr, getMemberIndexByName(record_type, _->member_name, _->location));

		errorf(_->location, "Expected a record type");
	}

	if (CASE(ExprLetVar, node))
	{
		llvm::Value* value = compileExpr(context, builder, _->body);

		value->setName(_->target->name);

		context.values[_->target] = value;

		return value;
	}

	if (CASE(ExprLetVars, node))
	{
		llvm::Value* value = compileExpr(context, builder, _->body);

		for (size_t i = 0; i < _->targets.size(); ++i)
		{
			if (!_->targets[i])
				continue;

			llvm::Value *element = builder.CreateExtractValue(value, i);

			element->setName(_->targets[i]->name);

			context.values[_->targets[i]] = element;
		}

		return builder.getInt32(0);
	}

	if (CASE(ExprLetFunc, node))
	{
		llvm::Value* context_data = 0;

		if (!_->externals.empty())
		{
			llvm::Type* context_ref_type = compileType(context, _->context_target->type, _->location);
			llvm::Type* context_type = context_ref_type->getContainedType(0);

			context_data = builder.CreateBitCast(builder.CreateCall(context.module->getFunction("malloc"), builder.getInt32(uint32_t(context.layout->getTypeAllocSize(context_type)))), context_ref_type);

			for (size_t i = 0; i < _->externals.size(); i++)
				builder.CreateStore(compileExpr(context, builder, _->externals[i]), builder.CreateStructGEP(context_data, i));
		}

		if (_->target->name.empty())
		{
			// anonymous function, compile right now
			llvm::Function* func = compileFunctionInstance(context, _, _->type, _->location);
			llvm::Value* funcptr = compileFunctionValue(context, builder, func, _->type, context_data, _->location);

			context.values[_->target] = funcptr;
			return funcptr;
		}
		else
		{
			// defer function compilation till use site to support generics
			context.functions[_->target] = std::make_pair(_, context_data);

			// TODO: this is really wrong :-/
			return NULL;
		}
	}

	if (CASE(ExprExternFunc, node))
	{
		llvm::FunctionType* function_type = compileFunctionType(context, _->type, _->location, 0);

		llvm::Function* func = llvm::cast<llvm::Function>(context.module->getOrInsertFunction(_->target->name, function_type));

		llvm::Function::arg_iterator argi = func->arg_begin();

		for (size_t i = 0; i < func->arg_size(); ++i, ++argi)
		{
			if (i < _->args.size())
				argi->setName(_->args[i]->name);
		}

		llvm::Value* value = compileFunctionValue(context, builder, func, _->type, NULL, _->location);

		context.values[_->target] = value;

		return value;
	}

	if (CASE(ExprStructConstructorFunc, node))
	{
		// defer function compilation till use site to support generics
		context.functions[_->target] = std::make_pair(_, (llvm::Value*)0);

		// TODO: this is really wrong :-/
		return NULL;
	}

	if (CASE(ExprUnionConstructorFunc, node))
	{
		// defer function compilation till use site to support generics
		context.functions[_->target] = std::make_pair(_, (llvm::Value*)0);

		// TODO: this is really wrong :-/
		return NULL;
	}

	if (CASE(ExprLLVM, node))
	{
		llvm::Function* func = builder.GetInsertBlock()->getParent();

		std::string name = func->getName().str() + "..autogen";
		std::string body = compileInlineLLVM(context, name, _->body, func, _->location);

		llvm::SMDiagnostic err;
		if (!llvm::ParseAssemblyString(body.c_str(), context.module, err, *context.context))
			errorf(_->location, "Failed to parse llvm inline code: %s at '%s'", err.getMessage().str().c_str(), err.getLineContents().str().c_str());

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

	if (CASE(ExprForInRangeDo, node))
	{
		llvm::Function* function = builder.GetInsertBlock()->getParent();

		llvm::Value* start = compileExpr(context, builder, _->start);
		llvm::Value* end = compileExpr(context, builder, _->end);

		llvm::BasicBlock* before = builder.GetInsertBlock();
		llvm::BasicBlock* step_basic_block = llvm::BasicBlock::Create(*context.context, "for_step");
		llvm::BasicBlock* body_basic_block = llvm::BasicBlock::Create(*context.context, "for_body");
		llvm::BasicBlock* end_basic_block = llvm::BasicBlock::Create(*context.context, "for_end");

		builder.CreateBr(step_basic_block);

		function->getBasicBlockList().push_back(step_basic_block);
		builder.SetInsertPoint(step_basic_block);

		llvm::PHINode* index = builder.CreatePHI(builder.getInt32Ty(), 2);
		index->addIncoming(start, before);

		builder.CreateCondBr(builder.CreateICmpSLE(index, end), body_basic_block, end_basic_block);

		function->getBasicBlockList().push_back(body_basic_block);
		builder.SetInsertPoint(body_basic_block);

		context.values[_->target] = index;

		compileExpr(context, builder, _->body);

		llvm::Value *next_index = builder.CreateAdd(index, builder.getInt32(1), "", true, true);
		index->addIncoming(next_index, body_basic_block);

		builder.CreateBr(step_basic_block);

		function->getBasicBlockList().push_back(end_basic_block);
		builder.SetInsertPoint(end_basic_block);

		return builder.getInt32(0); // Same as ExprUnit
	}

	if (CASE(ExprMatchWith, node))
	{
		// Check that case list will handle any value and that there are no unreachable cases
		MatchCase* options = new MatchCaseOr(0, Location());
		for (size_t i = 0; i < _->cases.size(); ++i)
		{
			std::vector<MatchCase*> case_options;

			if (dynamic_cast<MatchCaseIf*>(_->cases[i]))
				continue;

			if (MatchCaseOr *or_node = dynamic_cast<MatchCaseOr*>(_->cases[i]))
			{
				for (size_t i = 0; i < or_node->options.size(); ++i)
					case_options.push_back(or_node->options[i]);
			}
			else
			{
				case_options.push_back(_->cases[i]);
			}

			for (size_t i = 0; i < case_options.size(); ++i)
			{
				if (match(options, case_options[i]))
					errorf(case_options[i]->location, "This case is already covered");

				if (MatchCaseOr* options_pack = dynamic_cast<MatchCaseOr*>(options))
					options_pack->addOption(clone(case_options[i]));

				options = simplify(options);
			}
		}

		if (!match(options, new MatchCaseAny(0, Location(), 0)))
			errorf(_->location, "The match doesn't cover all cases");

		llvm::Function* function = builder.GetInsertBlock()->getParent();

		llvm::Value* variable = compileExpr(context, builder, _->variable);

		// Expression result
		llvm::Value* value = builder.CreateAlloca(compileType(context, _->type, _->location));

		// Create a block for all cases
		std::vector<llvm::BasicBlock*> case_blocks;
		llvm::BasicBlock* finish_block = llvm::BasicBlock::Create(*context.context, "finish");

		for (size_t i = 0; i < _->cases.size(); ++i)
			case_blocks.push_back(llvm::BasicBlock::Create(*context.context, "check_" + std::to_string((long long)i)));

		builder.CreateBr(case_blocks[0]);

		for (size_t i = 0; i < _->cases.size(); ++i)
		{
			function->getBasicBlockList().push_back(case_blocks[i]);
			builder.SetInsertPoint(case_blocks[i]);
			compileMatch(context, builder, _->cases[i], variable, value, _->expressions[i], i == _->cases.size() - 1 ? finish_block : case_blocks[i + 1], finish_block);
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
