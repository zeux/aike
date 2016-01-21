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
		case UnaryOpDeref: return "*";
		case UnaryOpNew: return "new";
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

static void dumpTypeSignature(const Arr<Ty*>& args)
{
	if (args.size == 0)
		return;

	printf("<");
	dumpList(args, [&](Ty* t) { dump(t); });
	printf(">");
}

static void dumpFunctionSignature(Ty* ty, const Arr<Variable*>& args)
{
	UNION_CASE(Function, f, ty);
	assert(f);

	printf("(");
	dumpList(args, [&](Variable* v) { dumpString(v->name); printf(": "); dump(v->type); });

	if (f->varargs)
	{
		if (args.size != 0) printf(", ");
		printf("...");
	}

	printf("): ");
	dump(f->ret);
}

static void dumpTypeArguments(const Arr<Ty*>& args)
{
	if (args.size == 0)
		return;

	printf(".");
	dumpTypeSignature(args);
}

static void dumpNode(Ast* root, int indent);

static void dumpDef(const Str& name, TyDef* def, int indent)
{
	if (UNION_CASE(Struct, sd, def))
	{
		printf("struct ");
		dumpString(name);
		dumpTypeSignature(sd->tyargs);
		printf("\n");

		for (auto& f: sd->fields)
		{
			dumpIndent(indent + 1);
			dumpString(f.name);
			printf(": ");
			dump(f.type);

			if (f.expr)
			{
				printf(" = ");
				dumpNode(f.expr, indent);
			}

			printf("\n");
		}
	}
	else
		ICE("Unknown TyDef kind %d", def->kind);
}

static void dumpNode(Ast* root, int indent)
{
	if (UNION_CASE(LiteralVoid, n, root))
	{
		printf("()");
	}
	else if (UNION_CASE(LiteralBool, n, root))
	{
		printf("%s", n->value ? "true" : "false");
	}
	else if (UNION_CASE(LiteralInteger, n, root))
	{
		printf("%lld", n->value);
	}
	else if (UNION_CASE(LiteralFloat, n, root))
	{
		printf("%f", n->value);
	}
	else if (UNION_CASE(LiteralString, n, root))
	{
		printf("\"");
		dumpString(n->value);
		printf("\"");
	}
	else if (UNION_CASE(LiteralArray, n, root))
	{
		printf("[");
		dumpList(n->elements, [&](Ast* c) { dumpNode(c, indent); });
		printf("]");
	}
	else if (UNION_CASE(LiteralStruct, n, root))
	{
		dumpString(n->name);
		printf(" { ");
		dumpList(n->fields, [&](const pair<FieldRef, Ast*>& p) { dumpString(p.first.name); printf(" = "); dumpNode(p.second, indent); });
		printf(" }");
	}
	else if (UNION_CASE(Ident, n, root))
	{
		dumpString(n->name);
		dumpTypeArguments(n->tyargs);
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
	else if (UNION_CASE(Module, n, root))
	{
		dumpNode(n->body, indent);
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
	else if (UNION_CASE(Index, n, root))
	{
		dumpNode(n->expr, indent);
		printf("[");
		dumpNode(n->index, indent);
		printf("]");
	}
	else if (UNION_CASE(Assign, n, root))
	{
		dumpNode(n->left, indent);
		printf(" = ");
		dumpNode(n->right, indent);
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
	else if (UNION_CASE(For, n, root))
	{
		printf("for ");
		dumpString(n->var->name);

		if (n->index)
		{
			printf(", ");
			dumpString(n->index->name);
		}

		printf(" in ");
		dumpNode(n->expr, indent);
		printf("\n");
		dumpNode(n->body, indent + 1);
	}
	else if (UNION_CASE(While, n, root))
	{
		printf("while ");
		dumpNode(n->expr, indent);
		printf("\n");
		dumpNode(n->body, indent + 1);
	}
	else if (UNION_CASE(Fn, n, root))
	{
		dumpNode(n->decl, indent);
	}
	else if (UNION_CASE(LLVM, n, root))
	{
		dumpIndent(indent);
		printf("llvm \"");
		dumpString(n->code);
		printf("\"");
	}
	else if (UNION_CASE(FnDecl, n, root))
	{
		if (n->attributes & FnAttributeExtern)
			printf("extern ");

		printf("fn ");
		dumpString(n->var->name);
		dumpTypeSignature(n->tyargs);
		dumpFunctionSignature(n->var->type, n->args);
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
	else if (UNION_CASE(Import, n, root))
	{
		printf("import ");
		dumpString(n->name);
		printf("\n");
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