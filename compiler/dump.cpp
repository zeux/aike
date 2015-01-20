#include "common.hpp"
#include "dump.hpp"

#include "ast.hpp"

static void dumpIndent(int indent)
{
	for (int i = 0; i < indent; ++i)
		printf("    ");
}

static void dumpString(const Str& s)
{
	printf("%.*s", int(s.size), s.data);
}

static void dumpNode(Ast* root, int indent)
{
	if (UNION_CASE(LiteralString, n, root))
	{
		printf("\"");
		dumpString(n->value);
		printf("\"");
	}
	else if (UNION_CASE(Ident, n, root))
	{
		dumpString(n->name);
	}
	else if (UNION_CASE(Block, n, root))
	{
		for (auto& c: n->body)
		{
			dumpIndent(indent);
			dumpNode(c, indent + 1);
			printf("\n");
		}
	}
	else if (UNION_CASE(Call, n, root))
	{
		dumpNode(n->expr, indent);

		printf("(");

		for (auto& c: n->arguments)
		{
			dumpNode(c, indent);
			printf(", ");
		}

		printf(")");
	}
	else if (UNION_CASE(FnDecl, n, root))
	{
		dumpIndent(indent);

		if (n->attributes & FnAttributeExtern)
			printf("extern ");

		printf("fn ");
		dumpString(n->name);
		printf("(");

		for (auto& arg: n->arguments)
		{
			dumpString(arg);
			printf(", ");
		}

		printf(")\n");

		if (n->body)
			dumpNode(n->body, indent + 1);
	}
}

void dump(Ast* root)
{
	dumpNode(root, 0);
}