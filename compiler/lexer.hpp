#pragma once

#include "string.hpp"
#include "location.hpp"

namespace lexer {

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
		TypeIdent,
		TypeString,
		TypeCharacter,
		TypeNumber,
	};

	Type type;
	Str data;
	Location location;
};

struct Tokens
{
	vector<Line> lines;
	vector<Token> tokens;
};

Tokens tokenize(const char* source, const Str& data);

}