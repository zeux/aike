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

	if (CASE(SynTypeArray, type))
	{
		return new TypeArray(resolveType(_->contained_type, env));
	}

	if (CASE(SynTypeFunction, type))
	{
		std::vector<Type*> argtys;

		for (size_t i = 0; i < _->args.size(); ++i)
			argtys.push_back(resolveType(_->args[i], env));

		return new TypeFunction(resolveType(_->result, env), argtys, new TypeOpaquePointer());
	}

	assert(!"Unknown syntax tree type");
	return 0;
}

Type* resolveFunctionType(SynType* rettype, const std::vector<SynTypedVar>& args, Environment& env, Type* context_type)
{
	std::vector<Type*> argtys;

	for (size_t i = 0; i < args.size(); ++i)
		argtys.push_back(resolveType(args[i].type, env));

	return new TypeFunction(resolveType(rettype, env), argtys, context_type);
}

Expr* resolveBindingAccess(const std::string& name, Location location, Environment& env)
{
	size_t scope;

	if (BindingBase* binding = tryResolveBinding(name, env, &scope))
	{
		if (scope < env.functions.back().scope)
		{
			if (CASE(BindingLocal, binding))
			{
				env.functions.back().externals.push_back(binding);
				return new ExprBindingExternal(_->target->type, location, env.functions.back().context, name, env.functions.back().externals.size() - 1);
			}
			else if (CASE(BindingFreeFunction, binding))
			{
				return new ExprBinding(_->target->type, location, _);
			}
			else
			{
				errorf(location, "Can't resolve the binding of the function external variable %s", name.c_str());
			}
		}

		if (CASE(BindingLocal, binding))
			return new ExprBinding(_->target->type, location, _);
		else if (CASE(BindingFreeFunction, binding))
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
		return new ExprBinaryOp(new TypeGeneric(), _->location, _->op, resolveExpr(_->left, env), resolveExpr(_->right, env));

	if (CASE(SynCall, node))
	{
		std::vector<Expr*> args;
		for (size_t i = 0; i < _->args.size(); ++i)
			args.push_back(resolveExpr(_->args[i], env));

		Expr* function = resolveExpr(_->expr, env);

		TypeFunction* function_type = dynamic_cast<TypeFunction*>(function->type);

		return new ExprCall(function_type ? function_type->result : new TypeGeneric(), _->location, function, args);
	}

	if (CASE(SynArrayIndex, node))
	{
		Expr* arr = resolveExpr(_->arr, env);

		TypeArray* arr_type = dynamic_cast<TypeArray*>(arr->type);

		return new ExprArrayIndex(arr_type ? arr_type->contained : new TypeGeneric(), _->location, arr, resolveExpr(_->index, env));
	}

	if (CASE(SynLetVar, node))
	{
		BindingTarget* target = new BindingTarget(_->var.name.name, resolveType(_->var.type, env));

		Expr* body = resolveExpr(_->body, env);

		// If the type is not defined, take the body type
		if (!_->var.type)
		{
			if (CASE(TypeFunction, body->type))
				target->type = _->toGeneralType();
			else
				target->type = body->type;
		}

		env.bindings.back().push_back(Binding(_->var.name.name, new BindingLocal(target)));

		return new ExprLetVar(target->type, _->location, target, body);
	}

	if (CASE(SynLLVM, node))
		return new ExprLLVM(new TypeGeneric(), _->location, _->body);

	if (CASE(SynLetFunc, node))
	{
		std::vector<BindingTarget*> args;

		env.functions.push_back(FunctionInfo(env.bindings.size()));
		env.bindings.push_back(std::vector<Binding>());

		for (size_t i = 0; i < _->args.size(); ++i)
		{
			BindingTarget* target = new BindingTarget(_->args[i].name.name, resolveType(_->args[i].type, env));

			args.push_back(target);
			env.bindings.back().push_back(Binding(_->args[i].name.name, new BindingLocal(target)));
		}

		BindingTarget* target = new BindingTarget(_->var.name, resolveFunctionType(_->ret_type, _->args, env, new TypeOpaquePointer()));

		// Add info about function context. Context type will be resolved later
		TypeStructure* context_type = new TypeStructure();
		BindingTarget* context_target = new BindingTarget("extern", new TypeReference(context_type));
		env.functions.back().context = new BindingLocal(context_target);

		Expr* body = resolveExpr(_->body, env);

		bool has_externals = !env.functions.back().externals.empty();

		std::vector<Type*> argtys;

		for (size_t i = 0; i < _->args.size(); ++i)
			argtys.push_back(resolveType(_->args[i].type, env));

		// If the function return type is not set, change it to the function body result type
		target->type = new TypeFunction(_->ret_type ? resolveType(_->ret_type, env) : body->type, argtys, has_externals ? context_type : 0);

		// Resolve function context type
		for (size_t i = 0; i < env.functions.back().externals.size(); ++i)
		{
			if (CASE(BindingLocal, env.functions.back().externals[i]))
				context_type->members.push_back(_->target->type);
		}

		std::vector<BindingBase*> function_externals = env.functions.back().externals;

		env.functions.pop_back();
		env.bindings.pop_back();

		env.bindings.back().push_back(Binding(_->var.name, function_externals.empty() ? (BindingBase*)new BindingFreeFunction(target) : (BindingBase*)new BindingLocal(target)));

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
		Type* funty = resolveFunctionType(_->ret_type, _->args, env, 0);

		BindingTarget* target = new BindingTarget(_->var.name, funty);

		std::vector<BindingTarget*> args;

		for (size_t i = 0; i < _->args.size(); ++i)
		{
			BindingTarget* target = new BindingTarget(_->args[i].name.name, resolveType(_->args[i].type, env));

			args.push_back(target);
		}

		env.bindings.back().push_back(Binding(_->var.name, new BindingFreeFunction(target)));

		return new ExprExternFunc(funty, _->location, target, args);
	}

	if (CASE(SynIfThenElse, node))
		return new ExprIfThenElse(new TypeGeneric(), _->location, resolveExpr(_->cond, env), resolveExpr(_->thenbody, env), resolveExpr(_->elsebody, env));

	if (CASE(SynForInDo, node))
	{
		Expr* arr = resolveExpr(_->arr, env);

		BindingTarget* target = new BindingTarget(_->var.name.name, resolveType(_->var.type, env));

		env.bindings.back().push_back(Binding(_->var.name.name, new BindingLocal(target)));

		Expr* body = resolveExpr(_->body, env);

		env.bindings.back().pop_back();

		return new ExprForInDo(new TypeUnit(), _->location, target, arr, body);
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

	return false;
}

Type* analyze(BindingBase* binding)
{
	if (CASE(BindingLocal, binding))
	{
		return _->target->type;
	}

	if (CASE(BindingFreeFunction, binding))
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

				if (!unify(t0, ti))
					errorf(_->elements[i]->location, "Array element type mismatch between %s and %s", typeName(t0).c_str(), typeName(ti).c_str());
			}

			if (!unify(new TypeArray(t0), _->type))
				errorf(_->location, "Array element type mismatch");
		}
		else
		{
			if (!unify(new TypeArray(new TypeGeneric()), _->type))
				errorf(_->location, "Array element type mismatch");
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
			if (!unify(te, new TypeInt()))
				errorf(_->expr->location, "Expected type 'int', got '%s'", typeName(te).c_str());
			return _->type = new TypeInt();
			
		case SynUnaryOpNot:
			if (!unify(te, new TypeBool()))
				errorf(_->expr->location, "Expected type 'bool', got '%s'", typeName(te).c_str());
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
			if (!unify(tl, new TypeInt()))
				errorf(_->left->location, "Expected type 'int', got '%s'", typeName(tl).c_str());
			if (!unify(tr, new TypeInt()))
				errorf(_->right->location, "Expected type 'int', got '%s'", typeName(tr).c_str());
			return _->type = new TypeInt();

		case SynBinaryOpLess:
		case SynBinaryOpLessEqual:
		case SynBinaryOpGreater:
		case SynBinaryOpGreaterEqual:
		case SynBinaryOpEqual:
		case SynBinaryOpNotEqual:
			if (!unify(tl, new TypeInt()))
				errorf(_->left->location, "Expected type 'int', got '%s'", typeName(tl).c_str());
			if (!unify(tr, new TypeInt()))
				errorf(_->right->location, "Expected type 'int', got '%s'", typeName(tr).c_str());
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

		TypeFunction* funty = new TypeFunction(new TypeGeneric(), argtys, NULL);

		if (!unify(te, funty))
			errorf(_->expr->location, "Expected type '%s', got '%s'", typeName(funty).c_str(), typeName(te).c_str());

		return _->type = funty->result;
	}

	if (CASE(ExprArrayIndex, root))
	{
		Type* ta = analyze(_->arr);
		Type* ti = analyze(_->index);

		TypeArray* tn = new TypeArray(new TypeGeneric());

		if (!unify(ta, tn))
			errorf(_->arr->location, "Expected an array type, got '%s'", typeName(ta).c_str());

		if (!unify(ti, new TypeInt()))
			errorf(_->index->location, "Expected type 'int', got '%s'", typeName(ti).c_str());

		return _->type = tn->contained;
	}

	if (CASE(ExprLetVar, root))
	{
		Type* tb = analyze(_->body);

		if (!unify(_->target->type, tb))
			errorf(_->location, "Expected type '%s', got '%s'", typeName(_->target->type).c_str(), typeName(tb).c_str());

		return _->type;
	}

	if (CASE(ExprLetFunc, root))
	{
		Type* tb = analyze(_->body);

		TypeFunction* funty = dynamic_cast<TypeFunction*>(_->type);
		Type* rettype = funty->result;

		if (!unify(rettype, tb))
			errorf(_->location, "Expected type '%s', got '%s'", typeName(rettype).c_str(), typeName(tb).c_str());

		for (size_t i = 0; i < _->args.size(); ++i)
		{
			// WTF
			unify(funty->args[i], _->args[i]->type);
		}

		return _->type;
	}

	if (CASE(ExprExternFunc, root))
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

		if (!unify(tcond, new TypeBool()))
			errorf(_->cond->location, "Expected type 'bool', got '%s'", typeName(tcond).c_str());

		if (!unify(tthen, telse))
			errorf(_->thenbody->location, "Expected type '%s', got '%s'", typeName(telse).c_str(), typeName(tthen).c_str());

		return _->type = tthen;
	}

	if (CASE(ExprForInDo, root))
	{
		Type* tarr = analyze(_->arr);
		Type* tbody = analyze(_->body);

		TypeArray* ta = new TypeArray(new TypeGeneric());

		if (!unify(ta, tarr))
			errorf(_->arr->location, "Expected array type, got '%s'", typeName(tarr).c_str());

		if (!unify(_->target->type, ta->contained))
			errorf(_->location, "Expected type '%s', got '%s'", typeName(_->target->type).c_str(), typeName(ta->contained).c_str());

		if (!unify(tbody, new TypeUnit()))
			errorf(_->body->location, "Expected type 'unit', got '%s'", typeName(tbody).c_str());

		return _->type = new TypeUnit();
	}

	if (CASE(ExprBlock, root))
	{
		if (_->expressions.empty())
			return new TypeUnit();

		for (size_t i = 0; i + 1 < _->expressions.size(); ++i)
			analyze(_->expressions[i]);

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