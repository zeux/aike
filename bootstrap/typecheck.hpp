#pragma once

#include <string>
#include <vector>

#include "parser.hpp"
#include "type.hpp"

struct BindingTarget
{
	Location location;
	std::string name;
	Type* type;

	BindingTarget(const Location& location, const std::string& name, Type* type): location(location), name(name), type(type)
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
	BindingTarget* context_target;

	BindingFunction(BindingTarget* target, const std::vector<std::string>& arg_names, BindingTarget* context_target): BindingLocal(target), arg_names(arg_names), context_target(context_target) {}
};

struct BindingFreeFunction: BindingFunction
{
	BindingFreeFunction(BindingTarget* target, const std::vector<std::string>& arg_names): BindingFunction(target, arg_names, 0) {}
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

struct ExprCharacterLiteral: Expr
{
	char value;

	ExprCharacterLiteral(Type* type, const Location& location, char value): Expr(type, location), value(value) {}
};

struct ExprArrayLiteral: Expr
{
	std::vector<Expr*> elements;

	ExprArrayLiteral(Type* type, const Location& location, const std::vector<Expr*>& elements): Expr(type, location), elements(elements) {}
};

struct ExprTupleLiteral: Expr
{
	std::vector<Expr*> elements;

	ExprTupleLiteral(Type* type, const Location& location, const std::vector<Expr*>& elements): Expr(type, location), elements(elements) {}
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
	BindingBase* binding;

	ExprBindingExternal(Type* type, const Location& location, BindingBase* context, const std::string& member_name, size_t member_index, BindingBase* binding): Expr(type, location), context(context), member_name(member_name), member_index(member_index), binding(binding) {}
};

struct ExprUnaryOp: Expr
{
	SynUnaryOpType op;
	Expr* expr;
	Type* refty;

	ExprUnaryOp(Type* type, const Location& location, SynUnaryOpType op, Expr* expr, Type* refty = NULL): Expr(type, location), op(op), expr(expr), refty(refty) {}
};

struct ExprBinaryOp: Expr
{
	SynBinaryOpType op;
	Expr* left;
	Expr* right;
	Type* refty;

	ExprBinaryOp(Type* type, const Location& location, SynBinaryOpType op, Expr* left, Expr* right, Type* refty = NULL): Expr(type, location), op(op), left(left), right(right), refty(refty) {}
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

struct ExprLetVars: Expr
{
	std::vector<BindingTarget*> targets;
	Expr* body;

	ExprLetVars(Type* type, const Location& location, const std::vector<BindingTarget*>& targets, Expr* body): Expr(type, location), targets(targets), body(body)
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

	ExprStructConstructorFunc(Type* type, const Location& location, BindingTarget* target, const std::vector<BindingTarget*>& args): Expr(type, location), target(target), args(args)
	{
	}
};

struct ExprUnionConstructorFunc: Expr
{
	BindingTarget* target;
	std::vector<BindingTarget*> args;

	std::string member_name;
	size_t member_id;
	Type* member_type;

	ExprUnionConstructorFunc(Type* type, const Location& location, BindingTarget* target, const std::vector<BindingTarget*>& args, const std::string& member_name, size_t member_id, Type* member_type): Expr(type, location), target(target), args(args), member_name(member_name), member_id(member_id), member_type(member_type)
	{
	}
};

struct ExprBuiltin: Expr
{
    std::string op;
	std::vector<Expr*> args;

	ExprBuiltin(Type* type, const Location& location, const std::string& op, const std::vector<Expr*>& args): Expr(type, location), op(op), args(args)
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

struct ExprForInRangeDo: Expr
{
	BindingTarget* target;
	Expr* start;
	Expr* end;
	Expr* body;

	ExprForInRangeDo(Type* type, const Location& location, BindingTarget* target, Expr* start, Expr* end, Expr* body): Expr(type, location), target(target), start(start), end(end), body(body)
	{
	}
};

struct ExprWhileDo: Expr
{
	Expr* condition;
	Expr* body;

	ExprWhileDo(Type* type, const Location& location, Expr* condition, Expr* body): Expr(type, location), condition(condition), body(body)
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

struct MatchCaseBoolean: MatchCase
{
	bool value;

	MatchCaseBoolean(Type* type, const Location& location, bool value): MatchCase(type, location), value(value)
	{
	}
};

struct MatchCaseNumber: MatchCase
{
	long long value;

	MatchCaseNumber(Type* type, const Location& location, long long value): MatchCase(type, location), value(value)
	{
	}
};

struct MatchCaseCharacter: MatchCase
{
	char value;

	MatchCaseCharacter(Type* type, const Location& location, char value): MatchCase(type, location), value(value)
	{
	}
};


struct MatchCaseValue: MatchCase
{
	BindingBase* value;

	MatchCaseValue(Type* type, const Location& location, BindingBase* value): MatchCase(type, location), value(value)
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
	std::vector<MatchCase*> member_values;
	std::vector<std::string> member_names;
	std::vector<Location> member_locations;

	MatchCaseMembers(Type* type, const Location& location, const std::vector<MatchCase*>& member_values, const std::vector<std::string>& member_names, const std::vector<Location>& member_locations): MatchCase(type, location), member_values(member_values), member_names(member_names), member_locations(member_locations)
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
	std::vector<std::vector<BindingTarget*>> binding_alternatives;
	std::vector<BindingTarget*> binding_actual;

	MatchCaseOr(Type* type, const Location& location): MatchCase(type, location)
	{
	}
	MatchCaseOr(Type* type, const Location& location, const std::vector<MatchCase*>& options): MatchCase(type, location), options(options)
	{
	}
	MatchCaseOr(Type* type, const Location& location, const std::vector<MatchCase*>& options, const std::vector<std::vector<BindingTarget*>>& binding_alternatives, const std::vector<BindingTarget*>& binding_actual): MatchCase(type, location), options(options), binding_alternatives(binding_alternatives), binding_actual(binding_actual)
	{
	}
};

struct MatchCaseIf: MatchCase
{
	MatchCase* match;
	Expr* condition;

	MatchCaseIf(Type* type, const Location& location, MatchCase* match, Expr* condition): MatchCase(type, location), match(match), condition(condition)
	{
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
