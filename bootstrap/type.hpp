#pragma once

#include <vector>
#include <string>
#include <map>
#include <set>

#include "output.hpp"

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

struct TypePrototype
{
	virtual ~TypePrototype() {}
};

struct TypePrototypeRecord: TypePrototype
{
	std::string name;
	std::vector<Type*> member_types;
	std::vector<std::string> member_names;
	std::vector<Type*> generics;

	TypePrototypeRecord(const std::string& name, const std::vector<Type*>& member_types, const std::vector<std::string>& member_names, const std::vector<Type*>& generics): name(name), member_types(member_types), member_names(member_names), generics(generics)
	{
	}
};

struct TypePrototypeUnion: TypePrototype
{
	std::string name;
	std::vector<Type*> member_types;
	std::vector<std::string> member_names;
	std::vector<Type*> generics;

	TypePrototypeUnion(const std::string& name, const std::vector<Type*>& member_types, const std::vector<std::string>& member_names, const std::vector<Type*>& generics): name(name), member_types(member_types), member_names(member_names), generics(generics)
	{
	}
};

struct TypeInstance: Type
{
	TypePrototype* prototype;
	std::vector<Type*> generics;

	TypeInstance(TypePrototype* prototype, const std::vector<Type*>& generics): prototype(prototype), generics(generics)
	{
	}
};

struct TypeClosureContext: Type
{
	std::vector<Type*> member_types;
	std::vector<std::string> member_names;

	TypeClosureContext() {}
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

size_t getMemberIndexByName(TypePrototypeRecord* type, const std::string& name, const Location& location);

const std::vector<Type*>& getGenericTypes(TypePrototype* proto);
