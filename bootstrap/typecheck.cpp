#include "typecheck.hpp"

#include "output.hpp"

#include <cassert>

struct Binding
{
	std::string name;
	BindingBase* binding;

	Binding(const std::string& name, BindingBase* binding): name(name), binding(binding)
	{
	}
};

struct TypeBinding
{
	std::string name;
	Type* type;

	TypeBinding(const std::string& name, Type* type): name(name), type(type)
	{
	}
};

struct FunctionInfo
{
	size_t scope;
	BindingBase* context;
	std::vector<BindingBase*> externals;

	FunctionInfo(size_t scope): scope(scope), context(0)
	{
	}
};

struct Environment
{
	std::vector<std::vector<Binding> > bindings;
	std::vector<FunctionInfo> functions;
	std::vector<TypeBinding> types;
	std::vector<TypeGeneric*> generic_types;
};

BindingBase* tryResolveBinding(const std::string& name, Environment& env, size_t* in_scope = 0)
{
	for (size_t scope = env.bindings.size(); scope > 0; --scope)
	{
		for (size_t i = env.bindings[scope - 1].size(); i > 0; --i)
		{
			if (env.bindings[scope - 1][i - 1].name == name)
			{
				if (in_scope)
					*in_scope = scope - 1;

				return env.bindings[scope - 1][i - 1].binding;
			}
		}
	}

	return 0;
}

BindingBase* resolveBinding(const std::string& name, Environment& env, const Location& location)
{
	if (BindingBase* result = tryResolveBinding(name, env))
		return result;

	errorf(location, "Unresolved binding %s", name.c_str());
}

Type* tryResolveType(const std::string& name, Environment& env)
{
	for (size_t i = 0; i < env.types.size(); ++i)
	{
		if (env.types[i].name == name)
			return env.types[i].type;
	}

	return 0;
}

Type* resolveType(const std::string& name, Environment& env, const Location& location)
{
	if (Type* type = tryResolveType(name, env))
		return type;

	errorf(location, "Unknown type %s", name.c_str());
}

TypeGeneric* resolveNewGenericType(SynTypeGeneric* type, Environment& env)
{
	for (size_t i = 0; i < env.generic_types.size(); ++i)
		if (env.generic_types[i]->name == type->type.name)
			errorf(type->type.location, "Generic type '%s already exists", type->type.name.c_str());

	env.generic_types.push_back(new TypeGeneric(type->type.name));

	return env.generic_types.back();
}

Type* resolveTypeInstance(Type* type, const std::string& name, const Location& location, const std::vector<Type*>& generics, Environment& env)
{
	if (CASE(TypeInstance, type))
	{
		if (_->generics.empty() && !generics.empty())
			errorf(location, "Can't instantiate non-generic type %s", name.c_str());

		if (_->generics.size() != generics.size())
			errorf(location, "Expected %d type arguments while instantiating %s, but got %d", static_cast<int>(_->generics.size()), name.c_str(), static_cast<int>(generics.size()));

		return new TypeInstance(_->prototype, generics);
	}

	if (!generics.empty())
		errorf(location, "Can't instantiate non-generic type %s", name.c_str());

	return type;
}

Type* resolveType(SynType* type, Environment& env, bool allow_new_generics = false)
{
	if (!type)
	{
		return new TypeGeneric();
	}

	if (CASE(SynTypeIdentifier, type))
	{
		Type* type = resolveType(_->type.name, env, _->type.location);

		std::vector<Type*> generics;

		for (size_t i = 0; i < _->generics.size(); ++i)
			generics.push_back(resolveType(_->generics[i], env, allow_new_generics));

		return resolveTypeInstance(type, _->type.name, _->type.location, generics, env);
	}

	if (CASE(SynTypeGeneric, type))
	{
		for (size_t i = 0; i < env.generic_types.size(); ++i)
			if (env.generic_types[i]->name == _->type.name)
				return env.generic_types[i];

		if (allow_new_generics)
		{
			return resolveNewGenericType(_, env);
		}
		else
		{
			errorf(_->type.location, "Unknown type '%s", _->type.name.c_str());
		}
	}

	if (CASE(SynTypeArray, type))
	{
		return new TypeArray(resolveType(_->contained_type, env, allow_new_generics));
	}

	if (CASE(SynTypeFunction, type))
	{
		std::vector<Type*> argtys;

		for (size_t i = 0; i < _->args.size(); ++i)
			argtys.push_back(resolveType(_->args[i], env, allow_new_generics));

		return new TypeFunction(resolveType(_->result, env, allow_new_generics), argtys);
	}

	assert(!"Unknown syntax tree type");
	return 0;
}

std::vector<Type*> resolveGenericTypeList(const std::vector<SynTypeGeneric*>& generics, Environment& env)
{
	std::vector<Type*> result;

	for (size_t i = 0; i < generics.size(); ++i)
		result.push_back(resolveNewGenericType(generics[i], env));

	return result;
}

TypePrototypeRecord* resolveRecordType(SynTypeStructure* type, const std::vector<Type*>& generics, Environment& env)
{
	std::vector<Type*> member_types;
	std::vector<std::string> member_names;

	for (size_t i = 0; i < type->members.size(); ++i)
	{
		member_types.push_back(resolveType(type->members[i].type, env));
		member_names.push_back(type->members[i].name.name);
	}

	return new TypePrototypeRecord(type->name.name, member_types, member_names, generics);
}

TypeFunction* resolveFunctionType(SynType* rettype, const std::vector<SynTypedVar>& args, Environment& env, bool allow_new_generics = false)
{
	std::vector<Type*> argtys;

	for (size_t i = 0; i < args.size(); ++i)
		argtys.push_back(resolveType(args[i].type, env, allow_new_generics));

	return new TypeFunction(resolveType(rettype, env, allow_new_generics), argtys);
}

std::pair<TypePrototypeUnion*, size_t> resolveUnionTypeByVariant(const std::string& variant, Environment& env)
{
	for (size_t i = 0; i < env.types.size(); ++i)
		if (TypeInstance* ti = dynamic_cast<TypeInstance*>(env.types[i].type))
			if (TypePrototypeUnion* tu = dynamic_cast<TypePrototypeUnion*>(ti->prototype))
				for (size_t j = 0; j < tu->member_names.size(); ++j)
					if (tu->member_names[j] == variant)
						return std::make_pair(tu, j);

	return std::make_pair(static_cast<TypePrototypeUnion*>(0), 0);
}

Expr* resolveBindingAccess(const std::string& name, Location location, Environment& env)
{
	size_t scope;

	if (BindingBase* binding = tryResolveBinding(name, env, &scope))
	{
		// unit union types are contructed without an empty argument list
		if (CASE(BindingUnionUnitConstructor, binding))
			return new ExprCall(new TypeGeneric(), location, new ExprBinding(_->target->type, location, _), std::vector<Expr*>());

		if (scope < env.functions.back().scope)
		{
			if (CASE(BindingFreeFunction, binding))
			{
				return new ExprBinding(_->target->type, location, _);
			}
			else if (CASE(BindingLocal, binding))
			{
				for (size_t i = 0; i < env.functions.back().externals.size(); ++i)
				{
					if (env.functions.back().externals[i] == binding)
						return new ExprBindingExternal(_->target->type, location, env.functions.back().context, name, i);
				}

				env.functions.back().externals.push_back(binding);
				return new ExprBindingExternal(_->target->type, location, env.functions.back().context, name, env.functions.back().externals.size() - 1);
			}
			else
			{
				errorf(location, "Can't resolve the binding of the function external variable %s", name.c_str());
			}
		}

		if (CASE(BindingLocal, binding))
			return new ExprBinding(_->target->type, location, _);
		else
			return new ExprBinding(new TypeGeneric(), location, binding);
	}

	return 0;
}

TypeInstance* instantiatePrototype(TypePrototype* proto)
{
	size_t generic_count = getGenericTypes(proto).size();

	std::vector<Type*> args;

	for (size_t i = 0; i < generic_count; ++i)
		args.push_back(new TypeGeneric());

	return new TypeInstance(proto, args);
}

MatchCase* resolveMatch(SynMatch* match, Environment& env)
{
	if (CASE(SynMatchNumber, match))
	{
		return new MatchCaseNumber(new TypeInt(), _->location, _->value);
	}

	if (CASE(SynMatchBoolean, match))
	{
		return new MatchCaseNumber(new TypeBool(), _->location, _->value);
	}

	if (CASE(SynMatchArray, match))
	{
		std::vector<MatchCase*> elements;

		for (size_t i = 0; i < _->elements.size(); ++i)
			elements.push_back(resolveMatch(_->elements[i], env));

		return new MatchCaseArray(new TypeGeneric(), _->location, elements);
	}

	if (CASE(SynMatchTypeSimple, match))
	{
		Type* type = tryResolveType(_->type.name, env);

		// Maybe it's a tag from a union
		if (!type)
		{
			std::pair<TypePrototypeUnion*, size_t> union_tag = resolveUnionTypeByVariant(_->type.name, env);

			if (!union_tag.first)
				errorf(_->location, "Unknown type or union tag '%s'", _->type.name.c_str());

			std::vector<Type*> fake_generics;
			for (size_t i = 0; i < union_tag.first->generics.size(); ++i)
				fake_generics.push_back(new TypeGeneric());

			TypeInstance* fake_inst = new TypeInstance(union_tag.first, fake_generics);

			Type* member_type = getMemberTypeByIndex(fake_inst, union_tag.first, union_tag.second, _->location);

			BindingTarget* target = new BindingTarget(_->alias.name, member_type);
		
			env.bindings.back().push_back(Binding(_->alias.name, new BindingLocal(target)));

			// First match the tag, then match the contents
			return new MatchCaseUnion(instantiatePrototype(union_tag.first), _->location, union_tag.second, new MatchCaseAny(member_type, _->location, target));
		}

		BindingTarget* target = new BindingTarget(_->alias.name, type);
		
		env.bindings.back().push_back(Binding(_->alias.name, new BindingLocal(target)));

		return new MatchCaseAny(type, _->location, target);
	}

	if (CASE(SynMatchTypeComplex, match))
	{
		Type* type = tryResolveType(_->type.name, env);

		std::vector<std::string> member_names;
		std::vector<MatchCase*> member_values;

		for (size_t i = 0; i < _->arg_values.size(); ++i)
		{
			if (!_->arg_names.empty())
				member_names.push_back(_->arg_names[i].name);
			member_values.push_back(resolveMatch(_->arg_values[i], env));
		}

		// Maybe it's a tag from a union
		if (!type)
		{
			std::pair<TypePrototypeUnion*, size_t> union_tag = resolveUnionTypeByVariant(_->type.name, env);

			if (!union_tag.first)
				errorf(_->location, "Unknown type or union tag '%s'", _->type.name.c_str());

			// First match the tag, then match the contents
			return new MatchCaseUnion(instantiatePrototype(union_tag.first), _->location, union_tag.second, new MatchCaseMembers(new TypeGeneric(), _->location, member_values, member_names));
		}

		return new MatchCaseMembers(type, _->location, member_values, member_names);
	}

	if (CASE(SynMatchPlaceholder, match))
	{
		// Special case for an unit union member
		std::pair<TypePrototypeUnion*, size_t> union_tag = resolveUnionTypeByVariant(_->alias.name.name, env);

		if (union_tag.first)
		{
			return new MatchCaseUnion(instantiatePrototype(union_tag.first), _->location, union_tag.second, new MatchCaseAny(new TypeGeneric(), _->location, 0));
		}

		BindingTarget* target = new BindingTarget(_->alias.name.name, new TypeGeneric());
		
		env.bindings.back().push_back(Binding(_->alias.name.name, new BindingLocal(target)));

		return new MatchCaseAny(target->type, _->location, target);
	}

	if (CASE(SynMatchPlaceholderUnnamed, match))
	{
		return new MatchCaseAny(new TypeGeneric(), _->location, 0);
	}

	assert(!"Unrecognized AST SynMatch type");
	return 0;
}

Expr* resolveExpr(SynBase* node, Environment& env)
{
	assert(node);

	if (CASE(SynUnit, node))
		return new ExprUnit(resolveType("unit", env, _->location), _->location);

	if (CASE(SynNumberLiteral, node))
		return new ExprNumberLiteral(resolveType("int", env, _->location), _->location, _->value);

	if (CASE(SynBooleanLiteral, node))
		return new ExprBooleanLiteral(resolveType("bool", env, _->location), _->location, _->value);

	if (CASE(SynArrayLiteral, node))
	{
		std::vector<Expr*> elements;

		for (size_t i = 0; i < _->elements.size(); ++i)
			elements.push_back(resolveExpr(_->elements[i], env));

		return new ExprArrayLiteral(elements.empty() ? (Type*)new TypeGeneric() : (Type*)new TypeArray(elements[0]->type), _->location, elements);
	}

	if (CASE(SynTypeDefinition, node))
	{
		size_t generic_type_count = env.generic_types.size();

		std::vector<Type*> generic_types = resolveGenericTypeList(_->generics, env);

		TypePrototypeRecord* record_type = resolveRecordType(_->type_struct, generic_types, env);
		TypeInstance* inst_type = new TypeInstance(record_type, generic_types);

		std::vector<BindingTarget*> args;

		for (size_t i = 0; i < record_type->member_types.size(); ++i)
			args.push_back(new BindingTarget(record_type->member_names[i], record_type->member_types[i]));

		env.types.push_back(TypeBinding(_->type_struct->name.name, inst_type));

		TypeFunction* function_type = new TypeFunction(inst_type, record_type->member_types);

		BindingTarget* target = new BindingTarget(_->type_struct->name.name, function_type);

		env.bindings.back().push_back(Binding(_->type_struct->name.name, new BindingFreeFunction(target, record_type->member_names)));

		env.generic_types.resize(generic_type_count);

		return new ExprStructConstructorFunc(function_type, _->location, target, args);
	}

	if (CASE(SynUnionDefinition, node))
	{
		size_t generic_type_count = env.generic_types.size();

		std::vector<Type*> generic_types = resolveGenericTypeList(_->generics, env);

		TypePrototypeUnion* union_type = new TypePrototypeUnion(_->name.name, std::vector<Type*>(), std::vector<std::string>(), generic_types);
		TypeInstance* inst_type = new TypeInstance(union_type, generic_types);

		ExprBlock *expression = new ExprBlock(new TypeUnit(), _->location);

		env.types.push_back(TypeBinding(_->name.name, inst_type));

		for (size_t i = 0; i < _->members.size(); i++)
		{
			std::vector<Type*> member_types;
			std::vector<std::string> member_names;
			std::vector<BindingTarget*> args;

			Type* element_type = 0;

			if (SynTypeStructure* type_struct = dynamic_cast<SynTypeStructure*>(_->members[i].type))
			{
				TypePrototypeRecord* record_type = resolveRecordType(type_struct, generic_types, env);
				TypeInstance* inst_type = new TypeInstance(record_type, generic_types);

				member_types = record_type->member_types;
				member_names = record_type->member_names;

				element_type = inst_type;
			}
			else if (_->members[i].type)
			{
				Type* type = resolveType(_->members[i].type, env);

				member_types.push_back(type);
				member_names.push_back("value");

				element_type = type;
			}
			else
			{
				element_type = new TypeUnit();
			}

			union_type->member_names.push_back(_->members[i].name.name);
			union_type->member_types.push_back(element_type);

			for (size_t k = 0; k < member_names.size(); ++k)
				args.push_back(new BindingTarget(member_names[k], member_types[k]));

			TypeFunction* function_type = new TypeFunction(inst_type, member_types);

			BindingTarget* target = new BindingTarget(_->members[i].name.name, function_type);

			env.bindings.back().push_back(Binding(_->members[i].name.name, _->members[i].type ? new BindingFreeFunction(target, member_names) : new BindingUnionUnitConstructor(target, member_names)));

			expression->expressions.push_back(new ExprUnionConstructorFunc(function_type, _->location, target, args, i, element_type));
		}

		expression->expressions.push_back(new ExprUnit(new TypeUnit(), Location()));

		env.generic_types.resize(generic_type_count);

		return expression;
	}

	if (CASE(SynVariableReference, node))
	{
		if (Expr* access = resolveBindingAccess(_->name, _->location, env))
			return access;

		errorf(_->location, "Unresolved variable reference %s", _->name.c_str());
	}

	if (CASE(SynUnaryOp, node))
	{
		Expr* value = resolveExpr(_->expr, env);

		Type* result_type = _->op == SynUnaryOpNot ? new TypeBool() : value->type;

		return new ExprUnaryOp(result_type, _->location, _->op, value);
	}

	if (CASE(SynBinaryOp, node))
	{
		Expr* left = resolveExpr(_->left, env);
		Expr* right = resolveExpr(_->right, env);

		return new ExprBinaryOp(new TypeGeneric(), _->location, _->op, left, right);
	}

	if (CASE(SynCall, node))
	{
		Expr* function = resolveExpr(_->expr, env);

		std::vector<Expr*> args;
		args.insert(args.begin(), _->arg_values.size(), 0);

		if (!_->arg_names.empty())
		{
			ExprBinding* expr_binding = dynamic_cast<ExprBinding*>(function);
			BindingFunction* binding_function = expr_binding ? dynamic_cast<BindingFunction*>(expr_binding->binding) : 0;

			if (!binding_function)
				errorf(_->location, "Cannot match argument names to a value");

			for (size_t i = 0; i < _->arg_names.size(); ++i)
			{
				// Find position of the function argument
				bool found = false;
				for (size_t k = 0; k < binding_function->arg_names.size() && !found; ++k)
				{
					if (_->arg_names[i].name == binding_function->arg_names[k])
					{
						if (args[k])
							errorf(_->location, "Value for argument '%s' is already defined", binding_function->arg_names[k].c_str());
						args[k] = resolveExpr(_->arg_values[i], env);
						found = true;
					}
				}
				if (!found)
					errorf(_->location, "Function doesn't accept an argument named '%s'", _->arg_names[i].name.c_str());
			}
		}
		else
		{
			for (size_t i = 0; i < _->arg_values.size(); ++i)
				args[i] = resolveExpr(_->arg_values[i], env);
		}

		TypeFunction* function_type = dynamic_cast<TypeFunction*>(function->type);

		return new ExprCall(function_type ? function_type->result : new TypeGeneric(), _->location, function, args);
	}

	if (CASE(SynArrayIndex, node))
	{
		Expr* arr = resolveExpr(_->arr, env);
		Expr* index = resolveExpr(_->index, env);

		TypeArray* arr_type = dynamic_cast<TypeArray*>(arr->type);

		return new ExprArrayIndex(arr_type ? arr_type->contained : new TypeGeneric(), _->location, arr, index);
	}

	if (CASE(SynArraySlice, node))
	{
		Expr* arr = resolveExpr(_->arr, env);
		Expr* index_start = resolveExpr(_->index_start, env);
		Expr* index_end = _->index_end ? resolveExpr(_->index_end, env) : 0;

		TypeArray* arr_type = dynamic_cast<TypeArray*>(arr->type);

		return new ExprArraySlice(arr_type ? (Type*)arr_type : new TypeGeneric(), _->location, arr, index_start, index_end);
	}

	if (CASE(SynMemberAccess, node))
	{
		Expr* aggr = resolveExpr(_->aggr, env);

		return new ExprMemberAccess(new TypeGeneric(), _->member.location, aggr, _->member.name);
	}

	if (CASE(SynLetVar, node))
	{
		BindingTarget* target = new BindingTarget(_->var.name.name, resolveType(_->var.type, env));

		Expr* body = resolveExpr(_->body, env);

		env.bindings.back().push_back(Binding(_->var.name.name, new BindingLocal(target)));

		return new ExprLetVar(target->type, _->location, target, body);
	}

	if (CASE(SynLLVM, node))
		return new ExprLLVM(new TypeGeneric(), _->location, _->body);

	if (CASE(SynLetFunc, node))
	{
		std::vector<BindingTarget*> args;
		std::vector<std::string> arg_names;

		env.functions.push_back(FunctionInfo(env.bindings.size()));
		env.bindings.push_back(std::vector<Binding>());

		size_t generic_type_count = env.generic_types.size();

		TypeFunction* funty = resolveFunctionType(_->ret_type, _->args, env, /* allow_new_generics= */ true);

		for (size_t i = 0; i < _->args.size(); ++i)
		{
			BindingTarget* target = new BindingTarget(_->args[i].name.name, funty->args[i]);

			args.push_back(target);
			arg_names.push_back(_->args[i].name.name);
			env.bindings.back().push_back(Binding(_->args[i].name.name, new BindingLocal(target)));
		}

		BindingTarget* target = new BindingTarget(_->var.name, funty);

		// Add info about function context. Context type will be resolved later
		TypeClosureContext* context_type = new TypeClosureContext();
		BindingTarget* context_target = new BindingTarget("extern", context_type);
		env.functions.back().context = new BindingLocal(context_target);

		env.bindings.back().push_back(Binding(_->var.name, new BindingLocal(target)));

		Expr* body = resolveExpr(_->body, env);

		bool has_externals = !env.functions.back().externals.empty();

		// Resolve function context type
		for (size_t i = 0; i < env.functions.back().externals.size(); ++i)
		{
			if (CASE(BindingLocal, env.functions.back().externals[i]))
			{
				context_type->member_types.push_back(_->target->type);
				context_type->member_names.push_back(_->target->name);
			}
		}

		std::vector<BindingBase*> function_externals = env.functions.back().externals;

		env.functions.pop_back();
		env.bindings.pop_back();

		env.bindings.back().push_back(Binding(_->var.name, function_externals.empty() ? (BindingBase*)new BindingFreeFunction(target, arg_names) : (BindingBase*)new BindingFunction(target, arg_names)));

		// Resolve function external variable capture
		std::vector<Expr*> externals;

		for (size_t i = 0; i < function_externals.size(); ++i)
		{
			if (CASE(BindingLocal, function_externals[i]))
				externals.push_back(resolveBindingAccess(_->target->name, Location(), env));
		}

		env.generic_types.resize(generic_type_count);

		return new ExprLetFunc(target->type, _->location, target, has_externals ? context_target : 0, args, body, externals);
	}

	if (CASE(SynExternFunc, node))
	{
		Type* funty = resolveFunctionType(_->ret_type, _->args, env);

		BindingTarget* target = new BindingTarget(_->var.name, funty);

		std::vector<BindingTarget*> args;
		std::vector<std::string> arg_names;

		for (size_t i = 0; i < _->args.size(); ++i)
		{
			BindingTarget* target = new BindingTarget(_->args[i].name.name, resolveType(_->args[i].type, env));

			args.push_back(target);
			arg_names.push_back(_->args[i].name.name);
		}

		env.bindings.back().push_back(Binding(_->var.name, new BindingFreeFunction(target, arg_names)));

		return new ExprExternFunc(funty, _->location, target, args);
	}

	if (CASE(SynIfThenElse, node))
	{
		Expr* cond = resolveExpr(_->cond, env);
		Expr* thenbody = resolveExpr(_->thenbody, env);
		Expr* elsebody = resolveExpr(_->elsebody, env);

		return new ExprIfThenElse(new TypeGeneric(), _->location, cond, thenbody, elsebody);
	}

	if (CASE(SynForInDo, node))
	{
		Expr* arr = resolveExpr(_->arr, env);

		BindingTarget* target = new BindingTarget(_->var.name.name, resolveType(_->var.type, env));

		env.bindings.back().push_back(Binding(_->var.name.name, new BindingLocal(target)));

		Expr* body = resolveExpr(_->body, env);

		env.bindings.back().pop_back();

		return new ExprForInDo(new TypeUnit(), _->location, target, arr, body);
	}

	if (CASE(SynMatchWith, node))
	{
		Expr* variable = resolveExpr(_->variable, env);

		std::vector<MatchCase*> cases;
		std::vector<Expr*> expressions;

		for (size_t i = 0; i < _->variants.size(); i++)
		{
			// Pattern can create new bindings to be used in the expression
			size_t binding_count = env.bindings.back().size();

			cases.push_back(resolveMatch(_->variants[i], env));

			expressions.push_back(resolveExpr(_->expressions[i], env));

			while (env.bindings.back().size() > binding_count)
				env.bindings.back().pop_back();
		}

		return new ExprMatchWith(new TypeGeneric(), _->location, variable, cases, expressions);
	}

	if (CASE(SynBlock, node))
	{
		ExprBlock *expression = new ExprBlock(new TypeUnit(), _->location);
		
		size_t type_count = env.types.size();

		env.bindings.push_back(std::vector<Binding>());

		for (size_t i = 0; i < _->expressions.size(); ++i)
			expression->expressions.push_back(resolveExpr(_->expressions[i], env));

		env.bindings.pop_back();

		while (env.types.size() > type_count)
			env.types.pop_back();

		// Block type is the type of the last expression in block
		if (!expression->expressions.empty())
			expression->type = expression->expressions.back()->type;

		return expression;
	}

	assert(!"Unrecognized AST type");
	return 0;
}

Expr* resolve(SynBase* root)
{
	Environment env;

	env.types.push_back(TypeBinding("unit", new TypeUnit()));
	env.types.push_back(TypeBinding("int", new TypeInt()));
	env.types.push_back(TypeBinding("float", new TypeFloat()));
	env.types.push_back(TypeBinding("bool", new TypeBool()));

	env.functions.push_back(FunctionInfo(env.bindings.size()));
	env.bindings.push_back(std::vector<Binding>());

	return resolveExpr(root, env);
}

Type* prune(Type* t)
{
	if (CASE(TypeGeneric, t))
	{
		if (_->instance)
		{
			_->instance = prune(_->instance);
			return _->instance;
		}

		return _;
	}

	return t;
}

bool occurs(Type* lhs, Type* rhs)
{
	rhs = prune(rhs);

	if (lhs == rhs)
		return true;

	if (CASE(TypeArray, rhs))
	{
		return occurs(lhs, _->contained);
	}

	if (CASE(TypeFunction, rhs))
	{
		if (occurs(lhs, _->result)) return true;

		for (size_t i = 0; i < _->args.size(); ++i)
			if (occurs(lhs, _->args[i]))
				return true;

		return false;
	}

	if (CASE(TypeInstance, rhs))
	{
		for (size_t i = 0; i < _->generics.size(); ++i)
			if (occurs(lhs, _->generics[i]))
				return true;

		return false;
	}

	return false;
}

bool occurs(Type* lhs, const std::vector<Type*>& rhs)
{
	for (size_t i = 0; i < rhs.size(); ++i)
		if (occurs(lhs, rhs[i]))
			return true;

	return false;
}

Type* fresh(Type* t, const std::vector<Type*>& nongen, std::map<TypeGeneric*, TypeGeneric*>& genremap)
{
	t = prune(t);

	if (CASE(TypeGeneric, t))
	{
		if (occurs(t, nongen))
			return t;

		if (genremap.count(_))
			return genremap[_];

		return genremap[_] = new TypeGeneric(_->name);
	}

	if (CASE(TypeArray, t))
	{
		return new TypeArray(fresh(_->contained, nongen, genremap));
	}

	if (CASE(TypeFunction, t))
	{
		std::vector<Type*> args;
		for (size_t i = 0; i < _->args.size(); ++i)
			args.push_back(fresh(_->args[i], nongen, genremap));

		return new TypeFunction(fresh(_->result, nongen, genremap), args);
	}

	if (CASE(TypeInstance, t))
	{
		std::vector<Type*> generics;

		for (size_t i = 0; i < _->generics.size(); ++i)
			generics.push_back(fresh(_->generics[i], nongen, genremap));

		return new TypeInstance(_->prototype, generics);
	}

	return t;
}

Type* fresh(Type* t, const std::vector<Type*>& nongen)
{
	std::map<TypeGeneric*, TypeGeneric*> genremap;

	return fresh(t, nongen, genremap);
}

bool unify(Type* lhs, Type* rhs)
{
	if (lhs == rhs) return true;

	lhs = prune(lhs);
	rhs = prune(rhs);

	if (lhs == rhs) return true;

	if (CASE(TypeGeneric, lhs))
	{
		if (occurs(lhs, rhs))
			return false;

		_->instance = rhs;

		return true;
	}

	if (CASE(TypeGeneric, rhs))
	{
		return unify(rhs, lhs);
	}

	if (CASE(TypeUnit, lhs))
	{
		return dynamic_cast<TypeUnit*>(rhs) != 0;
	}

	if (CASE(TypeInt, lhs))
	{
		return dynamic_cast<TypeInt*>(rhs) != 0;
	}

	if (CASE(TypeFloat, lhs))
	{
		return dynamic_cast<TypeFloat*>(rhs) != 0;
	}

	if (CASE(TypeBool, lhs))
	{
		return dynamic_cast<TypeBool*>(rhs) != 0;
	}

	if (CASE(TypeArray, lhs))
	{
		TypeArray* r = dynamic_cast<TypeArray*>(rhs);
		if (!r) return false;

		return unify(_->contained, r->contained);
	}

	if (CASE(TypeFunction, lhs))
	{
		TypeFunction* r = dynamic_cast<TypeFunction*>(rhs);
		if (!r) return false;

		if (_->args.size() != r->args.size()) return false;

		if (!unify(_->result, r->result)) return false;

		for (size_t i = 0; i < _->args.size(); ++i)
			if (!unify(_->args[i], r->args[i]))
				return false;

		return true;
	}

	if (CASE(TypeInstance, lhs))
	{
		TypeInstance* r = dynamic_cast<TypeInstance*>(rhs);
		if (!r) return false;

		if (_->prototype != r->prototype) return false;

		if (_->generics.size() != r->generics.size()) return false;

		for (size_t i = 0; i < _->generics.size(); ++i)
			if (!unify(_->generics[i], r->generics[i]))
				return false;

		return true;
	}

	return false;
}

void mustUnify(Type* actual, Type* expected, const Location& location)
{
	if (!unify(actual, expected))
	{
		PrettyPrintContext context;
		std::string expectedType = typeName(expected, context);
		std::string actualType = typeName(actual, context);

		errorf(location, "Type mismatch. Expecting a\n    %s\nbut given a\n    %s", expectedType.c_str(), actualType.c_str());
	}
}

Type* analyze(BindingBase* binding, const std::vector<Type*>& nongen)
{
	if (CASE(BindingFunction, binding))
	{
		return fresh(_->target->type, nongen);
	}

	if (CASE(BindingLocal, binding))
	{
		return _->target->type;
	}

	assert(!"Unknown binding type");
	return 0;
}

Type* analyze(MatchCase* case_)
{
	if (CASE(MatchCaseAny, case_))
	{
		return _->type;
	}

	if (CASE(MatchCaseNumber, case_))
	{
		return _->type;
	}

	if (CASE(MatchCaseArray, case_))
	{
		if (!_->elements.empty())
		{
			Type* t0 = analyze(_->elements[0]);

			for (size_t i = 1; i < _->elements.size(); ++i)
			{
				Type* ti = analyze(_->elements[i]);

				mustUnify(ti, t0, _->elements[i]->location);
			}

			mustUnify(_->type, new TypeArray(t0), _->location);
		}
		else
		{
			mustUnify(_->type, new TypeArray(new TypeGeneric()), _->location);
		}

		return _->type;
	}

	if (CASE(MatchCaseMembers, case_))
	{
		if (TypeInstance* inst_type = dynamic_cast<TypeInstance*>(_->type))
		if (TypePrototypeRecord* record_type = dynamic_cast<TypePrototypeRecord*>(inst_type->prototype))
		{
			// Resolve named arguments into unnamed arguments
			if (!_->member_names.empty())
			{
				std::vector<MatchCase*> clone_members;

				clone_members.insert(clone_members.begin(), record_type->member_types.size(), 0);

				for (size_t i = 0; i < _->member_values.size(); ++i)
				{
					size_t member_index = getMemberIndexByName(record_type, _->member_names[i], _->location);
					if (clone_members[member_index])
						errorf(_->location, "Member '%s' match is already specified", record_type->member_names[member_index]);

					clone_members[member_index] = _->member_values[i];
				}

				for (size_t i = 0; i < record_type->member_types.size(); ++i)
				{
					if (!clone_members[i])
						clone_members[i] = new MatchCaseAny(0, Location(), 0);
				}

				_->member_values = clone_members;
				_->member_names.clear();
			}

			if (_->member_values.size() != record_type->member_types.size())
				errorf(_->location, "Type has %d members, but %d are specified", record_type->member_types.size(), _->member_values.size());

			for (size_t i = 0; i < _->member_values.size(); ++i)
			{
				analyze(_->member_values[i]);
				_->member_values[i]->type = getMemberTypeByIndex(inst_type, record_type, i, _->location);
			}
		}

		return _->type;
	}

	if (CASE(MatchCaseUnion, case_))
	{
		TypeInstance* inst_type = dynamic_cast<TypeInstance*>(_->type);
		TypePrototypeUnion* union_type = dynamic_cast<TypePrototypeUnion*>(inst_type->prototype);

		mustUnify(_->pattern->type, getMemberTypeByIndex(inst_type, union_type, _->tag, _->location), _->location);

		analyze(_->pattern);

		return inst_type;
	}

	assert(!"Unknown match case type");
	return 0;
}

Type* analyze(Expr* root, std::vector<Type*>& nongen)
{
	if (CASE(ExprUnit, root))
	{
		return _->type;
	}

	if (CASE(ExprNumberLiteral, root))
	{
		return _->type;
	}

	if (CASE(ExprBooleanLiteral, root))
	{
		return _->type;
	}

	if (CASE(ExprArrayLiteral, root))
	{
		if (!_->elements.empty())
		{
			Type* t0 = analyze(_->elements[0], nongen);

			for (size_t i = 1; i < _->elements.size(); ++i)
			{
				Type* ti = analyze(_->elements[i], nongen);

				mustUnify(ti, t0, _->elements[i]->location);
			}

			mustUnify(_->type, new TypeArray(t0), _->location);
		}
		else
		{
			mustUnify(_->type, new TypeArray(new TypeGeneric()), _->location);
		}

		return _->type;
	}

	if (CASE(ExprBinding, root))
	{
		return _->type = analyze(_->binding, nongen);
	}

	if (CASE(ExprBindingExternal, root))
	{
		return _->type;
	}

	if (CASE(ExprUnaryOp, root))
	{
		Type* te = analyze(_->expr, nongen);

		switch (_->op)
		{
		case SynUnaryOpPlus:
		case SynUnaryOpMinus:
			mustUnify(te, new TypeInt(), _->expr->location);
			return _->type = new TypeInt();
			
		case SynUnaryOpNot:
			mustUnify(te, new TypeBool(), _->expr->location);
			return _->type = new TypeBool();

		default: assert(!"Unknown unary op");
		}
	}

	if (CASE(ExprBinaryOp, root))
	{
		Type* tl = analyze(_->left, nongen);
		Type* tr = analyze(_->right, nongen);

		switch (_->op)
		{
		case SynBinaryOpAdd:
		case SynBinaryOpSubtract:
		case SynBinaryOpMultiply:
		case SynBinaryOpDivide:
			mustUnify(tl, new TypeInt(), _->left->location);
			mustUnify(tr, new TypeInt(), _->right->location);
			return _->type = new TypeInt();

		case SynBinaryOpLess:
		case SynBinaryOpLessEqual:
		case SynBinaryOpGreater:
		case SynBinaryOpGreaterEqual:
		case SynBinaryOpEqual:
		case SynBinaryOpNotEqual:
			mustUnify(tl, new TypeInt(), _->left->location);
			mustUnify(tr, new TypeInt(), _->right->location);
			return _->type = new TypeBool();

		default: assert(!"Unknown binary op");
		}
	}

	if (CASE(ExprCall, root))
	{
		Type* te = analyze(_->expr, nongen);

		std::vector<Type*> argtys;
		for (size_t i = 0; i < _->args.size(); ++i)
			argtys.push_back(analyze(_->args[i], nongen));

		TypeFunction* funty = new TypeFunction(new TypeGeneric(), argtys);

		mustUnify(te, funty, _->expr->location);

		return _->type = funty->result;
	}

	if (CASE(ExprArrayIndex, root))
	{
		Type* ta = analyze(_->arr, nongen);
		Type* ti = analyze(_->index, nongen);

		TypeArray* tn = new TypeArray(new TypeGeneric());

		mustUnify(ta, tn, _->arr->location);
		mustUnify(ti, new TypeInt(), _->index->location);

		return _->type = tn->contained;
	}

	if (CASE(ExprArraySlice, root))
	{
		Type* ta = analyze(_->arr, nongen);
		Type* ts = analyze(_->index_start, nongen);
		Type* te = _->index_end ? analyze(_->index_end, nongen) : 0;

		TypeArray* tn = new TypeArray(new TypeGeneric());

		mustUnify(ta, tn, _->arr->location);
		mustUnify(ts, new TypeInt(), _->index_start->location);

		if (te)
			mustUnify(te, new TypeInt(), _->index_end->location);

		return _->type = ta;
	}

	if (CASE(ExprMemberAccess, root))
	{
		Type* ta = finalType(analyze(_->aggr, nongen));

		if (TypeInstance* inst_type = dynamic_cast<TypeInstance*>(ta))
		if (TypePrototypeRecord* record_type = dynamic_cast<TypePrototypeRecord*>(inst_type->prototype))
		{
			size_t index = getMemberIndexByName(record_type, _->member_name, _->location);

			Type* tm = getMemberTypeByIndex(inst_type, record_type, index, _->location);

			return _->type = tm;
		}

		errorf(_->aggr->location, "Expected a record type");
	}

	if (CASE(ExprLetVar, root))
	{
		Type* tb = analyze(_->body, nongen);

		mustUnify(tb, _->target->type, _->body->location);

		return _->type;
	}

	if (CASE(ExprLetFunc, root))
	{
		for (size_t i = 0; i < _->args.size(); ++i)
			nongen.push_back(_->args[i]->type);

		Type* tb = analyze(_->body, nongen);

		for (size_t i = 0; i < _->args.size(); ++i)
			nongen.pop_back();

		TypeFunction* funty = dynamic_cast<TypeFunction*>(_->type);

		mustUnify(tb, funty->result, _->body->location);

		return _->type;
	}

	if (CASE(ExprExternFunc, root))
	{
		return _->type;
	}

	if (CASE(ExprStructConstructorFunc, root))
	{
		return _->type;
	}

	if (CASE(ExprUnionConstructorFunc, root))
	{
		return _->type;
	}

	if (CASE(ExprLLVM, root))
	{
		return _->type;
	}

	if (CASE(ExprIfThenElse, root))
	{
		Type* tcond = analyze(_->cond, nongen);
		Type* tthen = analyze(_->thenbody, nongen);
		Type* telse = analyze(_->elsebody, nongen);

		mustUnify(tcond, new TypeBool(), _->cond->location);

		// this if/else is really only needed for nicer error messages
		if (dynamic_cast<ExprUnit*>(_->elsebody))
			mustUnify(tthen, new TypeUnit(), _->thenbody->location);
		else
			mustUnify(telse, tthen, _->elsebody->location);

		return _->type = tthen;
	}

	if (CASE(ExprForInDo, root))
	{
		Type* tarr = analyze(_->arr, nongen);
		Type* tbody = analyze(_->body, nongen);

		TypeArray* ta = new TypeArray(_->target->type);

		mustUnify(tarr, ta, _->arr->location);
		mustUnify(tbody, new TypeUnit(), _->body->location);

		return _->type = new TypeUnit();
	}

	if (CASE(ExprMatchWith, root))
	{
		Type* tvar = analyze(_->variable, nongen);
		Type* t0 = 0;

		for (size_t i = 0; i < _->cases.size(); ++i)
		{
			Type* tci = analyze(_->cases[i]);

			mustUnify(tci, tvar, _->cases[i]->location);

			Type* ti = analyze(_->expressions[i], nongen);

			if (i == 0)
				t0 = ti;
			else
				mustUnify(ti, t0, _->expressions[i]->location);
		}

		return _->type = t0;
	}

	if (CASE(ExprBlock, root))
	{
		if (_->expressions.empty())
			return new TypeUnit();

		for (size_t i = 0; i + 1 < _->expressions.size(); ++i)
		{
			Type* te = analyze(_->expressions[i], nongen);

			if (dynamic_cast<ExprLetVar*>(_->expressions[i]) == 0 && dynamic_cast<ExprLetFunc*>(_->expressions[i]) == 0 && dynamic_cast<ExprExternFunc*>(_->expressions[i]) == 0 && dynamic_cast<ExprStructConstructorFunc*>(_->expressions[i]) == 0 && dynamic_cast<ExprUnionConstructorFunc*>(_->expressions[i]) == 0)
			{
				mustUnify(te, new TypeUnit(), _->expressions[i]->location);
			}
		}

		return _->type = analyze(_->expressions.back(), nongen);
	}

	assert(!"Unknown expression type");
	return 0;
}

Expr* typecheck(SynBase* root)
{
	Expr* result = resolve(root);

	std::vector<Type*> nongen;
	analyze(result, nongen);

	assert(nongen.empty());

	return result;
}
