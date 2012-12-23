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
	LexNumber,
	LexString
};

struct Location
{
	const char *lineStart;

	size_t line;
	size_t column;

	size_t length;

	Location(): lineStart(0), line(0), column(0), length(0)
	{
	}

	Location(const char* lineData, size_t line, size_t column, size_t length): lineStart(lineData), line(line), column(column), length(length)
	{
	}
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