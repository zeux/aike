#pragma once

#include "string.hpp"

struct Ast
{
	enum Kind
	{
		KindString,
		KindIdent
	} kind;

	struct String
	{
		Str value;
	};

	struct Ident
	{
		Str name;
	};

	union
	{
		int dummy;
		String dataString;
		Ident dataIdent;
	};
};