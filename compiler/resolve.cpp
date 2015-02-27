#include "common.hpp"
#include "resolve.hpp"

#include "ast.hpp"
#include "visit.hpp"
#include "output.hpp"

template <typename T>
struct NameMap
{
	struct Binding
	{
		Str name;
		T* value;
		Binding* shadow;
	};

	unordered_map<Str, Binding*> data;
	vector<Binding*> stack;

	T* find(const Str& name) const
	{
		auto it = data.find(name);

		return (it != data.end() && it->second) ? it->second->value : nullptr;
	}

	void push(const Str& name, T* value)
	{
		Binding*& binding = data[name];
		binding = new Binding { name, value, binding };

		stack.push_back(binding);
	}

	void pop(size_t offset)
	{
		assert(stack.size() >= offset);

		while (stack.size() > offset)
		{
			Binding* b = stack.back();

			data[b->name] = b->shadow;

			stack.pop_back();
		}
	}

	size_t top() const
	{
		return stack.size();
	}
};

struct ResolveNames
{
	Output* output;

	NameMap<Variable> variables;
	NameMap<TyDef> typedefs;
	NameMap<Ty> generics;

	typedef array<size_t, 3> State;

	State top() const
	{
		return { variables.top(), typedefs.top(), generics.top() };
	}

	void pop(const State& offset)
	{
		variables.pop(offset[0]);
		typedefs.pop(offset[1]);
		generics.pop(offset[2]);
	}
};

static void resolveDecl(ResolveNames& rs, Ast* root)
{
	if (UNION_CASE(FnDecl, n, root))
		rs.variables.push(n->var->name, n->var);
	else if (UNION_CASE(TyDecl, n, root))
		rs.typedefs.push(n->name, n->def);
}

static void resolveTypeInstance(ResolveNames& rs, Ty* type)
{
	if (UNION_CASE(Instance, t, type))
	{
		assert(!t->def && !t->generic);

		if (TyDef* def = rs.typedefs.find(t->name))
			t->def = def;
		else if (Ty* generic = rs.generics.find(t->name))
			t->generic = generic;
		else
			rs.output->panic(t->location, "Unresolved type %s", t->name.str().c_str());
	}
}

static void resolveType(ResolveNames& rs, Ty* type)
{
	visitType(type, resolveTypeInstance, rs);
}

static bool resolveNamesNode(ResolveNames& rs, Ast* root)
{
	// TODO: refactor
	if (root->kind != Ast::KindFnDecl && root->kind != Ast::KindTyDecl)
		visitAstTypes(root, resolveType, rs);

	if (UNION_CASE(Ident, n, root))
	{
		if (Variable* var = rs.variables.find(n->name))
			n->target = var;
		else
			rs.output->panic(n->location, "Unresolved identifier %s", n->name.str().c_str());
	}
	else if (UNION_CASE(Block, n, root))
	{
		auto scope = rs.top();

		// Bind all declarations from this scope to allow recursive references
		for (auto& c: n->body)
			resolveDecl(rs, c);

		visitAstInner(root, resolveNamesNode, rs);

		rs.pop(scope);
	}
	else if (UNION_CASE(For, n, root))
	{
		visitAst(n->expr, resolveNamesNode, rs);

		rs.variables.push(n->var->name, n->var);

		if (n->index)
			rs.variables.push(n->index->name, n->index);

		visitAst(n->body, resolveNamesNode, rs);
	}
	else if (UNION_CASE(FnDecl, n, root))
	{
		auto scope = rs.top();

		for (auto& a: n->tyargs)
		{
			UNION_CASE(Generic, g, a);
			assert(g);

			rs.generics.push(g->name, a);
		}

		visitAstTypes(root, resolveType, rs);

		if (n->body)
		{
			for (auto& a: n->args)
				rs.variables.push(a->name, a);

			visitAstInner(root, resolveNamesNode, rs);
		}

		rs.pop(scope);
	}
	else if (UNION_CASE(TyDecl, n, root))
	{
		auto scope = rs.top();

		if (UNION_CASE(Struct, d, n->def))
		{
			for (auto& a: d->tyargs)
			{
				UNION_CASE(Generic, g, a);
				assert(g);

				rs.generics.push(g->name, a);
			}
		}
		else
			ICE("Unknown TyDef kind %d", n->def->kind);

		visitAstTypes(root, resolveType, rs);
		visitAstInner(root, resolveNamesNode, rs);

		rs.pop(scope);
	}
	else if (UNION_CASE(VarDecl, n, root))
	{
		visitAstInner(root, resolveNamesNode, rs);

		rs.variables.push(n->var->name, n->var);
	}
	else
		return false;

	return true;
}

void resolveNames(Output& output, Ast* root)
{
	ResolveNames rs = { &output };

	visitAst(root, resolveNamesNode, rs);
}

struct ResolveMembers
{
	Output* output;

	int counter;
};

static int findMember(Ty* type, const Str& name)
{
	if (UNION_CASE(Instance, i, type))
	{
		if (UNION_CASE(Struct, def, i->def))
		{
			for (size_t i = 0; i < def->fields.size; ++i)
			{
				auto& f = def->fields[i];

				if (f.name == name)
					return i;
			}

			return -1;
		}
	}

	return -1;
}

static bool resolveFieldRef(Output* output, FieldRef& f, Ty* ty)
{
	if (f.index < 0 && ty->kind != Ty::KindUnknown)
	{
		f.index = findMember(ty, f.name);

		if (f.index < 0)
			output->panic(f.location, "No member named '%s' in %s", f.name.str().c_str(), typeName(ty).c_str());

		return true;
	}

	return false;
}

static bool resolveMembersNode(ResolveMembers& rs, Ast* root)
{
	if (UNION_CASE(Member, n, root))
	{
		if (n->exprty)
			rs.counter += resolveFieldRef(rs.output, n->field, n->exprty);
	}
	else if (UNION_CASE(LiteralStruct, n, root))
	{
		for (auto& f: n->fields)
			rs.counter += resolveFieldRef(rs.output, f.first, n->type);
	}

	return false;
}

int resolveMembers(Output& output, Ast* root)
{
	ResolveMembers rs = { &output };

	visitAst(root, resolveMembersNode, rs);

	return rs.counter;
}