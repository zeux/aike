#include "compiler.hpp"

#include "parser.hpp"
#include "output.hpp"
#include "typecheck.hpp"
#include "match.hpp"
#include "llvmaike.hpp"

#include <exception>
#include <cassert>
#include <sstream>

typedef std::vector<std::pair<Type*, std::pair<Type*, LLVMTypeRef> > > GenericInstances;

struct Context
{
	LLVMContextRef context;
	LLVMModuleRef module;
	LLVMTargetDataRef targetData;

	std::map<BindingTarget*, LLVMValueRef> values;
	std::map<BindingTarget*, std::pair<Expr*, GenericInstances> > functions;
	std::map<std::pair<Expr*, std::string>, LLVMFunctionRef> function_instances;
	std::map<LLVMFunctionRef, LLVMFunctionRef> function_thunks;
	std::map<std::string, LLVMTypeRef> types;

	std::vector<LLVMTypeRef> function_context_type;

	GenericInstances generic_instances;
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

LLVMTypeRef compileType(Context& context, Type* type, const Location& location);

LLVMValueRef compileEqualityOperator(const Location& location, Context& context, LLVMBuilderRef builder, LLVMValueRef left, LLVMValueRef right, Type* type);

LLVMTypeRef compileTypePrototype(Context& context, TypePrototype* proto, const Location& location, const std::string& mtype)
{
	if (CASE(TypePrototypeRecord, proto))
	{
		std::vector<LLVMTypeRef> members;

		LLVMStructTypeRef struct_type = LLVMStructCreateNamed(context.context, (_->name + ".." + mtype).c_str());

		context.types[mtype] = struct_type;

		for (size_t i = 0; i < _->member_types.size(); ++i)
			members.push_back(compileType(context, _->member_types[i], location));

		LLVMStructSetBody(struct_type, members.data(), members.size(), false);

		return struct_type;
	}

	if (CASE(TypePrototypeUnion, proto))
	{
		std::vector<LLVMTypeRef> members;

		LLVMStructTypeRef struct_type = LLVMStructCreateNamed(context.context, (_->name + ".." + mtype).c_str());

		context.types[mtype] = struct_type;

		members.push_back(LLVMInt32TypeInContext(context.context)); // Current type ID
		members.push_back(LLVMPointerType(LLVMInt8TypeInContext(context.context), 0)); // Pointer to union data

		LLVMStructSetBody(struct_type, members.data(), members.size(), false);

		return struct_type;
	}

	assert(!"Unknown prototype type");
	return 0;
}

LLVMTypeRef compileType(Context& context, Type* type, const Location& location)
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
		return LLVMInt32TypeInContext(context.context);
	}

	if (CASE(TypeInt, type))
	{
		return LLVMInt32TypeInContext(context.context);
	}

	if (CASE(TypeChar, type))
	{
		return LLVMInt8TypeInContext(context.context);
	}

	if (CASE(TypeFloat, type))
	{
		return LLVMFloatTypeInContext(context.context);
	}

	if (CASE(TypeBool, type))
	{
		return LLVMInt1TypeInContext(context.context);
	}
	
	if (CASE(TypeClosureContext, type))
	{
		std::vector<LLVMTypeRef> members;

		for (size_t i = 0; i < _->member_types.size(); ++i)
		{
			if (dynamic_cast<TypeClosureContext*>(_->member_types[i]))
				members.push_back(LLVMPointerType(LLVMInt8TypeInContext(context.context), 0));
			else
				members.push_back(compileType(context, _->member_types[i], location));
		}

		return LLVMPointerType(LLVMStructTypeInContext(context.context, members.data(), members.size(), false), 0);
	}

	if (CASE(TypeTuple, type))
	{
		std::vector<LLVMTypeRef> members;

		for (size_t i = 0; i < _->members.size(); ++i)
			members.push_back(compileType(context, _->members[i], location));

		return LLVMStructTypeInContext(context.context, members.data(), members.size(), false);
	}

	if (CASE(TypeArray, type))
	{
		std::vector<LLVMTypeRef> members;

		members.push_back(LLVMPointerType(compileType(context, _->contained, location), 0));
		members.push_back(LLVMInt32TypeInContext(context.context));

		return LLVMStructTypeInContext(context.context, members.data(), members.size(), false);
	}

	if (CASE(TypeFunction, type))
	{
		std::vector<LLVMTypeRef> args;

		for (size_t i = 0; i < _->args.size(); ++i)
			args.push_back(compileType(context, _->args[i], location));

		args.push_back(LLVMPointerType(LLVMInt8TypeInContext(context.context), 0));

		LLVMTypeRef function_type = LLVMFunctionType(compileType(context, _->result, location), args.data(), args.size(), false);

		std::vector<LLVMTypeRef> members;

		members.push_back(LLVMPointerType(function_type, 0));
		members.push_back(LLVMPointerType(LLVMInt8TypeInContext(context.context), 0));

		LLVMStructTypeRef holder_type = LLVMStructTypeInContext(context.context, members.data(), members.size(), false);

		return holder_type;
	}

	if (CASE(TypeInstance, type))
	{
		// compute mangled instance type name
		std::string mtype = typeNameMangled(type, [&](TypeGeneric* tg) { return getTypeInstance(context, tg, location); } );

		if (context.types.count(mtype) > 0)
			return context.types[mtype];

		size_t generic_type_count = context.generic_instances.size();

		const std::vector<Type*>& generics = getGenericTypes(*_->prototype);
		assert(generics.size() == _->generics.size());

		for (size_t i = 0; i < _->generics.size(); ++i)
			context.generic_instances.push_back(std::make_pair(generics[i], std::make_pair(_->generics[i], compileType(context, _->generics[i], location))));

		LLVMTypeRef result = compileTypePrototype(context, *_->prototype, location, mtype);

		context.generic_instances.resize(generic_type_count);

		return result;
	}

	errorf(location, "Unrecognized type");
}

size_t instantiateInstanceTypes(Context& context, TypeInstance* instance, const Location& location)
{
	size_t generic_type_count = context.generic_instances.size();

	const std::vector<Type*>& generics = getGenericTypes(*instance->prototype);
	assert(generics.size() == instance->generics.size());

	for (size_t i = 0; i < instance->generics.size(); ++i)
		context.generic_instances.push_back(std::make_pair(generics[i], std::make_pair(getTypeInstance(context, instance->generics[i], location), compileType(context, instance->generics[i], location))));

	return generic_type_count;
}

LLVMFunctionTypeRef compileFunctionType(Context& context, Type* type, const Location& location, Type* context_type)
{
	type = finalType(type);

	if (CASE(TypeFunction, type))
	{
		std::vector<LLVMTypeRef> args;

		for (size_t i = 0; i < _->args.size(); ++i)
			args.push_back(compileType(context, _->args[i], location));

		if (context_type)
			args.push_back(compileType(context, context_type, location));

		return LLVMFunctionType(compileType(context, _->result, location), args.data(), args.size(), false);
	}

	errorf(location, "Unrecognized type");
}

LLVMFunctionRef compileFunctionThunk(Context& context, LLVMFunctionRef target, LLVMTypeRef funcptr_type)
{
	LLVMFunctionTypeRef thunk_type = (LLVMFunctionTypeRef)(LLVMGetElementType(LLVMGetContainedType(funcptr_type, 0)));

	if (context.function_thunks.count(target) > 0)
	{
		LLVMFunctionRef result = context.function_thunks[target];
		//assert(LLVMTypeOf(result) == thunk_type);
		return result;
	}

	std::string name = LLVMGetValueName(target);

	LLVMFunctionRef thunk_func = LLVMAddFunction(context.module, (name + "..thunk").c_str(), thunk_type);
	LLVMSetLinkage(thunk_func, LLVMInternalLinkage);

	assert(LLVMCountParams(thunk_func) == LLVMCountParams(target) || LLVMCountParams(thunk_func) == LLVMCountParams(target) + 1);

	LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(context.context, thunk_func, "entry");
	LLVMBuilderRef builder = LLVMCreateBuilderInContext(context.context);
	LLVMPositionBuilderAtEnd(builder, bb);

	std::vector<LLVMValueRef> args;

	// add all target arguments
	assert(LLVMCountParams(thunk_func) >= LLVMCountParams(target));

	LLVMValueRef argi = LLVMGetFirstParam(thunk_func);

	for (size_t i = 0; i < LLVMCountParams(thunk_func); ++i, argi = LLVMGetNextParam(argi))
		args.push_back(argi);

	// cast context argument to correct type
	if (LLVMCountParams(thunk_func) == LLVMCountParams(target))
	{
		args.back() = LLVMBuildPointerCast(builder, args.back(), LLVMTypeOf(LLVMGetLastParam(target)), "");
	}

	LLVMBuildRet(builder, LLVMBuildCall(builder, target, args.data(), LLVMCountParams(thunk_func) == LLVMCountParams(target) ? args.size() : args.size() - 1, ""));

	return context.function_thunks[target] = thunk_func;
}

LLVMValueRef compileFunctionValue(Context& context, LLVMBuilderRef builder, LLVMFunctionRef target, Type* type, BindingTarget* context_target, const Location& location)
{
	LLVMTypeRef funcptr_type = compileType(context, type, location);

	LLVMFunctionRef thunk = compileFunctionThunk(context, target, funcptr_type);

	LLVMValueRef result = LLVMConstNull(funcptr_type);
	
	result = LLVMBuildInsertValue(builder, result, thunk, 0, "");

	if (context_target)
	{
		LLVMValueRef context_ref = context.values[context_target];
		LLVMValueRef context_ref_opaque = LLVMBuildBitCast(builder, context_ref, LLVMPointerType(LLVMInt8TypeInContext(context.context), 0), "");

		result = LLVMBuildInsertValue(builder, result, context_ref_opaque, 1, "");
	}

	return result;
}

void instantiateGenericTypes(Context& context, GenericInstances& generic_instances, Type* generic, Type* instance, const Location& location)
{
	generic = finalType(generic);
	instance = finalType(instance);

	if (CASE(TypeGeneric, generic))
	{
		// Eliminate duplicates for nicer signatures & easier debugging
		for (size_t i = 0; i < generic_instances.size(); ++i)
			if (generic_instances[i].first == generic)
				return;

		generic_instances.push_back(std::make_pair(generic, std::make_pair(getTypeInstance(context, instance, location), compileType(context, instance, location))));
	}

	if (CASE(TypeArray, generic))
	{
		TypeArray* inst = dynamic_cast<TypeArray*>(instance);

		instantiateGenericTypes(context, generic_instances, _->contained, inst->contained, location);
	}

	if (CASE(TypeFunction, generic))
	{
		TypeFunction* inst = dynamic_cast<TypeFunction*>(instance);

		instantiateGenericTypes(context, generic_instances, _->result, inst->result, location);

		assert(_->args.size() == inst->args.size());

		for (size_t i = 0; i < _->args.size(); ++i)
			instantiateGenericTypes(context, generic_instances, _->args[i], inst->args[i], location);
	}

	if (CASE(TypeInstance, generic))
	{
		TypeInstance* inst = dynamic_cast<TypeInstance*>(instance);

		assert(_->prototype == inst->prototype);
		assert(_->generics.size() == inst->generics.size());

		for (size_t i = 0; i < _->generics.size(); ++i)
			instantiateGenericTypes(context, generic_instances, _->generics[i], inst->generics[i], location);
	}

	if (CASE(TypeTuple, generic))
	{
		TypeTuple* inst = dynamic_cast<TypeTuple*>(instance);

		assert(_->members.size() == inst->members.size());

		for (size_t i = 0; i < _->members.size(); ++i)
			instantiateGenericTypes(context, generic_instances, _->members[i], inst->members[i], location);
	}
}

LLVMValueRef compileExpr(Context& context, LLVMBuilderRef builder, Expr* node);

LLVMFunctionRef compileRegularFunction(Context& context, ExprLetFunc* node, const std::string& mtype, std::function<void(LLVMFunctionRef)> ready)
{
	LLVMFunctionTypeRef function_type = compileFunctionType(context, node->type, node->location, node->context_target ? node->context_target->type : 0);

	LLVMFunctionRef func = LLVMAddFunction(context.module, (node->target->name + ".." + mtype).c_str(), function_type);
	LLVMSetLinkage(func, LLVMInternalLinkage);

	ready(func);

	LLVMValueRef argi = LLVMGetFirstParam(func);

	LLVMValueRef context_value = 0;
	LLVMValueRef context_target_value = node->context_target ? context.values[node->context_target] : NULL;

	for (size_t i = 0; i < LLVMCountParams(func); ++i, argi = LLVMGetNextParam(argi))
	{
		if (i < node->args.size())
		{
			LLVMSetValueName(argi, node->args[i]->name.c_str());
			context.values[node->args[i]] = argi;
		}
		else
		{
			LLVMSetValueName(argi, "extern");
			context_value = context.values[node->context_target] = argi;
		}
	}

	LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(context.context, func, "entry");
	LLVMBuilderRef builder = LLVMCreateBuilderInContext(context.context);
	LLVMPositionBuilderAtEnd(builder, bb);

	// Compile function body
	context.function_context_type.push_back(context_value ? LLVMTypeOf(context_value) : NULL);

	LLVMValueRef value = compileExpr(context, builder, node->body);

	context.function_context_type.pop_back();

	LLVMBuildRet(builder, value);

	context.values[node->context_target] = context_target_value;

	return func;
}

LLVMFunctionRef compileStructConstructor(Context& context, ExprStructConstructorFunc* node, const std::string& mtype)
{
	LLVMFunctionTypeRef function_type = compileFunctionType(context, node->type, node->location, 0);

	LLVMFunctionRef func = LLVMAddFunction(context.module, (node->target->name + ".." + mtype).c_str(), function_type);
	LLVMSetLinkage(func, LLVMInternalLinkage);

	LLVMValueRef argi = LLVMGetFirstParam(func);

	for (size_t i = 0; i < LLVMCountParams(func); ++i, argi = LLVMGetNextParam(argi))
	{
		if (i < node->args.size())
			LLVMSetValueName(argi, node->args[i]->name.c_str());
	}

	LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(context.context, func, "entry");
	LLVMBuilderRef builder = LLVMCreateBuilderInContext(context.context);
	LLVMPositionBuilderAtEnd(builder, bb);

	LLVMValueRef aggr = LLVMConstNull(LLVMGetReturnType(function_type));

	argi = LLVMGetFirstParam(func);

	for (size_t i = 0; i < LLVMCountParams(func); ++i, argi = LLVMGetNextParam(argi))
		aggr = LLVMBuildInsertValue(builder, aggr, argi, i, "");

	LLVMBuildRet(builder, aggr);

	return func;
}

LLVMFunctionRef compileUnionConstructor(Context& context, ExprUnionConstructorFunc* node, const std::string& mtype)
{
	LLVMFunctionTypeRef function_type = compileFunctionType(context, node->type, node->location, 0);

	LLVMFunctionRef func = LLVMAddFunction(context.module, (node->target->name + ".." + mtype).c_str(), function_type);
	LLVMSetLinkage(func, LLVMInternalLinkage);

	LLVMValueRef argi = LLVMGetFirstParam(func);

	for (size_t i = 0; i < LLVMCountParams(func); ++i, argi = LLVMGetNextParam(argi))
	{
		if (i < node->args.size())
			LLVMSetValueName(argi, node->args[i]->name.c_str());
	}

	LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(context.context, func, "entry");
	LLVMBuilderRef builder = LLVMCreateBuilderInContext(context.context);
	LLVMPositionBuilderAtEnd(builder, bb);

	LLVMValueRef aggr = LLVMConstNull(LLVMGetReturnType(function_type));

	// Save type ID
	aggr = LLVMBuildInsertValue(builder, aggr, LLVMConstInt(LLVMInt32TypeInContext(context.context), node->member_id, false), 0, "");

	// Create union storage
	LLVMTypeRef member_type = compileType(context, node->member_type, node->location);
	LLVMTypeRef member_ref_type = LLVMPointerType(member_type, 0);

	argi = LLVMGetFirstParam(func);

	if (!node->args.empty())
	{
		LLVMValueRef arg =  LLVMConstInt(LLVMInt32TypeInContext(context.context), uint32_t(LLVMABISizeOfType(context.targetData, member_type)), false);
		LLVMValueRef data = LLVMBuildCall(builder, LLVMGetNamedFunction(context.module, "malloc"), &arg, 1, "");
		LLVMValueRef typed_data = LLVMBuildBitCast(builder, data, member_ref_type, "");

		if (node->args.size() > 1 || node->target->name.find("Syn") == 0)
		{
			for (size_t i = 0; i < LLVMCountParams(func); ++i, argi = LLVMGetNextParam(argi))
				LLVMBuildStore(builder, argi, LLVMBuildStructGEP(builder, typed_data, i, ""));
		}
		else
		{
			LLVMBuildStore(builder, argi, typed_data);
		}

		aggr = LLVMBuildInsertValue(builder, aggr, data, 1, "");
	}

	LLVMBuildRet(builder, aggr);

	return func;
}

LLVMFunctionRef compileFunction(Context& context, Expr* node, const std::string& mtype, std::function<void(LLVMFunctionRef)> ready)
{
	if (CASE(ExprLetFunc, node))
	{
		return compileRegularFunction(context, _, mtype, ready);
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

LLVMFunctionRef compileFunctionInstance(Context& context, Expr* node, const GenericInstances& generic_instances, Type* instance_type, const Location& location)
{
	// compute mangled instance type name
	std::string mtype = typeNameMangled(instance_type, [&](TypeGeneric* tg) { return getTypeInstance(context, tg, location); } );

	for (size_t i = 0; i < generic_instances.size(); ++i)
		 mtype += "." + typeNameMangled(generic_instances[i].second.first, [&](TypeGeneric* tg) -> Type* { assert(!"Unexpected generic type"); return getTypeInstance(context, tg, location); } );

	if (context.function_instances.count(std::make_pair(node, mtype)))
		return context.function_instances[std::make_pair(node, mtype)];

	GenericInstances old_generic_instances = context.generic_instances;

	// node->type is the generic type, and type is the instance. Instantiate all types into context, following the shape of the type.
	GenericInstances new_generic_instances;
	instantiateGenericTypes(context, new_generic_instances, node->type, instance_type, location);

	// the type context for the body compilation should consist of all generic instances defined at the declaration point, plus all types instantiated from declaration
	// note that we have to preserve old generic instances because instantiateGenericTypes is not guaranteed to produce final types
	context.generic_instances.insert(context.generic_instances.end(), new_generic_instances.begin(), new_generic_instances.end());

	// compile function body given a non-generic type
	LLVMFunctionRef func = compileFunction(context, node, mtype, [&](LLVMFunctionRef func) { context.function_instances[std::make_pair(node, mtype)] = func; });

	if(!LLVMAikeVerifyFunction(func))
		errorf(location, "Internal compiler error");

	// restore old generic type instantiations
	context.generic_instances = old_generic_instances;

	return context.function_instances[std::make_pair(node, mtype)] = func;
}

LLVMFunctionRef compileBindingFunction(Context& context, BindingFunction* binding, Type* type, const Location& location)
{
	if (context.functions.count(binding->target) > 0)
	{
		// Compile function instantiation
		auto p = context.functions[binding->target];

		LLVMFunctionRef func = compileFunctionInstance(context, p.first, p.second, type, location);

		return func;
	}

	errorf(location, "Variable %s has not been computed", binding->target->name.c_str());
}

LLVMValueRef compileBinding(Context& context, LLVMBuilderRef builder, BindingBase* binding, Type* type, const Location& location)
{
	if (CASE(BindingFunction, binding))
	{
		if (context.values.count(_->target) > 0)
			return context.values[_->target];

		if (context.functions.count(_->target) > 0)
		{
			// Compile function instantiation
			auto p = context.functions[_->target];
			Expr* node = p.first;

			LLVMFunctionRef func = compileFunctionInstance(context, node, p.second, type, location);

			// Create function value
			BindingTarget* context_target = dynamic_cast<ExprLetFunc*>(node) ? dynamic_cast<ExprLetFunc*>(node)->context_target : NULL;

			LLVMValueRef funcptr = compileFunctionValue(context, builder, func, type, context_target, node->location);

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

LLVMValueRef findFunctionArgument(LLVMFunctionRef func, const std::string& name)
{
	for (LLVMValueRef argi = LLVMGetFirstParam(func); argi; argi = LLVMGetNextParam(argi))
	{
		if (LLVMGetValueName(argi) == name)
			return argi;
	}

	return NULL;
}

LLVMTypeRef parseInlineLLVMType(Context& context, const std::string& name, LLVMFunctionRef func, const Location& location)
{
	if (name[0] == '%')
	{
		if (LLVMValueRef arg = findFunctionArgument(func, name.substr(1)))
		{
			return LLVMTypeOf(arg);
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

std::string compileInlineLLVM(Context& context, const std::string& name, const std::string& body, LLVMFunctionRef func, const Location& location)
{
	std::ostringstream declare;
	std::ostringstream os;

	os << "define internal " << LLVMAikeGetTypeName(LLVMGetReturnType(LLVMGetElementType(LLVMTypeOf(func)))) << " @" << name << "(";

	for (LLVMValueRef argi = LLVMGetFirstParam(func); argi; argi = LLVMGetNextParam(argi))
		os << (argi != LLVMGetFirstParam(func) ? ", " : "") << LLVMAikeGetTypeName(LLVMTypeOf(argi)) << " %" << LLVMGetValueName(argi);

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

			os << LLVMAikeGetTypeName(parseInlineLLVMType(context, var, func, location));
		}
		else if (body[i] == 's' && body.compare(i, 7, "sizeof(") == 0)
		{
			std::string::size_type end = body.find(')', i);

			if (end == std::string::npos)
				errorf(location, "Incorrect sizeof expression: closing brace expected");

			std::string var(body.begin() + i + 7, body.begin() + end);

			i = end + 1;

			os << LLVMABISizeOfType(context.targetData, parseInlineLLVMType(context, var, func, location));
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

	os << "\nret " << LLVMAikeGetTypeName(LLVMGetReturnType(LLVMGetElementType(LLVMTypeOf(func)))) << " %out }";
	os.flush();
	
	return declare.str() + os.str();
}

LLVMValueRef compileExpr(Context& context, LLVMBuilderRef builder, Expr* node);

void compileMatch(Context& context, LLVMBuilderRef builder, MatchCase* case_, LLVMValueRef value, LLVMValueRef target, Expr* rhs, LLVMBasicBlockRef on_fail, LLVMBasicBlockRef on_success)
{
	if (CASE(MatchCaseAny, case_))
	{
		if (_->alias)
			context.values[_->alias] = value;

		if (target)
			LLVMBuildStore(builder, compileExpr(context, builder, rhs), target);

		LLVMBuildBr(builder, on_success);
	}
	else if (CASE(MatchCaseBoolean, case_))
	{
		LLVMFunctionRef function = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));

		LLVMValueRef cond = LLVMBuildICmp(builder, LLVMIntEQ, value, LLVMConstInt(LLVMInt1TypeInContext(context.context), _->value, false), "");

		LLVMBasicBlockRef success = LLVMAppendBasicBlockInContext(context.context, function, "success");

		LLVMBuildCondBr(builder, cond, success, on_fail);

		LLVMPositionBuilderAtEnd(builder, success);

		if (target)
			LLVMBuildStore(builder, compileExpr(context, builder, rhs), target);

		LLVMBuildBr(builder, on_success);
	}
	else if (CASE(MatchCaseNumber, case_))
	{
		LLVMFunctionRef function = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));

		LLVMValueRef cond = LLVMBuildICmp(builder, LLVMIntEQ, value, LLVMConstInt(LLVMInt32TypeInContext(context.context), uint32_t(_->value), false), "");

		LLVMBasicBlockRef success = LLVMAppendBasicBlockInContext(context.context, function, "success");

		LLVMBuildCondBr(builder, cond, success, on_fail);

		LLVMPositionBuilderAtEnd(builder, success);

		if (target)
			LLVMBuildStore(builder, compileExpr(context, builder, rhs), target);

		LLVMBuildBr(builder, on_success);
	}
	else if (CASE(MatchCaseCharacter, case_))
	{
		LLVMFunctionRef function = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));

		LLVMValueRef cond = LLVMBuildICmp(builder, LLVMIntEQ, value, LLVMConstInt(LLVMInt8TypeInContext(context.context), uint32_t(_->value), false), "");

		LLVMBasicBlockRef success = LLVMAppendBasicBlockInContext(context.context, function, "success");

		LLVMBuildCondBr(builder, cond, success, on_fail);

		LLVMPositionBuilderAtEnd(builder, success);

		if (target)
			LLVMBuildStore(builder, compileExpr(context, builder, rhs), target);

		LLVMBuildBr(builder, on_success);
	}
	else if (CASE(MatchCaseValue, case_))
	{
		LLVMFunctionRef function = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));

		LLVMValueRef cond = compileEqualityOperator(_->location, context, builder, compileBinding(context, builder, _->value, finalType(_->type), _->location), value, finalType(_->type));

		LLVMBasicBlockRef success = LLVMAppendBasicBlockInContext(context.context, function, "success");

		LLVMBuildCondBr(builder, cond, success, on_fail);

		LLVMPositionBuilderAtEnd(builder, success);

		if (target)
			LLVMBuildStore(builder, compileExpr(context, builder, rhs), target);

		LLVMBuildBr(builder, on_success);
	}
	else if (CASE(MatchCaseArray, case_))
	{
		TypeArray* arr_type = dynamic_cast<TypeArray*>(finalType(_->type));
		if (!arr_type)
			errorf(_->location, "array type is unknown");

		LLVMFunctionRef function = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));

		LLVMValueRef size = LLVMBuildExtractValue(builder, value, 1, "");

		LLVMValueRef cond = LLVMBuildICmp(builder, LLVMIntEQ, size, LLVMConstInt(LLVMInt32TypeInContext(context.context), uint32_t(_->elements.size()), false), "");

		LLVMBasicBlockRef success_size = LLVMAppendBasicBlockInContext(context.context, function, "success_size");
		LLVMBasicBlockRef success_all = LLVMAppendBasicBlockInContext(context.context, function, "success_all");

		LLVMBuildCondBr(builder, cond, success_size, on_fail);
		LLVMPositionBuilderAtEnd(builder, success_size);

		for (size_t i = 0; i < _->elements.size(); ++i)
		{
			LLVMValueRef index = LLVMConstInt(LLVMInt32TypeInContext(context.context), uint32_t(i), false);
			LLVMValueRef element = LLVMBuildLoad(builder, LLVMBuildGEP(builder, LLVMBuildExtractValue(builder, value, 0, ""), &index, 1, ""), "");

			LLVMBasicBlockRef next_check = i != _->elements.size() - 1 ? LLVMAppendBasicBlockInContext(context.context, function, "next_check") : success_all;

			compileMatch(context, builder, _->elements[i], element, 0, 0, on_fail, next_check);

			LLVMMoveBasicBlockAfter(next_check, LLVMGetLastBasicBlock(function));
			LLVMPositionBuilderAtEnd(builder, next_check);
		}

		if (_->elements.empty())
		{
			LLVMBuildBr(builder, success_all);

			LLVMMoveBasicBlockAfter(success_all, LLVMGetLastBasicBlock(function));
			LLVMPositionBuilderAtEnd(builder, success_all);
		}

		if (target)
			LLVMBuildStore(builder, compileExpr(context, builder, rhs), target);

		LLVMBuildBr(builder, on_success);
	}
	else if (CASE(MatchCaseMembers, case_))
	{
		LLVMFunctionRef function = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));

		LLVMBasicBlockRef success_all = LLVMAppendBasicBlockInContext(context.context, function, "success_all");

		TypeInstance* inst_type = dynamic_cast<TypeInstance*>(finalType(_->type));
		TypePrototypeRecord* record_type = inst_type ? dynamic_cast<TypePrototypeRecord*>(*inst_type->prototype) : 0;

		if (record_type)
		{
			for (size_t i = 0; i < _->member_values.size(); ++i)
			{
				LLVMBasicBlockRef next_check = i != _->member_values.size() - 1 ? LLVMAppendBasicBlockInContext(context.context, function, "next_check") : success_all;

				size_t id = ~0u;

				if (!_->member_names.empty())
					id = getMemberIndexByName(record_type, _->member_names[i], _->location);

				LLVMValueRef element = LLVMBuildExtractValue(builder, value, id == ~0u ? i : id, "");

				compileMatch(context, builder, _->member_values[i], element, 0, 0, on_fail, next_check);

				LLVMMoveBasicBlockAfter(next_check, LLVMGetLastBasicBlock(function));
				LLVMPositionBuilderAtEnd(builder, next_check);
			}
		}
		else if(TypeTuple* tuple_type = dynamic_cast<TypeTuple*>(finalType(_->type)))
		{
			for (size_t i = 0; i < _->member_values.size(); ++i)
			{
				LLVMBasicBlockRef next_check = i != _->member_values.size() - 1 ? LLVMAppendBasicBlockInContext(context.context, function, "next_check") : success_all;

				LLVMValueRef element = LLVMBuildExtractValue(builder, value, i, "");

				compileMatch(context, builder, _->member_values[i], element, 0, 0, on_fail, next_check);

				LLVMMoveBasicBlockAfter(next_check, LLVMGetLastBasicBlock(function));
				LLVMPositionBuilderAtEnd(builder, next_check);
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

			LLVMMoveBasicBlockAfter(success_all, LLVMGetLastBasicBlock(function));
			LLVMPositionBuilderAtEnd(builder, success_all);
		}

		if (target)
			LLVMBuildStore(builder, compileExpr(context, builder, rhs), target);

		LLVMBuildBr(builder, on_success);
	}
	else if (CASE(MatchCaseUnion, case_))
	{
		TypeInstance* inst_type = dynamic_cast<TypeInstance*>(finalType(_->type));
		TypePrototypeUnion* union_type = inst_type ? dynamic_cast<TypePrototypeUnion*>(*inst_type->prototype) : 0;

		if (!union_type)
			errorf(_->location, "union type is unknown");

		LLVMFunctionRef function = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));

		LLVMValueRef type_id = LLVMBuildExtractValue(builder, value, 0, "");
		LLVMValueRef type_ptr = LLVMBuildExtractValue(builder, value, 1, "");

		LLVMValueRef cond = LLVMBuildICmp(builder, LLVMIntEQ, type_id, LLVMConstInt(LLVMInt32TypeInContext(context.context), uint32_t(_->tag), false), "");

		LLVMBasicBlockRef success_tag = LLVMAppendBasicBlockInContext(context.context, function, "success_tag");
		LLVMBasicBlockRef success_all = LLVMAppendBasicBlockInContext(context.context, function, "success_all");

		LLVMBuildCondBr(builder, cond, success_tag, on_fail);
		LLVMPositionBuilderAtEnd(builder, success_tag);

		Type* type = getMemberTypeByIndex(inst_type, union_type, _->tag, _->location);

		LLVMValueRef element = LLVMBuildLoad(builder, LLVMBuildBitCast(builder, type_ptr, LLVMPointerType(compileType(context, type, _->location), 0), ""), "");

		compileMatch(context, builder, _->pattern, element, 0, 0, on_fail, success_all);

		LLVMMoveBasicBlockAfter(success_all, LLVMGetLastBasicBlock(function));
		LLVMPositionBuilderAtEnd(builder, success_all);

		if (target)
			LLVMBuildStore(builder, compileExpr(context, builder, rhs), target);

		LLVMBuildBr(builder, on_success);
	}
	else if (CASE(MatchCaseOr, case_))
	{
		LLVMFunctionRef function = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));

		LLVMBasicBlockRef success_any = LLVMAppendBasicBlockInContext(context.context, function, "success_any");

		std::vector<LLVMBasicBlockRef> incoming;

		for (size_t i = 0; i < _->options.size(); ++i)
		{
			LLVMBasicBlockRef next_check = i != _->options.size() - 1 ? LLVMAppendBasicBlockInContext(context.context, function, "next_check") : on_fail;

			compileMatch(context, builder, _->options[i], value, 0, 0, next_check, success_any);

			incoming.push_back(LLVMGetInsertBlock(builder));

			if (next_check != on_fail)
			{
				LLVMMoveBasicBlockAfter(next_check, LLVMGetLastBasicBlock(function));
				LLVMPositionBuilderAtEnd(builder, next_check);
			}
		}

		LLVMMoveBasicBlockAfter(success_any, LLVMGetLastBasicBlock(function));
		LLVMPositionBuilderAtEnd(builder, success_any);

		// Merge all variants for binding into actual binding used in the expression
		for (size_t i = 0; i < _->binding_actual.size(); ++i)
		{
			LLVMPHIRef pn = LLVMBuildPhi(builder, compileType(context, _->binding_actual[i]->type, Location()), "");

			std::vector<LLVMValueRef> incoming_values;
			std::vector<LLVMBasicBlockRef> incoming_blocks;

			for (size_t k = 0; k < _->binding_alternatives.size(); ++k)
			{
				incoming_values.push_back(compileBinding(context, builder, new BindingLocal(_->binding_alternatives[k][i]), _->binding_alternatives[k][i]->type, Location()));
				incoming_blocks.push_back(incoming[k]);
			}

			LLVMAddIncoming(pn, incoming_values.data(), incoming_blocks.data(), incoming_values.size());

			context.values[_->binding_actual[i]] = pn;
		}

		if (target)
			LLVMBuildStore(builder, compileExpr(context, builder, rhs), target);

		LLVMBuildBr(builder, on_success);
	}
	else if (CASE(MatchCaseIf, case_))
	{
		LLVMFunctionRef function = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));

		LLVMBasicBlockRef success_pattern = LLVMAppendBasicBlockInContext(context.context, function, "success_pattern");
		LLVMBasicBlockRef success_cond = LLVMAppendBasicBlockInContext(context.context, function, "success_cond");

		compileMatch(context, builder, _->match, value, 0, 0, on_fail, success_pattern);

		LLVMMoveBasicBlockAfter(success_pattern, LLVMGetLastBasicBlock(function));
		LLVMPositionBuilderAtEnd(builder, success_pattern);

		LLVMValueRef condition = compileExpr(context, builder, _->condition);

		LLVMBuildCondBr(builder, condition, success_cond, on_fail);

		LLVMMoveBasicBlockAfter(success_cond, LLVMGetLastBasicBlock(function));
		LLVMPositionBuilderAtEnd(builder, success_cond);

		if (target)
			LLVMBuildStore(builder, compileExpr(context, builder, rhs), target);

		LLVMBuildBr(builder, on_success);
	}
	else
	{
		assert(!"Unknown MatchCase node");
	}
}

LLVMValueRef compileStructEqualityOperator(const Location& location, Context& context, LLVMBuilderRef builder, LLVMValueRef left, LLVMValueRef right, Type* parent, std::vector<Type*> types)
{
	std::string function_name = typeNameMangled(parent, [&](TypeGeneric* tg) { return getTypeInstance(context, tg, location); } ) + "..equal";

	if (LLVMFunctionRef function = LLVMGetNamedFunction(context.module, function_name.c_str()))
		return LLVMBuildCall2(builder, function, left, right);

	std::vector<LLVMTypeRef> args(2, compileType(context, parent, location));
	LLVMFunctionTypeRef function_type = LLVMFunctionType(LLVMInt1TypeInContext(context.context), args.data(), args.size(), false);

	LLVMFunctionRef function = LLVMAddFunction(context.module, function_name.c_str(), function_type);
	LLVMSetLinkage(function, LLVMInternalLinkage);

	LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(context.context, function, "entry");
	LLVMBuilderRef function_builder = LLVMCreateBuilderInContext(context.context);
	LLVMPositionBuilderAtEnd(function_builder, bb);

	LLVMValueRef argi = LLVMGetFirstParam(function);
	LLVMValueRef left_internal = argi;

	argi = LLVMGetNextParam(argi);
	LLVMValueRef right_internal = argi;

	LLVMBasicBlockRef compare_last = LLVMGetInsertBlock(function_builder);
	LLVMBasicBlockRef compare_end = LLVMAppendBasicBlockInContext(context.context, function, "compare_end");

	std::vector<LLVMBasicBlockRef> fail_block;

	for (size_t i = 0; i < types.size(); ++i)
	{
		LLVMBasicBlockRef compare_next = LLVMAppendBasicBlockInContext(context.context, function, "compare_next");

		LLVMBuildCondBr(function_builder, compileEqualityOperator(location, context, function_builder, LLVMBuildExtractValue(function_builder, left_internal, i, ""), LLVMBuildExtractValue(function_builder, right_internal, i, ""), types[i]), compare_next, compare_end);
				
		fail_block.push_back(LLVMGetInsertBlock(function_builder));
		compare_last = compare_next;

		LLVMMoveBasicBlockAfter(compare_next, LLVMGetLastBasicBlock(function));
		LLVMPositionBuilderAtEnd(function_builder, compare_next);
	}

	LLVMBuildBr(function_builder, compare_end);

	// Result computation node
	LLVMMoveBasicBlockAfter(compare_end, LLVMGetLastBasicBlock(function));
	LLVMPositionBuilderAtEnd(function_builder, compare_end);

	LLVMPHIRef result = LLVMBuildPhi(function_builder, LLVMInt1TypeInContext(context.context), "");

	std::vector<LLVMValueRef> incoming_values;
	std::vector<LLVMBasicBlockRef> incoming_blocks;

	// Comparison is only successful if we came here from the last node
	incoming_values.push_back(LLVMConstInt(LLVMInt1TypeInContext(context.context), true, false));
	incoming_blocks.push_back(compare_last);
	// If we came from any other block, it was from a member comparison failure
	for (size_t i = 0; i < fail_block.size(); ++i)
	{
		incoming_values.push_back(LLVMConstInt(LLVMInt1TypeInContext(context.context), false, false));
		incoming_blocks.push_back(fail_block[i]);
	}

	LLVMAddIncoming(result, incoming_values.data(), incoming_blocks.data(), incoming_values.size());

	LLVMBuildRet(function_builder, result);

	return LLVMBuildCall2(builder, function, left, right);
}

LLVMValueRef compileUnionEqualityOperator(const Location& location, Context& context, LLVMBuilderRef builder, LLVMValueRef left, LLVMValueRef right, Type* parent, TypePrototypeUnion* proto)
{
	std::string function_name = typeNameMangled(parent, [&](TypeGeneric* tg) { return getTypeInstance(context, tg, location); } ) + "..equal";

	if (LLVMFunctionRef function = LLVMGetNamedFunction(context.module, function_name.c_str()))
		return LLVMBuildCall2(builder, function, left, right);

	std::vector<LLVMTypeRef> args(2, compileType(context, parent, location));
	LLVMFunctionTypeRef function_type = LLVMFunctionType(LLVMInt1TypeInContext(context.context), args.data(), args.size(), false);

	LLVMFunctionRef function = LLVMAddFunction(context.module, function_name.c_str(), function_type);
	LLVMSetLinkage(function, LLVMInternalLinkage);

	LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(context.context, function, "entry");
	LLVMBuilderRef function_builder = LLVMCreateBuilderInContext(context.context);
	LLVMPositionBuilderAtEnd(function_builder, bb);

	LLVMValueRef argi = LLVMGetFirstParam(function);

	LLVMValueRef left_tag = LLVMBuildExtractValue(function_builder, argi, 0, "");
	LLVMValueRef left_ptr = LLVMBuildExtractValue(function_builder, argi, 1, "");

	argi = LLVMGetNextParam(argi);

	LLVMValueRef right_tag = LLVMBuildExtractValue(function_builder, argi, 0, "");
	LLVMValueRef right_ptr = LLVMBuildExtractValue(function_builder, argi, 1, "");

	LLVMBasicBlockRef compare_start = LLVMGetInsertBlock(function_builder);
	LLVMBasicBlockRef compare_data = LLVMAppendBasicBlockInContext(context.context, function, "compare_data");
	LLVMBasicBlockRef compare_end = LLVMAppendBasicBlockInContext(context.context, function, "compare_end");

	// Check tag equality
	LLVMBuildCondBr(function_builder, LLVMBuildICmp(function_builder, LLVMIntEQ, left_tag, right_tag, ""), compare_data, compare_end);

	// Switch by union tag
	LLVMMoveBasicBlockAfter(compare_data, LLVMGetLastBasicBlock(function));
	LLVMPositionBuilderAtEnd(function_builder, compare_data);

	LLVMValueRef swichInst = LLVMBuildSwitch(function_builder, left_tag, compare_end, proto->member_types.size());

	std::vector<LLVMValueRef> case_results;
	std::vector<LLVMBasicBlockRef> case_blocks;

	for (size_t i = 0; i < proto->member_types.size(); ++i)
	{
		LLVMBasicBlockRef compare_tag = LLVMAppendBasicBlockInContext(context.context, function, "compare_tag");
		LLVMPositionBuilderAtEnd(function_builder, compare_tag);

		LLVMValueRef left_elem = LLVMBuildLoad(function_builder, LLVMBuildBitCast(function_builder, left_ptr, LLVMPointerType(compileType(context, proto->member_types[i], location), 0), ""), "");
		LLVMValueRef right_elem = LLVMBuildLoad(function_builder, LLVMBuildBitCast(function_builder, right_ptr, LLVMPointerType(compileType(context, proto->member_types[i], location), 0), ""), "");

		case_results.push_back(compileEqualityOperator(location, context, function_builder, left_elem, right_elem, finalType(proto->member_types[i])));
		LLVMBuildBr(function_builder, compare_end);
		
		case_blocks.push_back(LLVMGetInsertBlock(function_builder));

		LLVMAddCase(swichInst, LLVMConstInt(LLVMInt32TypeInContext(context.context), i, false), compare_tag);
	}

	// Result computation node
	LLVMMoveBasicBlockAfter(compare_end, LLVMGetLastBasicBlock(function));
	LLVMPositionBuilderAtEnd(function_builder, compare_end);

	LLVMPHIRef result = LLVMBuildPhi(function_builder, LLVMInt1TypeInContext(context.context), "");

	std::vector<LLVMValueRef> incoming_values;
	std::vector<LLVMBasicBlockRef> incoming_blocks;

	// Tags are not equal
	incoming_values.push_back(LLVMConstInt(LLVMInt1TypeInContext(context.context), false, false));
	incoming_blocks.push_back(compare_start);
	// Tag is invalid
	incoming_values.push_back(LLVMConstInt(LLVMInt1TypeInContext(context.context), false, false));
	incoming_blocks.push_back(compare_data);
	// For other cases, take the result of the other comparisons
	for (size_t i = 0; i < proto->member_types.size(); ++i)
	{
		incoming_values.push_back(case_results[i]);
		incoming_blocks.push_back(case_blocks[i]);
	}

	LLVMAddIncoming(result, incoming_values.data(), incoming_blocks.data(), incoming_values.size());

	LLVMBuildRet(function_builder, result);

	return LLVMBuildCall2(builder, function, left, right);
}

LLVMValueRef compileArrayEqualityOperator(const Location& location, Context& context, LLVMBuilderRef builder, LLVMValueRef left, LLVMValueRef right, TypeArray* type)
{
	LLVMFunctionRef function = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));

	LLVMValueRef left_arr = LLVMBuildExtractValue(builder, left, 0, "");
	LLVMValueRef left_size = LLVMBuildExtractValue(builder, left, 1, "");

	LLVMValueRef right_arr = LLVMBuildExtractValue(builder, right, 0, "");
	LLVMValueRef right_size = LLVMBuildExtractValue(builder, right, 1, "");

	LLVMBasicBlockRef compare_size = LLVMGetInsertBlock(builder);
	LLVMBasicBlockRef compare_content = LLVMAppendBasicBlockInContext(context.context, function, "compare_content");
	LLVMBasicBlockRef compare_end = LLVMAppendBasicBlockInContext(context.context, function, "compare_end");
	LLVMBasicBlockRef compare_elem = LLVMAppendBasicBlockInContext(context.context, function, "compare_elem");
	LLVMBasicBlockRef compare_content_end = LLVMAppendBasicBlockInContext(context.context, function, "compare_content_end");

	// TODO: compare array pointers to determine equality immediately

	LLVMBuildCondBr(builder, LLVMBuildICmp(builder, LLVMIntEQ, left_size, right_size, ""), compare_content, compare_end);

	// Content computation node
	LLVMMoveBasicBlockAfter(compare_content, LLVMGetLastBasicBlock(function));
	LLVMPositionBuilderAtEnd(builder, compare_content);

	LLVMPHIRef index = LLVMBuildPhi(builder, LLVMInt32TypeInContext(context.context), "");

	std::vector<LLVMValueRef> incoming_values_local;
	std::vector<LLVMBasicBlockRef> incoming_blocks_local;

	incoming_values_local.push_back(LLVMConstInt(LLVMInt32TypeInContext(context.context), 0, false));
	incoming_blocks_local.push_back(compare_size);

	LLVMBuildCondBr(builder, LLVMBuildICmp(builder, LLVMIntULT, index, left_size, ""), compare_elem, compare_end);

	// Element computation node
	LLVMMoveBasicBlockAfter(compare_elem, LLVMGetLastBasicBlock(function));
	LLVMPositionBuilderAtEnd(builder, compare_elem);

	LLVMValueRef left_elem = LLVMBuildLoad(builder, LLVMBuildGEP(builder, left_arr, &index, 1, ""), "");
	LLVMValueRef right_elem = LLVMBuildLoad(builder, LLVMBuildGEP(builder, right_arr, &index, 1, ""), "");

	LLVMBuildCondBr(builder, compileEqualityOperator(location, context, builder, left_elem, right_elem, type->contained), compare_content_end, compare_end);

	// Index increment node
	LLVMMoveBasicBlockAfter(compare_content_end, LLVMGetLastBasicBlock(function));
	LLVMPositionBuilderAtEnd(builder, compare_content_end);

	LLVMValueRef next_index = LLVMBuildNSWAdd(builder, index, LLVMConstInt(LLVMInt32TypeInContext(context.context), 1, false), "");
	incoming_values_local.push_back(next_index);
	incoming_blocks_local.push_back(compare_content_end);

	LLVMAddIncoming(index, incoming_values_local.data(), incoming_blocks_local.data(), incoming_values_local.size());

	LLVMBuildBr(builder, compare_content);

	// Result computation node
	LLVMMoveBasicBlockAfter(compare_end, LLVMGetLastBasicBlock(function));
	LLVMPositionBuilderAtEnd(builder, compare_end);

	LLVMPHIRef result = LLVMBuildPhi(builder, LLVMInt1TypeInContext(context.context), "");

	std::vector<LLVMValueRef> incoming_values;
	std::vector<LLVMBasicBlockRef> incoming_blocks;

	// If we got here after we checked for equality of all array elements and succedeed, that means arrays are equal
	incoming_values.push_back(LLVMConstInt(LLVMInt1TypeInContext(context.context), true, false));
	incoming_blocks.push_back(compare_content);
	// If we got here from the block where the size comparison happened and failed, that means arrays were not equal
	incoming_values.push_back(LLVMConstInt(LLVMInt1TypeInContext(context.context), false, false));
	incoming_blocks.push_back(compare_size);
	// If we got here from the block where the array elements are compared and failed, that means arrays were not equal
	incoming_values.push_back(LLVMConstInt(LLVMInt1TypeInContext(context.context), false, false));
	incoming_blocks.push_back(compare_elem);

	LLVMAddIncoming(result, incoming_values.data(), incoming_blocks.data(), incoming_values.size());

	return result;
}

LLVMValueRef compileEqualityOperator(const Location& location, Context& context, LLVMBuilderRef builder, LLVMValueRef left, LLVMValueRef right, Type* type)
{
	type = getTypeInstance(context, type, location);

	if (CASE(TypeUnit, type))
		return LLVMConstInt(LLVMInt1TypeInContext(context.context), true, false);

	if (CASE(TypeInt, type))
		return LLVMBuildICmp(builder, LLVMIntEQ, left, right, "");

	if (CASE(TypeChar, type))
		return LLVMBuildICmp(builder, LLVMIntEQ, left, right, "");

	if (CASE(TypeFloat, type))
		return LLVMBuildFCmp(builder, LLVMRealOEQ, left, right, "");

	if (CASE(TypeBool, type))
		return LLVMBuildICmp(builder, LLVMIntEQ, left, right, "");

	if (CASE(TypeArray, type))
		return compileArrayEqualityOperator(location, context, builder, left, right, _);

	if (CASE(TypeFunction, type))
		errorf(location, "Cannot compare functions"); // Feel free to implement

	if (CASE(TypeTuple, type))
		return compileStructEqualityOperator(location, context, builder, left, right, _, _->members);

	if (CASE(TypeInstance, type))
	{
		auto instance = _;

		size_t generic_type_count = instantiateInstanceTypes(context, instance, location);

		if (CASE(TypePrototypeRecord, *instance->prototype))
		{
			LLVMValueRef result = compileStructEqualityOperator(location, context, builder, left, right, instance, _->member_types);
			context.generic_instances.resize(generic_type_count);
			return result;
		}

		if (CASE(TypePrototypeUnion, *instance->prototype))
		{
			LLVMValueRef result = compileUnionEqualityOperator(location, context, builder, left, right, instance, _);
			context.generic_instances.resize(generic_type_count);
			return result;
		}

		assert(!"Unknown type prototype");
		return 0;
	}

	assert(!"Unknown type in comparison");
	return 0;
}

LLVMValueRef compileUnit(Context& context)
{
	// since we only have int type right now, unit should be int :)
	return LLVMConstInt(LLVMInt32TypeInContext(context.context), 0, false);
}

LLVMValueRef compileExpr(Context& context, LLVMBuilderRef builder, Expr* node)
{
	assert(node);

	if (CASE(ExprUnit, node))
	{
		return compileUnit(context);
	}

	if (CASE(ExprNumberLiteral, node))
	{
		return LLVMConstInt(LLVMInt32TypeInContext(context.context), uint32_t(_->value), false);
	}

	if (CASE(ExprCharacterLiteral, node))
	{
		return LLVMConstInt(LLVMInt8TypeInContext(context.context), uint32_t(_->value), false);
	}

	if (CASE(ExprBooleanLiteral, node))
	{
		return LLVMConstInt(LLVMInt1TypeInContext(context.context), _->value, false);
	}

	if (CASE(ExprArrayLiteral, node))
	{
		if (!dynamic_cast<TypeArray*>(finalType(_->type)))
			errorf(_->location, "array type is unknown");

		LLVMTypeRef array_type = compileType(context, _->type, _->location);

		LLVMTypeRef element_type = LLVMGetElementType(LLVMGetContainedType(array_type, 0));
		
		LLVMValueRef arr = LLVMConstNull(array_type);

		LLVMValueRef data = LLVMBuildBitCast(builder, LLVMBuildCall1(builder, LLVMGetNamedFunction(context.module, "malloc"), LLVMConstInt(LLVMInt32TypeInContext(context.context), uint32_t(_->elements.size() * LLVMABISizeOfType(context.targetData, element_type)), false)), LLVMGetContainedType(array_type, 0), "");

		arr = LLVMBuildInsertValue(builder, arr, data, 0, "");

		for (size_t i = 0; i < _->elements.size(); i++)
		{
			LLVMValueRef index = LLVMConstInt(LLVMInt32TypeInContext(context.context), uint32_t(i), false);
			LLVMValueRef target = LLVMBuildGEP(builder, data, &index, 1, "");

			LLVMBuildStore(builder, compileExpr(context, builder, _->elements[i]), target);
		}

		return LLVMBuildInsertValue(builder, arr, LLVMConstInt(LLVMInt32TypeInContext(context.context), uint32_t(_->elements.size()), false), 1, "");
	}

	if (CASE(ExprTupleLiteral, node))
	{
		if (!dynamic_cast<TypeTuple*>(finalType(_->type)))
			errorf(_->location, "tuple type is unknown");

		LLVMTypeRef tuple_type = compileType(context, _->type, _->location);

		LLVMValueRef tuple = LLVMConstNull(tuple_type);

		for (size_t i = 0; i < _->elements.size(); i++)
			tuple = LLVMBuildInsertValue(builder, tuple, compileExpr(context, builder, _->elements[i]), uint32_t(i), "");

		return tuple;
	}

	if (CASE(ExprBinding, node))
	{
		return compileBinding(context, builder, _->binding, _->type, _->location);
	}

	if (CASE(ExprBindingExternal, node))
	{
		LLVMValueRef value = compileBinding(context, builder, _->context, _->type, _->location);

		value = LLVMBuildPointerCast(builder, value, context.function_context_type.back(), "");

		LLVMValueRef result = LLVMBuildLoad(builder, LLVMBuildStructGEP(builder, value, _->member_index, ""), "");
		
		std::string meta_name = "invariant.load";
		LLVMSetMetadata(result, LLVMGetMDKindIDInContext(context.context, meta_name.c_str(), meta_name.length()), LLVMMDNodeInContext(context.context, 0, 0));

		if (BindingFunction* bindfun = dynamic_cast<BindingFunction*>(_->binding))
		{
			// result here is not the function pointer, it's just the context (argh!)
			LLVMFunctionRef function = compileBindingFunction(context, bindfun, _->type, _->location);

			LLVMValueRef funcptr = compileFunctionValue(context, builder, function, _->type, NULL, _->location);

			return LLVMBuildInsertValue(builder, funcptr, result, 1, "");
		}

		return result;
	}

	if (CASE(ExprUnaryOp, node))
	{
		LLVMValueRef ev = compileExpr(context, builder, _->expr);

		switch (_->op)
		{
		case SynUnaryOpPlus: return ev;
		case SynUnaryOpMinus: return LLVMBuildNeg(builder, ev, "");
		case SynUnaryOpRefGet: return LLVMBuildLoad(builder, LLVMBuildBitCast(builder, LLVMBuildExtractValue(builder, ev, 1, ""), LLVMPointerType(compileType(context, _->type, _->location), 0), ""), "");
		case SynUnaryOpNot: return LLVMBuildNot(builder, ev, "");
		default: assert(!"Unknown unary operation"); return 0;
		}
	}

	if (CASE(ExprBinaryOp, node))
	{
		switch (_->op)
		{
		case SynBinaryOpAdd: return LLVMBuildAdd(builder, compileExpr(context, builder, _->left), compileExpr(context, builder, _->right), "");
		case SynBinaryOpSubtract: return LLVMBuildSub(builder, compileExpr(context, builder, _->left), compileExpr(context, builder, _->right), "");
		case SynBinaryOpMultiply: return LLVMBuildMul(builder, compileExpr(context, builder, _->left), compileExpr(context, builder, _->right), "");
		case SynBinaryOpDivide: return LLVMBuildSDiv(builder, compileExpr(context, builder, _->left), compileExpr(context, builder, _->right), "");
		case SynBinaryOpLess: return LLVMBuildICmp(builder, LLVMIntSLT, compileExpr(context, builder, _->left), compileExpr(context, builder, _->right), "");
		case SynBinaryOpLessEqual: return LLVMBuildICmp(builder, LLVMIntSLE, compileExpr(context, builder, _->left), compileExpr(context, builder, _->right), "");
		case SynBinaryOpGreater: return LLVMBuildICmp(builder, LLVMIntSGT, compileExpr(context, builder, _->left), compileExpr(context, builder, _->right), "");
		case SynBinaryOpGreaterEqual: return LLVMBuildICmp(builder, LLVMIntSGE, compileExpr(context, builder, _->left), compileExpr(context, builder, _->right), "");
		case SynBinaryOpEqual: return compileEqualityOperator(_->location, context, builder, compileExpr(context, builder, _->left), compileExpr(context, builder, _->right), _->left->type);
		case SynBinaryOpNotEqual: return LLVMBuildNot(builder, compileEqualityOperator(_->location, context, builder, compileExpr(context, builder, _->left), compileExpr(context, builder, _->right), _->left->type), "");
		case SynBinaryOpRefSet:
			{
				LLVMValueRef lv = compileExpr(context, builder, _->left);
				LLVMValueRef rv = compileExpr(context, builder, _->right);

				LLVMBuildStore(builder, rv, LLVMBuildBitCast(builder, LLVMBuildExtractValue(builder, lv, 1, ""), LLVMPointerType(LLVMTypeOf(rv), 0), ""));
				return compileUnit(context);
			}
		case SynBinaryOpAnd:
		case SynBinaryOpOr:
			{
				LLVMFunctionRef function = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));

				LLVMBasicBlockRef current_basic_block = LLVMGetInsertBlock(builder);
				LLVMBasicBlockRef next_basic_block = LLVMAppendBasicBlockInContext(context.context, function, "next");
				LLVMBasicBlockRef after_basic_block = LLVMAppendBasicBlockInContext(context.context, function, "after");

				LLVMBuildCondBr(builder, compileExpr(context, builder, _->left), _->op == SynBinaryOpOr ? after_basic_block : next_basic_block, _->op == SynBinaryOpOr ? next_basic_block : after_basic_block);

				LLVMMoveBasicBlockAfter(next_basic_block, LLVMGetLastBasicBlock(function));
				LLVMPositionBuilderAtEnd(builder, next_basic_block);

				LLVMValueRef rv = compileExpr(context, builder, _->right);
				LLVMBuildBr(builder, after_basic_block);

				LLVMMoveBasicBlockAfter(after_basic_block, LLVMGetLastBasicBlock(function));
				LLVMPositionBuilderAtEnd(builder, after_basic_block);

				LLVMPHIRef pn = LLVMBuildPhi(builder, compileType(context, _->type, _->location), "");

				LLVMAddIncoming(pn, &rv, &next_basic_block, 1);

				LLVMValueRef lv = LLVMConstInt(LLVMInt1TypeInContext(context.context), _->op == SynBinaryOpOr ? 1 : 0, false);

				LLVMAddIncoming(pn, &lv, &current_basic_block, 1);

				return pn;
			}
		default: assert(!"Unknown binary operation"); return 0;
		}
	}

	if (CASE(ExprCall, node))
	{
		LLVMValueRef holder = compileExpr(context, builder, _->expr);

		std::vector<LLVMValueRef> args;

		for (size_t i = 0; i < _->args.size(); ++i)
			args.push_back(compileExpr(context, builder, _->args[i]));

		args.push_back(LLVMBuildExtractValue(builder, holder, 1, ""));

		return LLVMBuildCall(builder, LLVMBuildExtractValue(builder, holder, 0, ""), args.data(), args.size(), "");
	}

	if (CASE(ExprArrayIndex, node))
	{
		LLVMFunctionRef function = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));

		LLVMValueRef arr = compileExpr(context, builder, _->arr);
		LLVMValueRef index = compileExpr(context, builder, _->index);

		LLVMValueRef data = LLVMBuildExtractValue(builder, arr, 0, "");
		LLVMValueRef size = LLVMBuildExtractValue(builder, arr, 1, "");

		LLVMBasicBlockRef trap_basic_block = LLVMAppendBasicBlockInContext(context.context, function, "trap");
		LLVMBasicBlockRef after_basic_block = LLVMAppendBasicBlockInContext(context.context, function, "after");

		LLVMBuildCondBr(builder, LLVMBuildICmp(builder, LLVMIntULT, index, size, ""), after_basic_block, trap_basic_block);

		LLVMMoveBasicBlockAfter(trap_basic_block, LLVMGetLastBasicBlock(function));
		LLVMPositionBuilderAtEnd(builder, trap_basic_block);

		LLVMBuildCall(builder, LLVMGetOrInsertFunction(context.module, "llvm.trap", LLVMFunctionType(LLVMVoidTypeInContext(context.context), 0, 0, false)), 0, 0, "");
		LLVMBuildUnreachable(builder);

		LLVMMoveBasicBlockAfter(after_basic_block, LLVMGetLastBasicBlock(function));
		LLVMPositionBuilderAtEnd(builder, after_basic_block);

		return LLVMBuildLoad(builder, LLVMBuildGEP(builder, data, &index, 1, ""), "");
	}

	if (CASE(ExprArraySlice, node))
	{
		LLVMFunctionRef function = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));

		LLVMValueRef arr = compileExpr(context, builder, _->arr);

		LLVMValueRef arr_data = LLVMBuildExtractValue(builder, arr, 0, "");
		LLVMValueRef arr_size = LLVMBuildExtractValue(builder, arr, 1, "");

		LLVMValueRef index_start = compileExpr(context, builder, _->index_start);
		LLVMValueRef index_end = _->index_end ? LLVMBuildAdd(builder, compileExpr(context, builder, _->index_end), LLVMConstInt(LLVMInt32TypeInContext(context.context), 1, false), "") : arr_size;

		LLVMTypeRef array_type = compileType(context, _->type, _->location);
		LLVMTypeRef element_type = LLVMGetElementType(LLVMGetContainedType(array_type, 0));

		LLVMValueRef length = LLVMBuildSub(builder, index_end, index_start, "");
		LLVMValueRef byte_length = LLVMBuildMul(builder, length, LLVMConstInt(LLVMInt32TypeInContext(context.context), LLVMABISizeOfType(context.targetData, element_type), false), "");

		LLVMValueRef arr_slice_data = LLVMBuildBitCast(builder, LLVMBuildCall1(builder, LLVMGetNamedFunction(context.module, "malloc"), byte_length), LLVMGetContainedType(array_type, 0), "");
		LLVMBuildMemCpy(builder, context.context, context.module, arr_slice_data, LLVMBuildGEP(builder, arr_data, &index_start, 1, ""), byte_length, 0, false);

		LLVMValueRef arr_slice = LLVMConstNull(array_type);
		arr_slice = LLVMBuildInsertValue(builder, arr_slice, arr_slice_data, 0, "");
		arr_slice = LLVMBuildInsertValue(builder, arr_slice, length, 1, "");

		return arr_slice;
	}

	if (CASE(ExprMemberAccess, node))
	{
		LLVMValueRef aggr = compileExpr(context, builder, _->aggr);

		TypeInstance* inst_type = dynamic_cast<TypeInstance*>(getTypeInstance(context, _->aggr->type, _->aggr->location));
		TypePrototypeRecord* record_type = inst_type ? dynamic_cast<TypePrototypeRecord*>(*inst_type->prototype) : 0;

		if (record_type)
			return LLVMBuildExtractValue(builder, aggr, getMemberIndexByName(record_type, _->member_name, _->location), "");

		errorf(_->location, "Expected a record type");
	}

	if (CASE(ExprLetVar, node))
	{
		LLVMValueRef value = compileExpr(context, builder, _->body);

		LLVMSetValueName(value, _->target->name.c_str());

		context.values[_->target] = value;

		return value;
	}

	if (CASE(ExprLetVars, node))
	{
		LLVMValueRef value = compileExpr(context, builder, _->body);

		for (size_t i = 0; i < _->targets.size(); ++i)
		{
			if (!_->targets[i])
				continue;

			LLVMValueRef element = LLVMBuildExtractValue(builder, value, i, "");

			LLVMSetValueName(element, _->targets[i]->name.c_str());

			context.values[_->targets[i]] = element;
		}

		return LLVMConstInt(LLVMInt32TypeInContext(context.context), 0, false);
	}

	if (CASE(ExprLetFunc, node))
	{
		if (!_->externals.empty())
		{
			// anonymous function hack
			// please make it stop
			if (_->target->name.empty())
			{
				LLVMTypeRef context_ref_type = compileType(context, _->context_target->type, _->location);
				LLVMTypeRef context_type = LLVMGetElementType(context_ref_type);

				LLVMValueRef context_data = LLVMBuildBitCast(builder, LLVMBuildCall1(builder, LLVMGetNamedFunction(context.module, "malloc"), LLVMConstInt(LLVMInt32TypeInContext(context.context), uint32_t(LLVMABISizeOfType(context.targetData, context_type)), false)), context_ref_type, "");

				context.values[_->context_target] = context_data;
			}

			LLVMValueRef context_data = context.values[_->context_target];

			for (size_t i = 0; i < _->externals.size(); i++)
			{
				Expr* external = _->externals[i];

				BindingBase* binding =
					dynamic_cast<ExprBinding*>(external)
						? dynamic_cast<ExprBinding*>(external)->binding
						: dynamic_cast<ExprBindingExternal*>(external)
							? dynamic_cast<ExprBindingExternal*>(external)->binding
							: 0;

				BindingFunction* bindfun = dynamic_cast<BindingFunction*>(binding);

				LLVMValueRef external_value;
				
				if (bindfun)
				{
					if (dynamic_cast<ExprBinding*>(external))
						external_value = context.values[bindfun->context_target];
					else
					{
						ExprBindingExternal* bindexpr = dynamic_cast<ExprBindingExternal*>(external);
						assert(bindexpr);

						LLVMValueRef value = compileBinding(context, builder, bindexpr->context, bindexpr->type, bindexpr->location);

						value = LLVMBuildPointerCast(builder, value, context.function_context_type.back(), "");

						external_value = LLVMBuildLoad(builder, LLVMBuildStructGEP(builder, value, bindexpr->member_index, ""), "");
					}
				}
				else
				{
					external_value = compileExpr(context, builder, external);
				}
		
				if (external_value)
				{
					LLVMValueRef external_value_cast = bindfun ? LLVMBuildBitCast(builder, external_value, LLVMPointerType(LLVMInt8TypeInContext(context.context), 0), "") : external_value;

					LLVMBuildStore(builder, external_value_cast, LLVMBuildStructGEP(builder, context_data, i, ""));
				}
			}
		}

		if (_->target->name.empty())
		{
			// anonymous function, compile right now
			LLVMFunctionRef func = compileFunctionInstance(context, _, context.generic_instances, _->type, _->location);
			LLVMValueRef funcptr = compileFunctionValue(context, builder, func, _->type, _->context_target, _->location);

			context.values[_->target] = funcptr;
			return funcptr;
		}
		else
		{
			// defer function compilation till use site to support generics
			context.functions[_->target] = std::make_pair(_, context.generic_instances);

			// TODO: this is really wrong :-/
			return NULL;
		}
	}

	if (CASE(ExprExternFunc, node))
	{
		LLVMFunctionTypeRef function_type = compileFunctionType(context, _->type, _->location, 0);

		LLVMFunctionRef func = LLVMGetOrInsertFunction(context.module, _->target->name.c_str(), function_type);

		LLVMValueRef argi = LLVMGetFirstParam(func);

		for (size_t i = 0; i < LLVMCountParams(func); ++i, argi = LLVMGetNextParam(argi))
		{
			if (i < _->args.size())
				LLVMSetValueName(argi, _->args[i]->name.c_str());
		}

		LLVMValueRef value = compileFunctionValue(context, builder, func, _->type, NULL, _->location);

		context.values[_->target] = value;

		return value;
	}

	if (CASE(ExprStructConstructorFunc, node))
	{
		// defer function compilation till use site to support generics
		context.functions[_->target] = std::make_pair(_, context.generic_instances);

		// TODO: this is really wrong :-/
		return NULL;
	}

	if (CASE(ExprUnionConstructorFunc, node))
	{
		// defer function compilation till use site to support generics
		context.functions[_->target] = std::make_pair(_, context.generic_instances);

		// TODO: this is really wrong :-/
		return NULL;
	}

	if (CASE(ExprLLVM, node))
	{
		LLVMFunctionRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));

		std::string name = std::string(LLVMGetValueName(func)) + "..autogen";
		std::string body = compileInlineLLVM(context, name, _->body, func, _->location);

		const char* error = LLVMAikeParseAssemblyString(body.c_str(), context.context, context.module);

		if (error)
			errorf(_->location, "Failed to parse llvm inline code: %s", error);

		std::vector<LLVMValueRef> arguments;
		for (LLVMValueRef argi = LLVMGetFirstParam(func); argi; argi = LLVMGetNextParam(argi))
			arguments.push_back(argi);

		return LLVMBuildCall(builder, LLVMGetNamedFunction(context.module, name.c_str()), arguments.data(), arguments.size(), "");
	}

	if (CASE(ExprIfThenElse, node))
	{
		LLVMFunctionRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));

		LLVMValueRef cond = compileExpr(context, builder, _->cond);

		LLVMBasicBlockRef thenbb = LLVMAppendBasicBlockInContext(context.context, func, "then");
		LLVMBasicBlockRef elsebb = LLVMAppendBasicBlockInContext(context.context, func, "else");
		LLVMBasicBlockRef ifendbb = LLVMAppendBasicBlockInContext(context.context, func, "ifend");

		LLVMBuildCondBr(builder, cond, thenbb, elsebb);

		LLVMPositionBuilderAtEnd(builder, thenbb);
		LLVMValueRef thenbody = compileExpr(context, builder, _->thenbody);
		LLVMBuildBr(builder, ifendbb);
		thenbb = LLVMGetInsertBlock(builder);

		LLVMMoveBasicBlockAfter(elsebb, LLVMGetLastBasicBlock(func));

		LLVMPositionBuilderAtEnd(builder, elsebb);
		LLVMValueRef elsebody = compileExpr(context, builder, _->elsebody);
		LLVMBuildBr(builder, ifendbb);
		elsebb = LLVMGetInsertBlock(builder);

		LLVMMoveBasicBlockAfter(ifendbb, LLVMGetLastBasicBlock(func));
		LLVMPositionBuilderAtEnd(builder, ifendbb);
		LLVMPHIRef pn = LLVMBuildPhi(builder, compileType(context, _->type, _->location), "");

		LLVMAddIncoming(pn, &thenbody, &thenbb, 1);
		LLVMAddIncoming(pn, &elsebody, &elsebb, 1);

		return pn;
	}

	if (CASE(ExprForInDo, node))
	{
		LLVMFunctionRef function = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));

		LLVMValueRef arr = compileExpr(context, builder, _->arr);

		LLVMValueRef data = LLVMBuildExtractValue(builder, arr, 0, "");
		LLVMValueRef size = LLVMBuildExtractValue(builder, arr, 1, "");

		LLVMValueRef index = LLVMBuildAlloca(builder, LLVMInt32TypeInContext(context.context), "");
		LLVMBuildStore(builder, LLVMConstInt(LLVMInt32TypeInContext(context.context), 0, false), index);

		LLVMBasicBlockRef step_basic_block = LLVMAppendBasicBlockInContext(context.context, function, "for_step");
		LLVMBasicBlockRef body_basic_block = LLVMAppendBasicBlockInContext(context.context, function, "for_body");
		LLVMBasicBlockRef end_basic_block = LLVMAppendBasicBlockInContext(context.context, function, "for_end");

		LLVMBuildBr(builder, step_basic_block);

		LLVMPositionBuilderAtEnd(builder, step_basic_block);

		LLVMBuildCondBr(builder, LLVMBuildICmp(builder, LLVMIntULT, LLVMBuildLoad(builder, index, ""), size, ""), body_basic_block, end_basic_block);

		LLVMMoveBasicBlockAfter(body_basic_block, LLVMGetLastBasicBlock(function));
		LLVMPositionBuilderAtEnd(builder, body_basic_block);

		LLVMValueRef value = LLVMBuildLoad(builder, index, "");
		context.values[_->target] = LLVMBuildLoad(builder, LLVMBuildGEP(builder, data, &value, 1, ""), "");

		compileExpr(context, builder, _->body);

		LLVMBuildStore(builder, LLVMBuildAdd(builder, LLVMBuildLoad(builder, index, ""), LLVMConstInt(LLVMInt32TypeInContext(context.context), 1, false), ""), index);

		LLVMBuildBr(builder, step_basic_block);

		LLVMMoveBasicBlockAfter(end_basic_block, LLVMGetLastBasicBlock(function));
		LLVMPositionBuilderAtEnd(builder, end_basic_block);

		return LLVMConstInt(LLVMInt32TypeInContext(context.context), 0, false); // Same as ExprUnit
	}

	if (CASE(ExprForInRangeDo, node))
	{
		LLVMFunctionRef function = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));

		LLVMValueRef start = compileExpr(context, builder, _->start);
		LLVMValueRef end = compileExpr(context, builder, _->end);

		LLVMBasicBlockRef before = LLVMGetInsertBlock(builder);
		LLVMBasicBlockRef step_basic_block = LLVMAppendBasicBlockInContext(context.context, function, "for_step");
		LLVMBasicBlockRef body_basic_block = LLVMAppendBasicBlockInContext(context.context, function, "for_body");
		LLVMBasicBlockRef end_basic_block = LLVMAppendBasicBlockInContext(context.context, function, "for_end");

		LLVMBuildBr(builder, step_basic_block);

		LLVMMoveBasicBlockAfter(step_basic_block, LLVMGetLastBasicBlock(function));
		LLVMPositionBuilderAtEnd(builder, step_basic_block);

		LLVMPHIRef index = LLVMBuildPhi(builder, LLVMInt32TypeInContext(context.context), "");
		LLVMAddIncoming(index, &start, &before, 1);

		LLVMBuildCondBr(builder, LLVMBuildICmp(builder, LLVMIntSLE, index, end, ""), body_basic_block, end_basic_block);

		LLVMMoveBasicBlockAfter(body_basic_block, LLVMGetLastBasicBlock(function));
		LLVMPositionBuilderAtEnd(builder, body_basic_block);

		context.values[_->target] = index;

		compileExpr(context, builder, _->body);

		LLVMValueRef next_index = LLVMBuildNSWAdd(builder, index, LLVMConstInt(LLVMInt32TypeInContext(context.context), 1, false), "");
		LLVMAddIncoming(index, &next_index, &body_basic_block, 1);

		LLVMBuildBr(builder, step_basic_block);

		LLVMMoveBasicBlockAfter(end_basic_block, LLVMGetLastBasicBlock(function));
		LLVMPositionBuilderAtEnd(builder, end_basic_block);

		return LLVMConstInt(LLVMInt32TypeInContext(context.context), 0, false); // Same as ExprUnit
	}

	if (CASE(ExprWhileDo, node))
	{
		LLVMFunctionRef function = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));

		LLVMBasicBlockRef condition_basic_block = LLVMAppendBasicBlockInContext(context.context, function, "while_condition");
		LLVMBasicBlockRef body_basic_block = LLVMAppendBasicBlockInContext(context.context, function, "while_body");
		LLVMBasicBlockRef end_basic_block = LLVMAppendBasicBlockInContext(context.context, function, "while_end");

		LLVMBuildBr(builder, condition_basic_block);

		LLVMPositionBuilderAtEnd(builder, condition_basic_block);

		LLVMBuildCondBr(builder, compileExpr(context, builder, _->condition), body_basic_block, end_basic_block);

		LLVMMoveBasicBlockAfter(body_basic_block, LLVMGetLastBasicBlock(function));
		LLVMPositionBuilderAtEnd(builder, body_basic_block);

		compileExpr(context, builder, _->body);

		LLVMBuildBr(builder, condition_basic_block);

		LLVMMoveBasicBlockAfter(end_basic_block, LLVMGetLastBasicBlock(function));
		LLVMPositionBuilderAtEnd(builder, end_basic_block);

		return LLVMConstInt(LLVMInt32TypeInContext(context.context), 0, false); // Same as ExprUnit
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
					options_pack->options.push_back(clone(case_options[i]));

				options = simplify(options);
			}
		}

		if (!match(options, new MatchCaseAny(0, Location(), 0)))
			errorf(_->location, "The match doesn't cover all cases");

		LLVMFunctionRef function = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));

		LLVMValueRef variable = compileExpr(context, builder, _->variable);

		// Expression result
		LLVMValueRef value = LLVMBuildAlloca(builder, compileType(context, _->type, _->location), "");

		// Create a block for all cases
		std::vector<LLVMBasicBlockRef> case_blocks;
		LLVMBasicBlockRef finish_block = LLVMAppendBasicBlockInContext(context.context, function, "finish");

		for (size_t i = 0; i < _->cases.size(); ++i)
			case_blocks.push_back(LLVMAppendBasicBlockInContext(context.context, function, ("check_" + std::to_string((long long)i)).c_str()));

		LLVMBuildBr(builder, case_blocks[0]);

		for (size_t i = 0; i < _->cases.size(); ++i)
		{
			LLVMMoveBasicBlockAfter(case_blocks[i], LLVMGetLastBasicBlock(function));
			LLVMPositionBuilderAtEnd(builder, case_blocks[i]);
			compileMatch(context, builder, _->cases[i], variable, value, _->expressions[i], i == _->cases.size() - 1 ? finish_block : case_blocks[i + 1], finish_block);
		}

		LLVMMoveBasicBlockAfter(finish_block, LLVMGetLastBasicBlock(function));
		LLVMPositionBuilderAtEnd(builder, finish_block);

		return LLVMBuildLoad(builder, value, "");
	}

	if (CASE(ExprBlock, node))
	{
		LLVMValueRef value = 0;

		for (size_t i = 0; i < _->expressions.size(); )
		{
			if (ExprLetFunc* func = dynamic_cast<ExprLetFunc*>(_->expressions[i]))
			{
				size_t count = 0;

				for (; i + count < _->expressions.size(); ++count)
				{
					if (ExprLetFunc* func = dynamic_cast<ExprLetFunc*>(_->expressions[i + count]))
					{
						if (!func->externals.empty())
						{
							LLVMTypeRef context_ref_type = compileType(context, func->context_target->type, func->location);
							LLVMTypeRef context_type = LLVMGetElementType(context_ref_type);

							LLVMValueRef context_data = LLVMBuildBitCast(builder, LLVMBuildCall1(builder, LLVMGetNamedFunction(context.module, "malloc"), LLVMConstInt(LLVMInt32TypeInContext(context.context), uint32_t(LLVMABISizeOfType(context.targetData, context_type)), false)), context_ref_type, "");

							context.values[func->context_target] = context_data;
						}
					}
					else
						break;
				}

				for (size_t j = 0; j < count; ++j)
					value = compileExpr(context, builder, _->expressions[i + j]);

				i += count;
			}
			else
			{
				value = compileExpr(context, builder, _->expressions[i]);

				i++;
			}
		}

		return value;
	}

	assert(!"Unknown AST node type");
	return 0;
}

void compile(LLVMContextRef context, LLVMModuleRef module, LLVMTargetDataRef targetData, Expr* root)
{
	Context ctx;
	ctx.context = context;
	ctx.module = module;
	ctx.targetData = targetData;

	LLVMFunctionRef entryf = LLVMAddFunction(ctx.module, "entrypoint", LLVMFunctionType(LLVMInt32TypeInContext(ctx.context), 0, 0, false));

	LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(ctx.context, entryf, "entry");

	LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx.context);
	LLVMPositionBuilderAtEnd(builder, bb);

	LLVMValueRef result = compileExpr(ctx, builder, root);

	LLVMBuildRet(builder, result);

	if(!LLVMAikeVerifyFunction(entryf))
		errorf(Location(), "Internal compiler error");
}
