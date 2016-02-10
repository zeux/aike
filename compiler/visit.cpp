#include "common.hpp"
#include "visit.hpp"

#include "ast.hpp"

static void visitAstRec(const function<bool (Ast*)>& f, Ast* node, Ast* ignore = nullptr)
{
	if (node != ignore && f(node))
		return;

	if (UNION_CASE(LiteralTuple, n, node))
	{
		for (auto& c: n->fields)
			visitAstRec(f, c);
	}
	else if (UNION_CASE(LiteralArray, n, node))
	{
		for (auto& c: n->elements)
			visitAstRec(f, c);
	}
	else if (UNION_CASE(LiteralStruct, n, node))
	{
		for (auto& c: n->fields)
			visitAstRec(f, c.second);
	}
	else if (UNION_CASE(Member, n, node))
	{
		visitAstRec(f, n->expr);
	}
	else if (UNION_CASE(Block, n, node))
	{
		for (auto& c: n->body)
			visitAstRec(f, c);
	}
	else if (UNION_CASE(Module, n, node))
	{
		visitAstRec(f, n->body);
	}
	else if (UNION_CASE(Call, n, node))
	{
		visitAstRec(f, n->expr);

		for (auto& a: n->args)
			visitAstRec(f, a);
	}
	else if (UNION_CASE(Unary, n, node))
	{
		visitAstRec(f, n->expr);
	}
	else if (UNION_CASE(Binary, n, node))
	{
		visitAstRec(f, n->left);
		visitAstRec(f, n->right);
	}
	else if (UNION_CASE(Index, n, node))
	{
		visitAstRec(f, n->expr);
		visitAstRec(f, n->index);
	}
	else if (UNION_CASE(Assign, n, node))
	{
		visitAstRec(f, n->left);
		visitAstRec(f, n->right);
	}
	else if (UNION_CASE(If, n, node))
	{
		visitAstRec(f, n->cond);
		visitAstRec(f, n->thenbody);

		if (n->elsebody)
			visitAstRec(f, n->elsebody);
	}
	else if (UNION_CASE(For, n, node))
	{
		visitAstRec(f, n->expr);
		visitAstRec(f, n->body);
	}
	else if (UNION_CASE(While, n, node))
	{
		visitAstRec(f, n->expr);
		visitAstRec(f, n->body);
	}
	else if (UNION_CASE(FnDecl, n, node))
	{
		if (n->body)
			visitAstRec(f, n->body);
	}
	else if (UNION_CASE(Fn, n, node))
	{
		visitAstRec(f, n->decl);
	}
	else if (UNION_CASE(VarDecl, n, node))
	{
		visitAstRec(f, n->expr);
	}
	else if (UNION_CASE(TyDecl, n, node))
	{
		if (UNION_CASE(Struct, t, n->def))
		{
			for (auto& c: t->fields)
				if (c.expr)
					visitAstRec(f, c.expr);
		}
	}
}

void visitAst(Ast* node, const function<bool (Ast*)>& f)
{
	visitAstRec(f, node);
}

void visitAstInner(Ast* node, const function<bool (Ast*)>& f)
{
	visitAstRec(f, node, /* ignore= */ node);
}

void visitAstTypes(Ast* node, const function<void (Ty*)>& f)
{
	if (Ty* type = astType(node))
		f(type);

	if (UNION_CASE(Ident, n, node))
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

void visitType(Ty* type, const function<void (Ty*)>& f)
{
	f(type);

	if (UNION_CASE(Tuple, t, type))
	{
		for (auto& e: t->fields)
			visitType(e, f);
	}
	else if (UNION_CASE(Array, t, type))
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
