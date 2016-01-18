#pragma once

#include "array.hpp"
#include "string.hpp"
#include "location.hpp"

struct Output;

struct Line
{
	unsigned int indent;

	size_t offset;
};

struct Token
{
	enum Type
	{
		TypeAtom,
		TypeBracket,
		TypeIdent,
		TypeString,
		TypeCharacter,
		TypeNumber,
		TypeLine,
		TypeEnd,
	};

	Type type;
	Str data;
	size_t offset;
	size_t matching;
	Location location;
};

struct Tokens
{
	Arr<Line> lines;
	Arr<Token> tokens;
};

Tokens tokenize(Output& output, const char* source, const Str& data);

string tokenName(Token::Type type);
string tokenName(const Token& token);