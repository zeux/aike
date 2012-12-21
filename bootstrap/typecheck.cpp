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

Expr* resolveExpr(SynBase* node, std::vector<Binding>& bindings)
{
	if (CASE(SynUnit, node))
		return new ExprUnit();

	if (CASE(SynLiteralNumber, node))
		return new ExprLiteralNumber(_->value);

	if (CASE(SynVariableReference, node))
	{
		for (size_t i = bindings.size(); i > 0; --i)
			if (bindings[i - 1].name == _->name)
				return new ExprBinding(bindings[i - 1].binding);

		errorf("Unresolved variable reference %s", _->name.c_str());
	}

	if (CASE(SynUnaryOp, node))
		return new ExprUnaryOp(_->op, resolveExpr(_->expr, bindings));

	if (CASE(SynBinaryOp, node))
		return new ExprBinaryOp(_->op, resolveExpr(_->left, bindings), resolveExpr(_->right, bindings));

	if (CASE(SynCall, node))
	{
		std::vector<Expr*> args;
		for (size_t i = 0; i < _->args.size(); ++i)
			args.push_back(resolveExpr(_->args[i], bindings));

		return new ExprCall(resolveExpr(_->expr, bindings), args);
	}

	if (CASE(SynLetVar, node))
	{
		BindingTarget* target = new BindingTarget();

		Expr* body = resolveExpr(_->body, bindings);

		bindings.push_back(Binding(_->var.name, new BindingLet(target)));

		Expr* result = new ExprLetVar(target, body, resolveExpr(_->expr, bindings));

		bindings.pop_back();

		return result;
	}

	if (CASE(SynLLVM, node))
		return new ExprLLVM(_->body);

	if (CASE(SynLetFunc, node))
	{
		BindingTarget* target = new BindingTarget();

		for (size_t i = 0; i < _->args.size(); ++i)
			bindings.push_back(Binding(_->args[i].name, new BindingFunarg(target, i)));

		Expr* body = resolveExpr(_->body, bindings);

		for (size_t i = 0; i < _->args.size(); ++i)
			bindings.pop_back();

		bindings.push_back(Binding(_->var.name, new BindingLet(target)));

		Expr* result = new ExprLetFunc(body, resolveExpr(_->expr, bindings));

		bindings.pop_back();

		return result;
	}

	if (CASE(SynIfThenElse, node))
		return new ExprIfThenElse(resolveExpr(_->cond, bindings), resolveExpr(_->thenbody, bindings), resolveExpr(_->elsebody, bindings));

	assert(!"Unrecognized AST type");
	return 0;
}

Expr* resolve(SynBase* root)
{
	std::vector<Binding> bindings;

	return resolveExpr(root, bindings);
}

Expr* typecheck(SynBase* root)
{
	return resolve(root);
}