#include "type.hpp"

#include <sstream>
#include <cassert>

Type* finalType(Type* type)
{
	if (CASE(TypeGeneric, type))
	{
		if (_->instance)
			return finalType(_->instance);
	}

	return type;
}

std::string generateGenericName(unsigned int index)
{
	if (index < 26)
		return std::string(1, 'a' + index);
	else
	{
		std::ostringstream oss;
		oss << "a";
		oss << (index - 26);
		return oss.str();
	}
}

std::string generateGenericName(PrettyPrintContext& context)
{
	while (true)
	{
		std::string name = generateGenericName(context.generic_autogen_index);
		context.generic_autogen_index++;

		if (context.generic_names.count(name) == 0)
		{
			context.generic_names.insert(name);
			return name;
		}
	}
}

bool containedTypeRequiresBraces(Type* type)
{
	return dynamic_cast<TypeFunction*>(type) != 0;
}

void prettyPrint(std::ostream& os, Type* type, PrettyPrintContext& context);

void prettyPrint(std::ostream& os, const std::vector<Type*>& generics, PrettyPrintContext& context)
{
	if (!generics.empty())
	{
		os << "<";
		for (size_t i = 0; i < generics.size(); ++i)
		{
			if (i != 0) os << ", ";
			prettyPrint(os, generics[i], context);
		}
		os << ">";
	}
}

void prettyPrint(std::ostream& os, Type* type, PrettyPrintContext& context)
{
	type = finalType(type);

	if (CASE(TypeGeneric, type))
	{
		os << "'";

		if (_->name.empty())
		{
			if (context.generic_types.count(_) == 0)
			{
				context.generic_types[_] = generateGenericName(context);
			}
				
			os << context.generic_types[_];
		}
		else
		{
			context.generic_names.insert(_->name);

			os << _->name;
		}
	}
	else if (CASE(TypeUnit, type))
		os << "unit";
	else if (CASE(TypeInt, type))
		os << "int";
	else if (CASE(TypeFloat, type))
		os << "float";
	else if (CASE(TypeBool, type))
		os << "bool";
	else if (CASE(TypeTuple, type))
	{
		os << "(";
		for (size_t i = 0; i < _->members.size(); ++i)
		{
			if (i != 0) os << ", ";
			prettyPrint(os, _->members[i], context);
		}
		os << ")";
	}
	else if (CASE(TypeArray, type))
	{
		if (containedTypeRequiresBraces(_->contained))
			os << "(";

		prettyPrint(os, _->contained, context);

		if (containedTypeRequiresBraces(_->contained))
			os << ")";

		os << "[]";
	}
	else if (CASE(TypeFunction, type))
	{
		os << "(";
		for (size_t i = 0; i < _->args.size(); ++i)
		{
			if (i != 0) os << ", ";
			prettyPrint(os, _->args[i], context);
		}
		os << ") -> ";
		prettyPrint(os, _->result, context);
	}
	else if (CASE(TypeInstance, type))
	{
		if (TypePrototypeRecord* p = dynamic_cast<TypePrototypeRecord*>(*_->prototype))
			os << p->name;
		else if (TypePrototypeUnion* p = dynamic_cast<TypePrototypeUnion*>(*_->prototype))
			os << p->name;
		else
			assert(!"Unknown prototype");

		prettyPrint(os, _->generics, context);
	}
	else if (CASE(TypeClosureContext, type))
	{
		os << "context [";
		assert(_->member_names.size() == _->member_types.size());
		for (size_t i = 0; i < _->member_types.size(); ++i)
		{
			if (i != 0) os << ", ";
			prettyPrint(os, _->member_types[i], context);
			os << " " << _->member_names[i];
		}
		os << "]";
	}
	else
	{
		assert(!"Unknown type");
	}
}

std::string typeName(Type* type, PrettyPrintContext& context)
{
	std::ostringstream oss;
	prettyPrint(oss, type, context);
	return oss.str();
}

void mangle(std::ostream& os, Type* type, const std::function<Type* (TypeGeneric*)>& resolve_generic)
{
	type = finalType(type);

	if (CASE(TypeGeneric, type))
	{
		Type* type = resolve_generic(_);
		assert(type != _);

		mangle(os, type, resolve_generic);
	}
	else if (CASE(TypeUnit, type))
		os << "u";
	else if (CASE(TypeInt, type))
		os << "i";
	else if (CASE(TypeFloat, type))
		os << "f";
	else if (CASE(TypeBool, type))
		os << "b";
	else if (CASE(TypeTuple, type))
	{
		os << "T" << _->members.size();

		for (size_t i = 0; i < _->members.size(); ++i)
			mangle(os, _->members[i], resolve_generic);
	}
	else if (CASE(TypeArray, type))
	{
		os << "A";
		mangle(os, _->contained, resolve_generic);
	}
	else if (CASE(TypeFunction, type))
	{
		os << "F" << _->args.size();

		for (size_t i = 0; i < _->args.size(); ++i)
			mangle(os, _->args[i], resolve_generic);

		mangle(os, _->result, resolve_generic);
	}
	else if (CASE(TypeInstance, type))
	{
		os << "I" << _->generics.size();

		for (size_t i = 0; i < _->generics.size(); ++i)
			mangle(os, _->generics[i], resolve_generic);

		os << "N";

		if (TypePrototypeRecord* p = dynamic_cast<TypePrototypeRecord*>(*_->prototype))
			os << p->name.length() << p->name;
		else if (TypePrototypeUnion* p = dynamic_cast<TypePrototypeUnion*>(*_->prototype))
			os << p->name.length() << p->name;
		else
			assert(!"Unknown prototype");
	}
	else
	{
		assert(!"Unknown type");
	}
}

std::string typeNameMangled(Type* type, const std::function<Type* (TypeGeneric*)>& resolve_generic)
{
	std::ostringstream oss;
	mangle(oss, type, resolve_generic);
	return oss.str();
}

size_t getMemberIndexByName(TypePrototypeRecord* proto, const std::string& name, const Location& location)
{
	for (size_t i = 0; i < proto->member_names.size(); ++i)
		if (proto->member_names[i] == name)
			return i;

	errorf(location, "Type %s doesn't have a member named '%s'", proto->name.c_str(), name.c_str());
}

const std::vector<Type*>& getGenericTypes(TypePrototype* proto)
{
	if (CASE(TypePrototypeRecord, proto))
	{
		return _->generics;
	}

	if (CASE(TypePrototypeUnion, proto))
	{
		return _->generics;
	}

	assert(!"Unknown prototype type");

	static std::vector<Type*> dummy;
	return dummy;
}

Type* fresh(Type* t, std::map<Type*, Type*>& genremap, const Location& location)
{
	t = finalType(t);

	if (CASE(TypeGeneric, t))
	{
		if (genremap.count(_))
			return genremap[_];

		errorf(location, "Unable to instantiate generic type %s", _->name.c_str());
	}

	if (CASE(TypeArray, t))
	{
		return new TypeArray(fresh(_->contained, genremap, location));
	}

	if (CASE(TypeFunction, t))
	{
		std::vector<Type*> args;
		for (size_t i = 0; i < _->args.size(); ++i)
			args.push_back(fresh(_->args[i], genremap, location));

		return new TypeFunction(fresh(_->result, genremap, location), args);
	}

	if (CASE(TypeInstance, t))
	{
		std::vector<Type*> generics;

		for (size_t i = 0; i < _->generics.size(); ++i)
			generics.push_back(fresh(_->generics[i], genremap, location));

		return new TypeInstance(_->prototype, generics);
	}

	if (CASE(TypeTuple, t))
	{
		std::vector<Type*> members;
		for (size_t i = 0; i < _->members.size(); ++i)
			members.push_back(fresh(_->members[i], genremap, location));

		return new TypeTuple(members);
	}

	return t;
}

Type* getMemberTypeByIndex(TypeInstance* instance, TypePrototypeRecord* proto, size_t index, const Location& location)
{
	assert(*instance->prototype == proto);
	assert(instance->generics.size() == proto->generics.size());
	assert(index < proto->member_types.size());

	std::map<Type*, Type*> genremap;

	for (size_t i = 0; i < instance->generics.size(); ++i)
		genremap[proto->generics[i]] = instance->generics[i];

	return fresh(proto->member_types[index], genremap, location);
}

Type* getMemberTypeByIndex(TypeInstance* instance, TypePrototypeUnion* proto, size_t index, const Location& location)
{
	assert(*instance->prototype == proto);
	assert(instance->generics.size() == proto->generics.size());
	assert(index < proto->member_types.size());

	std::map<Type*, Type*> genremap;

	for (size_t i = 0; i < instance->generics.size(); ++i)
		genremap[proto->generics[i]] = instance->generics[i];

	return fresh(proto->member_types[index], genremap, location);
}
