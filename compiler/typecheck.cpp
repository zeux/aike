#include "common.hpp"
#include "typecheck.hpp"

#include "ast.hpp"
#include "visit.hpp"
#include "output.hpp"

static void typeMustEqual(Ty* type, Ty* expected, TypeConstraints* constraints, Output& output, const Location& location)
{
	if (!typeUnify(type, expected, constraints) && !constraints)
		output.panic(location, "Type mismatch: expected %s but given %s", typeName(expected).c_str(), typeName(type).c_str());

	if (!constraints && type->kind == Ty::KindUnknown)
		output.panic(location, "Type mismatch: expected a known type");
}

static void validateLiteralStruct(Output& output, Ast::LiteralStruct* n)
{
	if (UNION_CASE(Instance, i, n->type))
	{
		if (UNION_CASE(Struct, def, i->def))
		{
			vector<bool> fields(def->fields.size);

			for (auto& f: n->fields)
			{
				assert(f.first.index >= 0);

				if (fields[f.first.index])
					output.panic(f.first.location, "Field %s already has an initializer", f.first.name.str().c_str());

				fields[f.first.index] = true;
			}

			for (size_t i = 0; i < def->fields.size; ++i)
			{
				const StructField& f = def->fields[i];

				if (!fields[i] && !f.expr)
					output.panic(n->location, "Field %s does not have an initializer", f.name.str().c_str());
			}
		}
		else
			output.panic(n->location, "Type mismatch: expected a struct type but given %s", typeName(n->type).c_str());
	}
	else
		output.panic(n->location, "Type mismatch: expected a struct type but given %s", typeName(n->type).c_str());
}

static bool isAssignable(Ast* node)
{
	if (UNION_CASE(Ident, n, node))
		return n->target->kind == Variable::KindVariable;
	else if (UNION_CASE(Member, n, node))
		return isAssignable(n->expr);
	else if (UNION_CASE(Index, n, node))
		return true;
	else if (UNION_CASE(Unary, n, node))
		return n->op == UnaryOpDeref;
	else
		return false;
}

static pair<Ty*, Location> type(Output& output, Ast* root, TypeConstraints* constraints)
{
	if (UNION_CASE(LiteralBool, n, root))
		return make_pair(UNION_NEW(Ty, Bool, {}), n->location);

	if (UNION_CASE(LiteralNumber, n, root))
		return make_pair(UNION_NEW(Ty, Integer, {}), n->location);

	if (UNION_CASE(LiteralString, n, root))
		return make_pair(UNION_NEW(Ty, String, {}), n->location);

	if (UNION_CASE(LiteralArray, n, root))
	{
		if (UNION_CASE(Array, t, n->type))
		{
			for (auto& e: n->elements)
			{
				auto expr = type(output, e, constraints);

				typeMustEqual(expr.first, t->element, constraints, output, expr.second);
			}

			if (!constraints && t->element->kind == Ty::KindUnknown)
				output.panic(n->location, "Type mismatch: expected a known type");
		}
		else
			ICE("LiteralArray type is not Array");

		return make_pair(n->type, n->location);
	}

	if (UNION_CASE(LiteralStruct, n, root))
	{
		for (auto& f: n->fields)
		{
			auto expr = type(output, f.second, constraints);

			if (f.first.index >= 0)
				typeMustEqual(expr.first, typeMember(n->type, f.first.index), constraints, output, expr.second);
		}

		if (!constraints)
			validateLiteralStruct(output, n);

		return make_pair(n->type, n->location);
	}

	if (UNION_CASE(Ident, n, root))
	{
		assert(n->type);

		if (!constraints)
			for (auto& a: n->tyargs)
				if (a->kind == Ty::KindUnknown)
				{
					string inst;
					for (auto& a: n->tyargs)
					{
						if (!inst.empty()) inst += ", ";
						inst += typeName(a);
					}

					output.panic(n->location, "Unable to instantiate %s<%s>", n->name.str().c_str(), inst.c_str());
				}

		return make_pair(n->type, n->location);
	}

	if (UNION_CASE(Member, n, root))
	{
		auto expr = type(output, n->expr, constraints);

		n->exprty = expr.first;

		if (n->field.index >= 0)
			return make_pair(typeMember(n->exprty, n->field.index), n->location);
		else if (constraints)
			return make_pair(UNION_NEW(Ty, Unknown, {}), Location());
		else
			output.panic(expr.second, "%s does not have a field %s", typeName(expr.first).c_str(), n->field.name.str().c_str());
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
			Arr<Ty*> args;

			for (auto& a: n->args)
				args.push(type(output, a, constraints).first);

			Ty* ret = UNION_NEW(Ty, Unknown, {});

			typeMustEqual(expr.first, UNION_NEW(Ty, Function, { args, ret }), constraints, output, expr.second);

			return make_pair(ret, expr.second);
		}
	}

	if (UNION_CASE(Unary, n, root))
	{
		auto expr = type(output, n->expr, constraints);

		switch (n->op)
		{
		case UnaryOpPlus:
		case UnaryOpMinus:
			typeMustEqual(expr.first, UNION_NEW(Ty, Integer, {}), constraints, output, expr.second);
			return expr;

		case UnaryOpNot:
			typeMustEqual(expr.first, UNION_NEW(Ty, Bool, {}), constraints, output, expr.second);
			return expr;

		case UnaryOpDeref:
			if (UNION_CASE(Pointer, t, expr.first))
				return make_pair(t->element, expr.second); // TODO: Location
			else
			{
				Ty* ret = UNION_NEW(Ty, Unknown, {});

				typeMustEqual(expr.first, UNION_NEW(Ty, Pointer, { ret }), constraints, output, expr.second);

				return make_pair(ret, expr.second); // TODO: Location
			}

		case UnaryOpNew:
			return make_pair(UNION_NEW(Ty, Pointer, { expr.first }), expr.second); // TODO: Location

		default:
			ICE("Unknown UnaryOp %d", n->op);
		}
	}

	if (UNION_CASE(Binary, n, root))
	{
		auto left = type(output, n->left, constraints);
		auto right = type(output, n->right, constraints);

		switch (n->op)
		{
			case BinaryOpAddWrap:
			case BinaryOpSubtractWrap:
			case BinaryOpMultiplyWrap:
			case BinaryOpAdd:
			case BinaryOpSubtract:
			case BinaryOpMultiply:
			case BinaryOpDivide:
			case BinaryOpModulo:
				typeMustEqual(left.first, UNION_NEW(Ty, Integer, {}), constraints, output, left.second);
				typeMustEqual(right.first, UNION_NEW(Ty, Integer, {}), constraints, output, right.second);
				return left;

			case BinaryOpLess:
			case BinaryOpLessEqual:
			case BinaryOpGreater:
			case BinaryOpGreaterEqual:
				typeMustEqual(left.first, UNION_NEW(Ty, Integer, {}), constraints, output, left.second);
				typeMustEqual(right.first, UNION_NEW(Ty, Integer, {}), constraints, output, right.second);
				return make_pair(UNION_NEW(Ty, Bool, {}), Location()); // TODO: Location

			case BinaryOpEqual:
			case BinaryOpNotEqual:
				typeMustEqual(left.first, right.first, constraints, output, left.second);

				if (!constraints && left.first->kind != Ty::KindInteger && left.first->kind != Ty::KindBool)
					output.panic(left.second, "Type mismatch: expected int or bool but given %s", typeName(left.first).c_str());

				return make_pair(UNION_NEW(Ty, Bool, {}), Location()); // TODO: Location

			case BinaryOpAnd:
			case BinaryOpOr:
				typeMustEqual(left.first, UNION_NEW(Ty, Bool, {}), constraints, output, left.second);
				typeMustEqual(right.first, UNION_NEW(Ty, Bool, {}), constraints, output, right.second);
				return left;

			default:
				ICE("Unknown BinaryOp %d", n->op);
		}
	}

	if (UNION_CASE(Index, n, root))
	{
		auto expr = type(output, n->expr, constraints);
		auto index = type(output, n->index, constraints);

		typeMustEqual(index.first, UNION_NEW(Ty, Integer, {}), constraints, output, index.second);

		if (UNION_CASE(Array, t, expr.first))
		{
			return make_pair(t->element, n->location);
		}
		else
		{
			Ty* element = UNION_NEW(Ty, Unknown, {});

			typeMustEqual(expr.first, UNION_NEW(Ty, Array, { element }), constraints, output, expr.second);

			return make_pair(element, n->location);
		}
	}

	if (UNION_CASE(Assign, n, root))
	{
		auto left = type(output, n->left, constraints);
		auto right = type(output, n->right, constraints);

		typeMustEqual(right.first, left.first, constraints, output, right.second);

		if (!isAssignable(n->left))
			output.panic(n->location, "Expression is not assignable");

		return make_pair(UNION_NEW(Ty, Void, {}), Location());
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

	if (UNION_CASE(For, n, root))
	{
		auto expr = type(output, n->expr, constraints);
		auto body = type(output, n->body, constraints);

		typeMustEqual(expr.first, UNION_NEW(Ty, Array, { n->var->type }), constraints, output, expr.second);

		if (n->index)
			typeMustEqual(n->index->type, UNION_NEW(Ty, Integer, {}), constraints, output, n->index->location);

		return make_pair(UNION_NEW(Ty, Void, {}), Location());
	}

	if (UNION_CASE(Fn, n, root))
	{
		auto ret = type(output, n->decl, constraints);

		UNION_CASE(FnDecl, decl, n->decl);
		assert(decl);

		return make_pair(decl->var->type, n->location);
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

	if (UNION_CASE(TyDecl, n, root))
	{
		if (UNION_CASE(Struct, t, n->def))
		{
			for (auto& c: t->fields)
				if (c.expr)
				{
					auto expr = type(output, c.expr, constraints);

					typeMustEqual(expr.first, c.type, constraints, output, expr.second);
				}
		}

		return make_pair(UNION_NEW(Ty, Void, {}), Location());
	}

	ICE("Unknown Ast kind %d", root->kind);
}

static bool propagate(TypeConstraints& constraints, Ast* root)
{
	if (UNION_CASE(LiteralArray, n, root))
	{
		n->type = constraints.rewrite(n->type);
	}
	else if (UNION_CASE(LiteralStruct, n, root))
	{
		n->type = constraints.rewrite(n->type);
	}
	else if (UNION_CASE(Ident, n, root))
	{
		n->type = constraints.rewrite(n->type);

		for (auto& a: n->tyargs)
			a = constraints.rewrite(a);
	}
	else if (UNION_CASE(For, n, root))
	{
		n->var->type = constraints.rewrite(n->var->type);

		if (n->index)
			n->index->type = constraints.rewrite(n->index->type);
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

static void instantiateType(Output& output, Ty* type)
{
	if (UNION_CASE(Instance, t, type))
	{
		assert(t->def || t->generic);

		if (t->def)
		{
			if (UNION_CASE(Struct, def, t->def))
			{
				if (t->tyargs.size == 0)
				{
					for (auto& arg: def->tyargs)
						t->tyargs.push(UNION_NEW(Ty, Unknown, {}));
				}
				else
				{
					if (t->tyargs.size != def->tyargs.size)
						output.panic(t->location, "Expected %d type arguments but given %d", int(def->tyargs.size), int(t->tyargs.size));
				}
			}
			else
				ICE("Unknown TyDef kind %d", t->def->kind);
		}
		else
		{
			if (t->tyargs.size != 0)
				output.panic(t->location, "Expected 0 type arguments but given %d", int(t->tyargs.size));
		}
	}
}

static bool instantiateNode(Output& output, Ast* node)
{
	visitAstTypes(node, instantiateType, output);

	if (UNION_CASE(Ident, n, node))
	{
		Variable* var = n->target;

		if (var->kind == Variable::KindFunction)
		{
			UNION_CASE(FnDecl, decl, var->fn);
			assert(decl);

			if (n->tyargs.size == 0)
			{
				for (auto& arg: decl->tyargs)
					n->tyargs.push(UNION_NEW(Ty, Unknown, {}));
			}
			else
			{
				if (n->tyargs.size != decl->tyargs.size)
					output.panic(n->location, "Expected %d type arguments but given %d", int(decl->tyargs.size), int(n->tyargs.size));
			}

			n->type = typeInstantiate(var->type, [&](Ty* ty) -> Ty* {
				for (size_t i = 0; i < decl->tyargs.size; ++i)
					if (ty == decl->tyargs[i])
						return n->tyargs[i];

				return nullptr;
			});
		}
		else
		{
			n->type = var->type;
		}

		return true;
	}

	return false;
}

void typeckInstantiate(Output& output, Ast* root)
{
	visitAst(root, instantiateNode, output);
}

int typeckPropagate(Output& output, Ast* root)
{
	TypeConstraints constraints;
	type(output, root, &constraints);

	if (!constraints.data.empty())
		visitAst(root, propagate, constraints);

	return constraints.rewrites;
}

void typeckVerify(Output& output, Ast* root)
{
	type(output, root, nullptr);
}