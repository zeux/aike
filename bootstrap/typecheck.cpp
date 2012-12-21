#include "typecheck.hpp"

#include "output.hpp"

#include <cassert>

Expr* resolve(SynBase* root)
{
	if (CASE(SynUnit, root))
		return new ExprUnit();

	if (CASE(SynLiteralNumber, root))
		return new ExprLiteralNumber(_->value);

	if (CASE(SynVariableReference, root))
		return new ExprVariableReference(_->name);

	if (CASE(SynUnaryOp, root))
		return new ExprUnaryOp(_->op, resolve(_->expr));

	if (CASE(SynBinaryOp, root))
		return new ExprBinaryOp(_->op, resolve(_->left), resolve(_->right));

	if (CASE(SynCall, root))
	{
		std::vector<Expr*> args;
		for (size_t i = 0; i < _->args.size(); ++i)
			args.push_back(resolve(_->args[i]));

		return new ExprCall(resolve(_->expr), args);
	}

	if (CASE(SynLetVar, root))
		return new ExprLetVar(ExprTypedVar(_->var.name, _->var.type), resolve(_->body), resolve(_->expr));

	if (CASE(SynLLVM, root))
		return new ExprLLVM(_->body);

	if (CASE(SynLetFunc, root))
	{
		std::vector<ExprTypedVar> args;
		for (size_t i = 0; i < _->args.size(); ++i)
			args.push_back(ExprTypedVar(_->args[i].name, _->args[i].type));

		return new ExprLetFunc(ExprTypedVar(_->var.name, _->var.type), args, resolve(_->body), resolve(_->expr));
	}

	if (CASE(SynIfThenElse, root))
		return new ExprIfThenElse(resolve(_->cond), resolve(_->thenbody), resolve(_->elsebody));

	assert(!"Unrecognized AST type");
	return 0;
}

Expr* typecheck(SynBase* root)
{
	return resolve(root);
}