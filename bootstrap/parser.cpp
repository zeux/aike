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

inline bool issameline(Lexeme& start, Lexeme& current)
{
	return current.location.line == start.location.line;
}

SynBase* parseExpr(Lexer& lexer);
SynBase* parseBlock(Lexer& lexer);

SynBase* parseTerm(Lexer& lexer)
{
	if (lexer.current.type == LexOpenBrace)
	{
		movenext(lexer);

		if (lexer.current.type == LexCloseBrace)
		{
			SynBase* result = new SynUnit(lexer.current.location);
			movenext(lexer);

			return result;
		}

		SynBase* expr = parseExpr(lexer);

		if (lexer.current.type != LexCloseBrace)
			errorf(lexer.current.location, "Expected closing brace");

		movenext(lexer);

		return expr;
	}
	else if (lexer.current.type == LexOpenBracket)
	{
		Location location = lexer.current.location;

		movenext(lexer);

		std::vector<SynBase*> elements;

		while (lexer.current.type != LexCloseBracket)
		{
			if (!elements.empty())
			{
				if (lexer.current.type != LexComma)
					errorf(lexer.current.location, "',' expected after previous array element");
				movenext(lexer);
			}

			elements.push_back(parseExpr(lexer));
		}

		movenext(lexer);

		return new SynArrayLiteral(location, elements);
	}
	else if (lexer.current.type == LexNumber)
	{
		SynBase* result = new SynNumberLiteral(lexer.current.location, lexer.current.number);
		movenext(lexer);
		return result;
	}
	else if (lexer.current.type == LexIdentifier)
	{
		SynBase* result = new SynVariableReference(lexer.current.location, lexer.current.contents);
		movenext(lexer);
		return result;
	}
	else if (iskeyword(lexer, "true"))
	{
		SynBase* result = new SynBooleanLiteral(lexer.current.location, true);
		movenext(lexer);
		return result;
	}
	else if (iskeyword(lexer, "false"))
	{
		SynBase* result = new SynBooleanLiteral(lexer.current.location, false);
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

SynType* parseType(Lexer& lexer);

SynType* parseTypeBraced(Lexer& lexer)
{
	assert(lexer.current.type == LexOpenBrace);
	movenext(lexer);

	std::vector<SynType*> list;

	if (lexer.current.type != LexCloseBrace)
	{
		list.push_back(parseType(lexer));

		while (lexer.current.type == LexComma)
		{
			movenext(lexer);

			list.push_back(parseType(lexer));
		}
	}

	if (lexer.current.type != LexCloseBrace)
		errorf(lexer.current.location, "Expected ')' after function type argument list");
	movenext(lexer);

	if (lexer.current.type == LexArrow)
	{
		movenext(lexer);

		return new SynTypeFunction(parseType(lexer), list);
	}
	else
	{
		if (list.size() != 1)
			errorf(lexer.current.location, "Expected '->' after ')' in function type declaration");

		return list[0];
	}
}

SynType* parseType(Lexer& lexer)
{
	SynType* type;

	if (lexer.current.type == LexOpenBrace)
	{
		type = parseTypeBraced(lexer);
	}
	else
	{
		type = new SynTypeBasic(parseIdentifier(lexer));
	}
	
	if (lexer.current.type == LexOpenBracket)
	{
		movenext(lexer);

		if (lexer.current.type != LexCloseBracket)
			errorf(lexer.current.location, "Expected ']' after '['");
		movenext(lexer);

		return new SynTypeArray(type);
	}
	else
	{
		return type;
	}
}

SynTypedVar parseTypedVar(Lexer& lexer)
{
	SynIdentifier argname = parseIdentifier(lexer);
	SynType* argtype = 0;

	if (lexer.current.type == LexColon)
	{
		movenext(lexer);

		argtype = parseType(lexer);
	}

	return SynTypedVar(argname, argtype);
}

std::vector<SynTypedVar> parseFunctionArguments(Lexer& lexer)
{
	assert(lexer.current.type == LexOpenBrace);
	movenext(lexer);

	std::vector<SynTypedVar> args;

	while (lexer.current.type != LexCloseBrace)
	{
		SynTypedVar var = parseTypedVar(lexer);

		args.push_back(var);

		if (lexer.current.type == LexComma)
			movenext(lexer);
		else if (lexer.current.type == LexCloseBrace)
			;
		else
			errorf(lexer.current.location, "Expected comma or closing brace");
	}

	movenext(lexer);

	return args;
}

SynBase* parseLetFunc(Lexer& lexer, const SynIdentifier& name)
{
	std::vector<SynTypedVar> args = parseFunctionArguments(lexer);

	SynType* rettype = 0;

	if (lexer.current.type == LexColon)
	{
		movenext(lexer);
		
		rettype = parseType(lexer);
	}

	if (lexer.current.type != LexEqual) errorf(lexer.current.location, "Expected =");

	movenext(lexer);

	return new SynLetFunc(name.location, name, rettype, args, parseBlock(lexer));
}

SynBase* parseExternFunc(Lexer& lexer)
{
	assert(iskeyword(lexer, "extern"));
	movenext(lexer);

	SynIdentifier name = parseIdentifier(lexer);

	std::vector<SynTypedVar> args = parseFunctionArguments(lexer);

	for (size_t i = 0; i < args.size(); ++i)
		if (!args[i].type)
			errorf(args[i].name.location, "Extern function '%s': type declaration missing for argument '%s'", name.name.c_str(), args[i].name.name.c_str());

	if (lexer.current.type != LexColon)
		errorf(name.location, "Extern function '%s': type declaration missing for return type", name.name.c_str());

	movenext(lexer);
	
	SynType* rettype = parseType(lexer);

	return new SynExternFunc(name.location, name, rettype, args);
}

SynBase* parseAnonymousFunc(Lexer& lexer)
{
	Location start = lexer.current.location;

	assert(iskeyword(lexer, "fun"));
	movenext(lexer);

	std::vector<SynTypedVar> args;

	SynType* rettype = 0;

	if (lexer.current.type == LexOpenBrace)
	{
		args = parseFunctionArguments(lexer);

		if (lexer.current.type == LexColon)
		{
			movenext(lexer);
		
			rettype = parseType(lexer);
		}
	}
	else if (lexer.current.type == LexIdentifier)
	{
		args.push_back(SynTypedVar(parseIdentifier(lexer), 0));
	}

	if (lexer.current.type != LexArrow) errorf(lexer.current.location, "Expected ->");
	movenext(lexer);

	SynBase* body = parseBlock(lexer);

	return new SynLetFunc(start, SynIdentifier("", start), rettype, args, body);
}

SynBase* parseLet(Lexer& lexer)
{
	assert(iskeyword(lexer, "let"));
	movenext(lexer);

	SynIdentifier name = parseIdentifier(lexer);

	if (lexer.current.type == LexOpenBrace)
		return parseLetFunc(lexer, name);

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

SynBase* parseForInDo(Lexer& lexer)
{
	Location location = lexer.current.location;
	assert(iskeyword(lexer, "for"));
	movenext(lexer);

	SynTypedVar var = parseTypedVar(lexer);

	if (!iskeyword(lexer, "in")) errorf(lexer.current.location, "Expected 'in' after array element name");
	movenext(lexer);

	SynBase* arr = parseExpr(lexer);

	if (!iskeyword(lexer, "do")) errorf(lexer.current.location, "Expected 'do' after array expression");
	movenext(lexer);

	return new SynForInDo(location, var, arr, parseBlock(lexer));
}

SynBase* parsePrimary(Lexer& lexer)
{
	SynUnaryOpType uop = getUnaryOp(lexer.current.type);

	if (uop != SynUnaryOpUnknown)
	{
		Location location = lexer.current.location;

		movenext(lexer);

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

	if (iskeyword(lexer, "for"))
		return parseForInDo(lexer);

	if (iskeyword(lexer, "fun"))
		return parseAnonymousFunc(lexer);

	SynBase* result = parseTerm(lexer);

	while (lexer.current.type == LexOpenBrace || lexer.current.type == LexOpenBracket)
	{
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

			result = new SynCall(location, result, args);
		}
		else if (lexer.current.type == LexOpenBracket)
		{
			Location location = lexer.current.location;
			movenext(lexer);

			SynBase* index_start = 0;
			SynBase* index_end = 0;
			bool to_end = false;

			if (lexer.current.type == LexCloseBracket) errorf(lexer.current.location, "index or range is expected after '['");

			if (lexer.current.type == LexRange)
				index_start = new SynNumberLiteral(lexer.current.location, 0);
			else
				index_start = parseExpr(lexer);

			if (lexer.current.type == LexRange)
			{
				movenext(lexer);

				if (lexer.current.type == LexCloseBracket)
					to_end = true;
				else
					index_end = parseExpr(lexer);
			}

			if (lexer.current.type != LexCloseBracket) errorf(lexer.current.location, "']' expected after index");
			movenext(lexer);

			if (to_end || index_end)
				result = new SynArraySlice(location, result, index_start, index_end);
			else
				result = new SynArrayIndex(location, result, index_start);
		}
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

	if (!islower(start, lexer.current) && !issameline(start, lexer.current))
	{
		std::vector<SynBase*> exprs;

		exprs.push_back(head);

		while (!islower(start, lexer.current) && !issameline(start, lexer.current))
			exprs.push_back(parseExpr(lexer));
		
		return new SynBlock(start.location, exprs);
	}
	else
		return head;
}

SynBase* parse(Lexer& lexer)
{
	SynBase* code = parseBlock(lexer);

	if(lexer.current.type != LexEOF)
		errorf(lexer.current.location, "Unexpected expression");

	return code;
}
