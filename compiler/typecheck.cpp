#include "common.hpp"
#include "typecheck.hpp"

#include "ast.hpp"
#include "visit.hpp"
#include "output.hpp"

static void typeMustKnow(Ty* type, Output& output, const Location& location)
{
	if (!typeKnown(type))
		output.panic(location, "Expected a known type but given %s", typeName(type).c_str());
}

static void typeMustEqual(Ty* type, Ty* expected, TypeConstraints* constraints, Output& output, const Location& location)
{
	if (!typeUnify(type, expected, constraints) && !constraints)
		output.panic(location, "Type mismatch: expected %s but given %s", typeName(expected).c_str(), typeName(type).c_str());

	if (!constraints && !typeKnown(type))
		output.panic(location, "Type mismatch: expected a known type but given %s", typeName(type).c_str());
}

static void typeMustEqual(Ast* node, Ty* expected, TypeConstraints* constraints, Output& output)
{
	typeMustEqual(astType(node), expected, constraints, output, astLocation(node));
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

static string getCandidates(const Arr<Variable*>& targets)
{
	string result;

	for (Variable* v: targets)
	{
		char linecolumn[32];
		sprintf(linecolumn, "(%d,%d)", v->location.line + 1, v->location.column + 1);

		result += "\tCandidate: ";
		result += typeName(v->type);
		result += "; declared at ";
		result += v->location.source;
		result += linecolumn;
		result += "\n";
	}

	// Since the result is used for error output, remove trailing newline
	if (!result.empty())
		result.pop_back();

	return result;
}

static bool isArgumentCountValid(Ty::Function* fnty, size_t args)
{
	return fnty->varargs ? fnty->args.size <= args : fnty->args.size == args;
}

static bool isCandidateValid(Variable* target, const vector<Ty*>& args)
{
	assert(target->kind == Variable::KindFunction);

	UNION_CASE(Function, fnty, target->type);
	assert(fnty);

	if (!isArgumentCountValid(fnty, args.size()))
		return false;

	TypeConstraints constraints;

	for (size_t i = 0; i < fnty->args.size; ++i)
		if (!typeUnify(fnty->args[i], args[i], &constraints))
			return false;

	return true;
}

static size_t reduceCandidates(Arr<Variable*>& targets, const vector<Ty*>& args)
{
	// assume targets is uniquely owned...
	size_t write = 0;

	for (size_t i = 0; i < targets.size; ++i)
		if (isCandidateValid(targets[i], args))
			targets[write++] = targets[i];

	size_t size = targets.size;

	targets.size = write;

	return size - write;
}

static bool isAssignable(Ast* node)
{
	if (UNION_CASE(Ident, n, node))
		return n->targets.size == 1 && n->targets[0]->kind == Variable::KindVariable;
	else if (UNION_CASE(Member, n, node))
		return isAssignable(n->expr);
	else if (UNION_CASE(Index, n, node))
		return true;
	else if (UNION_CASE(Unary, n, node))
		return n->op == UnaryOpDeref;
	else
		return false;
}

static void type(Output& output, Ast* root, TypeConstraints* constraints)
{
	if (UNION_CASE(LiteralBool, n, root))
	{
		if (!n->type)
			n->type = UNION_NEW(Ty, Bool, {});
	}
	else if (UNION_CASE(LiteralInteger, n, root))
	{
		if (!n->type)
			n->type = UNION_NEW(Ty, Integer, {});
	}
	else if (UNION_CASE(LiteralFloat, n, root))
	{
		if (!n->type)
			n->type = UNION_NEW(Ty, Float, {});
	}
	else if (UNION_CASE(LiteralString, n, root))
	{
		if (!n->type)
			n->type = UNION_NEW(Ty, String, {});
	}
	else if (UNION_CASE(LiteralArray, n, root))
	{
		if (!n->type)
			n->type = UNION_NEW(Ty, Array, { UNION_NEW(Ty, Unknown, {}) });

		UNION_CASE(Array, ta, n->type);
		assert(ta);

		for (auto& e: n->elements)
		{
			type(output, e, constraints);

			typeMustEqual(e, ta->element, constraints, output);
		}
	}
	else if (UNION_CASE(LiteralStruct, n, root))
	{
		if (!n->type)
			n->type = UNION_NEW(Ty, Unknown, {});

		for (auto& f: n->fields)
		{
			type(output, f.second, constraints);

			if (f.first.index >= 0)
				typeMustEqual(f.second, typeMember(n->type, f.first.index), constraints, output);
		}

		if (!constraints)
			validateLiteralStruct(output, n);
	}
	else if (UNION_CASE(Ident, n, root))
	{
		if (!n->type)
			n->type = UNION_NEW(Ty, Unknown, {});

		if (n->targets.size == 0 && !constraints)
			output.panic(n->location, "Unable to deduce the type of %s", n->name.str().c_str());

		if (n->targets.size > 1 && !constraints)
			output.panic(n->location, "Ambiguous identifier %s\n%s", n->name.str().c_str(), getCandidates(n->targets).c_str());

		if (!constraints)
			for (auto& a: n->tyargs)
				if (!typeKnown(a))
				{
					string inst;
					for (auto& a: n->tyargs)
					{
						if (!inst.empty()) inst += ", ";
						inst += typeName(a);
					}

					output.panic(n->location, "Unable to instantiate %s<%s>: all argument types must be known", n->name.str().c_str(), inst.c_str());
				}
	}
	else if (UNION_CASE(Member, n, root))
	{
		type(output, n->expr, constraints);

		if (!n->type)
			n->type = UNION_NEW(Ty, Unknown, {});

		n->exprty = astType(n->expr);

		if (n->field.index >= 0)
			n->type = typeMember(n->exprty, n->field.index);
		else if (!constraints)
			output.panic(astLocation(n->expr), "%s does not have a field %s", typeName(n->exprty).c_str(), n->field.name.str().c_str());
	}
	else if (UNION_CASE(Block, n, root))
	{
		for (auto& b: n->body)
			type(output, b, constraints);

		n->type = (n->body.size == 0) ? UNION_NEW(Ty, Void, {}) : astType(n->body[n->body.size - 1]);
	}
	else if (UNION_CASE(Module, n, root))
	{
		type(output, n->body, constraints);

		n->type = astType(n->body);
	}
	else if (UNION_CASE(Call, n, root))
	{
		type(output, n->expr, constraints);

		for (auto& a: n->args)
			type(output, a, constraints);

		if (UNION_CASE(Ident, ne, n->expr))
		{
			if (constraints && ne->targets.size > 1)
			{
				vector<Ty*> args;

				for (auto& a: n->args)
					args.push_back(astType(a));

				constraints->rewrites += reduceCandidates(ne->targets, args);
			}
		}

		// TODO: this is a horrible hack
		n->exprty = astType(n->expr);
		n->argtys = Arr<Ty*>();
		for (auto& a: n->args)
			n->argtys.push(astType(a));

		// This is important for vararg functions and generates nicer errors for argument count/type mismatch
		if (UNION_CASE(Function, fnty, astType(n->expr)))
		{
			if (isArgumentCountValid(fnty, n->args.size))
			{
				for (size_t i = 0; i < fnty->args.size; ++i)
					typeMustEqual(n->args[i], fnty->args[i], constraints, output);
			}
			else if (!constraints)
				output.panic(n->location, "Expected %d arguments but given %d", int(fnty->args.size), int(n->args.size));

			n->type = fnty->ret;
		}
		else
		{
			Arr<Ty*> args;

			for (auto& a: n->args)
				args.push(astType(a));

			Ty* ret = UNION_NEW(Ty, Unknown, {});

			typeMustEqual(n->expr, UNION_NEW(Ty, Function, { args, ret }), constraints, output);

			n->type = ret;
		}
	}
	else if (UNION_CASE(Unary, n, root))
	{
		type(output, n->expr, constraints);

		if (n->op == UnaryOpNot)
		{
			n->type = UNION_NEW(Ty, Bool, {});
			typeMustEqual(n->expr, n->type, constraints, output);
		}
		else if (n->op == UnaryOpDeref)
		{
			if (UNION_CASE(Pointer, t, astType(n->expr)))
			{
				n->type = t->element;
			}
			else
			{
				n->type = UNION_NEW(Ty, Unknown, {});

				typeMustEqual(n->expr, UNION_NEW(Ty, Pointer, { n->type }), constraints, output);
			}
		}
		else if (n->op == UnaryOpNew)
		{
			n->type = UNION_NEW(Ty, Pointer, { astType(n->expr) });
		}
		else
		{
			ICE("Unknown UnaryOp %d", n->op);
		}
	}
	else if (UNION_CASE(Binary, n, root))
	{
		type(output, n->left, constraints);
		type(output, n->right, constraints);

		if (n->op == BinaryOpAnd || n->op == BinaryOpOr)
		{
			n->type = UNION_NEW(Ty, Bool, {});

			typeMustEqual(n->left, n->type, constraints, output);
			typeMustEqual(n->right, n->type, constraints, output);
		}
		else
		{
			ICE("Unknown BinaryOp %d", n->op);
		}
	}
	else if (UNION_CASE(Index, n, root))
	{
		type(output, n->expr, constraints);
		type(output, n->index, constraints);

		typeMustEqual(n->index, UNION_NEW(Ty, Integer, {}), constraints, output);

		if (UNION_CASE(Array, t, astType(n->expr)))
		{
			n->type = t->element;
		}
		else
		{
			Ty* element = UNION_NEW(Ty, Unknown, {});

			typeMustEqual(n->expr, UNION_NEW(Ty, Array, { element }), constraints, output);

			n->type = element;
		}
	}
	else if (UNION_CASE(Assign, n, root))
	{
		type(output, n->left, constraints);
		type(output, n->right, constraints);

		typeMustEqual(n->right, astType(n->left), constraints, output);

		if (!isAssignable(n->left) && !constraints)
			output.panic(n->location, "Expression is not assignable");

		if (!n->type)
			n->type = UNION_NEW(Ty, Void, {});
	}
	else if (UNION_CASE(If, n, root))
	{
		type(output, n->cond, constraints);

		typeMustEqual(n->cond, UNION_NEW(Ty, Bool, {}), constraints, output);

		type(output, n->thenbody, constraints);

		if (n->elsebody)
		{
			type(output, n->elsebody, constraints);

			typeMustEqual(n->elsebody, astType(n->thenbody), constraints, output);
		}
		else
		{
			typeMustEqual(n->thenbody, UNION_NEW(Ty, Void, {}), constraints, output);
		}

		n->type = astType(n->thenbody);
	}
	else if (UNION_CASE(For, n, root))
	{
		type(output, n->expr, constraints);
		type(output, n->body, constraints);

		typeMustEqual(n->expr, UNION_NEW(Ty, Array, { n->var->type }), constraints, output);
		typeMustEqual(n->body, UNION_NEW(Ty, Void, {}), constraints, output);

		if (n->index)
			typeMustEqual(n->index->type, UNION_NEW(Ty, Integer, {}), constraints, output, n->index->location);

		if (!n->type)
			n->type = UNION_NEW(Ty, Void, {});
	}
	else if (UNION_CASE(While, n, root))
	{
		type(output, n->expr, constraints);
		type(output, n->body, constraints);

		typeMustEqual(n->expr, UNION_NEW(Ty, Bool, {}), constraints, output);
		typeMustEqual(n->body, UNION_NEW(Ty, Void, {}), constraints, output);

		if (!n->type)
			n->type = UNION_NEW(Ty, Void, {});
	}
	else if (UNION_CASE(Fn, n, root))
	{
		type(output, n->decl, constraints);

		UNION_CASE(FnDecl, decl, n->decl);
		assert(decl);

		n->type = decl->var->type;
	}
	else if (UNION_CASE(FnDecl, n, root))
	{
		if (n->body && n->body->kind != Ast::KindLLVM)
		{
			type(output, n->body, constraints);

			if (UNION_CASE(Function, fnty, n->var->type))
			{
				if (fnty->ret->kind != Ty::KindVoid)
					typeMustEqual(n->body, fnty->ret, constraints, output);
			}
			else
				ICE("FnDecl type is not Function");
		}
		else
		{
			if (!constraints)
				typeMustKnow(n->var->type, output, n->var->location);
		}

		if (!n->type)
			n->type = UNION_NEW(Ty, Void, {});
	}
	else if (UNION_CASE(VarDecl, n, root))
	{
		type(output, n->expr, constraints);

		typeMustEqual(n->expr, n->var->type, constraints, output);

		if (!n->type)
			n->type = UNION_NEW(Ty, Void, {});
	}
	else if (UNION_CASE(TyDecl, n, root))
	{
		if (UNION_CASE(Struct, t, n->def))
		{
			for (auto& c: t->fields)
				if (c.expr)
				{
					type(output, c.expr, constraints);

					typeMustEqual(c.expr, c.type, constraints, output);
				}
		}

		if (!n->type)
			n->type = UNION_NEW(Ty, Void, {});
	}
	else if (UNION_CASE(Import, n, root))
	{
		if (!n->type)
			n->type = UNION_NEW(Ty, Void, {});
	}
	else
	{
		ICE("Unknown Ast kind %d", root->kind);
	}
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
		if (n->type)
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

static bool instantiateNode(Output& output, Ast* node, TypeConstraints* constraints)
{
	visitAstTypes(node, instantiateType, output);

	if (UNION_CASE(Ident, n, node))
	{
		if (n->resolved)
			return true;

		if (n->targets.size != 1)
			return true;

		Variable* var = n->targets[0];

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

		n->resolved = true;

		constraints->rewrites++;

		return true;
	}

	return false;
}

int typeckPropagate(Output& output, Ast* root)
{
	TypeConstraints constraints;
	type(output, root, &constraints);

	// Currently this also resolves overloads; it's probably better to do it in the type() to speed up convergence
	visitAst(root, [&](Ast* node) { return instantiateNode(output, node, &constraints); });

	if (!constraints.data.empty())
		visitAst(root, propagate, constraints);

	return constraints.rewrites;
}

static bool verifyNode(Output& output, Ast* node)
{
	visitAstTypes(node, [&](Ty* ty) {
		assert(ty);

		typeMustKnow(ty, output, astLocation(node));
	});

	if (UNION_CASE(Ident, n, node))
	{
		if (n->targets.size == 0)
			ICE("Unresolved identifier %s", n->name.str().c_str());
		else if (n->targets.size > 1)
			ICE("Ambiguous identifier %s", n->name.str().c_str());
	}

	return false;
}

void typeckVerify(Output& output, Ast* root)
{
	type(output, root, nullptr);

	visitAst(root, verifyNode, output);
}