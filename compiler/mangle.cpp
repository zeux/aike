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
		return;
	}

	ICE("Unknown Ty kind %d", type->kind);
}

static string mangleFn(const Str& name, int unnamed, Ty* type, const Arr<Ty*>& tyargs, const string& parent)
{
	string result;

	if (!parent.empty())
	{
		result += "Z";
		result += parent;
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

string mangleFn(const Str& name, Ty* type, const Arr<Ty*>& tyargs, const string& parent)
{
	return mangleFn(name, 0, type, tyargs, parent);
}

string mangleFn(int unnamed, Ty* type, const string& parent)
{
	return mangleFn(Str(), unnamed, type, Arr<Ty*>(), parent);
}

string mangle(const string& name)
{
	return "_Z" + name;
}