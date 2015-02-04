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
	X(LiteralBool, { bool value; Location location; }) \
	X(LiteralNumber, { Str value; Location location; }) \
	X(LiteralString, { Str value; Location location; }) \
	X(LiteralStruct, { Str name; Location location; Ty* type; Array<pair<Str, Ast*>> fields; }) \
	X(Ident, { Str name; Location location; Variable* target; }) \
	X(Block, { Array<Ast*> body; }) \
	X(Call, { Ast* expr; Array<Ast*> args; Location location; }) \
	X(If, { Ast* cond; Ast* thenbody; Ast* elsebody; }) \
	X(Fn, { Ty* type; Location location; Array<Variable*> args; Ast* body; }) \
	X(FnDecl, { Variable* var; Array<Variable*> args; unsigned attributes; Ast* body; }) \
	X(VarDecl, { Variable* var; Ast* expr; }) \
	X(TyDecl, { Str name; Location location; TyDef* def; })

UNION_DECL(Ast, UD_AST)

template <typename F, typename FC> inline void visitAst(Ast* node, F f, FC& fc);

template <typename F, typename FC> inline void visitAstInner(Ast* node, F f, FC& fc)
{
	if (UNION_CASE(LiteralStruct, n, node))
	{
		for (auto& c: n->fields)
			visitAst(c.second, f, fc);
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