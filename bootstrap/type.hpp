#pragma once

#include <vector>
#include <string>
#include <map>
#include <set>

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

struct PrettyPrintContext
{
	std::map<TypeGeneric*, std::string> generic_types;
	std::set<std::string> generic_names;
	unsigned int generic_autogen_index;

	PrettyPrintContext(): generic_autogen_index(0)
	{
	}
};

std::string typeName(Type* type, PrettyPrintContext& context);

Type* finalType(Type* type);