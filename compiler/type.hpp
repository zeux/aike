#pragma once

#include "location.hpp"

struct Ty;

#define UD_TYDEF(X) \
	X(Struct, { Arr<pair<Str, Ty*>> fields; })

UNION_DECL(TyDef, UD_TYDEF)

#define UD_TY(X) \
	X(Void, {}) \
	X(Bool, {}) \
	X(Integer, {}) \
	X(String, {}) \
	X(Function, { Arr<Ty*> args; Ty* ret; }) \
	X(Instance, { Str name; Location location; TyDef* def; }) \
	X(Unknown, {})

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

Ty* typeMember(Ty* type, int index);

string typeName(Ty* type);