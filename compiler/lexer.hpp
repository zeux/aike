#pragma once

#include "string.hpp"

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
	StringPiece data;
};

struct Tokens
{
	unique_ptr<char[]> data;

	vector<Token> tokens;
};

Tokens tokenize(const string& data);