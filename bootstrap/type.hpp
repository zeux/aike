#pragma once

#include <vector>
#include <string>

struct Type
{
	virtual ~Type() {}
};

struct TypeGeneric: Type
{
	Type* instance;

	TypeGeneric(): instance(NULL)
	{
	}
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

#ifndef CASE
#define CASE(type, node) type* _ = dynamic_cast<type*>(node)
#endif

Type* finalType(Type* type);

std::string typeName(Type* type);