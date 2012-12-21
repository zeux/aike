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

BindingBase* resolveBinding(const std::string& name, Environment& env)
{
	if (BindingBase* result = tryResolveBinding(name, env))
		return result;

	errorf("Unresolved binding %s", name.c_str());
}

Type* tryResolveType(const std::string& name, Environment& env)
{
	for (size_t i = 0; i < env.types.size(); ++i)
		if (env.types[i].name == name)
			return env.types[i].type;

	return 0;
}

Type* resolveType(const std::string& name, Environment& env)
{
	if (name == "")
		return new TypeGeneric();

	if (Type* type = tryResolveType(name, env))
		return type;

	errorf("Unknown type %s", name.c_str());
}

Expr* resolveExpr(SynBase* node, Environment& env)
{
	if (CASE(SynUnit, node))
		return new ExprUnit(resolveType("unit", env));

	if (CASE(SynLiteralNumber, node))
		return new ExprLiteralNumber(resolveType("int", env), _->value);

	if (CASE(SynVariableReference, node))
	{
		for (size_t i = env.bindings.size(); i > 0; --i)
			if (env.bindings[i - 1].name == _->name)
				return new ExprBinding(new TypeGeneric(), env.bindings[i - 1].binding);

		errorf("Unresolved variable reference %s", _->name.c_str());
	}

	if (CASE(SynUnaryOp, node))
		return new ExprUnaryOp(new TypeGeneric(), _->op, resolveExpr(_->expr, env));

	if (CASE(SynBinaryOp, node))
		return new ExprBinaryOp(new TypeGeneric(), _->op, resolveExpr(_->left, env), resolveExpr(_->right, env));

	if (CASE(SynCall, node))
	{
		std::vector<Expr*> args;
		for (size_t i = 0; i < _->args.size(); ++i)
			args.push_back(resolveExpr(_->args[i], env));

		return new ExprCall(new TypeGeneric(), resolveExpr(_->expr, env), args);
	}

	if (CASE(SynLetVar, node))
	{
		BindingTarget* target = new BindingTarget(_->var.name);

		Expr* body = resolveExpr(_->body, env);

		env.bindings.push_back(Binding(_->var.name, new BindingLet(target)));

		Expr* result = new ExprLetVar(new TypeGeneric(), target, resolveType(_->var.type, env), body, resolveExpr(_->expr, env));

		env.bindings.pop_back();

		return result;
	}

	if (CASE(SynLLVM, node))
		return new ExprLLVM(new TypeGeneric(), _->body);

	if (CASE(SynLetFunc, node))
	{
		BindingTarget* target = new BindingTarget(_->var.name);

		for (size_t i = 0; i < _->args.size(); ++i)
			env.bindings.push_back(Binding(_->args[i].name, new BindingFunarg(target, i)));

		Expr* body = resolveExpr(_->body, env);

		for (size_t i = 0; i < _->args.size(); ++i)
			env.bindings.pop_back();

		std::vector<Type*> argtys;

		for (size_t i = 0; i < _->args.size(); ++i)
			argtys.push_back(resolveType(_->args[i].type, env));

		env.bindings.push_back(Binding(_->var.name, new BindingLet(target)));

		Expr* result = new ExprLetFunc(new TypeGeneric(), target, new TypeFunction(resolveType(_->var.type, env), argtys), body, resolveExpr(_->expr, env));

		env.bindings.pop_back();

		return result;
	}

	if (CASE(SynIfThenElse, node))
		return new ExprIfThenElse(new TypeGeneric(), resolveExpr(_->cond, env), resolveExpr(_->thenbody, env), resolveExpr(_->elsebody, env));

	if (CASE(SynSequence, node))
		return new ExprSequence(new TypeGeneric(), resolveExpr(_->head, env), resolveExpr(_->tail, env));

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