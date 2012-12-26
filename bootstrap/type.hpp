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

struct TypeOpaquePointer: Type
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
	Type* context_type;

	TypeFunction(Type* result, const std::vector<Type*>& args, Type* context_type): result(result), args(args), context_type(context_type)
	{
	}

	Type* toGeneralType()
	{
		return new TypeFunction(result, args, new TypeOpaquePointer());
	}
};

struct TypeStructure: Type
{
	std::vector<Type*> members;

	TypeStructure()
	{
	}

	TypeStructure(const std::vector<Type*>& members): members(members)
	{
	}
};
