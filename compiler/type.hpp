#pragma once

#define UD_TY(X) \
	X(Void, {}) \
	X(Bool, {}) \
	X(Integer, {}) \
	X(String, {}) \
	X(Function, { Array<Ty*> args; Ty* ret; }) \
	X(Unknown, {})

UNION_DECL(Ty, UD_TY)

bool typeEquals(Ty* lhs, Ty* rhs);
string typeName(Ty* type);