#include "common.hpp"
#include "type.hpp"

bool typeEquals(Ty* lhs, Ty* rhs)
{
	if (lhs->kind != rhs->kind)
		return false;

	if (UNION_CASE(Function, lf, lhs))
	{
		UNION_CASE(Function, rf, rhs);

		if (lf->args.size != rf->args.size)
			return false;

		for (size_t i = 0; i < lf->args.size; ++i)
			if (!typeEquals(lf->args[i], rf->args[i]))
				return false;

		return typeEquals(lf->ret, rf->ret);
	}

	return true;
}

static void typeName(string& buffer, Ty* type)
{
	if (UNION_CASE(Void, t, type))
	{
		buffer += "void";
		return;
	}

	if (UNION_CASE(Bool, t, type))
	{
		buffer += "bool";
		return;
	}

	if (UNION_CASE(Integer, t, type))
	{
		buffer += "int";
		return;
	}

	if (UNION_CASE(String, t, type))
	{
		buffer += "string";
		return;
	}

	if (UNION_CASE(Function, t, type))
	{
		buffer += "fn(";

		for (size_t i = 0; i < t->args.size; ++i)
		{
			if (i != 0) buffer += ", ";
			typeName(buffer, t->args[i]);
		}

		buffer += ")";

		if (UNION_CASE(Void, _, t->ret))
			;
		else
		{
			buffer += ": ";
			typeName(buffer, t->ret);
		}
		return;
	}

	if (UNION_CASE(Unknown, t, type))
	{
		buffer += "_";
		return;
	}

	ICE("Unknown Ty kind %d", type->kind);
}

string typeName(Ty* type)
{
	string result;
	typeName(result, type);
	return result;
}
