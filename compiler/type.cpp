#include "common.hpp"
#include "type.hpp"

#include "visit.hpp"

bool TypeConstraints::tryAdd(Ty* lhs, Ty* rhs)
{
	assert(lhs != rhs);
	assert(lhs->kind == Ty::KindUnknown || rhs->kind == Ty::KindUnknown);

	if (lhs->kind == Ty::KindUnknown && rhs->kind == Ty::KindUnknown)
	{
		auto li = data.find(lhs);
		auto ri = data.find(rhs);

		if (li == data.end() && ri == data.end())
			data[lhs] = rhs;
		else if (li == data.end() && ri->second != lhs)
			data[lhs] = ri->second;
		else if (ri == data.end() && li->second != rhs)
			data[rhs] = li->second;
		else
			return false;
	}
	else
	{
		if (lhs->kind != Ty::KindUnknown)
			std::swap(lhs, rhs);

		auto li = data.find(lhs);

		if (li != data.end())
			return typeEquals(li->second, rhs);
		else if (!typeOccurs(rhs, lhs))
			data[lhs] = rhs;
		else
			return false;
	}

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

	if (UNION_CASE(Pointer, t, type))
	{
		Ty* element = rewrite(t->element);

		return UNION_NEW(Ty, Pointer, { element });
	}

	if (UNION_CASE(Function, t, type))
	{
		Arr<Ty*> args;

		for (Ty* arg: t->args)
			args.push(rewrite(arg));

		Ty* ret = rewrite(t->ret);

		return UNION_NEW(Ty, Function, { args, ret, t->varargs });
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

	if (UNION_CASE(Pointer, lp, lhs))
	{
		UNION_CASE(Pointer, rp, rhs);

		return typeUnify(lp->element, rp->element, constraints);
	}

	if (UNION_CASE(Function, lf, lhs))
	{
		UNION_CASE(Function, rf, rhs);

		if (lf->args.size != rf->args.size)
			return false;

		if (lf->varargs != rf->varargs)
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

		for (size_t i = 0; i < li->tyargs.size; ++i)
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

	if (UNION_CASE(Pointer, lp, lhs))
	{
		return typeOccurs(lp->element, rhs);
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

bool typeKnown(Ty* type)
{
	if (UNION_CASE(Unknown, tu, type))
	{
		return false;
	}

	if (UNION_CASE(Array, ta, type))
	{
		return typeKnown(ta->element);
	}

	if (UNION_CASE(Pointer, tp, type))
	{
		return typeKnown(tp->element);
	}

	if (UNION_CASE(Function, tf, type))
	{
		for (auto& a: tf->args)
			if (!typeKnown(a))
				return false;

		return typeKnown(tf->ret);
	}

	if (UNION_CASE(Instance, ti, type))
	{
		for (auto& a: ti->tyargs)
			if (!typeKnown(a))
				return false;

		return true;
	}

	return true;
}

Ty* typeInstantiate(Ty* type, const function<Ty*(Ty*)>& inst)
{
	if (UNION_CASE(Array, t, type))
	{
		Ty* element = typeInstantiate(t->element, inst);

		return UNION_NEW(Ty, Array, { element });
	}

	if (UNION_CASE(Pointer, t, type))
	{
		Ty* element = typeInstantiate(t->element, inst);

		return UNION_NEW(Ty, Pointer, { element });
	}

	if (UNION_CASE(Function, t, type))
	{
		Arr<Ty*> args;

		for (Ty* arg: t->args)
			args.push(typeInstantiate(arg, inst));

		Ty* ret = typeInstantiate(t->ret, inst);

		return UNION_NEW(Ty, Function, { args, ret, t->varargs });
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

	if (UNION_CASE(Float, t, type))
	{
		buffer += "float";
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

	if (UNION_CASE(Pointer, t, type))
	{
		buffer += "*";
		typeName(buffer, t->element);
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

		if (t->varargs)
		{
			if (t->args.size != 0) buffer += ", ";
			buffer += "...";
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