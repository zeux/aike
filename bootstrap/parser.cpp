#include "parser.hpp"

#include "lexer.hpp"

#include <exception>
#include <cassert>

inline void error(const char* msg)
{
	throw std::runtime_error(msg);
}

inline bool iskeyword(Lexer& lexer, const char* expected)
{
	return lexer.current.type == LexKeyword && lexer.current.contents == expected;
}

AstBase* parseExpr(Lexer& lexer);

AstBase* parseTerm(Lexer& lexer)
{
	if (lexer.current.type == LexOpenBrace)
	{
		movenext(lexer);

		AstBase* expr = parseExpr(lexer);

		if (lexer.current.type != LexCloseBrace)
			error("2");

		movenext(lexer);

		return expr;
	}
	else if (lexer.current.type == LexNumber)
	{
		AstBase* result = new AstLiteralNumber(lexer.current.number);
		movenext(lexer);
		return result;
	}
	else if (lexer.current.type == LexIdentifier)
	{
		AstBase* result = new AstVariableReference(lexer.current.contents);
		movenext(lexer);
		return result;
	}
	else
	{
		error("1");
		assert(false); // unreachable
		return 0;
	}
}

AstUnaryOpType getUnaryOp(LexemeType type)
{
	switch (type)
	{
	case LexPlus: return AstUnaryOpPlus;
	case LexMinus: return AstUnaryOpMinus;
	case LexNot: return AstUnaryOpNot;
	default: return AstUnaryOpUnknown;
	}
}

AstBinaryOpType getBinaryOp(LexemeType type)
{
	switch (type)
	{
	case LexPlus: return AstBinaryOpAdd;
	case LexMinus: return AstBinaryOpSubtract;
	case LexMultiply: return AstBinaryOpMultiply;
	case LexDivide: return AstBinaryOpDivide;
	case LexLess: return AstBinaryOpLess;
	case LexLessEqual: return AstBinaryOpLessEqual;
	case LexGreater: return AstBinaryOpGreater;
	case LexGreaterEqual: return AstBinaryOpGreaterEqual;
	case LexEqual: return AstBinaryOpEqual;
	case LexNotEqual: return AstBinaryOpNotEqual;
	default: return AstBinaryOpUnknown;
	}
}

int getBinaryOpPrecedence(AstBinaryOpType op)
{
	switch (op)
	{
	case AstBinaryOpAdd: return 3;
	case AstBinaryOpSubtract: return 3;
	case AstBinaryOpMultiply: return 4;
	case AstBinaryOpDivide: return 4;
	case AstBinaryOpLess: return 2;
	case AstBinaryOpLessEqual: return 2;
	case AstBinaryOpGreater: return 2;
	case AstBinaryOpGreaterEqual: return 2;
	case AstBinaryOpEqual: return 1;
	case AstBinaryOpNotEqual: return 1;
	default: return 0;
	}
}

std::string parseType(Lexer& lexer)
{
	if (lexer.current.type != LexIdentifier) error("5");

	std::string result = lexer.current.contents;

	movenext(lexer);

	return result;
}

AstBase* parseLetFunc(Lexer& lexer, const std::string& name)
{
	assert(lexer.current.type == LexOpenBrace);
	movenext(lexer);

	std::vector<AstTypedVar> args;

	while (lexer.current.type != LexCloseBrace)
	{
		if (lexer.current.type != LexIdentifier) error("5");

		std::string name = lexer.current.contents;
		movenext(lexer);

		std::string type;

		if (lexer.current.type == LexColon)
		{
			movenext(lexer);
			type = parseType(lexer);
		}

		args.push_back(AstTypedVar(name, type));

		if (lexer.current.type == LexComma)
			movenext(lexer);
		else if (lexer.current.type == LexCloseBrace)
			;
		else
			error("2");
	}

	movenext(lexer);

	std::string rettype;

	if (lexer.current.type == LexColon)
	{
		movenext(lexer);
		rettype = parseType(lexer);
	}

	if (lexer.current.type != LexEqual) error("6");

	movenext(lexer);

	AstBase* body = parseExpr(lexer);

	if (!iskeyword(lexer, "in")) error("8");
	movenext(lexer);

	AstBase* expr = parseExpr(lexer);

	return new AstLetFunc(AstTypedVar(name, rettype), args, body, expr);
}

AstBase* parseLet(Lexer& lexer)
{
	assert(iskeyword(lexer, "let"));
	movenext(lexer);

	if (lexer.current.type != LexIdentifier) error("4");

	std::string name = lexer.current.contents;
	movenext(lexer);

	if (lexer.current.type == LexOpenBrace)
		return parseLetFunc(lexer, name);

	std::string type;

	if (lexer.current.type == LexColon)
	{
		movenext(lexer);

		type = parseType(lexer);
	}

	if (lexer.current.type != LexEqual) error("7");

	movenext(lexer);

	AstBase* body = parseExpr(lexer);

	if (!iskeyword(lexer, "in")) error("8");
	movenext(lexer);

	AstBase* expr = parseExpr(lexer);

	return new AstLetVar(AstTypedVar(name, type), body, expr);
}

AstBase* parsePrimary(Lexer& lexer)
{
	AstUnaryOpType uop = getUnaryOp(lexer.current.type);

	if (uop != AstUnaryOpUnknown)
		return new AstUnaryOp(uop, parsePrimary(lexer));

	if (iskeyword(lexer, "let"))
		return parseLet(lexer);

	AstBase* result = parseTerm(lexer);

	if (lexer.current.type == LexOpenBrace)
	{
		movenext(lexer);

		std::vector<AstBase*> args;

		while (lexer.current.type != LexCloseBrace)
		{
			args.push_back(parseExpr(lexer));

			if (lexer.current.type == LexComma)
				movenext(lexer);
			else if (lexer.current.type == LexCloseBrace)
				;
			else
				error("2");
		}

		movenext(lexer);

		return new AstCall(result, args);
	}

	return result;
}

AstBase* parseExprClimb(Lexer& lexer, AstBase* left, int limit)
{
	AstBinaryOpType op = getBinaryOp(lexer.current.type);

	while (op != AstBinaryOpUnknown && getBinaryOpPrecedence(op) >= limit)
	{
		movenext(lexer);

		AstBase* right = parsePrimary(lexer);

		AstBinaryOpType nextop = getBinaryOp(lexer.current.type);

		while (nextop != AstBinaryOpUnknown && getBinaryOpPrecedence(nextop) > getBinaryOpPrecedence(op))
		{
			right = parseExprClimb(lexer, right, getBinaryOpPrecedence(nextop));

			nextop = getBinaryOp(lexer.current.type);
		}

		left = new AstBinaryOp(op, left, right);

		op = getBinaryOp(lexer.current.type);
	}

	return left;
}

AstBase* parseExpr(Lexer& lexer)
{
	return parseExprClimb(lexer, parsePrimary(lexer), 0);
}

AstBase* parse(Lexer& lexer)
{
	return parseExpr(lexer);
}