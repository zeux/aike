#pragma once

#include <string>
#include <vector>

#include "parser.hpp"
#include "type.hpp"

struct BindingTarget
{
	std::string name;
	Type* type;

	BindingTarget(const std::string& name, Type* type): name(name), type(type)
	{
	}
};

struct BindingBase
{
	virtual ~BindingBase() {}
};

struct BindingLocal: BindingBase
{
	BindingTarget* target;

	BindingLocal(BindingTarget* target): target(target) {}
};

struct Expr
{
	Type* type;
	Location location;

	Expr(Type* type, const Location& location): type(type), location(location) {}
	virtual ~Expr() {}
};

struct ExprUnit: Expr
{
	ExprUnit(Type* type, const Location& location): Expr(type, location) {}
};

struct ExprLiteralNumber: Expr
{
	long long value;

	ExprLiteralNumber(Type* type, const Location& location, long long value): Expr(type, location), value(value) {}
};

struct ExprArray: Expr
{
	std::vector<Expr*> elements;

	ExprArray(Type* type, const Location& location, const std::vector<Expr*>& elements): Expr(type, location), elements(elements) {}
};

struct ExprBinding: Expr
{
	BindingBase* binding;

	ExprBinding(Type* type, const Location& location, BindingBase* binding): Expr(type, location), binding(binding) {}
};

struct ExprUnaryOp: Expr
{
	SynUnaryOpType op;
	Expr* expr;

	ExprUnaryOp(Type* type, const Location& location, SynUnaryOpType op, Expr* expr): Expr(type, location), op(op), expr(expr) {}
};

struct ExprBinaryOp: Expr
{
	SynBinaryOpType op;
	Expr* left;
	Expr* right;

	ExprBinaryOp(Type* type, const Location& location, SynBinaryOpType op, Expr* left, Expr* right): Expr(type, location), op(op), left(left), right(right) {}
};

struct ExprCall: Expr
{
	Expr* expr;
	std::vector<Expr*> args;

	ExprCall(Type* type, const Location& location, Expr* expr, const std::vector<Expr*>& args): Expr(type, location), expr(expr), args(args)
	{
	}
};

struct ExprArrayIndex: Expr
{
	Expr* arr;
	Expr* index;

	ExprArrayIndex(Type* type, const Location& location, Expr* arr, Expr* index): Expr(type, location), arr(arr), index(index)
	{
	}
};

struct ExprLetVar: Expr
{
	BindingTarget* target;
	Expr* body;

	ExprLetVar(Type* type, const Location& location, BindingTarget* target, Expr* body): Expr(type, location), target(target), body(body)
	{
	}
};

struct ExprLetFunc: Expr
{
	BindingTarget* target;
	std::vector<BindingTarget*> args;
	Expr* body;

	ExprLetFunc(Type* type, const Location& location, BindingTarget* target, std::vector<BindingTarget*> args, Expr* body): Expr(type, location), target(target), args(args), body(body)
	{
	}
};

struct ExprExternFunc: Expr
{
	BindingTarget* target;
	std::vector<BindingTarget*> args;

	ExprExternFunc(Type* type, const Location& location, BindingTarget* target, std::vector<BindingTarget*> args): Expr(type, location), target(target), args(args)
	{
	}
};

struct ExprLLVM: Expr
{
	std::string body;

	ExprLLVM(Type* type, const Location& location, const std::string& body): Expr(type, location), body(body)
	{
	}
};

struct ExprIfThenElse: Expr
{
	Expr* cond;
	Expr* thenbody;
	Expr* elsebody;

	ExprIfThenElse(Type* type, const Location& location, Expr* cond, Expr* thenbody, Expr* elsebody): Expr(type, location), cond(cond), thenbody(thenbody), elsebody(elsebody)
	{
	}
};

struct ExprBlock: Expr
{
	std::vector<Expr*> expressions;

	ExprBlock(Type* type, const Location& location): Expr(type, location)
	{
	}
};

#ifndef CASE
#define CASE(type, node) type* _ = dynamic_cast<type*>(node)
#endif

Expr* typecheck(SynBase* root);
