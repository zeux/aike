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

struct Environment
{
	std::vector<Binding> bindings;
	std::vector<TypeBinding> types;
};

BindingBase* tryResolveBinding(const std::string& name, Environment& env)
{
	for (size_t i = env.bindings.size(); i > 0; --i)
		if (env.bindings[i - 1].name == name)
			return env.bindings[i - 1].binding;

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
		if (env.types[i].name == name)
			return env.types[i].type;

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
		return new TypeGeneric();

	if (CASE(SynTypeBasic, type))
	{
		return resolveType(_->type.name, env, _->type.location);
	}

	if (CASE(SynTypeFunction, type))
	{
		std::vector<Type*> argtys;

		for (size_t i = 0; i < _->argument_types.size(); ++i)
			argtys.push_back(resolveType(_->argument_types[i], env));

		return new TypeFunction(resolveType(_->return_type, env), argtys);
	}

	assert(!"Unknown syntax tree type");
	return 0;
}

Expr* resolveExpr(SynBase* node, Environment& env)
{
	assert(node);

	if (CASE(SynUnit, node))
		return new ExprUnit(resolveType("unit", env, _->location), _->location);

	if (CASE(SynLiteralNumber, node))
		return new ExprLiteralNumber(resolveType("int", env, _->location), _->location, _->value);

	if (CASE(SynVariableReference, node))
	{
		for (size_t i = env.bindings.size(); i > 0; --i)
			if (env.bindings[i - 1].name == _->name)
				return new ExprBinding(new TypeGeneric(), _->location, env.bindings[i - 1].binding);

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

		return new ExprCall(new TypeGeneric(), _->location, resolveExpr(_->expr, env), args);
	}

	if (CASE(SynLetVar, node))
	{
		BindingTarget* target = new BindingTarget(_->var.name.name);

		Expr* body = resolveExpr(_->body, env);

		env.bindings.push_back(Binding(_->var.name.name, new BindingLocal(target)));

		return new ExprLetVar(resolveType(_->var.type, env), _->location, target, body);
	}

	if (CASE(SynLLVM, node))
		return new ExprLLVM(new TypeGeneric(), _->location, _->body);

	if (CASE(SynLetFunc, node))
	{
		BindingTarget* target = new BindingTarget(_->var.name.name);

		std::vector<BindingTarget*> args;
		std::vector<Type*> argtys;

		for (size_t i = 0; i < _->args.size(); ++i)
		{
			BindingTarget* target = new BindingTarget(_->args[i].name.name);

			args.push_back(target);
			argtys.push_back(resolveType(_->args[i].type, env));

			env.bindings.push_back(Binding(_->args[i].name.name, new BindingLocal(target)));
		}

		Type* funty = new TypeFunction(resolveType(_->var.type, env), argtys);

		Expr* body = _->body ? resolveExpr(_->body, env) : 0;

		for (size_t i = 0; i < _->args.size(); ++i)
			env.bindings.pop_back();

		env.bindings.push_back(Binding(_->var.name.name, new BindingLocal(target)));

		return new ExprLetFunc(funty, _->location, target, args, body);
	}

	if (CASE(SynIfThenElse, node))
		return new ExprIfThenElse(new TypeGeneric(), _->location, resolveExpr(_->cond, env), resolveExpr(_->thenbody, env), resolveExpr(_->elsebody, env));

	if (CASE(SynBlock, node))
	{
		ExprBlock *expression = new ExprBlock(new TypeGeneric(), _->location);
		
		size_t bind_count = env.bindings.size();
		size_t type_count = env.types.size();

		for (size_t i = 0; i < _->expressions.size(); ++i)
			expression->expressions.push_back(resolveExpr(_->expressions[i], env));

		while (env.bindings.size() > bind_count)
			env.bindings.pop_back();

		while (env.types.size() > type_count)
			env.types.pop_back();

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

	return resolveExpr(root, env);
}

Expr* typecheck(SynBase* root)
{
	return resolve(root);
}