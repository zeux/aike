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
	X(LiteralString, { Str value; }) \
	X(Ident, { Str name; Location location; Variable* target; }) \
	X(Block, { Array<Ast*> body; }) \
	X(Call, { Ast* expr; Array<Ast*> args; }) \
	X(FnDecl, { Variable* var; Array<Variable*> args; unsigned attributes; Ast* body; }) \
	X(VarDecl, { Variable* var; Ast* expr; })

UNION_DECL(Ast, UD_AST)
