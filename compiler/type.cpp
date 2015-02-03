#include "common.hpp"
#include "type.hpp"

bool TypeConstraints::tryAdd(Ty* lhs, Ty* rhs)
{
	assert(lhs != rhs);
	assert(lhs->kind == Ty::KindUnknown || rhs->kind == Ty::KindUnknown);

	if (lhs->kind == Ty::KindUnknown && data.count(lhs) == 0)
		data[lhs] = rhs;
	else if (rhs->kind == Ty::KindUnknown && data.count(rhs) == 0)
		data[rhs] = lhs;
	else
		return false;

	return true;
}

Ty* TypeConstraints::rewrite(Ty* type) const
{
	auto it = data.find(type);

	if (it != data.end())
		type = it->second;

	if (UNION_CASE(Function, funty, type))
	{
		Array<Ty*> args;

		for (Ty* arg: funty->args)
			args.push(rewrite(arg));

		Ty* ret = rewrite(funty->ret);

		return UNION_NEW(Ty, Function, { args, ret });
	}

	return type;
}

bool typeUnify(Ty* lhs, Ty* rhs, TypeConstraints* constraints)
{
	if (lhs == rhs)
		return true;

	if (constraints && (lhs->kind == Ty::KindUnknown || rhs->kind == Ty::KindUnknown))
		return constraints->tryAdd(lhs, rhs);

	if (lhs->kind != rhs->kind)
		return false;

	if (UNION_CASE(Function, lf, lhs))
	{
		UNION_CASE(Function, rf, rhs);

		if (lf->args.size != rf->args.size)
			return false;

		for (size_t i = 0; i < lf->args.size; ++i)
			if (!typeUnify(lf->args[i], rf->args[i], constraints))
				return false;

		return typeUnify(lf->ret, rf->ret, constraints);
	}

	return true;
}

bool typeEquals(Ty* lhs, Ty* rhs)
{
	return typeUnify(lhs, rhs, nullptr);
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
