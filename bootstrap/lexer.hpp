#pragma once

#include <string>

enum LexemeType
{
	LexUnknown,
	LexEOF,

	LexComma,
	LexOpenBrace,
	LexCloseBrace,
	LexEqual,
	LexPlus,
	LexMinus,
	LexColon,
	LexSemicolon,
	LexArrow,
	LexLess,
	LexLessEqual,
	LexGreater,
	LexGreaterEqual,
	LexNot,
	LexNotEqual,
	LexEqualEqual,
	LexPipe,
	LexMultiply,
	LexDivide,
	LexKeyword,
	LexIdentifier,
	LexNumber
};

struct Lexeme
{
	LexemeType type;
	std::string contents;
	long long number;

	Lexeme(): type(LexUnknown), number(0)
	{
	}

	Lexeme(LexemeType type): type(type), number(0)
	{
	}

	Lexeme(LexemeType type, const std::string& contents): type(type), contents(contents), number(0)
	{
	}

	Lexeme(LexemeType type, long long number): type(type), number(number)
	{
	}
};

struct Lexer
{
	std::string data;
	size_t position;

	Lexeme current;
};

void movenext(Lexer& lexer);