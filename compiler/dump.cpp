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
		dumpString(n->value);
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
			dumpString(arg.first);
			printf(": ");
			dump(arg.second);
			printf(", ");
		}

		printf(")\n");

		if (n->body)
			dumpNode(n->body, indent + 1);
	}
	else
	{
		assert(false);
	}
}

void dump(Ty* type)
{
	if (UNION_CASE(String, t, type))
		printf("string");
	else if (UNION_CASE(Unknown, t, type))
		printf("_");
	else
		assert(false);
}

void dump(Ast* root)
{
	dumpNode(root, 0);
}