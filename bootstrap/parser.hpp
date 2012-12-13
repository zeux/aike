#pragma once

#include <string>
#include <vector>

struct AstTypedVar
{
	std::string name;
	std::string type;

	AstTypedVar(const std::string& name, const std::string& type): name(name), type(type)
	{
	}
};

struct AstBase
{
	virtual ~AstBase() {}
};

struct AstLiteralNumber: AstBase
{
	long long value;

	AstLiteralNumber(long long value): value(value) {}
};

struct AstVariableReference: AstBase
{
	std::string name;

	AstVariableReference(std::string name): name(name) {}
};

enum AstUnaryOpType
{
	AstUnaryOpUnknown,
	AstUnaryOpPlus,
	AstUnaryOpMinus,
	AstUnaryOpNot
};

struct AstUnaryOp: AstBase
{
	AstUnaryOpType type;
	AstBase* expr;

	AstUnaryOp(AstUnaryOpType type, AstBase* expr): type(type), expr(expr) {}
};

enum AstBinaryOpType
{
	AstBinaryOpUnknown,

	AstBinaryOpAdd,
	AstBinaryOpSubtract,
	AstBinaryOpMultiply,
	AstBinaryOpDivide,
	AstBinaryOpLess,
	AstBinaryOpLessEqual,
	AstBinaryOpGreater,
	AstBinaryOpGreaterEqual,
	AstBinaryOpEqual,
	AstBinaryOpNotEqual
};

struct AstBinaryOp: AstBase
{
	AstBinaryOpType type;
	AstBase* left;
	AstBase* right;

	AstBinaryOp(AstBinaryOpType type, AstBase* left, AstBase* right): type(type), left(left), right(right) {}
};

struct AstCall: AstBase
{
	AstBase* expr;
	std::vector<AstBase*> args;

	AstCall(AstBase* expr, const std::vector<AstBase*>& args): expr(expr), args(args)
	{
	}
};

struct AstLetVar: AstBase
{
	AstTypedVar var;
	AstBase* body;
	AstBase* expr;

	AstLetVar(const AstTypedVar& var, AstBase* body, AstBase* expr): var(var), body(body), expr(expr)
	{
	}
};

struct AstLetFunc: AstBase
{
	AstTypedVar var;
	std::vector<AstTypedVar> args;
	AstBase* body;
	AstBase* expr;

	AstLetFunc(const AstTypedVar& var, const std::vector<AstTypedVar>& args, AstBase* body, AstBase* expr): var(var), args(args), body(body), expr(expr)
	{
	}
};

#define ASTCASE(type, node) type* _ = dynamic_cast<type*>(node)

struct Lexer;

AstBase* parse(Lexer& lexer);