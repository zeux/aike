#pragma once

#include <string>
#include <vector>

#include "parser.hpp"
#include "type.hpp"

struct BindingTarget
{
};

struct BindingBase
{
	virtual ~BindingBase() {}
};

struct BindingLet: BindingBase
{
	BindingTarget* target;

	BindingLet(BindingTarget* target): target(target) {}
};

struct BindingFunarg: BindingBase
{
	BindingTarget* target;
	size_t index;

	BindingFunarg(BindingTarget* target, size_t index): target(target), index(index) {}
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

struct ExprBinding: Expr
{
	BindingBase* binding;

	ExprBinding(BindingBase* binding): binding(binding) {}
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
	BindingTarget* target;
	Expr* body;
	Expr* expr;

	ExprLetVar(BindingTarget* target, Expr* body, Expr* expr): target(target), body(body), expr(expr)
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
	BindingTarget* target;
	Expr* body;
	Expr* expr;

	ExprLetFunc(Expr* body, Expr* expr): target(target), body(body), expr(expr)
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
