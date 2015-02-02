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
	printf("%s", s.str().c_str());
}

template <typename T, typename F> static void dumpList(const T& list, F pred)
{
	const char* sep = "";

	for (auto& e: list)
	{
		printf("%s", sep);
		pred(e);
		sep = ", ";
	}
}

static void dumpNode(Ast* root, int indent)
{
	if (UNION_CASE(LiteralBool, n, root))
	{
		printf("%s", n->value ? "true" : "false");
	}
	else if (UNION_CASE(LiteralNumber, n, root))
	{
		dumpString(n->value);
	}
	else if (UNION_CASE(LiteralString, n, root))
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
			dumpNode(c, indent);
			printf("\n");
		}
	}
	else if (UNION_CASE(Call, n, root))
	{
		dumpNode(n->expr, indent);

		printf("(");
		dumpList(n->args, [&](Ast* c) { dumpNode(c, indent); });
		printf(")");
	}
	else if (UNION_CASE(If, n, root))
	{
		printf("if ");
		dumpNode(n->cond, indent);
		printf("\n");
		dumpNode(n->thenbody, indent + 1);

		if (n->elsebody)
		{
			printf("\n");
			dumpIndent(indent);
			printf("else\n");
			dumpNode(n->elsebody, indent + 1);
		}
	}
	else if (UNION_CASE(FnDecl, n, root))
	{
		if (n->attributes & FnAttributeExtern)
			printf("extern ");

		printf("fn ");
		dumpString(n->var->name);
		printf("(");
		dumpList(n->args, [&](Variable* v) { dumpString(v->name); printf(": "); dump(v->type); });
		printf("): ");

		if (UNION_CASE(Function, f, n->var->type))
			dump(f->ret);
		else
			ICE("FnDecl type is not Function");

		printf("\n");

		if (n->body)
			dumpNode(n->body, indent + 1);
	}
	else if (UNION_CASE(VarDecl, n, root))
	{
		printf("var ");
		dumpString(n->var->name);
		printf(": ");
		dump(n->var->type);
		printf(" = ");
		dump(n->expr);
	}
	else
	{
		ICE("Unknown Ast kind %d", root->kind);
	}
}

void dump(Ty* type)
{
	printf("%s", typeName(type).c_str());
}

void dump(Ast* root)
{
	dumpNode(root, 0);
}