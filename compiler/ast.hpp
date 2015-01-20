#pragma once

enum FnAttribute
{
	FnAttributeExtern = 1 << 0,
};

#define UD_AST(X) \
	X(LiteralString, { Str value; }) \
	X(Ident, { Str name; }) \
	X(Block, { Array<Ast*> body; }) \
	X(Call, { Ast* expr; Array<Ast*> arguments; }) \
	X(FnDecl, { Str name; Array<Str> arguments; unsigned attributes; Ast* body; })

UNION_DECL(Ast, UD_AST)