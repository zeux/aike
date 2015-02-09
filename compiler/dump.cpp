#include "common.hpp"
#include "dump.hpp"

#include "ast.hpp"

static const char* getOpName(UnaryOp op)
{
	switch (op)
	{
		case UnaryOpPlus: return "+";
		case UnaryOpMinus: return "-";
		case UnaryOpNot: return "not";
		default: ICE("Unknown UnaryOp %d", op);
	}
}

static const char* getOpName(BinaryOp op)
{
	switch (op)
	{
		case BinaryOpAddWrap: return "+%";
		case BinaryOpSubtractWrap: return "-%";
		case BinaryOpMultiplyWrap: return "*%";
		case BinaryOpAdd: return "+";
		case BinaryOpSubtract: return "-";
		case BinaryOpMultiply: return "*";
		case BinaryOpDivide: return "/";
		case BinaryOpModulo: return "%";
		case BinaryOpLess: return "<";
		case BinaryOpLessEqual: return "<=";
		case BinaryOpGreater: return ">";
		case BinaryOpGreaterEqual: return ">=";
		case BinaryOpEqual: return "==";
		case BinaryOpNotEqual: return "!=";
		case BinaryOpAnd: return "and";
		case BinaryOpOr: return "or";
		default: ICE("Unknown BinaryOp %d", op);
	}
}

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

static void dumpSignature(Ty* ty, const Array<Variable*>& args)
{
	printf("(");
	dumpList(args, [&](Variable* v) { dumpString(v->name); printf(": "); dump(v->type); });
	printf("): ");

	if (UNION_CASE(Function, f, ty))
		dump(f->ret);
	else
		ICE("Fn type is not Function");
}

static void dumpDef(const Str& name, TyDef* def, int indent)
{
	if (UNION_CASE(Struct, sd, def))
	{
		printf("struct ");
		dumpString(name);
		printf("\n");

		for (auto& f: sd->fields)
		{
			dumpIndent(indent + 1);
			dumpString(f.first);
			printf(": ");
			dump(f.second);
			printf("\n");
		}
	}
	else
		ICE("Unknown TyDef kind %d", def->kind);
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
	else if (UNION_CASE(LiteralStruct, n, root))
	{
		dumpString(n->name);
		printf(" { ");
		dumpList(n->fields, [&](const pair<Field, Ast*>& p) { dumpString(p.first.name); printf(" = "); dumpNode(p.second, indent); });
		printf(" }");
	}
	else if (UNION_CASE(Ident, n, root))
	{
		dumpString(n->name);
	}
	else if (UNION_CASE(Member, n, root))
	{
		dumpNode(n->expr, indent);
		printf(".");
		dumpString(n->field.name);
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
	else if (UNION_CASE(Unary, n, root))
	{
		printf("(%s ", getOpName(n->op));
		dumpNode(n->expr, indent);
		printf(")");
	}
	else if (UNION_CASE(Binary, n, root))
	{
		printf("(");
		dumpNode(n->left, indent);
		printf(" %s ", getOpName(n->op));
		dumpNode(n->right, indent);
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
	else if (UNION_CASE(Fn, n, root))
	{
		printf("fn ");
		dumpSignature(n->type, n->args);
		printf("\n");

		if (n->body)
			dumpNode(n->body, indent + 1);
	}
	else if (UNION_CASE(FnDecl, n, root))
	{
		if (n->attributes & FnAttributeExtern)
			printf("extern ");

		printf("fn ");
		dumpString(n->var->name);
		dumpSignature(n->var->type, n->args);
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
	else if (UNION_CASE(TyDecl, n, root))
	{
		dumpDef(n->name, n->def, indent);
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