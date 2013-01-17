#include "typecheck.hpp"

#include "output.hpp"

#include <cassert>
#include <algorithm>

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

TypeInstance* instantiatePrototype(TypePrototype** proto, size_t generic_count)
{
	std::vector<Type*> args;

	for (size_t i = 0; i < generic_count; ++i)
		args.push_back(new TypeGeneric());

	return new TypeInstance(proto, args);
}

Type* tryResolveType(const std::string& name, Environment& env)
{
	for (size_t i = 0; i < env.types.size(); ++i)
	{
		if (env.types[i].name == name)
		{
			if (CASE(TypeInstance, env.types[i].type))
			{
				if (_->generics.size() > 0)
				{
					return instantiatePrototype(_->prototype, _->generics.size());
				}
			}

			return env.types[i].type;
		}
	}

	return 0;
}

Type* resolveType(const std::string& name, Environment& env, const Location& location)
{
	if (Type* type = tryResolveType(name, env))
		return type;

	errorf(location, "Unknown type %s", name.c_str());
}

TypeGeneric* resolveNewGenericType(SynTypeGeneric* type, Environment& env, bool frozen = false)
{
	for (size_t i = 0; i < env.generic_types.size(); ++i)
		if (env.generic_types[i]->name == type->type.name)
			errorf(type->type.location, "Generic type '%s already exists", type->type.name.c_str());

	env.generic_types.push_back(new TypeGeneric(type->type.name, frozen));

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

	if (CASE(SynTypeTuple, type))
	{
		std::vector<Type*> members;

		for (size_t i = 0; i < _->members.size(); ++i)
			members.push_back(resolveType(_->members[i], env, allow_new_generics));

		return new TypeTuple(members);
	}

	assert(!"Unknown syntax tree type");
	return 0;
}

std::vector<Type*> resolveGenericTypeList(const std::vector<SynTypeGeneric*>& generics, Environment& env)
{
	std::vector<Type*> result;

	for (size_t i = 0; i < generics.size(); ++i)
		result.push_back(resolveNewGenericType(generics[i], env, true));

	return result;
}

TypePrototypeRecord* resolveRecordType(const SynIdentifier& name, SynTypeRecord* type, const std::vector<Type*>& generics, Environment& env)
{
	std::vector<Type*> member_types;
	std::vector<std::string> member_names;

	for (size_t i = 0; i < type->members.size(); ++i)
	{
		member_types.push_back(resolveType(type->members[i].type, env));
		member_names.push_back(type->members[i].name.name);
	}

	return new TypePrototypeRecord(name.name, member_types, member_names, generics);
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
			if (TypePrototypeUnion* tu = dynamic_cast<TypePrototypeUnion*>(*ti->prototype))
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
						return new ExprBindingExternal(_->target->type, location, env.functions.back().context, name, i, binding);
				}

				env.functions.back().externals.push_back(binding);
				return new ExprBindingExternal(_->target->type, location, env.functions.back().context, name, env.functions.back().externals.size() - 1, binding);
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

Expr* resolveExpr(SynBase* node, Environment& env);

MatchCase* resolveMatch(SynMatch* match, Environment& env)
{
	if (CASE(SynMatchNumber, match))
	{
		return new MatchCaseNumber(new TypeInt(), _->location, _->value);
	}

	if (CASE(SynMatchBoolean, match))
	{
		return new MatchCaseBoolean(new TypeBool(), _->location, _->value);
	}

	if (CASE(SynMatchArray, match))
	{
		std::vector<MatchCase*> elements;

		for (size_t i = 0; i < _->elements.size(); ++i)
			elements.push_back(resolveMatch(_->elements[i], env));

		return new MatchCaseArray(new TypeGeneric(), _->location, elements);
	}

	if (CASE(SynMatchTuple, match))
	{
		std::vector<MatchCase*> elements;
		std::vector<Type*> types;

		for (size_t i = 0; i < _->elements.size(); ++i)
		{
			elements.push_back(resolveMatch(_->elements[i], env));
			types.push_back(elements.back()->type);
		}

		return new MatchCaseMembers(new TypeTuple(types), _->location, elements, std::vector<std::string>());
	}

	if (CASE(SynMatchTypeSimple, match))
	{
		std::pair<TypePrototypeUnion*, size_t> union_tag = resolveUnionTypeByVariant(_->type.name, env);

		// Maybe it's a tag from a union
		if (union_tag.first)
		{
			TypeInstance* inst = instantiatePrototype(new TypePrototype*(union_tag.first), union_tag.first->generics.size());

			Type* member_type = getMemberTypeByIndex(inst, union_tag.first, union_tag.second, _->location);

			BindingTarget* target = new BindingTarget(_->alias.name, member_type);
		
			env.bindings.back().push_back(Binding(_->alias.name, new BindingLocal(target)));

			// First match the tag, then match the contents
			return new MatchCaseUnion(inst, _->location, union_tag.second, new MatchCaseAny(member_type, _->location, target));
		}

		Type* type = tryResolveType(_->type.name, env);

		if (!type)
			errorf(_->location, "Unknown type or union tag '%s'", _->type.name.c_str());

		BindingTarget* target = new BindingTarget(_->alias.name, type);
		
		env.bindings.back().push_back(Binding(_->alias.name, new BindingLocal(target)));

		return new MatchCaseAny(type, _->location, target);
	}

	if (CASE(SynMatchTypeComplex, match))
	{
		std::vector<std::string> member_names;
		std::vector<MatchCase*> member_values;

		for (size_t i = 0; i < _->arg_values.size(); ++i)
		{
			if (!_->arg_names.empty())
				member_names.push_back(_->arg_names[i].name);
			member_values.push_back(resolveMatch(_->arg_values[i], env));
		}

		std::pair<TypePrototypeUnion*, size_t> union_tag = resolveUnionTypeByVariant(_->type.name, env);

		// Maybe it's a tag from a union
		if (union_tag.first)
		{
			// First match the tag, then match the contents
			TypeInstance* inst = instantiatePrototype(new TypePrototype*(union_tag.first), union_tag.first->generics.size());

			return new MatchCaseUnion(inst, _->location, union_tag.second, new MatchCaseMembers(new TypeGeneric(), _->location, member_values, member_names));
		}

		Type* type = tryResolveType(_->type.name, env);

		if (!type)
			errorf(_->location, "Unknown type or union tag '%s'", _->type.name.c_str());

		return new MatchCaseMembers(type, _->location, member_values, member_names);
	}

	if (CASE(SynMatchPlaceholder, match))
	{
		// Special case for an unit union member
		std::pair<TypePrototypeUnion*, size_t> union_tag = resolveUnionTypeByVariant(_->alias.name.name, env);

		if (union_tag.first)
		{
			TypeInstance* inst = instantiatePrototype(new TypePrototype*(union_tag.first), union_tag.first->generics.size());

			return new MatchCaseUnion(inst, _->location, union_tag.second, new MatchCaseAny(new TypeGeneric(), _->location, 0));
		}

		// Find a binding with the same name in current scope
		BindingBase* previous = 0;
		for (size_t i = 0; i < env.bindings.back().size() && !previous; ++i)
		{
			if (env.bindings.back()[i].name == _->alias.name.name)
				previous = env.bindings.back()[i].binding;
		}

		if (!previous)
		{
			BindingTarget* target = new BindingTarget(_->alias.name.name, resolveType(_->alias.type, env, true));
		
			env.bindings.back().push_back(Binding(_->alias.name.name, new BindingLocal(target)));

			return new MatchCaseAny(target->type, _->location, target);
		}
		else
		{
			return new MatchCaseValue(resolveType(_->alias.type, env, true), _->location, previous);
		}
	}

	if (CASE(SynMatchPlaceholderUnnamed, match))
	{
		return new MatchCaseAny(new TypeGeneric(), _->location, 0);
	}

	if (CASE(SynMatchOr, match))
	{
		std::vector<MatchCase*> options;

		std::vector<std::vector<BindingTarget*>> all_bindings;

		for (size_t i = 0; i < _->options.size(); ++i)
		{
			env.bindings.push_back(std::vector<Binding>());

			options.push_back(resolveMatch(_->options[i], env));

			// Take all the bindings 
			all_bindings.push_back(std::vector<BindingTarget*>());
			for (size_t k = 0; k < env.bindings.back().size(); ++k)
				all_bindings.back().push_back(dynamic_cast<BindingLocal*>(env.bindings.back()[k].binding)->target);

			std::sort(all_bindings.back().begin(), all_bindings.back().end(), [](BindingTarget *left, BindingTarget *right){ return left->name < right->name; });

			// Check that the exact same patterns are used in the following alternatives
			if (i != 0)
			{
				if (all_bindings.back().size() != all_bindings[0].size())
					errorf(_->options[i]->location, "Different patterns must use the same placeholders");

				for (size_t k = 0; k < all_bindings[0].size(); ++k)
				{
					if (all_bindings.back()[k]->name != all_bindings[0][k]->name)
						errorf(_->options[i]->location, "Different patterns must use the same placeholders");
				}
			}

			env.bindings.pop_back();
		}

		std::vector<BindingTarget*> actual_bindings;

		// Create new bindings for all used placeholders
		for (size_t i = 0; i < all_bindings[0].size(); ++i)
		{
			BindingTarget* target = new BindingTarget(all_bindings[0][i]->name, new TypeGeneric());
		
			actual_bindings.push_back(target);
			env.bindings.back().push_back(Binding(all_bindings[0][i]->name, new BindingLocal(target)));
		}

		std::sort(actual_bindings.begin(), actual_bindings.end(), [](BindingTarget *left, BindingTarget *right){ return left->name < right->name; });

		return new MatchCaseOr(new TypeGeneric(), _->location, options, all_bindings, actual_bindings);
	}

	if (CASE(SynMatchIf, match))
	{
		MatchCase* match = resolveMatch(_->match, env);

		return new MatchCaseIf(new TypeGeneric(), _->location, match, resolveExpr(_->condition, env));
	}

	assert(!"Unrecognized AST SynMatch type");
	return 0;
}

TypeInstance* resolveTypeDeclaration(const std::string& name, const std::vector<SynTypeGeneric*>& generics, Environment& env)
{
	size_t generic_type_count = env.generic_types.size();

	std::vector<Type*> generic_types = resolveGenericTypeList(generics, env);

	env.generic_types.resize(generic_type_count);

	TypeInstance* inst_type = new TypeInstance(new TypePrototype*(0), generic_types);

	env.types.push_back(TypeBinding(name, inst_type));

	return inst_type;
}

TypeInstance* resolveTypeDeclarationRec(const std::string& name, const std::vector<SynTypeGeneric*>& generics, Environment& env)
{
	for (size_t i = 0; i < env.types.size(); ++i)
		if (env.types[i].name == name)
			return dynamic_cast<TypeInstance*>(env.types[i].type);

	return resolveTypeDeclaration(name, generics, env);
}

BindingFunction* resolveFunctionDeclaration(SynLetFunc* node, Environment& env)
{
	size_t generic_type_count = env.generic_types.size();

	TypeFunction* funty = resolveFunctionType(node->ret_type, node->args, env, /* allow_new_generics= */ true);

	std::vector<std::string> arg_names;

	for (size_t i = 0; i < node->args.size(); ++i)
		arg_names.push_back(node->args[i].name.name);

	BindingTarget* target = new BindingTarget(node->var.name, funty);

	TypeClosureContext* context_type = new TypeClosureContext();
	BindingTarget* context_target = new BindingTarget("extern", context_type);

	BindingFunction* binding = new BindingFunction(target, arg_names, context_target);

	env.bindings.back().push_back(Binding(node->var.name, binding));

	env.generic_types.resize(generic_type_count);

	return binding;
}

BindingFunction* resolveFunctionDeclarationRec(SynLetFunc* node, Environment& env)
{
	for (size_t i = 0; i < env.bindings.back().size(); ++i)
		if (env.bindings.back()[i].name == node->var.name)
			return dynamic_cast<BindingFunction*>(env.bindings.back()[i].binding);

	return resolveFunctionDeclaration(node, env);
}

size_t resolveRecursiveDeclarations(const std::vector<SynBase*>& expressions, size_t offset, Environment& env)
{
	if (dynamic_cast<SynRecordDefinition*>(expressions[offset]) || dynamic_cast<SynUnionDefinition*>(expressions[offset]))
	{
		size_t count = 0;

		for (; offset + count < expressions.size(); ++count)
		{
			if (SynRecordDefinition* type_definition = dynamic_cast<SynRecordDefinition*>(expressions[offset + count]))
				resolveTypeDeclaration(type_definition->name.name, type_definition->generics, env);
			else if (SynUnionDefinition *type_definition = dynamic_cast<SynUnionDefinition*>(expressions[offset + count]))
				resolveTypeDeclaration(type_definition->name.name, type_definition->generics, env);
			else
				break;
		}

		return count;
	}

	if (dynamic_cast<SynLetFunc*>(expressions[offset]))
	{
		size_t count = 0;

		for (; offset + count < expressions.size(); ++count)
		{
			if (SynLetFunc* func_definition = dynamic_cast<SynLetFunc*>(expressions[offset + count]))
				resolveFunctionDeclaration(func_definition, env);
			else
				break;
		}

		return count;
	}

	return 1;
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

	if (CASE(SynTupleLiteral, node))
	{
		std::vector<Expr*> elements;
		std::vector<Type*> types;

		for (size_t i = 0; i < _->elements.size(); ++i)
		{
			elements.push_back(resolveExpr(_->elements[i], env));
			types.push_back(elements.back()->type);
		}

		return new ExprTupleLiteral(new TypeTuple(types), _->location, elements);
	}

	if (CASE(SynRecordDefinition, node))
	{
		TypeInstance* inst_type = resolveTypeDeclarationRec(_->name.name, _->generics, env);

		size_t generic_type_count = env.generic_types.size();

		const std::vector<Type*>& generic_types = inst_type->generics;

		for (size_t i = 0; i < generic_types.size(); ++i)
			env.generic_types.push_back(dynamic_cast<TypeGeneric*>(generic_types[i]));

		TypePrototypeRecord* record_type = resolveRecordType(_->name, _->type, generic_types, env);

		*inst_type->prototype = record_type;

		std::vector<BindingTarget*> args;

		for (size_t i = 0; i < record_type->member_types.size(); ++i)
			args.push_back(new BindingTarget(record_type->member_names[i], record_type->member_types[i]));

		TypeFunction* function_type = new TypeFunction(inst_type, record_type->member_types);

		BindingTarget* target = new BindingTarget(_->name.name, function_type);

		env.bindings.back().push_back(Binding(_->name.name, new BindingFreeFunction(target, record_type->member_names)));

		env.generic_types.resize(generic_type_count);

		return new ExprStructConstructorFunc(function_type, _->location, target, args);
	}

	if (CASE(SynUnionDefinition, node))
	{
		TypeInstance* inst_type = resolveTypeDeclarationRec(_->name.name, _->generics, env);

		size_t generic_type_count = env.generic_types.size();

		const std::vector<Type*>& generic_types = inst_type->generics;

		for (size_t i = 0; i < generic_types.size(); ++i)
			env.generic_types.push_back(dynamic_cast<TypeGeneric*>(generic_types[i]));

		TypePrototypeUnion* union_type = new TypePrototypeUnion(_->name.name, std::vector<Type*>(), std::vector<std::string>(), generic_types);

		*inst_type->prototype = union_type;

		ExprBlock *expression = new ExprBlock(new TypeUnit(), _->location);

		env.types.push_back(TypeBinding(_->name.name, inst_type));

		for (size_t i = 0; i < _->members.size(); i++)
		{
			std::vector<Type*> member_types;
			std::vector<std::string> member_names;
			std::vector<BindingTarget*> args;

			Type* element_type = 0;

			if (SynTypeRecord* type_record = dynamic_cast<SynTypeRecord*>(_->members[i].type))
			{
				TypePrototypeRecord* record_type = resolveRecordType(_->members[i].name, type_record, generic_types, env);
				TypeInstance* inst_type = new TypeInstance(new TypePrototype*(record_type), generic_types);

				member_types = record_type->member_types;
				member_names = record_type->member_names;

				element_type = inst_type;
			}
			else if (SynTypeTuple* type_tuple = dynamic_cast<SynTypeTuple*>(_->members[i].type))
			{
				TypeTuple* type = dynamic_cast<TypeTuple*>(resolveType(type_tuple, env));

				member_types = type->members;
				member_names.insert(member_names.begin(), member_types.size(), "value");

				element_type = type;
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

	if (CASE(SynLetVars, node))
	{
		Expr* body = resolveExpr(_->body, env);

		std::vector<BindingTarget*> targets;

		for (size_t i = 0; i < _->vars.size(); ++i)
		{
			if (_->vars[i].name.name == "_")
			{
				targets.push_back(0);
			}
			else
			{
				BindingTarget* target = new BindingTarget(_->vars[i].name.name, resolveType(_->vars[i].type, env));

				env.bindings.back().push_back(Binding(_->vars[i].name.name, new BindingLocal(target)));

				targets.push_back(target);
			}
		}

		return new ExprLetVars(new TypeUnit(), _->location, targets, body);
	}

	if (CASE(SynLLVM, node))
		return new ExprLLVM(new TypeGeneric(), _->location, _->body);

	if (CASE(SynLetFunc, node))
	{
		BindingFunction* binding = resolveFunctionDeclarationRec(_, env);

		std::vector<BindingTarget*> args;

		env.functions.push_back(FunctionInfo(env.bindings.size()));
		env.bindings.push_back(std::vector<Binding>());

		size_t generic_type_count = env.generic_types.size();

		TypeFunction* funty = resolveFunctionType(_->ret_type, _->args, env, /* allow_new_generics= */ true);

		// hack :( needed since resolveFunctionType introduced copies of generic args
		binding->target->type = funty;

		for (size_t i = 0; i < _->args.size(); ++i)
		{
			BindingTarget* target = new BindingTarget(_->args[i].name.name, funty->args[i]);

			args.push_back(target);
			env.bindings.back().push_back(Binding(_->args[i].name.name, new BindingLocal(target)));
		}

		// Add info about function context. Context type will be resolved later
		TypeClosureContext* context_type = dynamic_cast<TypeClosureContext*>(binding->context_target->type);

		env.functions.back().context = new BindingLocal(binding->context_target);

		Expr* body = resolveExpr(_->body, env);

		bool has_externals = !env.functions.back().externals.empty();

		// Resolve function context type
		for (size_t i = 0; i < env.functions.back().externals.size(); ++i)
		{
			if (CASE(BindingFunction, env.functions.back().externals[i]))
			{
				context_type->member_types.push_back(_->context_target->type);
				context_type->member_names.push_back(_->target->name + ".context");
			}
			else if (CASE(BindingLocal, env.functions.back().externals[i]))
			{
				context_type->member_types.push_back(_->target->type);
				context_type->member_names.push_back(_->target->name);
			}
			else
				; // ???
		}

		std::vector<BindingBase*> function_externals = env.functions.back().externals;

		env.functions.pop_back();
		env.bindings.pop_back();

		// Resolve function external variable capture
		std::vector<Expr*> externals;

		for (size_t i = 0; i < function_externals.size(); ++i)
		{
			if (CASE(BindingLocal, function_externals[i]))
				externals.push_back(resolveBindingAccess(_->target->name, Location(), env));
			else
				; // ???
		}

		env.generic_types.resize(generic_type_count);

		return new ExprLetFunc(funty, _->location, binding->target, has_externals ? binding->context_target : 0, args, body, externals);
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

	if (CASE(SynForInRangeDo, node))
	{
		Expr* start = resolveExpr(_->start, env);
		Expr* end = resolveExpr(_->end, env);

		BindingTarget* target = new BindingTarget(_->var.name.name, resolveType(_->var.type, env));

		env.bindings.back().push_back(Binding(_->var.name.name, new BindingLocal(target)));

		Expr* body = resolveExpr(_->body, env);

		env.bindings.back().pop_back();

		return new ExprForInRangeDo(new TypeUnit(), _->location, target, start, end, body);
	}

	if (CASE(SynMatchWith, node))
	{
		Expr* variable = resolveExpr(_->variable, env);

		std::vector<MatchCase*> cases;
		std::vector<Expr*> expressions;

		for (size_t i = 0; i < _->variants.size(); i++)
		{
			// Pattern can create new bindings to be used in the expression
			env.bindings.push_back(std::vector<Binding>());

			cases.push_back(resolveMatch(_->variants[i], env));

			expressions.push_back(resolveExpr(_->expressions[i], env));

			env.bindings.pop_back();
		}

		return new ExprMatchWith(new TypeGeneric(), _->location, variable, cases, expressions);
	}

	if (CASE(SynBlock, node))
	{
		ExprBlock *expression = new ExprBlock(new TypeUnit(), _->location);
		
		size_t type_count = env.types.size();

		env.bindings.push_back(std::vector<Binding>());

		for (size_t i = 0; i < _->expressions.size(); )
		{
			size_t decls = resolveRecursiveDeclarations(_->expressions, i, env);
			assert(decls > 0);

			for (size_t j = 0; j < decls; ++j)
				expression->expressions.push_back(resolveExpr(_->expressions[i + j], env));

			i += decls;
		}

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

	if (CASE(TypeTuple, rhs))
	{
		for (size_t i = 0; i < _->members.size(); ++i)
			if (occurs(lhs, _->members[i]))
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

		return genremap[_] = new TypeGeneric();
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

	if (CASE(TypeTuple, t))
	{
		std::vector<Type*> members;
		for (size_t i = 0; i < _->members.size(); ++i)
			members.push_back(fresh(_->members[i], nongen, genremap));

		return new TypeTuple(members);
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

		assert(!_->frozen);
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

		if (*_->prototype != *r->prototype) return false;

		if (_->generics.size() != r->generics.size()) return false;

		for (size_t i = 0; i < _->generics.size(); ++i)
			if (!unify(_->generics[i], r->generics[i]))
				return false;

		return true;
	}

	if (CASE(TypeTuple, lhs))
	{
		TypeTuple* r = dynamic_cast<TypeTuple*>(rhs);
		if (!r) return false;

		if (_->members.size() != r->members.size()) return false;

		for (size_t i = 0; i < _->members.size(); ++i)
			if (!unify(_->members[i], r->members[i]))
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

Type* analyze(Expr* root, std::vector<Type*>& nongen);

Type* analyze(MatchCase* case_, std::vector<Type*>& nongen)
{
	if (CASE(MatchCaseAny, case_))
	{
		return _->type;
	}

	if (CASE(MatchCaseBoolean, case_))
	{
		return _->type;
	}

	if (CASE(MatchCaseNumber, case_))
	{
		return _->type;
	}

	if (CASE(MatchCaseValue, case_))
	{
		mustUnify(_->type, analyze(_->value, nongen), _->location);

		return _->type;
	}

	if (CASE(MatchCaseArray, case_))
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

	if (CASE(MatchCaseMembers, case_))
	{
		if (TypeInstance* inst_type = dynamic_cast<TypeInstance*>(finalType(_->type)))
		{
			if (TypePrototypeRecord* record_type = dynamic_cast<TypePrototypeRecord*>(*inst_type->prototype))
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
							clone_members[i] = new MatchCaseAny(new TypeGeneric(), Location(), 0);
					}

					_->member_values = clone_members;
					_->member_names.clear();
				}

				if (_->member_values.size() != record_type->member_types.size())
					errorf(_->location, "Type has %d members, but %d are specified", record_type->member_types.size(), _->member_values.size());

				for (size_t i = 0; i < _->member_values.size(); ++i)
				{
					Type* mtype = analyze(_->member_values[i], nongen);

					mustUnify(mtype, getMemberTypeByIndex(inst_type, record_type, i, _->location), _->member_values[i]->location);
				}
			}
		}
		else if(TypeTuple* tuple_type = dynamic_cast<TypeTuple*>(finalType(_->type)))
		{
			if (!_->member_names.empty())
				errorf(_->location, "Type has no named members");

			if (_->member_values.size() != tuple_type->members.size())
				errorf(_->location, "Type has %d member(s), but %d is (are) specified", tuple_type->members.size(), _->member_values.size());

			for (size_t i = 0; i < _->member_values.size(); ++i)
			{
				Type* mtype = analyze(_->member_values[i], nongen);

				mustUnify(mtype, tuple_type->members[i], _->member_values[i]->location);
			}
		}
		else
		{
			PrettyPrintContext context;
			std::string name = typeName(finalType(_->type), context);

			if (!_->member_names.empty() || _->member_values.size() > 1)
				errorf(_->location, "Type %s has no members", name.c_str());

			if (_->member_values.size() == 1)
			{
				Type* mtype = analyze(_->member_values[0], nongen);

				mustUnify(mtype, finalType(_->type), _->member_values[0]->location);
			}
		}

		return _->type;
	}

	if (CASE(MatchCaseUnion, case_))
	{
		TypeInstance* inst_type = dynamic_cast<TypeInstance*>(finalType(_->type));
		TypePrototypeUnion* union_type = dynamic_cast<TypePrototypeUnion*>(*inst_type->prototype);

		// Unify should be before analyze since analyze has to know the union type to resolve field names
		mustUnify(_->pattern->type, getMemberTypeByIndex(inst_type, union_type, _->tag, _->location), _->location);

		analyze(_->pattern, nongen);

		return _->type;
	}

	if (CASE(MatchCaseOr, case_))
	{
		for (size_t i = 0; i < _->options.size(); ++i)
			analyze(_->options[i], nongen);

		for (size_t i = 0; i < _->binding_actual.size(); ++i)
		{
			for (size_t k = 0; k < _->binding_alternatives.size(); ++k)
				mustUnify(_->binding_alternatives[k][i]->type, _->binding_actual[i]->type, _->location);
		}

		return _->type;
	}

	if (CASE(MatchCaseIf, case_))
	{
		Type* tmatch = analyze(_->match, nongen);
		Type* tcond = analyze(_->condition, nongen);

		mustUnify(tcond, new TypeBool(), _->condition->location);

		return _->type = tmatch;
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

	if (CASE(ExprTupleLiteral, root))
	{
		std::vector<Type*> types;

		for (size_t i = 0; i < _->elements.size(); ++i)
			types.push_back(analyze(_->elements[i], nongen));

		mustUnify(_->type, new TypeTuple(types), _->location);

		return _->type;
	}

	if (CASE(ExprBinding, root))
	{
		return _->type = analyze(_->binding, nongen);
	}

	if (CASE(ExprBindingExternal, root))
	{
		return _->type = analyze(_->binding, nongen);
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
			mustUnify(tl, new TypeInt(), _->left->location);
			mustUnify(tr, new TypeInt(), _->right->location);
			return _->type = new TypeBool();

		case SynBinaryOpEqual:
		case SynBinaryOpNotEqual:
			mustUnify(tr, tl, _->right->location);
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

		// this if/else is really only needed for nicer error messages
		if (TypeFunction* funty = dynamic_cast<TypeFunction*>(finalType(te)))
		{
			if (funty->args.size() != argtys.size())
				errorf(_->location, "Expected %d arguments but given %d", funty->args.size(), argtys.size());

			for (size_t i = 0; i < argtys.size(); ++i)
				mustUnify(argtys[i], funty->args[i], _->args[i]->location);

			return _->type = funty->result;
		}
		else
		{
			funty = new TypeFunction(new TypeGeneric(), argtys);

			mustUnify(te, funty, _->expr->location);

			return _->type = funty->result;
		}
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
		if (TypePrototypeRecord* record_type = dynamic_cast<TypePrototypeRecord*>(*inst_type->prototype))
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

	if (CASE(ExprLetVars, root))
	{
		Type* tb = analyze(_->body, nongen);

		std::vector<Type*> types;
		for (size_t i = 0; i < _->targets.size(); ++i)
			types.push_back(_->targets[i] ? _->targets[i]->type : new TypeGeneric());

		mustUnify(tb, new TypeTuple(types), _->body->location);

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

	if (CASE(ExprForInRangeDo, root))
	{
		Type* tbody = analyze(_->body, nongen);

		Type* tstart = analyze(_->start, nongen);
		Type* tend = analyze(_->end, nongen);

		mustUnify(_->target->type, new TypeInt(), _->location);
		mustUnify(tstart, new TypeInt(), _->start->location);
		mustUnify(tend, new TypeInt(), _->end->location);

		mustUnify(tbody, new TypeUnit(), _->body->location);

		return _->type = new TypeUnit();
	}

	if (CASE(ExprMatchWith, root))
	{
		Type* tvar = analyze(_->variable, nongen);
		Type* t0 = 0;

		for (size_t i = 0; i < _->cases.size(); ++i)
		{
			Type* tci = analyze(_->cases[i], nongen);

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

Type* typecheck(Expr* root)
{
	std::vector<Type*> nongen;
	Type* result = analyze(root, nongen);

	assert(nongen.empty());

	return result;
}
