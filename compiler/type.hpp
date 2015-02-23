#pragma once

#include "location.hpp"

struct Ty;
struct Ast;

struct StructField
{
	Str name;
	Ty* type;
	Ast* expr;
};

#define UD_TYDEF(X) \
	X(Struct, { Arr<Ty*> tyargs; Arr<StructField> fields; }) \

UNION_DECL(TyDef, UD_TYDEF)

#define UD_TY(X) \
	X(Unknown, {}) \
	X(Void, {}) \
	X(Bool, {}) \
	X(Integer, {}) \
	X(String, {}) \
	X(Array, { Ty* element; }) \
	X(Function, { Arr<Ty*> args; Ty* ret; }) \
	X(Instance, { Str name; Location location; Arr<Ty*> tyargs; TyDef* def; Ty* generic; }) \
	X(Generic, { Str name; Location location; }) \

UNION_DECL(Ty, UD_TY)

struct TypeConstraints
{
	unordered_map<Ty*, Ty*> data;
	int rewrites = 0;

	bool tryAdd(Ty* lhs, Ty* rhs);

	Ty* rewrite(Ty* type);
};

bool typeUnify(Ty* lhs, Ty* rhs, TypeConstraints* constraints);
bool typeEquals(Ty* lhs, Ty* rhs);
bool typeOccurs(Ty* lhs, Ty* rhs);

Ty* typeInstantiate(Ty* type, const function<Ty*(Ty*)>& inst);

Ty* typeMember(Ty* type, int index);

string typeName(Ty* type);