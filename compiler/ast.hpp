#pragma once

#include "location.hpp"
#include "type.hpp"

struct Variable
{
	Str name;
	Ty* type;
	Location location;
};

enum FnAttribute
{
	FnAttributeExtern = 1 << 0,
};

#define UD_AST(X) \
	X(LiteralString, { Str value; Location location; }) \
	X(LiteralNumber, { Str value; Location location; }) \
	X(Ident, { Str name; Location location; Variable* target; }) \
	X(Block, { Array<Ast*> body; }) \
	X(Call, { Ast* expr; Array<Ast*> args; Location location; }) \
	X(FnDecl, { Variable* var; Array<Variable*> args; unsigned attributes; Ast* body; }) \
	X(VarDecl, { Variable* var; Ast* expr; })

UNION_DECL(Ast, UD_AST)

template <typename F, typename FC> inline void visitAst(Ast* node, F f, FC& fc);

template <typename F, typename FC> inline void visitAstInner(Ast* node, F f, FC& fc)
{
	if (UNION_CASE(Block, n, node))
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
	else if (UNION_CASE(FnDecl, n, node))
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