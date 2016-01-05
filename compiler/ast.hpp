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
	X(Common, { Ty* type; Location location; }) \
	X(LiteralBool, { Ty* type; Location location; bool value; }) \
	X(LiteralInteger, { Ty* type; Location location; long long value; }) \
	X(LiteralFloat, { Ty* type; Location location; double value; }) \
	X(LiteralString, { Ty* type; Location location; Str value; }) \
	X(LiteralArray, { Ty* type; Location location; Arr<Ast*> elements; }) \
	X(LiteralStruct, { Ty* type; Location location; Str name; Arr<pair<FieldRef, Ast*>> fields; }) \
	X(Ident, { Ty* type; Location location; Str name; Arr<Ty*> tyargs; Arr<Variable*> targets; bool resolved; }) \
	X(Member, { Ty* type; Location location; Ast* expr; FieldRef field; }) \
	X(Block, { Ty* type; Location location; Arr<Ast*> body; }) \
	X(Module, { Ty* type; Location location; Str name; Ast* body; Arr<Str> autoimports; }) \
	X(Call, { Ty* type; Location location; Ast* expr; Arr<Ast*> args; }) \
	X(Unary, { Ty* type; Location location; UnaryOp op; Ast* expr; }) \
	X(Binary, { Ty* type; Location location; BinaryOp op; Ast* left; Ast* right; }) \
	X(Index, { Ty* type; Location location; Ast* expr; Ast* index; }) \
	X(Assign, { Ty* type; Location location; Ast* left; Ast* right; }) \
	X(If, { Ty* type; Location location; Ast* cond; Ast* thenbody; Ast* elsebody; }) \
	X(For, { Ty* type; Location location; Variable* var; Variable* index; Ast* expr; Ast* body; }) \
	X(While, { Ty* type; Location location; Ast* expr; Ast* body; }) \
	X(Fn, { Ty* type; Location location; int id; Ast* decl; }) \
	X(LLVM, { Ty* type; Location location; Str code; }) \
	X(FnDecl, { Ty* type; Location location; Variable* var; Arr<Ty*> tyargs; Arr<Variable*> args; unsigned attributes; Ast* body; Ast::FnDecl* parent; Ast::Module* module; }) \
	X(VarDecl, { Ty* type; Location location; Variable* var; Ast* expr; }) \
	X(TyDecl, { Ty* type; Location location; Str name; TyDef* def; }) \
	X(Import, { Ty* type; Location location; Str name; })

UNION_DECL(Ast, UD_AST)

inline Ty* astType(Ast* node)
{
	return node->dataCommon.type;
}

inline Location astLocation(Ast* node)
{
	return node->dataCommon.location;
}