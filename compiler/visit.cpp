#include "common.hpp"
#include "visit.hpp"

#include "ast.hpp"

void visitAst(Ast* node, function<bool (Ast*)> f)
{
	if (!f(node))
		visitAstInner(node, f);
}

void visitAstInner(Ast* node, function<bool (Ast*)> f)
{
	if (UNION_CASE(LiteralArray, n, node))
	{
		for (auto& c: n->elements)
			visitAst(c, f);
	}
	else if (UNION_CASE(LiteralStruct, n, node))
	{
		for (auto& c: n->fields)
			visitAst(c.second, f);
	}
	else if (UNION_CASE(Member, n, node))
	{
		visitAst(n->expr, f);
	}
	else if (UNION_CASE(Block, n, node))
	{
		for (auto& c: n->body)
			visitAst(c, f);
	}
	else if (UNION_CASE(Module, n, node))
	{
		visitAst(n->body, f);
	}
	else if (UNION_CASE(Call, n, node))
	{
		visitAst(n->expr, f);

		for (auto& a: n->args)
			visitAst(a, f);
	}
	else if (UNION_CASE(Unary, n, node))
	{
		visitAst(n->expr, f);
	}
	else if (UNION_CASE(Binary, n, node))
	{
		visitAst(n->left, f);
		visitAst(n->right, f);
	}
	else if (UNION_CASE(Index, n, node))
	{
		visitAst(n->expr, f);
		visitAst(n->index, f);
	}
	else if (UNION_CASE(Assign, n, node))
	{
		visitAst(n->left, f);
		visitAst(n->right, f);
	}
	else if (UNION_CASE(If, n, node))
	{
		visitAst(n->cond, f);
		visitAst(n->thenbody, f);

		if (n->elsebody)
			visitAst(n->elsebody, f);
	}
	else if (UNION_CASE(For, n, node))
	{
		visitAst(n->expr, f);
		visitAst(n->body, f);
	}
	else if (UNION_CASE(While, n, node))
	{
		visitAst(n->expr, f);
		visitAst(n->body, f);
	}
	else if (UNION_CASE(FnDecl, n, node))
	{
		if (n->body)
			visitAst(n->body, f);
	}
	else if (UNION_CASE(Fn, n, node))
	{
		visitAst(n->decl, f);
	}
	else if (UNION_CASE(VarDecl, n, node))
	{
		visitAst(n->expr, f);
	}
	else if (UNION_CASE(TyDecl, n, node))
	{
		if (UNION_CASE(Struct, t, n->def))
		{
			for (auto& c: t->fields)
				if (c.expr)
					visitAst(c.expr, f);
		}
	}
}

void visitAstTypes(Ast* node, function<void (Ty*)> f)
{
	if (UNION_CASE(LiteralArray, n, node))
	{
		f(n->type);
	}
	else if (UNION_CASE(LiteralStruct, n, node))
	{
		f(n->type);
	}
	else if (UNION_CASE(Ident, n, node))
	{
		for (auto& a: n->tyargs)
			f(a);
	}
	else if (UNION_CASE(For, n, node))
	{
		f(n->var->type);

		if (n->index)
			f(n->index->type);
	}
	else if (UNION_CASE(FnDecl, n, node))
	{
		f(n->var->type);
	}
	else if (UNION_CASE(VarDecl, n, node))
	{
		f(n->var->type);
	}
	else if (UNION_CASE(TyDecl, n, node))
	{
		if (UNION_CASE(Struct, d, n->def))
		{
			for (auto& c: d->fields)
				f(c.type);
		}
	}
}

void visitType(Ty* type, function<void (Ty*)> f)
{
	f(type);

	if (UNION_CASE(Array, t, type))
	{
		visitType(t->element, f);
	}
	else if (UNION_CASE(Pointer, t, type))
	{
		visitType(t->element, f);
	}
	else if (UNION_CASE(Function, t, type))
	{
		for (auto& a: t->args)
			visitType(a, f);

		visitType(t->ret, f);
	}
	else if (UNION_CASE(Instance, t, type))
	{
		for (auto& a: t->tyargs)
			visitType(a, f);
	}
}
