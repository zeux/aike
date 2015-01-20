#include "common.hpp"
#include "dump.hpp"

#include "ast.hpp"

void dump(struct Ast* root)
{
	if (UNION_CASE(String, node, root))
	{
		printf("string %.*s\n", int(node->value.size), node->value.data);
	}

	if (UNION_CASE(Ident, node, root))
	{
		printf("ident %.*s\n", int(node->name.size), node->name.data);
	}
}