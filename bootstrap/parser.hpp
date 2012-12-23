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

	SynVariableReference(const std::string& name): name(name) {}
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
	SynUnaryOpType op;
	SynBase* expr;

	SynUnaryOp(SynUnaryOpType op, SynBase* expr): op(op), expr(expr) {}
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
	SynBinaryOpType op;
	SynBase* left;
	SynBase* right;

	SynBinaryOp(SynBinaryOpType op, SynBase* left, SynBase* right): op(op), left(left), right(right) {}
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

	SynLetVar(const SynTypedVar& var, SynBase* body): var(var), body(body)
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

	SynLetFunc(const SynTypedVar& var, const std::vector<SynTypedVar>& args, SynBase* body): var(var), args(args), body(body)
	{
	}
};

struct SynIfThenElse: SynBase
{
	SynBase* cond;
	SynBase* thenbody;
	SynBase* elsebody;

	SynIfThenElse(SynBase* cond, SynBase* thenbody, SynBase* elsebody): cond(cond), thenbody(thenbody), elsebody(elsebody)
	{
	}
};

struct SynBlock: SynBase
{
	std::vector<SynBase*> expressions;

	SynBlock(SynBase* head)
	{
		expressions.push_back(head);
	}
};

#ifndef CASE
#define CASE(type, node) type* _ = dynamic_cast<type*>(node)
#endif

struct Lexer;

SynBase* parse(Lexer& lexer);
