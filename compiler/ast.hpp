#pragma once

#include "location.hpp"
#include "type.hpp"

struct Variable
{
	Str name;
	Ty* type;
	Location location;
};

enum FnAttribute
{
	FnAttributeExtern = 1 << 0,
};

enum UnaryOp
{
	UnaryOpPlus,
	UnaryOpMinus,
	UnaryOpNot,
};

enum BinaryOp
{
	BinaryOpAddWrap,
	BinaryOpSubtractWrap,
	BinaryOpMultiplyWrap,
	BinaryOpAdd,
	BinaryOpSubtract,
	BinaryOpMultiply,
	BinaryOpDivide,
	BinaryOpModulo,
	BinaryOpLess,
	BinaryOpLessEqual,
	BinaryOpGreater,
	BinaryOpGreaterEqual,
	BinaryOpEqual,
	BinaryOpNotEqual,
	BinaryOpAnd,
	BinaryOpOr
};

#define UD_AST(X) \
	X(LiteralBool, { bool value; Location location; }) \
	X(LiteralNumber, { Str value; Location location; }) \
	X(LiteralString, { Str value; Location location; }) \
	X(LiteralStruct, { Str name; Location location; Ty* type; Array<pair<Str, Ast*>> fields; }) \
	X(Ident, { Str name; Location location; Variable* target; }) \
	X(Index, { Ast* expr; Str name; Location location; int field; }) \
	X(Block, { Array<Ast*> body; }) \
	X(Call, { Ast* expr; Array<Ast*> args; Location location; }) \
	X(Unary, { UnaryOp op; Ast* expr; }) \
	X(Binary, { BinaryOp op; Ast* left; Ast* right; }) \
	X(If, { Ast* cond; Ast* thenbody; Ast* elsebody; }) \
	X(Fn, { Ty* type; Location location; Array<Variable*> args; Ast* body; }) \
	X(FnDecl, { Variable* var; Array<Variable*> args; unsigned attributes; Ast* body; }) \
	X(VarDecl, { Variable* var; Ast* expr; }) \
	X(TyDecl, { Str name; Location location; TyDef* def; })

UNION_DECL(Ast, UD_AST)