#include "common.hpp"
#include "resolve.hpp"

#include "ast.hpp"
#include "visit.hpp"
#include "output.hpp"

struct Binding
{
	Variable* var;
	Binding* shadow;
};

struct TyBinding
{
	TyDef* def;
	TyBinding* shadow;
};

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

struct Resolve
{
	Output* output;

	NameMap<Variable> variables;
	NameMap<TyDef> typedefs;

	pair<size_t, size_t> top() const
	{
		return make_pair(variables.top(), typedefs.top());
	}

	void pop(const pair<size_t, size_t>& offset)
	{
		variables.pop(offset.first);
		typedefs.pop(offset.second);
	}
};

static void resolveDecl(Resolve& rs, Ast* root)
{
	if (UNION_CASE(FnDecl, n, root))
		rs.variables.push(n->var->name, n->var);
}

static bool resolveNode(Resolve& rs, Ast* root)
{
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

		visitAstInner(root, resolveNode, rs);

		rs.pop(scope);
	}
	else if (UNION_CASE(Fn, n, root))
	{
		auto scope = rs.top();

		for (auto& a: n->args)
			rs.variables.push(a->name, a);

		visitAstInner(root, resolveNode, rs);

		rs.pop(scope);
	}
	else if (UNION_CASE(FnDecl, n, root))
	{
		if (n->body)
		{
			auto scope = rs.top();

			for (auto& a: n->args)
				rs.variables.push(a->name, a);

			visitAstInner(root, resolveNode, rs);

			rs.pop(scope);
		}
	}
	else if (UNION_CASE(VarDecl, n, root))
	{
		visitAstInner(root, resolveNode, rs);

		rs.variables.push(n->var->name, n->var);
	}
	else
		return false;

	return true;
}

void resolve(Output& output, Ast* root)
{
	Resolve rs = { &output };

	visitAst(root, resolveNode, rs);
}
