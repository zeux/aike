#pragma once

#include "location.hpp"

#include <string>

enum LexemeType
{
	LexUnknown,
	LexEOF,

	LexComma,
	LexOpenBrace,
	LexCloseBrace,
	LexOpenBracket,
	LexCloseBracket,
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
	LexNumber,
	LexString
};

struct Lexeme
{
	LexemeType type;
	std::string contents;
	long long number;

	Location location;

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
	size_t line_start_pos;

	size_t line;

	Lexeme current;
};

void movenext(Lexer& lexer);
