#pragma once

#include "location.hpp"

#include <string>

enum LexemeType
{
	LexUnknown,
	LexEOF,

	LexComma,
	LexPoint,
	LexPointPoint,
	LexOpenBrace,
	LexCloseBrace,
	LexOpenBracket,
	LexCloseBracket,
	LexOpenCurlyBrace,
	LexCloseCurlyBrace,
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
	LexSharp,
	LexKeyword,
	LexIdentifier,
	LexIdentifierGeneric,
	LexNumber,
	LexCharacter,
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

	Lexer save()
	{
		Lexer result;
		result.position = position;
		result.line_start_pos = line_start_pos;
		result.line = line;
		result.current = current;
		return result;
	}

	void load(const Lexer& state)
	{
		position = state.position;
		line_start_pos = state.line_start_pos;
		line = state.line;
		current = state.current;
	}
};

void movenext(Lexer& lexer);
