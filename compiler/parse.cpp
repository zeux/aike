#include "common.hpp"
#include "parse.hpp"

#include "ast.hpp"
#include "tokenize.hpp"

Ast* parse(const Tokens& tokens)
{
	return UNION_NEW(Ast, String, Str(""));
}