#include "common.hpp"
#include "typecheck.hpp"

#include "ast.hpp"
#include "output.hpp"

static void typeMustEqual(Ty* type, Ty* expected, TypeConstraints* constraints, Output& output, const Location& location)
{
	if (!typeUnify(type, expected, constraints) && !constraints)
		output.panic(location, "Type mismatch: expected %s but given %s", typeName(expected).c_str(), typeName(type).c_str());

	if (!constraints && type->kind == Ty::KindUnknown)
		output.panic(location, "Type mismatch: expected a known type");
}

static pair<Ty*, Location> type(Output& output, Ast* root, TypeConstraints* constraints)
{
	if (UNION_CASE(LiteralBool, n, root))
		return make_pair(UNION_NEW(Ty, Bool, {}), n->location);

	if (UNION_CASE(LiteralNumber, n, root))
		return make_pair(UNION_NEW(Ty, Integer, {}), n->location);

	if (UNION_CASE(LiteralString, n, root))
		return make_pair(UNION_NEW(Ty, String, {}), n->location);

	if (UNION_CASE(Ident, n, root))
	{
		assert(n->target);
		return make_pair(n->target->type, n->location);
	}

	if (UNION_CASE(Block, n, root))
	{
		if (n->body.size == 0)
			return make_pair(UNION_NEW(Ty, Void, {}), Location());

		for (size_t i = 0; i < n->body.size - 1; ++i)
			type(output, n->body[i], constraints);

		return type(output, n->body[n->body.size - 1], constraints);
	}

	if (UNION_CASE(Call, n, root))
	{
		auto expr = type(output, n->expr, constraints);

		if (UNION_CASE(Function, fnty, expr.first))
		{
			if (fnty->args.size != n->args.size)
				output.panic(n->location, "Expected %d arguments but given %d", int(fnty->args.size), int(n->args.size));

			for (size_t i = 0; i < n->args.size; ++i)
			{
				auto arg = type(output, n->args[i], constraints);

				typeMustEqual(arg.first, fnty->args[i], constraints, output, arg.second);
			}

			return make_pair(fnty->ret, n->location);
		}
		else
		{
			if (expr.first->kind == Ty::KindUnknown && constraints)
			{
				Array<Ty*> args;

				for (auto& a: n->args)
					args.push(UNION_NEW(Ty, Unknown, {}));

				Ty* ret = UNION_NEW(Ty, Unknown, {});

				typeMustEqual(expr.first, UNION_NEW(Ty, Function, { args, ret }), constraints, output, Location());

				return make_pair(ret, Location());
			}
			else
				output.panic(expr.second, "Expression does not evaluate to a function");
		}
	}

	if (UNION_CASE(If, n, root))
	{
		auto cond = type(output, n->cond, constraints);

		typeMustEqual(cond.first, UNION_NEW(Ty, Bool, {}), constraints, output, cond.second);

		auto thenty = type(output, n->thenbody, constraints);

		if (n->elsebody)
		{
			auto elsety = type(output, n->elsebody, constraints);

			typeMustEqual(elsety.first, thenty.first, constraints, output, elsety.second);
		}
		else
		{
			typeMustEqual(thenty.first, UNION_NEW(Ty, Void, {}), constraints, output, thenty.second);
		}

		return thenty;
	}

	if (UNION_CASE(Fn, n, root))
	{
		auto ret = type(output, n->body, constraints);

		if (UNION_CASE(Function, fnty, n->type))
		{
			if (fnty->ret->kind != Ty::KindVoid)
				typeMustEqual(ret.first, fnty->ret, constraints, output, ret.second);
		}
		else
			ICE("FnDecl type is not Function");

		return make_pair(n->type, n->location);
	}

	if (UNION_CASE(FnDecl, n, root))
	{
		if (n->body)
		{
			auto ret = type(output, n->body, constraints);

			if (UNION_CASE(Function, fnty, n->var->type))
			{
				if (fnty->ret->kind != Ty::KindVoid)
					typeMustEqual(ret.first, fnty->ret, constraints, output, ret.second);
			}
			else
				ICE("FnDecl type is not Function");
		}

		return make_pair(UNION_NEW(Ty, Void, {}), Location());
	}

	if (UNION_CASE(VarDecl, n, root))
	{
		auto expr = type(output, n->expr, constraints);

		typeMustEqual(expr.first, n->var->type, constraints, output, expr.second);

		return make_pair(UNION_NEW(Ty, Void, {}), Location());
	}

	ICE("Unknown Ast kind %d", root->kind);
}

static bool propagate(TypeConstraints& constraints, Ast* root)
{
	if (UNION_CASE(Fn, n, root))
	{
		n->type = constraints.rewrite(n->type);

		for (auto& arg: n->args)
			arg->type = constraints.rewrite(arg->type);
	}
	else if (UNION_CASE(FnDecl, n, root))
	{
		n->var->type = constraints.rewrite(n->var->type);

		for (auto& arg: n->args)
			arg->type = constraints.rewrite(arg->type);
	}
	else if (UNION_CASE(VarDecl, n, root))
	{
		n->var->type = constraints.rewrite(n->var->type);
	}

	return false;
}

int typeckPropagate(Output& output, Ast* root)
{
	TypeConstraints constraints;
	type(output, root, &constraints);

	if (!constraints.data.empty())
		visitAst(root, propagate, constraints);

	return constraints.data.size();
}

void typeckVerify(Output& output, Ast* root)
{
	type(output, root, nullptr);
}