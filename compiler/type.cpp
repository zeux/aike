#include "common.hpp"
#include "type.hpp"

bool TypeConstraints::tryAdd(Ty* lhs, Ty* rhs)
{
	assert(lhs != rhs);
	assert(lhs->kind == Ty::KindUnknown || rhs->kind == Ty::KindUnknown);

	if (lhs->kind == Ty::KindUnknown && data.count(lhs) == 0 && !typeOccurs(rhs, lhs))
		data[lhs] = rhs;
	else if (rhs->kind == Ty::KindUnknown && data.count(rhs) == 0 && !typeOccurs(lhs, rhs))
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

	if (UNION_CASE(Instance, li, lhs))
	{
		UNION_CASE(Instance, ri, rhs);

		assert(li->def && ri->def);

		return li->def == ri->def;
	}

	return true;
}

bool typeEquals(Ty* lhs, Ty* rhs)
{
	return typeUnify(lhs, rhs, nullptr);
}

bool typeOccurs(Ty* lhs, Ty* rhs)
{
	if (lhs == rhs)
		return true;

	if (UNION_CASE(Function, lf, lhs))
	{
		for (auto& a: lf->args)
			if (typeOccurs(a, rhs))
				return true;

		return typeOccurs(lf->ret, rhs);
	}

	return false;
}

Ty* typeIndex(Ty* type, const Str& name)
{
	if (UNION_CASE(Instance, i, type))
	{
		if (UNION_CASE(Struct, def, i->def))
		{
			for (auto& f: def->fields)
				if (f.first == name)
					return f.second;

			return nullptr;
		}

		ICE("Unknown TyDef kind %d", i->def->kind);
	}

	return nullptr;
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

	if (UNION_CASE(Instance, t, type))
	{
		buffer += t->name.str();
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