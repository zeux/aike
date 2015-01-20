#pragma once

#define UD_AST(X) \
	X(String, { Str value; }) \
	X(Ident, { Str name; })

UNION_DECL(Ast, UD_AST)