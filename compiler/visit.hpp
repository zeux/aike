#pragma once

#include "ast.hpp"

template <typename F, typename FC> inline void visitAst(Ast* node, F f, FC& fc);

template <typename F, typename FC> inline void visitAstInner(Ast* node, F f, FC& fc)
{
	if (UNION_CASE(LiteralArray, n, node))
	{
		for (auto& c: n->elements)
			visitAst(c, f, fc);
	}
	else if (UNION_CASE(LiteralStruct, n, node))
	{
		for (auto& c: n->fields)
			visitAst(c.second, f, fc);
	}
	else if (UNION_CASE(Member, n, node))
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
	else if (UNION_CASE(Unary, n, node))
	{
		visitAst(n->expr, f, fc);
	}
	else if (UNION_CASE(Binary, n, node))
	{
		visitAst(n->left, f, fc);
		visitAst(n->right, f, fc);
	}
	else if (UNION_CASE(Index, n, node))
	{
		visitAst(n->expr, f, fc);
		visitAst(n->index, f, fc);
	}
	else if (UNION_CASE(Assign, n, node))
	{
		visitAst(n->left, f, fc);
		visitAst(n->right, f, fc);
	}
	else if (UNION_CASE(If, n, node))
	{
		visitAst(n->cond, f, fc);
		visitAst(n->thenbody, f, fc);

		if (n->elsebody)
			visitAst(n->elsebody, f, fc);
	}
	else if (UNION_CASE(For, n, node))
	{
		visitAst(n->expr, f, fc);
		visitAst(n->body, f, fc);
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
	else if (UNION_CASE(TyDecl, n, node))
	{
		if (UNION_CASE(Struct, t, n->def))
		{
			for (auto& c: t->fields)
				if (c.expr)
					visitAst(c.expr, f, fc);
		}
	}
}

template <typename F, typename FC> inline void visitAst(Ast* node, F f, FC& fc)
{
	if (!f(fc, node))
		visitAstInner(node, f, fc);
}

template <typename F, typename FC> inline void visitAstTypes(Ast* node, F f, FC& fc)
{
	if (UNION_CASE(LiteralArray, n, node))
	{
		f(fc, n->type);
	}
	else if (UNION_CASE(LiteralStruct, n, node))
	{
		f(fc, n->type);
	}
	else if (UNION_CASE(Ident, n, node))
	{
		for (auto& a: n->tyargs)
			f(fc, a);
	}
	else if (UNION_CASE(For, n, node))
	{
		f(fc, n->var->type);

		if (n->index)
			f(fc, n->index->type);
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
				f(fc, c.type);
		}
	}
}

template <typename F, typename FC> inline void visitType(Ty* type, F f, FC& fc)
{
	f(fc, type);

	if (UNION_CASE(Array, t, type))
	{
		visitType(t->element, f, fc);
	}
	else if (UNION_CASE(Function, t, type))
	{
		for (auto& a: t->args)
			visitType(a, f, fc);

		visitType(t->ret, f, fc);
	}
	else if (UNION_CASE(Instance, t, type))
	{
		for (auto& a: t->tyargs)
			visitType(a, f, fc);
	}
}