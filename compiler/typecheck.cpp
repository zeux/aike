#include "common.hpp"
#include "typecheck.hpp"

#include "ast.hpp"
#include "output.hpp"

static pair<Ty*, Location> type(Output& output, Ast* root)
{
	if (UNION_CASE(LiteralString, n, root))
		return make_pair(UNION_NEW(Ty, String, {}), n->location);

	if (UNION_CASE(LiteralNumber, n, root))
		return make_pair(UNION_NEW(Ty, Integer, {}), n->location);

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
			type(output, n->body[i]);

		return type(output, n->body[n->body.size - 1]);
	}

	if (UNION_CASE(Call, n, root))
	{
		auto expr = type(output, n->expr);

		if (UNION_CASE(Function, fnty, expr.first))
		{
			if (fnty->args.size != n->args.size)
				output.panic(n->location, "Expected %d arguments but given %d", int(fnty->args.size), int(n->args.size));

			for (size_t i = 0; i < n->args.size; ++i)
			{
				auto arg = type(output, n->args[i]);

				if (!typeEquals(arg.first, fnty->args[i]))
					output.panic(arg.second, "Type mismatch: expected %s but given %s", typeName(fnty->args[i]).c_str(), typeName(arg.first).c_str());
			}

			return make_pair(fnty->ret, n->location);
		}
		else
		{
			output.panic(expr.second, "Expression does not evaluate to a function");
		}
	}

	if (UNION_CASE(FnDecl, n, root))
	{
		if (n->body)
		{
			auto ret = type(output, n->body);

			if (UNION_CASE(Function, fnty, n->var->type))
			{
				if (fnty->ret->kind != Ty::KindVoid && !typeEquals(ret.first, fnty->ret))
					output.panic(ret.second, "Type mismatch: expected %s but given %s", typeName(fnty->ret).c_str(), typeName(ret.first).c_str());
			}
			else
				ICE("FnDecl type is not Function");
		}

		return make_pair(UNION_NEW(Ty, Void, {}), Location());
	}

	if (UNION_CASE(VarDecl, n, root))
	{
		type(output, n->expr);
		return make_pair(UNION_NEW(Ty, Void, {}), Location());
	}

	ICE("Unknown Ast kind %d", root->kind);
}

void typecheck(Output& output, Ast* root)
{
	type(output, root);
}