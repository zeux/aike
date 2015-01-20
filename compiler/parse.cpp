#include "common.hpp"
#include "parse.hpp"

#include "ast.hpp"
#include "dump.hpp"

void parse()
{
	auto f = UNION_MAKE(Ast, String, Str("foo"));
	auto g = UNION_NEW(Ast, Ident, Str("foo"));

	dump(&f);
	dump(g);
}