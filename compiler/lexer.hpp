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
		TypeCharacter,
		TypeNumber,
	};

	Type type;
	Str data;
};

struct Line
{
	unsigned int indent;

	size_t start, end;
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
pair<int, int> getLocation(const Lines& lines, size_t offset);

Tokens tokenize(const Str& data, const Lines& lines);

}