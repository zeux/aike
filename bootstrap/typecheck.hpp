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

struct BindingFunction: BindingLocal
{
	std::vector<std::string> arg_names;

	BindingFunction(BindingTarget* target, const std::vector<std::string>& arg_names): BindingLocal(target), arg_names(arg_names) {}
};

struct BindingFreeFunction: BindingFunction
{
	BindingFreeFunction(BindingTarget* target, const std::vector<std::string>& arg_names): BindingFunction(target, arg_names) {}
};

struct BindingUnionUnitConstructor: BindingFreeFunction
{
	BindingUnionUnitConstructor(BindingTarget* target, const std::vector<std::string>& arg_names): BindingFreeFunction(target, arg_names) {}
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

struct ExprBooleanLiteral: Expr
{
	bool value;

	ExprBooleanLiteral(Type* type, const Location& location, bool value): Expr(type, location), value(value) {}
};

struct ExprNumberLiteral: Expr
{
	long long value;

	ExprNumberLiteral(Type* type, const Location& location, long long value): Expr(type, location), value(value) {}
};

struct ExprArrayLiteral: Expr
{
	std::vector<Expr*> elements;

	ExprArrayLiteral(Type* type, const Location& location, const std::vector<Expr*>& elements): Expr(type, location), elements(elements) {}
};

struct ExprBinding: Expr
{
	BindingBase* binding;

	ExprBinding(Type* type, const Location& location, BindingBase* binding): Expr(type, location), binding(binding) {}
};

struct ExprBindingExternal: Expr
{
	BindingBase* context;
	std::string member_name;
	size_t member_index;

	ExprBindingExternal(Type* type, const Location& location, BindingBase* context, const std::string& member_name, size_t member_index): Expr(type, location), context(context), member_name(member_name), member_index(member_index) {}
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

struct ExprArraySlice: Expr
{
	Expr* arr;
	Expr* index_start;
	Expr* index_end;

	ExprArraySlice(Type* type, const Location& location, Expr* arr, Expr* index_start, Expr* index_end): Expr(type, location), arr(arr), index_start(index_start), index_end(index_end)
	{
	}
};

struct ExprMemberAccess: Expr
{
	Expr* aggr;
	std::string member_name;

	ExprMemberAccess(Type* type, const Location& location, Expr* aggr, const std::string& member_name): Expr(type, location), aggr(aggr), member_name(member_name)
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
	BindingTarget* context_target;
	std::vector<BindingTarget*> args;
	Expr* body;
	std::vector<Expr*> externals;

	ExprLetFunc(Type* type, const Location& location, BindingTarget* target, BindingTarget* context_target, const std::vector<BindingTarget*>& args, Expr* body, const std::vector<Expr*>& externals): Expr(type, location), target(target), context_target(context_target), args(args), body(body), externals(externals)
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

struct ExprStructConstructorFunc: Expr
{
	BindingTarget* target;
	std::vector<BindingTarget*> args;

	ExprStructConstructorFunc(Type* type, const Location& location, BindingTarget* target, std::vector<BindingTarget*> args): Expr(type, location), target(target), args(args)
	{
	}
};

struct ExprUnionConstructorFunc: Expr
{
	BindingTarget* target;
	std::vector<BindingTarget*> args;

	size_t member_id;
	Type* member_type;

	ExprUnionConstructorFunc(Type* type, const Location& location, BindingTarget* target, std::vector<BindingTarget*> args, size_t member_id, Type* member_type): Expr(type, location), target(target), args(args), member_id(member_id), member_type(member_type)
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

struct ExprForInDo: Expr
{
	BindingTarget* target;
	Expr* arr;
	Expr* body;

	ExprForInDo(Type* type, const Location& location, BindingTarget* target, Expr* arr, Expr* body): Expr(type, location), target(target), arr(arr), body(body)
	{
	}
};

struct MatchCase
{
	Type* type;
	Location location;

	MatchCase(Type* type, const Location& location): type(type), location(location) {}
	virtual ~MatchCase() {}
};

struct MatchCaseAny: MatchCase
{
	BindingTarget* alias;

	MatchCaseAny(Type* type, const Location& location, BindingTarget* alias): MatchCase(type, location), alias(alias)
	{
	}
};

struct MatchCaseNumber: MatchCase
{
	long long number;

	MatchCaseNumber(Type* type, const Location& location, long long number): MatchCase(type, location), number(number)
	{
	}
};

struct MatchCaseArray: MatchCase
{
	std::vector<MatchCase*> elements;

	MatchCaseArray(Type* type, const Location& location, const std::vector<MatchCase*>& elements): MatchCase(type, location), elements(elements)
	{
	}
};

struct MatchCaseMembers: MatchCase
{
	std::vector<std::string> member_names;
	std::vector<MatchCase*> member_values;

	MatchCaseMembers(Type* type, const Location& location, const std::vector<MatchCase*>& member_values, const std::vector<std::string>& member_names): MatchCase(type, location), member_values(member_values), member_names(member_names)
	{
	}
};

struct MatchCaseUnion: MatchCase
{
	size_t tag;
	MatchCase* pattern;

	MatchCaseUnion(Type* type, const Location& location, size_t tag, MatchCase* pattern): MatchCase(type, location), tag(tag), pattern(pattern)
	{
	}
};

struct MatchCaseOr: MatchCase
{
	std::vector<MatchCase*> options;

	MatchCaseOr(Type* type, const Location& location): MatchCase(type, location)
	{
	}

	void addOption(MatchCase* option)
	{
		options.push_back(option);
	}
};

struct ExprMatchWith: Expr
{
	Expr* variable;
	std::vector<MatchCase*> cases;
	std::vector<Expr*> expressions;

	ExprMatchWith(Type* type, const Location& location, Expr* variable, const std::vector<MatchCase*>& cases, const std::vector<Expr*>& expressions): Expr(type, location), variable(variable), cases(cases), expressions(expressions)
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

Expr* resolve(SynBase* root);
Type* typecheck(Expr* root);
