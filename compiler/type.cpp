#include "common.hpp"
#include "type.hpp"

#include "visit.hpp"

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

Ty* TypeConstraints::rewrite(Ty* type)
{
	auto it = data.find(type);

	if (it != data.end())
	{
		rewrites++;
		type = it->second;
	}

	if (UNION_CASE(Array, t, type))
	{
		Ty* element = rewrite(t->element);

		return UNION_NEW(Ty, Array, { element });
	}

	if (UNION_CASE(Function, t, type))
	{
		Arr<Ty*> args;

		for (Ty* arg: t->args)
			args.push(rewrite(arg));

		Ty* ret = rewrite(t->ret);

		return UNION_NEW(Ty, Function, { args, ret });
	}

	if (UNION_CASE(Instance, t, type))
	{
		Arr<Ty*> tyargs;

		for (Ty* arg: t->tyargs)
			tyargs.push(rewrite(arg));

		return UNION_NEW(Ty, Instance, { t->name, t->location, tyargs, t->def, t->generic });
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

	if (UNION_CASE(Array, la, lhs))
	{
		UNION_CASE(Array, ra, rhs);

		return typeUnify(la->element, ra->element, constraints);
	}

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

		assert(li->def || li->generic);
		assert(ri->def || ri->generic);

		if (li->def != ri->def || li->generic != ri->generic)
			return false;

		if (li->tyargs.size != ri->tyargs.size)
			return false;

		for (size_t i = 0; i < li->tyargs.size; ++ri)
			if (!typeUnify(li->tyargs[i], ri->tyargs[i], constraints))
				return false;

		return true;
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

	if (UNION_CASE(Array, la, lhs))
	{
		return typeOccurs(la->element, rhs);
	}

	if (UNION_CASE(Function, lf, lhs))
	{
		for (auto& a: lf->args)
			if (typeOccurs(a, rhs))
				return true;

		return typeOccurs(lf->ret, rhs);
	}

	if (UNION_CASE(Instance, li, lhs))
	{
		for (auto& a: li->tyargs)
			if (typeOccurs(a, rhs))
				return true;

		return false;
	}

	return false;
}

Ty* typeInstantiate(Ty* type, const function<Ty*(Ty*)>& inst)
{
	if (UNION_CASE(Array, t, type))
	{
		Ty* element = typeInstantiate(t->element, inst);

		return UNION_NEW(Ty, Array, { element });
	}

	if (UNION_CASE(Function, t, type))
	{
		Arr<Ty*> args;

		for (Ty* arg: t->args)
			args.push(typeInstantiate(arg, inst));

		Ty* ret = typeInstantiate(t->ret, inst);

		return UNION_NEW(Ty, Function, { args, ret });
	}

	if (UNION_CASE(Instance, t, type))
	{
		if (t->generic)
		{
			if (Ty* i = inst(t->generic))
			{
				assert(i != t->generic);
				return i;
			}

			return type;
		}
		else
		{
			Arr<Ty*> tyargs;

			for (Ty* arg: t->tyargs)
				tyargs.push(typeInstantiate(arg, inst));

			return UNION_NEW(Ty, Instance, { t->name, t->location, tyargs, t->def, t->generic });
		}
	}

	return type;
}

Ty* typeMember(Ty* type, int index)
{
	assert(index >= 0);

	if (UNION_CASE(Instance, inst, type))
	{
		if (UNION_CASE(Struct, def, inst->def))
		{
			assert(index < def->fields.size);
			assert(def->tyargs.size == inst->tyargs.size);

			return typeInstantiate(def->fields[index].type, [&](Ty* ty) -> Ty* {
				for (size_t i = 0; i < def->tyargs.size; ++i)
					if (ty == def->tyargs[i])
						return inst->tyargs[i];

				return nullptr;
			});
		}

		ICE("Unexpected TyDef kind %d", inst->def->kind);
	}

	ICE("Unexpected Ty kind %d", type->kind);

	return nullptr;
}

static void typeName(string& buffer, Ty* type)
{
	if (UNION_CASE(Unknown, t, type))
	{
		buffer += "_";
		return;
	}

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

	if (UNION_CASE(Array, t, type))
	{
		buffer += "[";
		typeName(buffer, t->element);
		buffer += "]";
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
		if (t->tyargs.size > 0)
		{
			buffer += "<";

			for (size_t i = 0; i < t->tyargs.size; ++i)
			{
				if (i != 0) buffer += ", ";
				typeName(buffer, t->tyargs[i]);
			}

			buffer += ">";
		}
		return;
	}

	if (UNION_CASE(Generic, t, type))
	{
		buffer += t->name.str();
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