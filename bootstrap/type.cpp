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
			os << _->name;
	}
	else if (CASE(TypeUnit, type))
		os << "unit";
	else if (CASE(TypeInt, type))
		os << "int";
	else if (CASE(TypeFloat, type))
		os << "float";
	else if (CASE(TypeBool, type))
		os << "bool";
	else if (CASE(TypeReference, type))
	{
		os << "ref<";
		prettyPrint(os, _->contained, context);
		os << ">";
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
	else if (CASE(TypeStructure, type))
	{
		if (_->name.empty())
		{
			os << "[";
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
			os << _->name;
		}
	}
	else if (CASE(TypeUnion, type))
	{
		os << _->name;
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