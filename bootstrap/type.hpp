#pragma once

#include <vector>

struct Type
{
	virtual ~Type() {}
};

struct TypeGeneric: Type
{
};

struct TypeUnit: Type
{
};

struct TypeInt: Type
{
};

struct TypeFloat: Type
{
};

struct TypeArray: Type
{
	Type* contained;

	TypeArray(Type* contained): contained(contained)
	{
	}
};

struct TypeFunction: Type
{
	Type* result;
	std::vector<Type*> args;

	TypeFunction(Type* result, const std::vector<Type*>& args): result(result), args(args)
	{
	}
};
