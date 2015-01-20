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
		TypeEnd
	};

	Type type;
	Str data;
	size_t matching;
	Location location;
};

struct Tokens
{
	Array<Line> lines;
	Array<Token> tokens;
};

Tokens tokenize(Output& output, const char* source, const Str& data);
const char* tokenTypeName(Token::Type type);