#include "parser.hpp"

#include "lexer.hpp"
#include "output.hpp"

#include <exception>
#include <cassert>

inline bool iskeyword(Lexer& lexer, const char* expected)
{
	return lexer.current.type == LexKeyword && lexer.current.contents == expected;
}

inline bool islower(Lexeme& start, Lexeme& current)
{
	return current.column < start.column || current.type == LexEOF;
}

SynBase* parseExpr(Lexer& lexer);
SynBase* parseBlock(Lexer& lexer);

SynBase* parseTerm(Lexer& lexer)
{
	if (lexer.current.type == LexOpenBrace)
	{
		movenext(lexer);

		SynBase* expr = parseExpr(lexer);

		if (lexer.current.type != LexCloseBrace)
			errorf("Expected closing brace");

		movenext(lexer);

		return expr;
	}
	else if (lexer.current.type == LexNumber)
	{
		SynBase* result = new SynLiteralNumber(lexer.current.number);
		movenext(lexer);
		return result;
	}
	else if (lexer.current.type == LexIdentifier)
	{
		SynBase* result = new SynVariableReference(lexer.current.contents);
		movenext(lexer);
		return result;
	}
	else
	{
		errorf("Unexpected lexeme %d", lexer.current.type);
	}
}

SynUnaryOpType getUnaryOp(LexemeType type)
{
	switch (type)
	{
	case LexPlus: return SynUnaryOpPlus;
	case LexMinus: return SynUnaryOpMinus;
	case LexNot: return SynUnaryOpNot;
	default: return SynUnaryOpUnknown;
	}
}

SynBinaryOpType getBinaryOp(LexemeType type)
{
	switch (type)
	{
	case LexPlus: return SynBinaryOpAdd;
	case LexMinus: return SynBinaryOpSubtract;
	case LexMultiply: return SynBinaryOpMultiply;
	case LexDivide: return SynBinaryOpDivide;
	case LexLess: return SynBinaryOpLess;
	case LexLessEqual: return SynBinaryOpLessEqual;
	case LexGreater: return SynBinaryOpGreater;
	case LexGreaterEqual: return SynBinaryOpGreaterEqual;
	case LexEqualEqual: return SynBinaryOpEqual;
	case LexNotEqual: return SynBinaryOpNotEqual;
	default: return SynBinaryOpUnknown;
	}
}

int getBinaryOpPrecedence(SynBinaryOpType op)
{
	switch (op)
	{
	case SynBinaryOpAdd: return 3;
	case SynBinaryOpSubtract: return 3;
	case SynBinaryOpMultiply: return 4;
	case SynBinaryOpDivide: return 4;
	case SynBinaryOpLess: return 2;
	case SynBinaryOpLessEqual: return 2;
	case SynBinaryOpGreater: return 2;
	case SynBinaryOpGreaterEqual: return 2;
	case SynBinaryOpEqual: return 1;
	case SynBinaryOpNotEqual: return 1;
	default: return 0;
	}
}

std::string parseType(Lexer& lexer)
{
	if (lexer.current.type != LexIdentifier)
		errorf("Expected identifier");

	std::string result = lexer.current.contents;

	movenext(lexer);

	return result;
}

SynBase* parseLetFunc(Lexer& lexer, const std::string& name)
{
	assert(lexer.current.type == LexOpenBrace);
	movenext(lexer);

	std::vector<SynTypedVar> args;

	while (lexer.current.type != LexCloseBrace)
	{
		if (lexer.current.type != LexIdentifier)
			errorf("Expected identifier");

		std::string name = lexer.current.contents;
		movenext(lexer);

		std::string type;

		if (lexer.current.type == LexColon)
		{
			movenext(lexer);
			type = parseType(lexer);
		}

		args.push_back(SynTypedVar(name, type));

		if (lexer.current.type == LexComma)
			movenext(lexer);
		else if (lexer.current.type == LexCloseBrace)
			;
		else
			errorf("Expected comma or closing brace");
	}

	movenext(lexer);

	std::string rettype;

	if (lexer.current.type == LexColon)
	{
		movenext(lexer);
		rettype = parseType(lexer);
	}

	if (lexer.current.type != LexEqual) errorf("Expected =");

	movenext(lexer);

	return new SynLetFunc(SynTypedVar(name, rettype), args, parseBlock(lexer));
}

SynBase* parseLet(Lexer& lexer)
{
	assert(iskeyword(lexer, "let"));
	movenext(lexer);

	if (lexer.current.type != LexIdentifier) errorf("Expected identifier");

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

	if (lexer.current.type != LexEqual) errorf("Expected =");

	movenext(lexer);

	return new SynLetVar(SynTypedVar(name, type), parseBlock(lexer));
}

SynBase* parseLLVM(Lexer& lexer)
{
	assert(iskeyword(lexer, "llvm"));
	movenext(lexer);

	if (lexer.current.type != LexString) errorf("String expected after llvm keyword");

	std::string body = lexer.current.contents;
	movenext(lexer);

	return new SynLLVM(body);
}

SynBase* parseIfThenElse(Lexer& lexer)
{
	assert(iskeyword(lexer, "if"));
	movenext(lexer);

	SynBase* cond = parseExpr(lexer);

	if (!iskeyword(lexer, "then")) errorf("Expected 'then'");
	movenext(lexer);

	SynBase* thenbody = parseBlock(lexer);

	SynBase* elsebody =
		iskeyword(lexer, "else")
		? (movenext(lexer), parseBlock(lexer))
		: new SynUnit();

	return new SynIfThenElse(cond, thenbody, elsebody);
}

SynBase* parsePrimary(Lexer& lexer)
{
	SynUnaryOpType uop = getUnaryOp(lexer.current.type);

	if (uop != SynUnaryOpUnknown)
		return new SynUnaryOp(uop, parsePrimary(lexer));

	if (iskeyword(lexer, "let"))
		return parseLet(lexer);

	if (iskeyword(lexer, "llvm"))
		return parseLLVM(lexer);

	if (iskeyword(lexer, "if"))
		return parseIfThenElse(lexer);

	SynBase* result = parseTerm(lexer);

	if (lexer.current.type == LexOpenBrace)
	{
		movenext(lexer);

		std::vector<SynBase*> args;

		while (lexer.current.type != LexCloseBrace)
		{
			args.push_back(parseExpr(lexer));

			if (lexer.current.type == LexComma)
				movenext(lexer);
			else if (lexer.current.type == LexCloseBrace)
				;
			else
				errorf("Expected comma or closing brace");
		}

		movenext(lexer);

		return new SynCall(result, args);
	}

	return result;
}

SynBase* parseExprClimb(Lexer& lexer, SynBase* left, int limit)
{
	SynBinaryOpType op = getBinaryOp(lexer.current.type);

	while (op != SynBinaryOpUnknown && getBinaryOpPrecedence(op) >= limit)
	{
		movenext(lexer);

		SynBase* right = parsePrimary(lexer);

		SynBinaryOpType nextop = getBinaryOp(lexer.current.type);

		while (nextop != SynBinaryOpUnknown && getBinaryOpPrecedence(nextop) > getBinaryOpPrecedence(op))
		{
			right = parseExprClimb(lexer, right, getBinaryOpPrecedence(nextop));

			nextop = getBinaryOp(lexer.current.type);
		}

		left = new SynBinaryOp(op, left, right);

		op = getBinaryOp(lexer.current.type);
	}

	return left;
}

SynBase* parseExprSingle(Lexer& lexer)
{
	return parseExprClimb(lexer, parsePrimary(lexer), 0);
}

SynBase* parseExpr(Lexer& lexer)
{
	SynBase* result = parseExprSingle(lexer);

	while (lexer.current.type == LexSemicolon)
	{
		movenext(lexer);

		SynBase* next = parseExprSingle(lexer);

		result = new SynSequence(result, next);
	}

	return result;
}

SynBase* parseBlock(Lexer& lexer)
{
	Lexeme start = lexer.current;

	SynBlock* result = new SynBlock(parseExpr(lexer));

	while (!islower(start, lexer.current))
		result->expressions.push_back(parseExpr(lexer));

	return result;
}

SynBase* parse(Lexer& lexer)
{
	return parseBlock(lexer);
}
