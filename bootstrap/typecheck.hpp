#pragma once

#include <string>
#include <vector>

#include "parser.hpp"
#include "type.hpp"

struct BindingTarget
{
	std::string name;

	BindingTarget(const std::string& name): name(name)
	{
	}
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
	Location location;

	Type* type;

	Expr(Type* type): type(type) {}
	virtual ~Expr() {}
};

struct ExprUnit: Expr
{
	ExprUnit(Type* type): Expr(type) {}
};

struct ExprLiteralNumber: Expr
{
	long long value;

	ExprLiteralNumber(Type* type, long long value): Expr(type), value(value) {}
};

struct ExprBinding: Expr
{
	BindingBase* binding;

	ExprBinding(Type* type, BindingBase* binding): Expr(type), binding(binding) {}
};

struct ExprUnaryOp: Expr
{
	SynUnaryOpType op;
	Expr* expr;

	ExprUnaryOp(Type* type, SynUnaryOpType op, Expr* expr): Expr(type), op(op), expr(expr) {}
};

struct ExprBinaryOp: Expr
{
	SynBinaryOpType op;
	Expr* left;
	Expr* right;

	ExprBinaryOp(Type* type, SynBinaryOpType op, Expr* left, Expr* right): Expr(type), op(op), left(left), right(right) {}
};

struct ExprCall: Expr
{
	Expr* expr;
	std::vector<Expr*> args;

	ExprCall(Type* type, Expr* expr, const std::vector<Expr*>& args): Expr(type), expr(expr), args(args)
	{
	}
};

struct ExprLetVar: Expr
{
	BindingTarget* target;
	Type* vartype;
	Expr* body;

	ExprLetVar(Type* type, BindingTarget* target, Type* vartype, Expr* body): Expr(type), target(target), vartype(vartype), body(body)
	{
	}
};

struct ExprLLVM: Expr
{
	std::string body;

	ExprLLVM(Type* type, const std::string& body): Expr(type), body(body)
	{
	}
};

struct ExprLetFunc: Expr
{
	BindingTarget* target;
	Type* funtype;
	Expr* body;

	ExprLetFunc(Type* type, BindingTarget* target, Type* funtype, Expr* body): Expr(type), target(target), funtype(funtype), body(body)
	{
	}
};

struct ExprIfThenElse: Expr
{
	Expr* cond;
	Expr* thenbody;
	Expr* elsebody;

	ExprIfThenElse(Type* type, Expr* cond, Expr* thenbody, Expr* elsebody): Expr(type), cond(cond), thenbody(thenbody), elsebody(elsebody)
	{
	}
};

struct ExprBlock: Expr
{
	std::vector<Expr*> expressions;

	ExprBlock(Type* type): Expr(type)
	{
	}
};

#ifndef CASE
#define CASE(type, node) type* _ = dynamic_cast<type*>(node)
#endif

Expr* typecheck(SynBase* root);
