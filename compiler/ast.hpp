#pragma once

#include "location.hpp"
#include "type.hpp"

struct Variable
{
	enum Kind
	{
		KindVariable,
		KindFunction,
		KindArgument,
		KindValue,
	};

	Kind kind;
	Str name;
	Ty* type;
	Location location;

	Ast* fn;
};

enum FnAttribute
{
	FnAttributeExtern = 1 << 0,
	FnAttributeBuiltin = 1 << 1,
};

enum UnaryOp
{
	UnaryOpPlus,
	UnaryOpMinus,
	UnaryOpNot,
	UnaryOpDeref,
	UnaryOpNew,
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

struct FieldRef
{
	Str name;
	Location location;
	int index;
};

#define UD_AST(X) \
	X(LiteralBool, { bool value; Location location; }) \
	X(LiteralNumber, { Str value; Location location; }) \
	X(LiteralString, { Str value; Location location; }) \
	X(LiteralArray, { Location location; Ty* type; Arr<Ast*> elements; }) \
	X(LiteralStruct, { Str name; Location location; Ty* type; Arr<pair<FieldRef, Ast*>> fields; }) \
	X(Ident, { Str name; Location location; Ty* type; Variable* target; Arr<Ty*> tyargs; }) \
	X(Member, { Ast* expr; Location location; Ty* exprty; FieldRef field; }) \
	X(Block, { Arr<Ast*> body; }) \
	X(Call, { Ast* expr; Arr<Ast*> args; Location location; }) \
	X(Unary, { UnaryOp op; Ast* expr; }) \
	X(Binary, { BinaryOp op; Ast* left; Ast* right; }) \
	X(Index, { Ast* expr; Ast* index; Location location; }) \
	X(Assign, { Location location; Ast* left; Ast* right; }) \
	X(If, { Ast* cond; Ast* thenbody; Ast* elsebody; }) \
	X(For, { Location location; Variable* var; Variable* index; Ast* expr; Ast* body; }) \
	X(Fn, { Location location; int id; Ast* decl; }) \
	X(FnDecl, { Variable* var; Arr<Ty*> tyargs; Arr<Variable*> args; unsigned attributes; Ast* body; Ast::FnDecl* parent; }) \
	X(VarDecl, { Variable* var; Ast* expr; }) \
	X(TyDecl, { Str name; Location location; TyDef* def; }) \

UNION_DECL(Ast, UD_AST)