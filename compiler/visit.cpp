#include "common.hpp"
#include "visit.hpp"

#include "ast.hpp"

static void visitAstStackless(Ast* root, function<bool (Ast*)> f, Ast* ignore = nullptr)
{
	vector<Ast*> stack;
	stack.push_back(root);

	while (!stack.empty())
	{
		Ast* node = stack.back();
		stack.pop_back();

		if (node != ignore && f(node))
			continue;

		size_t offset = stack.size();

		if (UNION_CASE(LiteralArray, n, node))
		{
			for (auto& c: n->elements)
				stack.push_back(c);
		}
		else if (UNION_CASE(LiteralStruct, n, node))
		{
			for (auto& c: n->fields)
				stack.push_back(c.second);
		}
		else if (UNION_CASE(Member, n, node))
		{
			stack.push_back(n->expr);
		}
		else if (UNION_CASE(Block, n, node))
		{
			for (auto& c: n->body)
				stack.push_back(c);
		}
		else if (UNION_CASE(Module, n, node))
		{
			stack.push_back(n->body);
		}
		else if (UNION_CASE(Call, n, node))
		{
			stack.push_back(n->expr);

			for (auto& a: n->args)
				stack.push_back(a);
		}
		else if (UNION_CASE(Unary, n, node))
		{
			stack.push_back(n->expr);
		}
		else if (UNION_CASE(Binary, n, node))
		{
			stack.push_back(n->left);
			stack.push_back(n->right);
		}
		else if (UNION_CASE(Index, n, node))
		{
			stack.push_back(n->expr);
			stack.push_back(n->index);
		}
		else if (UNION_CASE(Assign, n, node))
		{
			stack.push_back(n->left);
			stack.push_back(n->right);
		}
		else if (UNION_CASE(If, n, node))
		{
			stack.push_back(n->cond);
			stack.push_back(n->thenbody);

			if (n->elsebody)
				stack.push_back(n->elsebody);
		}
		else if (UNION_CASE(For, n, node))
		{
			stack.push_back(n->expr);
			stack.push_back(n->body);
		}
		else if (UNION_CASE(While, n, node))
		{
			stack.push_back(n->expr);
			stack.push_back(n->body);
		}
		else if (UNION_CASE(FnDecl, n, node))
		{
			if (n->body)
				stack.push_back(n->body);
		}
		else if (UNION_CASE(Fn, n, node))
		{
			stack.push_back(n->decl);
		}
		else if (UNION_CASE(VarDecl, n, node))
		{
			stack.push_back(n->expr);
		}
		else if (UNION_CASE(TyDecl, n, node))
		{
			if (UNION_CASE(Struct, t, n->def))
			{
				for (auto& c: t->fields)
					if (c.expr)
						stack.push_back(c.expr);
			}
		}

		reverse(stack.begin() + offset, stack.end());
	}
}

void visitAst(Ast* node, function<bool (Ast*)> f)
{
	visitAstStackless(node, f);
}

void visitAstInner(Ast* node, function<bool (Ast*)> f)
{
	visitAstStackless(node, f, /* ignore= */ node);
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
