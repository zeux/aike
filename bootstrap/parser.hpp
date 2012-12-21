#pragma once

#include <string>
#include <vector>

struct SynTypedVar
{
	std::string name;
	std::string type;

	SynTypedVar(const std::string& name, const std::string& type): name(name), type(type)
	{
	}
};

struct SynBase
{
	virtual ~SynBase() {}
};

struct SynUnit: SynBase
{
	SynUnit() {}
};

struct SynLiteralNumber: SynBase
{
	long long value;

	SynLiteralNumber(long long value): value(value) {}
};

struct SynVariableReference: SynBase
{
	std::string name;

	SynVariableReference(std::string name): name(name) {}
};

enum SynUnaryOpType
{
	SynUnaryOpUnknown,
	SynUnaryOpPlus,
	SynUnaryOpMinus,
	SynUnaryOpNot
};

struct SynUnaryOp: SynBase
{
	SynUnaryOpType type;
	SynBase* expr;

	SynUnaryOp(SynUnaryOpType type, SynBase* expr): type(type), expr(expr) {}
};

enum SynBinaryOpType
{
	SynBinaryOpUnknown,

	SynBinaryOpAdd,
	SynBinaryOpSubtract,
	SynBinaryOpMultiply,
	SynBinaryOpDivide,
	SynBinaryOpLess,
	SynBinaryOpLessEqual,
	SynBinaryOpGreater,
	SynBinaryOpGreaterEqual,
	SynBinaryOpEqual,
	SynBinaryOpNotEqual
};

struct SynBinaryOp: SynBase
{
	SynBinaryOpType type;
	SynBase* left;
	SynBase* right;

	SynBinaryOp(SynBinaryOpType type, SynBase* left, SynBase* right): type(type), left(left), right(right) {}
};

struct SynCall: SynBase
{
	SynBase* expr;
	std::vector<SynBase*> args;

	SynCall(SynBase* expr, const std::vector<SynBase*>& args): expr(expr), args(args)
	{
	}
};

struct SynLetVar: SynBase
{
	SynTypedVar var;
	SynBase* body;
	SynBase* expr;

	SynLetVar(const SynTypedVar& var, SynBase* body, SynBase* expr): var(var), body(body), expr(expr)
	{
	}
};

struct SynLLVM: SynBase
{
	std::string body;

	SynLLVM(const std::string& body): body(body)
	{
	}
};

struct SynLetFunc: SynBase
{
	SynTypedVar var;
	std::vector<SynTypedVar> args;
	SynBase* body;
	SynBase* expr;

	SynLetFunc(const SynTypedVar& var, const std::vector<SynTypedVar>& args, SynBase* body, SynBase* expr): var(var), args(args), body(body), expr(expr)
	{
	}
};

struct SynIfThenElse: SynBase
{
	SynBase* cond;
	SynBase* thenbody;
	SynBase* elsebody;

	SynIfThenElse(SynBase* cond, SynBase* thenbody, SynBase* elsebody): cond(cond), thenbody(thenbody), elsebody(elsebody) {}
};

#define CASE(type, node) type* _ = dynamic_cast<type*>(node)

struct Lexer;

SynBase* parse(Lexer& lexer);
