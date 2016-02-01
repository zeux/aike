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

static void dumpCommon(Ast::Common* n, int indent)
{
}

static void dumpLiteralVoid(Ast::LiteralVoid* n, int indent)
{
	printf("()");
}

static void dumpLiteralBool(Ast::LiteralBool* n, int indent)
{
	printf("%s", n->value ? "true" : "false");
}

static void dumpLiteralInteger(Ast::LiteralInteger* n, int indent)
{
	printf("%lld", n->value);
}

static void dumpLiteralFloat(Ast::LiteralFloat* n, int indent)
{
	printf("%f", n->value);
}

static void dumpLiteralString(Ast::LiteralString* n, int indent)
{
	printf("\"");
	dumpString(n->value);
	printf("\"");
}

static void dumpLiteralTuple(Ast::LiteralTuple* n, int indent)
{
	printf("(");
	dumpList(n->fields, [&](Ast* c) { dumpNode(c, indent); });
	printf(")");
}

static void dumpLiteralArray(Ast::LiteralArray* n, int indent)
{
	printf("[");
	dumpList(n->elements, [&](Ast* c) { dumpNode(c, indent); });
	printf("]");
}

static void dumpLiteralStruct(Ast::LiteralStruct* n, int indent)
{
	dumpString(n->name);
	printf(" { ");
	dumpList(n->fields, [&](const pair<FieldRef, Ast*>& p) { dumpString(p.first.name); printf(" = "); dumpNode(p.second, indent); });
	printf(" }");
}

static void dumpIdent(Ast::Ident* n, int indent)
{
	dumpString(n->name);
	dumpTypeArguments(n->tyargs);
}

static void dumpMember(Ast::Member* n, int indent)
{
	dumpNode(n->expr, indent);
	printf(".");
	dumpString(n->field.name);
}

static void dumpBlock(Ast::Block* n, int indent)
{
	for (auto& c: n->body)
	{
		dumpIndent(indent);
		dumpNode(c, indent);
		printf("\n");
	}
}

static void dumpModule(Ast::Module* n, int indent)
{
	dumpNode(n->body, indent);
}

static void dumpCall(Ast::Call* n, int indent)
{
	dumpNode(n->expr, indent);

	printf("(");
	dumpList(n->args, [&](Ast* c) { dumpNode(c, indent); });
	printf(")");
}

static void dumpUnary(Ast::Unary* n, int indent)
{
	printf("(%s ", getOpName(n->op));
	dumpNode(n->expr, indent);
	printf(")");
}

static void dumpBinary(Ast::Binary* n, int indent)
{
	printf("(");
	dumpNode(n->left, indent);
	printf(" %s ", getOpName(n->op));
	dumpNode(n->right, indent);
	printf(")");
}

static void dumpIndex(Ast::Index* n, int indent)
{
	dumpNode(n->expr, indent);
	printf("[");
	dumpNode(n->index, indent);
	printf("]");
}

static void dumpAssign(Ast::Assign* n, int indent)
{
	dumpNode(n->left, indent);
	printf(" = ");
	dumpNode(n->right, indent);
}

static void dumpIf(Ast::If* n, int indent)
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

static void dumpFor(Ast::For* n, int indent)
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

static void dumpWhile(Ast::While* n, int indent)
{
	printf("while ");
	dumpNode(n->expr, indent);
	printf("\n");
	dumpNode(n->body, indent + 1);
}

static void dumpFn(Ast::Fn* n, int indent)
{
	dumpNode(n->decl, indent);
}

static void dumpLLVM(Ast::LLVM* n, int indent)
{
	dumpIndent(indent);
	printf("llvm \"");
	dumpString(n->code);
	printf("\"");
}

static void dumpFnDecl(Ast::FnDecl* n, int indent)
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

static void dumpVarDecl(Ast::VarDecl* n, int indent)
{
	printf("var ");
	dumpString(n->var->name);
	printf(": ");
	dump(n->var->type);
	printf(" = ");
	dump(n->expr);
}

static void dumpTyDecl(Ast::TyDecl* n, int indent)
{
	dumpDef(n->name, n->def, indent);
}

static void dumpImport(Ast::Import* n, int indent)
{
	printf("import ");
	dumpString(n->name);
	printf("\n");
}

static void dumpNode(Ast* root, int indent)
{
#define CALL(name, ...) else if (UNION_CASE(name, n, root)) dump##name(n, indent);

	if (false) ;
	UD_AST(CALL)
	else ICE("Unknown Ast kind %d", root->kind);

#undef CALL
}

void dump(Ty* type)
{
	printf("%s", typeName(type).c_str());
}

void dump(Ast* root)
{
	dumpNode(root, 0);
}
