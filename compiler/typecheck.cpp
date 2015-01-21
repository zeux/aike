#include "common.hpp"
#include "typecheck.hpp"

#include "ast.hpp"
#include "output.hpp"

static Ty* type(Output& output, Ast* root)
{
	if (UNION_CASE(LiteralString, n, root))
		return UNION_NEW(Ty, String, {});

	if (UNION_CASE(Ident, n, root))
	{
		assert(n->target);
		return n->target->type;
	}

	if (UNION_CASE(Block, n, root))
	{
		if (n->body.size == 0)
			return UNION_NEW(Ty, Void, {});

		for (size_t i = 0; i < n->body.size - 1; ++i)
			type(output, n->body[i]);

		return type(output, n->body[n->body.size - 1]);
	}

	if (UNION_CASE(Call, n, root))
	{
		Ty* ty = type(output, n->expr);

		if (UNION_CASE(Function, fnty, ty))
		{
			if (fnty->args.size != n->args.size)
				output.panic(Location(/*TODO*/), "Expected %d arguments but given %d", int(fnty->args.size), int(n->args.size));

			for (size_t i = 0; i < n->args.size; ++i)
			{
				Ty* arg = type(output, n->args[i]);

				if (!typeEquals(arg, fnty->args[i]))
					output.panic(Location(/*TODO*/), "Type mismatch: expected %s but given %s", typeName(fnty->args[i]).c_str(), typeName(arg).c_str());
			}

			return fnty->ret;
		}
		else
		{
			output.panic(Location(/*TODO*/), "Expression does not evaluate to a function");
		}
	}

	if (UNION_CASE(FnDecl, n, root))
	{
		if (n->body)
		{
			Ty* ret = type(output, n->body);

			if (UNION_CASE(Function, fnty, n->var->type))
			{
				if (fnty->ret->kind != Ty::KindVoid && !typeEquals(ret, fnty->ret))
					output.panic(Location(/*TODO*/), "Type mismatch: expected %s but given %s", typeName(fnty->ret).c_str(), typeName(ret).c_str());
			}
			else
				ICE("FnDecl type is not Function");
		}

		return UNION_NEW(Ty, Void, {});
	}

	if (UNION_CASE(VarDecl, n, root))
	{
		type(output, n->expr);
		return UNION_NEW(Ty, Void, {});
	}

	ICE("Unknown Ast kind %d", root->kind);
}

void typecheck(Output& output, Ast* root)
{
	type(output, root);
}