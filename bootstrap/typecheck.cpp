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
	if (name == "")
		return new TypeGeneric();

	if (Type* type = tryResolveType(name, env))
		return type;

	errorf(location, "Unknown type %s", name.c_str());
}

Type* resolveType(SynType* type, Environment& env)
{
	if (!type)
	{
		return new TypeGeneric();
	}

	if (CASE(SynTypeBasic, type))
	{
		return resolveType(_->type.name, env, _->type.location);
	}

	if (CASE(SynTypeGeneric, type))
	{
		for (size_t i = 0; i < env.generic_types.size(); ++i)
			if (env.generic_types[i]->name == _->type.name)
				return env.generic_types[i];

		env.generic_types.push_back(new TypeGeneric(_->type.name));

		return env.generic_types.back();
	}

	if (CASE(SynTypeArray, type))
	{
		return new TypeArray(resolveType(_->contained_type, env));
	}

	if (CASE(SynTypeFunction, type))
	{
		std::vector<Type*> argtys;

		for (size_t i = 0; i < _->args.size(); ++i)
			argtys.push_back(resolveType(_->args[i], env));

		return new TypeFunction(resolveType(_->result, env), argtys);
	}

	if (CASE(SynTypeStructure, type))
	{
		std::vector<Type*> member_types;
		std::vector<std::string> member_names;

		for (size_t i = 0; i < _->members.size(); ++i)
		{
			member_types.push_back(resolveType(_->members[i].type, env));
			member_names.push_back(_->members[i].name.name);
		}

		return new TypeStructure(_->name.name, member_types, member_names);
	}

	assert(!"Unknown syntax tree type");
	return 0;
}

TypeFunction* resolveFunctionType(SynType* rettype, const std::vector<SynTypedVar>& args, Environment& env)
{
	std::vector<Type*> argtys;

	for (size_t i = 0; i < args.size(); ++i)
		argtys.push_back(resolveType(args[i].type, env));

	return new TypeFunction(resolveType(rettype, env), argtys);
}

Expr* resolveBindingAccess(const std::string& name, Location location, Environment& env)
{
	size_t scope;

	if (BindingBase* binding = tryResolveBinding(name, env, &scope))
	{
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
		std::vector<Type*> member_types;
		std::vector<std::string> member_names;

		std::vector<BindingTarget*> args;

		for (size_t i = 0; i < _->type_struct->members.size(); ++i)
		{
			member_types.push_back(resolveType(_->type_struct->members[i].type, env));
			member_names.push_back(_->type_struct->members[i].name.name);

			args.push_back(new BindingTarget(_->type_struct->members[i].name.name, member_types.back()));
		}

		Type* type = resolveType(_->type_struct, env);

		env.types.push_back(TypeBinding(_->type_struct->name.name, type));

		TypeFunction* function_type = new TypeFunction(type, member_types);

		BindingTarget* target = new BindingTarget(_->type_struct->name.name, function_type);

		env.bindings.back().push_back(Binding(_->type_struct->name.name, new BindingFreeFunction(target, member_names)));

		return new ExprStructConstructorFunc(function_type, _->location, target, args);
	}

	if (CASE(SynUnionDefinition, node))
	{
		ExprBlock *expression = new ExprBlock(new TypeUnit(), _->location);

		TypeUnion* target_type = new TypeUnion(_->name.name);

		env.types.push_back(TypeBinding(_->name.name, target_type));

		for (size_t i = 0; i < _->members.size(); i++)
		{
			Type* element_type = _->members[i].type ? resolveType(_->members[i].type, env) : new TypeUnit();

			target_type->member_names.push_back(_->members[i].name.name);
			target_type->member_types.push_back(element_type);

			std::vector<Type*> member_types;
			std::vector<std::string> member_names;
			std::vector<BindingTarget*> args;

			if (SynTypeStructure* type_struct = dynamic_cast<SynTypeStructure*>(_->members[i].type))
			{
				for (size_t k = 0; k < type_struct->members.size(); ++k)
				{
					member_types.push_back(resolveType(type_struct->members[k].type, env));
					member_names.push_back(type_struct->members[k].name.name);
					args.push_back(new BindingTarget(type_struct->members[k].name.name, member_types.back()));
				}
			}
			else if (_->members[i].type)
			{
				member_types.push_back(resolveType(_->members[i].type, env));
				args.push_back(new BindingTarget(_->members[i].name.name, member_types.back()));
			}

			TypeFunction* function_type = new TypeFunction(target_type, member_types);

			BindingTarget* target = new BindingTarget(_->members[i].name.name, function_type);

			env.bindings.back().push_back(Binding(_->members[i].name.name, new BindingFreeFunction(target, member_names)));

			expression->expressions.push_back(new ExprUnionConstructorFunc(function_type, _->location, target, args, i, element_type));
		}

		expression->expressions.push_back(new ExprUnit(new TypeUnit(), Location()));

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

		Type* result = 0;

		if (TypeStructure* struct_type = dynamic_cast<TypeStructure*>(aggr->type))
		{
			assert(struct_type->member_names.size() == struct_type->member_types.size());
			for (size_t i = 0; i < struct_type->member_names.size() && !result; i++)
			{
				if (struct_type->member_names[i] == _->member.name)
					result = struct_type->member_types[i];
			}
		}

		return new ExprMemberAccess(result ? result : new TypeGeneric(), _->member.location, aggr, _->member.name);
	}

	if (CASE(SynLetVar, node))
	{
		BindingTarget* target = new BindingTarget(_->var.name.name, resolveType(_->var.type, env));

		Expr* body = resolveExpr(_->body, env);

		// If the type is not defined, take the body type
		if (!_->var.type)
			target->type = body->type;

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

		TypeFunction* funty = resolveFunctionType(_->ret_type, _->args, env);

		for (size_t i = 0; i < _->args.size(); ++i)
		{
			BindingTarget* target = new BindingTarget(_->args[i].name.name, funty->args[i]);

			args.push_back(target);
			arg_names.push_back(_->args[i].name.name);
			env.bindings.back().push_back(Binding(_->args[i].name.name, new BindingLocal(target)));
		}

		BindingTarget* target = new BindingTarget(_->var.name, funty);

		// Add info about function context. Context type will be resolved later
		TypeStructure* context_type = new TypeStructure();
		BindingTarget* context_target = new BindingTarget("extern", new TypeReference(context_type));
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

		std::vector<std::string> variants;
		std::vector<BindingTarget*> aliases;
		std::vector<Expr*> expressions;

		TypeUnion* union_type = dynamic_cast<TypeUnion*>(variable->type);

		for (size_t i = 0; i < _->variants.size(); i++)
		{
			// Find the type of the variant
			Type* type = 0;
			for (size_t k = 0; union_type && k < union_type->member_names.size() && !type; ++k)
			{
				if (_->variants[i].name == union_type->member_names[k])
					type = union_type->member_types[k];
			}
			if (union_type && !type)
				errorf(_->variants[i].location, "Union '%s' doesn't have a tag named '%s'", union_type->name.c_str(), _->variants[i].name.c_str());

			if (!type)
				type = new TypeGeneric();

			// Check that the same variant is not already defined
			for (size_t k = 0; k < variants.size(); ++k)
			{
				if (variants[k] == _->variants[i].name)
					errorf(_->variants[i].location, "Case for tag '%s' is already defind", _->variants[i].name.c_str());
			}

			variants.push_back(_->variants[i].name);

			BindingTarget* target = new BindingTarget(_->aliases[i].name, type);

			aliases.push_back(target);

			env.bindings.back().push_back(Binding(_->aliases[i].name, new BindingLocal(target)));

			expressions.push_back(resolveExpr(_->expressions[i], env));

			env.bindings.back().pop_back();
		}

		return new ExprMatchWith(new TypeGeneric(), _->location, variable, variants, aliases, expressions);
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

	return false;
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

	if (CASE(TypeStructure, lhs))
	{
		TypeStructure* r = dynamic_cast<TypeStructure*>(rhs);
		if (!r) return false;

		if (_->name != r->name) return false;

		if (_->member_types.size() != r->member_types.size()) return false;

		for (size_t i = 0; i < _->member_types.size(); ++i)
		{
			if (!unify(_->member_types[i], r->member_types[i]))
				return false;
		}

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

Type* analyze(BindingBase* binding)
{
	if (CASE(BindingLocal, binding))
	{
		return _->target->type;
	}

	assert(!"Unknown binding type");
	return 0;
}

Type* analyze(Expr* root)
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

	if (CASE(ExprBinding, root))
	{
		return _->type;
	}

	if (CASE(ExprBindingExternal, root))
	{
		return _->type;
	}

	if (CASE(ExprUnaryOp, root))
	{
		Type* te = analyze(_->expr);

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
		Type* tl = analyze(_->left);
		Type* tr = analyze(_->right);

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
		Type* te = analyze(_->expr);

		std::vector<Type*> argtys;
		for (size_t i = 0; i < _->args.size(); ++i)
			argtys.push_back(analyze(_->args[i]));

		TypeFunction* funty = new TypeFunction(new TypeGeneric(), argtys);

		mustUnify(te, funty, _->expr->location);

		return _->type = funty->result;
	}

	if (CASE(ExprArrayIndex, root))
	{
		Type* ta = analyze(_->arr);

		Type* ti = analyze(_->index);

		TypeArray* tn = new TypeArray(new TypeGeneric());

		mustUnify(ta, tn, _->arr->location);
		mustUnify(ti, new TypeInt(), _->index->location);

		return _->type = tn->contained;
	}

	if (CASE(ExprArraySlice, root))
	{
		Type* ta = analyze(_->arr);

		Type* ts = analyze(_->index_start);
		Type* te = _->index_end ? analyze(_->index_end) : 0;

		TypeArray* tn = new TypeArray(new TypeGeneric());

		mustUnify(ta, tn, _->arr->location);
		mustUnify(ts, new TypeInt(), _->index_start->location);

		if (te)
			mustUnify(te, new TypeInt(), _->index_end->location);

		return _->type = ta;
	}

	if (CASE(ExprMemberAccess, root))
	{
		Type* ta = analyze(_->aggr);

		if (TypeStructure* struct_type = dynamic_cast<TypeStructure*>(ta))
		{
			assert(struct_type->member_names.size() == struct_type->member_types.size());
			for (size_t i = 0; i < struct_type->member_names.size(); i++)
			{
				if (struct_type->member_names[i] == _->member_name)
					_->type = struct_type->member_types[i];
			}
		}

		return _->type;
	}

	if (CASE(ExprLetVar, root))
	{
		Type* tb = analyze(_->body);

		mustUnify(tb, _->target->type, _->body->location);

		return _->type;
	}

	if (CASE(ExprLetFunc, root))
	{
		Type* tb = analyze(_->body);

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
		Type* tcond = analyze(_->cond);
		Type* tthen = analyze(_->thenbody);
		Type* telse = analyze(_->elsebody);

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
		Type* tarr = analyze(_->arr);
		Type* tbody = analyze(_->body);

		TypeArray* ta = new TypeArray(_->target->type);

		mustUnify(tarr, ta, _->arr->location);
		mustUnify(tbody, new TypeUnit(), _->body->location);

		return _->type = new TypeUnit();
	}

	if (CASE(ExprMatchWith, root))
	{
		// TODO: _->variable type must be a union
		Type* tvar = analyze(_->variable);
		
		_->type = new TypeUnit();

		if (!_->expressions.empty())
		{
			Type* t0 = analyze(_->expressions[0]);

			for (size_t i = 1; i < _->expressions.size(); ++i)
			{
				Type* ti = analyze(_->expressions[i]);

				mustUnify(ti, t0, _->expressions[i]->location);
			}

			_->type = t0;
		}

		return _->type;
	}

	if (CASE(ExprBlock, root))
	{
		if (_->expressions.empty())
			return new TypeUnit();

		for (size_t i = 0; i + 1 < _->expressions.size(); ++i)
		{
			Type* te = analyze(_->expressions[i]);

			if (dynamic_cast<ExprLetVar*>(_->expressions[i]) == 0 && dynamic_cast<ExprLetFunc*>(_->expressions[i]) == 0 && dynamic_cast<ExprExternFunc*>(_->expressions[i]) == 0 && dynamic_cast<ExprStructConstructorFunc*>(_->expressions[i]) == 0 && dynamic_cast<ExprUnionConstructorFunc*>(_->expressions[i]) == 0)
			{
				mustUnify(te, new TypeUnit(), _->expressions[i]->location);
			}
		}

		return _->type = analyze(_->expressions.back());
	}

	assert(!"Unknown expression type");
	return 0;
}

Expr* typecheck(SynBase* root)
{
	Expr* result = resolve(root);

	analyze(result);

	return result;
}
