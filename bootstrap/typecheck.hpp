#pragma once

#include <string>
#include <vector>

#include "parser.hpp"
#include "type.hpp"

struct ExprTypedVar
{
	std::string name;
	std::string type;

	ExprTypedVar(const std::string& name, const std::string& type): name(name), type(type)
	{
	}
};

struct Expr
{
	virtual ~Expr() {}
};

struct ExprUnit: Expr
{
	ExprUnit() {}
};

struct ExprLiteralNumber: Expr
{
	long long value;

	ExprLiteralNumber(long long value): value(value) {}
};

struct ExprVariableReference: Expr
{
	std::string name;

	ExprVariableReference(const std::string& name): name(name) {}
};

struct ExprUnaryOp: Expr
{
	SynUnaryOpType op;
	Expr* expr;

	ExprUnaryOp(SynUnaryOpType op, Expr* expr): op(op), expr(expr) {}
};

struct ExprBinaryOp: Expr
{
	SynBinaryOpType op;
	Expr* left;
	Expr* right;

	ExprBinaryOp(SynBinaryOpType op, Expr* left, Expr* right): op(op), left(left), right(right) {}
};

struct ExprCall: Expr
{
	Expr* expr;
	std::vector<Expr*> args;

	ExprCall(Expr* expr, const std::vector<Expr*>& args): expr(expr), args(args)
	{
	}
};

struct ExprLetVar: Expr
{
	ExprTypedVar var;
	Expr* body;
	Expr* expr;

	ExprLetVar(const ExprTypedVar& var, Expr* body, Expr* expr): var(var), body(body), expr(expr)
	{
	}
};

struct ExprLLVM: Expr
{
	std::string body;

	ExprLLVM(const std::string& body): body(body)
	{
	}
};

struct ExprLetFunc: Expr
{
	ExprTypedVar var;
	std::vector<ExprTypedVar> args;
	Expr* body;
	Expr* expr;

	ExprLetFunc(const ExprTypedVar& var, const std::vector<ExprTypedVar>& args, Expr* body, Expr* expr): var(var), args(args), body(body), expr(expr)
	{
	}
};

struct ExprIfThenElse: Expr
{
	Expr* cond;
	Expr* thenbody;
	Expr* elsebody;

	ExprIfThenElse(Expr* cond, Expr* thenbody, Expr* elsebody): cond(cond), thenbody(thenbody), elsebody(elsebody) {}
};

#ifndef CASE
#define CASE(type, node) type* _ = dynamic_cast<type*>(node)
#endif

Expr* typecheck(SynBase* root);
