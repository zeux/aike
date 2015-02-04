#pragma once

#include "ast.hpp"

template <typename F, typename FC> inline void visitAst(Ast* node, F f, FC& fc);

template <typename F, typename FC> inline void visitAstInner(Ast* node, F f, FC& fc)
{
	if (UNION_CASE(LiteralStruct, n, node))
	{
		for (auto& c: n->fields)
			visitAst(c.second, f, fc);
	}
	else if (UNION_CASE(Index, n, node))
	{
		visitAst(n->expr, f, fc);
	}
	else if (UNION_CASE(Block, n, node))
	{
		for (auto& c: n->body)
			visitAst(c, f, fc);
	}
	else if (UNION_CASE(Call, n, node))
	{
		visitAst(n->expr, f, fc);

		for (auto& a: n->args)
			visitAst(a, f, fc);
	}
	else if (UNION_CASE(If, n, node))
	{
		visitAst(n->cond, f, fc);
		visitAst(n->thenbody, f, fc);

		if (n->elsebody)
			visitAst(n->elsebody, f, fc);
	}
	else if (UNION_CASE(FnDecl, n, node))
	{
		if (n->body)
			visitAst(n->body, f, fc);
	}
	else if (UNION_CASE(Fn, n, node))
	{
		visitAst(n->body, f, fc);
	}
	else if (UNION_CASE(VarDecl, n, node))
	{
		visitAst(n->expr, f, fc);
	}
}

template <typename F, typename FC> inline void visitAst(Ast* node, F f, FC& fc)
{
	if (!f(fc, node))
		visitAstInner(node, f, fc);
}

template <typename F, typename FC> inline void visitAstTypes(Ast* node, F f, FC& fc)
{
	if (UNION_CASE(LiteralStruct, n, node))
	{
		f(fc, n->type);
	}
	else if (UNION_CASE(Fn, n, node))
	{
		f(fc, n->type);
	}
	else if (UNION_CASE(FnDecl, n, node))
	{
		f(fc, n->var->type);
	}
	else if (UNION_CASE(VarDecl, n, node))
	{
		f(fc, n->var->type);
	}
	else if (UNION_CASE(TyDecl, n, node))
	{
		if (UNION_CASE(Struct, d, n->def))
		{
			for (auto& c: d->fields)
				f(fc, c.second);
		}
	}
}

template <typename F, typename FC> inline void visitType(Ty* type, F f, FC& fc)
{
	f(fc, type);

	if (UNION_CASE(Function, t, type))
	{
		for (auto& c: t->args)
			visitType(c, f, fc);

		visitType(t->ret, f, fc);
	}
}