#include "common.hpp"
#include "mangle.hpp"

#include "type.hpp"

static void mangle(string& buffer, Ty* type)
{
	if (UNION_CASE(Void, t, type))
	{
		buffer += "v";
		return;
	}

	if (UNION_CASE(Bool, t, type))
	{
		buffer += "b";
		return;
	}

	if (UNION_CASE(Integer, t, type))
	{
		buffer += "i";
		return;
	}

	if (UNION_CASE(String, t, type))
	{
		buffer += "6string";
		return;
	}

	if (UNION_CASE(Array, t, type))
	{
		buffer += "U5array";
		mangle(buffer, t->element);
		return;
	}

	if (UNION_CASE(Function, t, type))
	{
		buffer += "F";

		mangle(buffer, t->ret);

		for (auto& a: t->args)
			mangle(buffer, a);

		buffer += "E";
		return;
	}

	if (UNION_CASE(Instance, t, type))
	{
		assert(!t->generic);

		buffer += to_string(t->name.size);
		buffer += t->name.str();

		if (t->tyargs.size > 0)
		{
			buffer += "I";

			for (auto& a: t->tyargs)
				mangle(buffer, a);

			buffer += "E";
		}
		return;
	}

	ICE("Unknown Ty kind %d", type->kind);
}

string mangleFn(const Str& name, int unnamed, Ty* type, const Arr<Ty*>& tyargs, const string& parent)
{
	string result;

	result += "_Z";

	if (!parent.empty())
	{
		assert(parent.size() > 2 && parent[0] == '_' && parent[1] == 'Z');

		result += "Z";
		result += parent.substr(2);
		result += "E";
	}

	if (name.size > 0)
	{
		result += to_string(name.size);
		result += name.str();
	}
	else
	{
		result += "Ut";
		result += to_string(unnamed);
		result += "_";
	}

	UNION_CASE(Function, t, type);
	assert(t);

	if (tyargs.size > 0)
	{
		result += "I";

		for (auto& a: tyargs)
			mangle(result, a);

		result += "E";

		// Itanium mangling rules are... weird
		mangle(result, t->ret);
	}

	for (auto& a: t->args)
		mangle(result, a);

	return result;
}

string mangleType(Ty* type)
{
	string result;
	mangle(result, type);
	return result;
}