#pragma once

#include "string.hpp"

namespace lexer {

struct Token
{
	enum Type
	{
		TypeAtom,
		TypeIdent,
		TypeString,
		TypeNumber,
	};

	Type type;
	Str data;
};

struct Line
{
	unsigned int indent;

	size_t offset;
};

struct Lines
{
	vector<Line> lines;
};

struct Tokens
{
	vector<Token> tokens;
};

Lines lines(const Str& data);
Tokens tokenize(const Str& data);

}