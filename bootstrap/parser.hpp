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

struct SynTypedVar
{
	SynIdentifier name;
	SynType* type;

	SynTypedVar(const SynIdentifier& name, SynType* type): name(name), type(type)
	{
	}
};

struct SynTypeGeneric: SynType
{
	SynIdentifier type;

	SynTypeGeneric(const SynIdentifier& type): type(type)
	{
	}
};

struct SynTypeIdentifier: SynType
{
	SynIdentifier type;
	std::vector<SynType*> generics;

	SynTypeIdentifier(const SynIdentifier& type, const std::vector<SynType*>& generics): type(type), generics(generics)
	{
	}
};

struct SynTypeArray: SynType
{
	SynType* contained_type;

	SynTypeArray(SynType* contained_type): contained_type(contained_type)
	{
	}
};

struct SynTypeFunction: SynType
{
	SynType* result;
	std::vector<SynType*> args;

	SynTypeFunction(SynType* result, const std::vector<SynType*>& args): result(result), args(args)
	{
	}
};

struct SynTypeRecord: SynType
{
	std::vector<SynTypedVar> members;

	SynTypeRecord(const std::vector<SynTypedVar>& members): members(members)
	{
	}
};

struct SynTypeTuple: SynType
{
	std::vector<SynType*> members;

	SynTypeTuple(const std::vector<SynType*>& members): members(members)
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

struct SynNumberLiteral: SynBase
{
	long long value;

	SynNumberLiteral(const Location& location, long long value): SynBase(location), value(value) {}
};

struct SynCharacterLiteral: SynBase
{
	char value;

	SynCharacterLiteral(const Location& location, char value): SynBase(location), value(value) {}
};

struct SynBooleanLiteral: SynBase
{
	bool value;

	SynBooleanLiteral(const Location& location, bool value): SynBase(location), value(value) {}
};

struct SynArrayLiteral: SynBase
{
	std::vector<SynBase*> elements;

	SynArrayLiteral(const Location& location, const std::vector<SynBase*>& elements): SynBase(location), elements(elements) {}
};

struct SynTupleLiteral: SynBase
{
	std::vector<SynBase*> elements;

	SynTupleLiteral(const Location& location, const std::vector<SynBase*>& elements): SynBase(location), elements(elements) {}
};

struct SynRecordDefinition: SynBase
{
	SynIdentifier name;
	SynTypeRecord* type;
	std::vector<SynTypeGeneric*> generics;

	SynRecordDefinition(const Location& location, const SynIdentifier& name, SynTypeRecord* type, const std::vector<SynTypeGeneric*>& generics): SynBase(location), name(name), type(type), generics(generics) {}
};

struct SynUnionDefinition: SynBase
{
	SynIdentifier name;
	std::vector<SynTypedVar> members;
	std::vector<SynTypeGeneric*> generics;

	SynUnionDefinition(const Location& location, const SynIdentifier& name, const std::vector<SynTypedVar>& members, const std::vector<SynTypeGeneric*>& generics): SynBase(location), name(name), members(members), generics(generics) {}
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
	SynUnaryOpRefGet
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
	SynBinaryOpNotEqual,
	SynBinaryOpRefSet
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
	std::vector<SynBase*> arg_values;
	std::vector<SynIdentifier> arg_names;

	SynCall(const Location& location, SynBase* expr, const std::vector<SynBase*>& arg_values): SynBase(location), expr(expr), arg_values(arg_values)
	{
	}

	SynCall(const Location& location, SynBase* expr, const std::vector<SynBase*>& arg_values, const std::vector<SynIdentifier>& arg_names): SynBase(location), expr(expr), arg_values(arg_values), arg_names(arg_names)
	{
	}
};

struct SynArrayIndex: SynBase
{
	SynBase* arr;
	SynBase* index;

	SynArrayIndex(const Location& location, SynBase* arr, SynBase* index): SynBase(location), arr(arr), index(index)
	{
	}
};

struct SynArraySlice: SynBase
{
	SynBase* arr;
	SynBase* index_start;
	SynBase* index_end;

	SynArraySlice(const Location& location, SynBase* arr, SynBase* index_start, SynBase* index_end): SynBase(location), arr(arr), index_start(index_start), index_end(index_end)
	{
	}
};

struct SynMemberAccess: SynBase
{
	SynBase* aggr;
	SynIdentifier member;

	SynMemberAccess(const Location& location, SynBase* aggr, SynIdentifier member): SynBase(location), aggr(aggr), member(member)
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

struct SynLetVars: SynBase
{
	std::vector<SynTypedVar> vars;
	SynBase* body;

	SynLetVars(const Location& location, const std::vector<SynTypedVar>& vars, SynBase* body): SynBase(location), vars(vars), body(body)
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
	SynIdentifier var;
	SynType* ret_type;
	std::vector<SynTypedVar> args;
	SynBase* body;

	SynLetFunc(const Location& location, const SynIdentifier& var, SynType* ret_type, const std::vector<SynTypedVar>& args, SynBase* body): SynBase(location), var(var), ret_type(ret_type), args(args), body(body)
	{
	}
};

struct SynExternFunc: SynBase
{
	SynIdentifier var;
	SynType* ret_type;
	std::vector<SynTypedVar> args;

	SynExternFunc(const Location& location, const SynIdentifier& var, SynType* ret_type, const std::vector<SynTypedVar>& args): SynBase(location), var(var), ret_type(ret_type), args(args)
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

struct SynForInDo: SynBase
{
	SynTypedVar var;
	SynBase* arr;
	SynBase* body;

	SynForInDo(const Location& location, const SynTypedVar& var, SynBase* arr, SynBase* body): SynBase(location), var(var), arr(arr), body(body)
	{
	}
};

struct SynForInRangeDo: SynBase
{
	SynTypedVar var;
	SynBase* start;
	SynBase* end;
	SynBase* body;

	SynForInRangeDo(const Location& location, const SynTypedVar& var, SynBase* start, SynBase* end, SynBase* body): SynBase(location), var(var), start(start), end(end), body(body)
	{
	}
};

struct SynWhileDo: SynBase
{
	SynBase* condition;
	SynBase* body;

	SynWhileDo(const Location& location, SynBase* condition, SynBase* body): SynBase(location), condition(condition), body(body)
	{
	}
};

struct SynMatch: SynBase
{
	SynMatch(const Location& location): SynBase(location) {}
};

struct SynMatchNumber: SynMatch
{
	long long value;

	SynMatchNumber(const Location& location, long long value): SynMatch(location), value(value) {}
};

struct SynMatchCharacter: SynMatch
{
	char value;

	SynMatchCharacter(const Location& location, char value): SynMatch(location), value(value) {}
};

struct SynMatchBoolean: SynMatch
{
	bool value;

	SynMatchBoolean(const Location& location, bool value): SynMatch(location), value(value) {}
};

struct SynMatchArray: SynMatch
{
	std::vector<SynMatch*> elements;

	SynMatchArray(const Location& location, const std::vector<SynMatch*>& elements): SynMatch(location), elements(elements) {}
};

struct SynMatchTuple: SynMatch
{
	std::vector<SynMatch*> elements;

	SynMatchTuple(const Location& location, const std::vector<SynMatch*>& elements): SynMatch(location), elements(elements) {}
};

struct SynMatchTypeSimple: SynMatch
{
	SynIdentifier type;
	SynIdentifier alias;

	SynMatchTypeSimple(const Location& location, const SynIdentifier& type, const SynIdentifier& alias): SynMatch(location), type(type), alias(alias) {}
};

struct SynMatchTypeComplex: SynMatch
{
	SynIdentifier type;
	std::vector<SynMatch*> arg_values;
	std::vector<SynIdentifier> arg_names;

	SynMatchTypeComplex(const Location& location, const SynIdentifier& type, const std::vector<SynMatch*>& arg_values, const std::vector<SynIdentifier>& arg_names): SynMatch(location), type(type), arg_values(arg_values), arg_names(arg_names) {}
};

// Later this can be resolved to MatchCaseUnion if the identifier was a union type tag
struct SynMatchPlaceholder: SynMatch
{
	SynTypedVar alias;

	SynMatchPlaceholder(const Location& location, const SynTypedVar& alias): SynMatch(location), alias(alias) {}
};

struct SynMatchPlaceholderUnnamed: SynMatch
{
	SynMatchPlaceholderUnnamed(const Location& location): SynMatch(location) {}
};

struct SynMatchOr: SynMatch
{
	std::vector<SynMatch*> options;

	SynMatchOr(const Location& location, const std::vector<SynMatch*>& options): SynMatch(location), options(options) {}
};

struct SynMatchIf: SynMatch
{
	SynMatch* match;
	SynBase* condition;

	SynMatchIf(const Location& location, SynMatch* match, SynBase* condition): SynMatch(location), match(match), condition(condition) {}
};

struct SynMatchWith: SynBase
{
	SynBase* variable;
	std::vector<SynMatch*> variants;
	std::vector<SynBase*> expressions;

	SynMatchWith(const Location& location, SynBase* variable, const std::vector<SynMatch*>& variants, const std::vector<SynBase*> expressions): SynBase(location), variable(variable), variants(variants), expressions(expressions)
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
