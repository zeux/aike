#pragma once

#include <vector>
#include <string>

struct Type
{
	virtual ~Type() {}
};

struct TypeGeneric: Type
{
	std::string name;
	Type* instance;

	TypeGeneric(): instance(NULL)
	{
	}

	TypeGeneric(const std::string& name): name(name), instance(NULL)
	{
	}
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
	std::string name;
	std::vector<Type*> member_types;
	std::vector<std::string> member_names;

	TypeStructure()
	{
	}

	TypeStructure(const std::string& name, const std::vector<Type*>& member_types, const std::vector<std::string>& member_names): name(name), member_types(member_types), member_names(member_names)
	{
	}
};

#ifndef CASE
#define CASE(type, node) type* _ = dynamic_cast<type*>(node)
#endif

Type* finalType(Type* type);

std::string typeName(Type* type);