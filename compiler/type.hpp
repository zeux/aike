#pragma once

struct Ty;

#define UD_TYDEF(X) \
	X(Struct, { Array<pair<Str, Ty*>> fields; })

UNION_DECL(TyDef, UD_TYDEF)

#define UD_TY(X) \
	X(Void, {}) \
	X(Bool, {}) \
	X(Integer, {}) \
	X(String, {}) \
	X(Function, { Array<Ty*> args; Ty* ret; }) \
	X(Instance, { Str name; TyDef* def; }) \
	X(Unknown, {})

UNION_DECL(Ty, UD_TY)

struct TypeConstraints
{
	unordered_map<Ty*, Ty*> data;

	bool tryAdd(Ty* lhs, Ty* rhs);

	Ty* rewrite(Ty* type) const;
};

bool typeUnify(Ty* lhs, Ty* rhs, TypeConstraints* constraints);
bool typeEquals(Ty* lhs, Ty* rhs);
bool typeOccurs(Ty* lhs, Ty* rhs);

string typeName(Ty* type);