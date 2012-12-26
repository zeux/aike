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

		return new TypeFunction(resolveType(_->result, env), argtys);
	}

	assert(!"Unknown syntax tree type");
	return 0;
}

Type* resolveFunctionType(SynType* rettype, const std::vector<SynTypedVar>& args, Environment& env)
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
			env.functions.back().externals.push_back(binding);

			if (CASE(BindingLocal, binding))
				return new ExprBindingExternal(_->target->type, location, env.functions.back().context, name, env.functions.back().externals.size() - 1);
			else
				errorf(location, "Can't resolve the binding of the function external variable %s", name.c_str());
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
		return new ExprUnaryOp(new TypeGeneric(), _->location, _->op, resolveExpr(_->expr, env));

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
			target->type = body->type;

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

		BindingTarget* target = new BindingTarget(_->var.name, resolveFunctionType(_->ret_type, _->args, env));

		// Add info about function context. Context type will be resolved later
		BindingTarget* context_target = new BindingTarget("extern", new TypeGeneric());
		env.functions.back().context = new BindingLocal(context_target);

		Expr* body = resolveExpr(_->body, env);

		// If the function return type is not set, change it to the function body result type
		if (!_->ret_type)
		{
			std::vector<Type*> argtys;

			for (size_t i = 0; i < _->args.size(); ++i)
				argtys.push_back(resolveType(_->args[i].type, env));

			target->type = new TypeFunction(body->type, argtys);
		}

		// Resolve function context type
		std::vector<Type*> context_members;

		for (size_t i = 0; i < env.functions.back().externals.size(); ++i)
		{
			if (CASE(BindingLocal, env.functions.back().externals[i]))
				context_members.push_back(_->target->type);
		}

		context_target->type = new TypeReference(new TypeStructure(context_members));

		std::vector<BindingBase*> function_externals = env.functions.back().externals;

		env.functions.pop_back();
		env.bindings.pop_back();

		env.bindings.back().push_back(Binding(_->var.name, new BindingLocal(target)));

		// Resolve function external variable capture
		std::vector<Expr*> externals;

		for (size_t i = 0; i < function_externals.size(); ++i)
		{
			if (CASE(BindingLocal, function_externals[i]))
				externals.push_back(resolveBindingAccess(_->target->name, Location(), env));
		}

		return new ExprLetFunc(target->type, _->location, target, context_target, args, body, externals);
	}

	if (CASE(SynExternFunc, node))
	{
		Type* funty = resolveFunctionType(_->ret_type, _->args, env);

		BindingTarget* target = new BindingTarget(_->var.name, funty);

		std::vector<BindingTarget*> args;

		for (size_t i = 0; i < _->args.size(); ++i)
		{
			BindingTarget* target = new BindingTarget(_->args[i].name.name, resolveType(_->args[i].type, env));

			args.push_back(target);
		}

		env.bindings.back().push_back(Binding(_->var.name, new BindingLocal(target)));

		return new ExprExternFunc(funty, _->location, target, args);
	}

	if (CASE(SynIfThenElse, node))
		return new ExprIfThenElse(new TypeGeneric(), _->location, resolveExpr(_->cond, env), resolveExpr(_->thenbody, env), resolveExpr(_->elsebody, env));

	if (CASE(SynForInDo, node))
	{
		Expr* arr = resolveExpr(_->arr, env);

		TypeArray* arr_type = dynamic_cast<TypeArray*>(arr->type);
		if (!arr_type) errorf(arr->location, "iteration is only available on array types");

		BindingTarget* target = new BindingTarget(_->var.name, arr_type->contained);

		env.bindings.back().push_back(Binding(_->var.name, new BindingLocal(target)));

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

Expr* typecheck(SynBase* root)
{
	return resolve(root);
}