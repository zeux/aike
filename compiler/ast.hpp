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
	FnAttributeInline = 1 << 2,
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
	X(LiteralInteger, { long long value; Location location; }) \
	X(LiteralFloat, { double value; Location location; }) \
	X(LiteralString, { Str value; Location location; }) \
	X(LiteralArray, { Location location; Ty* type; Arr<Ast*> elements; }) \
	X(LiteralStruct, { Str name; Location location; Ty* type; Arr<pair<FieldRef, Ast*>> fields; }) \
	X(Ident, { Str name; Location location; Ty* type; Arr<Ty*> tyargs; Arr<Variable*> targets; }) \
	X(Member, { Ast* expr; Location location; Ty* exprty; FieldRef field; }) \
	X(Block, { Arr<Ast*> body; }) \
	X(Module, { Str name; Location location; Ast* body; Arr<Str> autoimports; }) \
	X(Call, { Ast* expr; Arr<Ast*> args; Location location; Ty* exprty; Arr<Ty*> argtys; }) \
	X(Unary, { UnaryOp op; Ast* expr; Location location; Ty* type; }) \
	X(Binary, { BinaryOp op; Ast* left; Ast* right; Location location; }) \
	X(Index, { Ast* expr; Ast* index; Location location; }) \
	X(Assign, { Location location; Ast* left; Ast* right; }) \
	X(If, { Ast* cond; Ast* thenbody; Ast* elsebody; Location location; }) \
	X(For, { Location location; Variable* var; Variable* index; Ast* expr; Ast* body; }) \
	X(While, { Location location; Ast* expr; Ast* body; }) \
	X(Fn, { Location location; int id; Ast* decl; }) \
	X(LLVM, { Location location; Str code; }) \
	X(FnDecl, { Variable* var; Arr<Ty*> tyargs; Arr<Variable*> args; unsigned attributes; Ast* body; Ast::FnDecl* parent; Ast::Module* module; }) \
	X(VarDecl, { Variable* var; Ast* expr; }) \
	X(TyDecl, { Str name; Location location; TyDef* def; }) \
	X(Import, { Str name; Location location; })

UNION_DECL(Ast, UD_AST)