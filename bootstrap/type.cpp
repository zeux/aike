#include "type.hpp"

#include "dump.hpp"

#include <sstream>

Type* finalType(Type* type)
{
	if (CASE(TypeGeneric, type))
	{
		if (_->instance)
			return finalType(_->instance);
	}

	return type;
}

std::string typeName(Type* type)
{
	std::ostringstream oss;
	dump(oss, type);
	return oss.str();
}