#pragma once

#include <string>
#include <vector>

#include "location.hpp"

struct SynIdentifier
{
	std::string name;
	Location location;

	SynIdentifier()
	{
	}

	SynIdentifier(const std::string& name, const Location& location): name(name), location(location)
	{
	}
};

struct SynType
{
	virtual ~SynType(){ }
};

struct SynTypeBasic: SynType
{
	SynIdentifier type;

	SynTypeBasic(const SynIdentifier& type): type(type)
	{
	}
};

struct SynTypeFunction: SynType
{
	std::vector<SynType*> argument_types;
	SynType* return_type;

	SynTypeFunction(const std::vector<SynType*>& argument_types, SynType* return_type): argument_types(argument_types), return_type(return_type)
	{
	}
};

struct SynTypedVar
{
	SynIdentifier name;
	SynType* type;

	SynTypedVar(const SynIdentifier& name, SynType* type): name(name), type(type)
	{
	}
};

struct SynBase
{
	Location location;

	SynBase(const Location& location): location(location) {}
	virtual ~SynBase() {}
};

struct SynUnit: SynBase
{
	SynUnit(const Location& location): SynBase(location) {}
};

struct SynLiteralNumber: SynBase
{
	long long value;

	SynLiteralNumber(const Location& location, long long value): SynBase(location), value(value) {}
};

struct SynVariableReference: SynBase
{
	std::string name;

	SynVariableReference(const Location& location, const std::string& name): SynBase(location), name(name) {}
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

	SynUnaryOp(const Location& location, SynUnaryOpType op, SynBase* expr): SynBase(location), op(op), expr(expr) {}
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

	SynBinaryOp(const Location& location, SynBinaryOpType op, SynBase* left, SynBase* right): SynBase(location), op(op), left(left), right(right) {}
};

struct SynCall: SynBase
{
	SynBase* expr;
	std::vector<SynBase*> args;

	SynCall(const Location& location, SynBase* expr, const std::vector<SynBase*>& args): SynBase(location), expr(expr), args(args)
	{
	}
};

struct SynLetVar: SynBase
{
	SynTypedVar var;
	SynBase* body;

	SynLetVar(const Location& location, const SynTypedVar& var, SynBase* body): SynBase(location), var(var), body(body)
	{
	}
};

struct SynLLVM: SynBase
{
	std::string body;

	SynLLVM(const Location& location, const std::string& body): SynBase(location), body(body)
	{
	}
};

struct SynLetFunc: SynBase
{
	SynTypedVar var;
	std::vector<SynTypedVar> args;
	SynBase* body;

	SynLetFunc(const Location& location, const SynTypedVar& var, const std::vector<SynTypedVar>& args, SynBase* body): SynBase(location), var(var), args(args), body(body)
	{
	}
};

struct SynIfThenElse: SynBase
{
	SynBase* cond;
	SynBase* thenbody;
	SynBase* elsebody;

	SynIfThenElse(const Location& location, SynBase* cond, SynBase* thenbody, SynBase* elsebody): SynBase(location), cond(cond), thenbody(thenbody), elsebody(elsebody)
	{
	}
};

struct SynBlock: SynBase
{
	std::vector<SynBase*> expressions;

	SynBlock(const Location& location, const std::vector<SynBase*> expressions): SynBase(location), expressions(expressions)
	{
	}
};

#ifndef CASE
#define CASE(type, node) type* _ = dynamic_cast<type*>(node)
#endif

struct Lexer;

SynBase* parse(Lexer& lexer);
