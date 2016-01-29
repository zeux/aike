#include "common.hpp"
#include "mangle.hpp"

#include "type.hpp"

// This implementation is based on Itanium C++ ABI
// https://mentorembedded.github.io/cxx-abi/abi.html#mangling

static void mangleName(string& result, const Str& name)
{
	result += to_string(name.size);
	result += name.str();
}

static void mangle(string& buffer, Ty* type, const function<Ty*(Ty*)>& inst)
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

	if (UNION_CASE(Float, t, type))
	{
		buffer += "f";
		return;
	}

	if (UNION_CASE(String, t, type))
	{
		// N..E is a workaround for http://reviews.llvm.org/D13192
		buffer += "N6stringE";
		return;
	}

	if (UNION_CASE(Array, t, type))
	{
		buffer += "U5array";
		mangle(buffer, t->element, inst);
		return;
	}

	if (UNION_CASE(Pointer, t, type))
	{
		buffer += "U3ptr";
		mangle(buffer, t->element, inst);
		return;
	}

	if (UNION_CASE(Function, t, type))
	{
		buffer += "F";

		mangle(buffer, t->ret, inst);

		for (auto& a: t->args)
			mangle(buffer, a, inst);

		if (t->varargs)
			buffer += "z";

		buffer += "E";
		return;
	}

	if (UNION_CASE(Instance, t, type))
	{
		if (t->generic)
		{
			mangle(buffer, inst(t->generic), inst);
			return;
		}
		else
		{
			// N..E is a workaround for http://reviews.llvm.org/D13192
			buffer += "N";
			mangleName(buffer, t->name);

			if (t->tyargs.size > 0)
			{
				buffer += "I";

				for (auto& a: t->tyargs)
					mangle(buffer, a, inst);

				buffer += "E";
			}
			buffer += "E";
			return;
		}
	}

	ICE("Unknown Ty kind %d", type->kind);
}

static void mangleFnName(string& result, const Str& name, int unnamed, const Arr<Ty*>& tyargs, const function<Ty*(Ty*)>& inst)
{
	if (name.size > 0)
	{
		mangleName(result, name);
	}
	else
	{
		// <unnamed-type-name>
		result += "Ut";
		result += to_string(unnamed);
		result += "_";
	}

	if (tyargs.size > 0)
	{
		result += "I";

		for (auto& a: tyargs)
			mangle(result, a, inst);

		result += "E";
	}
}

string mangleFn(const Str& name, int unnamed, Ty* type, const Arr<Ty*>& tyargs, const function<Ty*(Ty*)>& inst, const string& parent)
{
	UNION_CASE(Function, t, type);
	assert(t);

	string result;

	result += "_Z";

	if (!parent.empty())
	{
		// Nested names have different mangling based on whether parent is a function or a namespace
		// See <nested-name> vs <local-name>
		if (parent.size() > 2 && parent[0] == '_' && parent[1] == 'Z')
		{
			result += "Z";
			result += parent.substr(2);
			result += "E";

			mangleFnName(result, name, unnamed, tyargs, inst);
		}
		else
		{
			result += "N";
			result += parent;

			mangleFnName(result, name, unnamed, tyargs, inst);

			result += "E";
		}
	}
	else
	{
		mangleFnName(result, name, unnamed, tyargs, inst);
	}

	if (tyargs.size > 0)
	{
		// Itanium mangling rules are... weird
		mangle(result, t->ret, inst);
	}

	if (t->args.size == 0 && !t->varargs)
	{
		result += "v";
	}
	else
	{
		for (auto& a: t->args)
			mangle(result, a, inst);

		if (t->varargs)
			result += "z";
	}

	return result;
}

string mangleType(Ty* type, const function<Ty*(Ty*)>& inst)
{
	string result;
	mangle(result, type, inst);
	return result;
}

string mangleTypeInfo(Ty* type, const function<Ty*(Ty*)>& inst)
{
	string result;

	result += "_Z";
	result += "TI";

	mangle(result, type, inst);

	return result;
}

string mangleModule(const Str& name)
{
	string result;
	mangleName(result, name);
	return result;
}