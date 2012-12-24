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
	return current.location.column < start.location.column || current.type == LexEOF;
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
			errorf(lexer.current.location, "Expected closing brace");

		movenext(lexer);

		return expr;
	}
	else if (lexer.current.type == LexNumber)
	{
		SynBase* result = new SynLiteralNumber(lexer.current.location, lexer.current.number);
		movenext(lexer);
		return result;
	}
	else if (lexer.current.type == LexIdentifier)
	{
		SynBase* result = new SynVariableReference(lexer.current.location, lexer.current.contents);
		movenext(lexer);
		return result;
	}
	else
	{
		errorf(lexer.current.location, "Unexpected lexeme %d", lexer.current.type);
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

SynIdentifier parseIdentifier(Lexer& lexer)
{
	if (lexer.current.type != LexIdentifier)
		errorf(lexer.current.location, "Expected identifier");

	Location location = lexer.current.location;
	std::string name = lexer.current.contents;
	movenext(lexer);

	return SynIdentifier(name, location);
}

SynType* parseType(Lexer& lexer)
{
	// Parse function type
	if (lexer.current.type == LexOpenBrace)
	{
		movenext(lexer);

		std::vector<SynType*> argument_types;

		if (lexer.current.type != LexCloseBrace)
		{
			argument_types.push_back(parseType(lexer));

			while (lexer.current.type == LexComma)
			{
				movenext(lexer);

				argument_types.push_back(parseType(lexer));
			}
		}

		if (lexer.current.type != LexCloseBrace)
			errorf(lexer.current.location, "Expected ')' after function type argument list");
		movenext(lexer);

		if (lexer.current.type != LexArrow)
			errorf(lexer.current.location, "Expected '->' after ')' in function type declaration");
		movenext(lexer);

		return new SynTypeFunction(argument_types, parseType(lexer));
	}

	return new SynTypeBasic(parseIdentifier(lexer));
}

SynBase* parseLetFunc(Lexer& lexer, const SynIdentifier& name, bool is_extern)
{
	assert(lexer.current.type == LexOpenBrace);
	movenext(lexer);

	std::vector<SynTypedVar> args;

	while (lexer.current.type != LexCloseBrace)
	{
		SynIdentifier argname = parseIdentifier(lexer);
		SynType* argtype = 0;

		if (lexer.current.type == LexColon)
		{
			movenext(lexer);

			argtype = parseType(lexer);
		}

		args.push_back(SynTypedVar(argname, argtype));

		if (lexer.current.type == LexComma)
			movenext(lexer);
		else if (lexer.current.type == LexCloseBrace)
			;
		else
			errorf(lexer.current.location, "Expected comma or closing brace");
	}

	movenext(lexer);

	SynType* rettype = 0;

	if (lexer.current.type == LexColon)
	{
		movenext(lexer);
		
		rettype = parseType(lexer);
	}

	SynBase* body = 0;

	if(!is_extern)
	{
		if (lexer.current.type != LexEqual) errorf(lexer.current.location, "Expected =");

		movenext(lexer);

		body = parseBlock(lexer);
	}

	return new SynLetFunc(name.location, SynTypedVar(name, rettype), args, body);
}

SynBase* parseExternFunc(Lexer& lexer)
{
	assert(iskeyword(lexer, "extern"));
	movenext(lexer);

	SynIdentifier name = parseIdentifier(lexer);

	return parseLetFunc(lexer, name, true);
}

SynBase* parseLet(Lexer& lexer)
{
	assert(iskeyword(lexer, "let"));
	movenext(lexer);

	SynIdentifier name = parseIdentifier(lexer);

	if (lexer.current.type == LexOpenBrace)
		return parseLetFunc(lexer, name, false);

	SynType* type = 0;

	if (lexer.current.type == LexColon)
	{
		movenext(lexer);

		type = parseType(lexer);
	}

	if (lexer.current.type != LexEqual) errorf(lexer.current.location, "Expected =");

	movenext(lexer);

	return new SynLetVar(name.location, SynTypedVar(name, type), parseBlock(lexer));
}

SynBase* parseLLVM(Lexer& lexer)
{
	assert(iskeyword(lexer, "llvm"));
	movenext(lexer);

	if (lexer.current.type != LexString) errorf(lexer.current.location, "String expected after llvm keyword");

	std::string body = lexer.current.contents;
	Location location = lexer.current.location;
	movenext(lexer);

	return new SynLLVM(location, body);
}

SynBase* parseIfThenElse(Lexer& lexer)
{
	Location location = lexer.current.location;
	assert(iskeyword(lexer, "if"));
	movenext(lexer);

	SynBase* cond = parseExpr(lexer);

	if (!iskeyword(lexer, "then")) errorf(lexer.current.location, "Expected 'then'");
	movenext(lexer);

	SynBase* thenbody = parseBlock(lexer);

	SynBase* elsebody =
		iskeyword(lexer, "else")
		? (movenext(lexer), parseBlock(lexer))
		: new SynUnit(lexer.current.location);

	return new SynIfThenElse(location, cond, thenbody, elsebody);
}

SynBase* parsePrimary(Lexer& lexer)
{
	SynUnaryOpType uop = getUnaryOp(lexer.current.type);

	if (uop != SynUnaryOpUnknown)
	{
		Location location = lexer.current.location;
		return new SynUnaryOp(location, uop, parsePrimary(lexer));
	}

	if (iskeyword(lexer, "extern"))
		return parseExternFunc(lexer);

	if (iskeyword(lexer, "let"))
		return parseLet(lexer);

	if (iskeyword(lexer, "llvm"))
		return parseLLVM(lexer);

	if (iskeyword(lexer, "if"))
		return parseIfThenElse(lexer);

	SynBase* result = parseTerm(lexer);

	if (lexer.current.type == LexOpenBrace)
	{
		Location location = lexer.current.location;
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
				errorf(lexer.current.location, "Expected comma or closing brace");
		}

		movenext(lexer);

		return new SynCall(location, result, args);
	}

	return result;
}

SynBase* parseExprClimb(Lexer& lexer, SynBase* left, int limit)
{
	Location location = lexer.current.location;
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

		left = new SynBinaryOp(location, op, left, right);

		location = lexer.current.location;
		op = getBinaryOp(lexer.current.type);
	}

	return left;
}

SynBase* parseExpr(Lexer& lexer)
{
	return parseExprClimb(lexer, parsePrimary(lexer), 0);
}

SynBase* parseBlock(Lexer& lexer)
{
	Lexeme start = lexer.current;

	SynBase* head = parseExpr(lexer);

	if (!islower(start, lexer.current))
	{
		std::vector<SynBase*> exprs;

		exprs.push_back(head);

		while (!islower(start, lexer.current))
			exprs.push_back(parseExpr(lexer));
		
		return new SynBlock(start.location, exprs);
	}
	else
		return head;
}

SynBase* parse(Lexer& lexer)
{
	return parseBlock(lexer);
}
