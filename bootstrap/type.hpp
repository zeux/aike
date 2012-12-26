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

struct TypeBool: Type
{
};

struct TypeReference: Type
{
	Type* contained;

	TypeReference(Type* contained): contained(contained)
	{
	}
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

struct TypeStructure: Type
{
	std::vector<Type*> members;

	TypeStructure(const std::vector<Type*>& members): members(members)
	{
	}
};
