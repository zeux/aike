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

struct TypeFunction: Type
{
    Type* result;
    std::vector<Type*> args;

	TypeFunction(Type* result, const std::vector<Type*>& args): result(result), args(args)
    {
    }
};
