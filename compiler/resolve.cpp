#include "common.hpp"
#include "resolve.hpp"

#include "ast.hpp"
#include "output.hpp"

struct Binding
{
	Variable* var;
	Binding* shadow;
};

struct Resolve
{
	Output* output;
	unordered_map<string, Binding*> bindings;
	vector<Binding*> stack;
};

static void popScope(Resolve& rs, size_t index)
{
	assert(rs.stack.size() >= index);

	while (rs.stack.size() > index)
	{
		Binding* b = rs.stack.back();

		rs.bindings[b->var->name.str()] = b->shadow;

		rs.stack.pop_back();
	}
}

static void pushVariable(Resolve& rs, Variable* var)
{
	Binding*& binding = rs.bindings[var->name.str()];

	binding = new Binding { var, binding };

	rs.stack.push_back(binding);
}

static void resolveDecl(Resolve& rs, Ast* root)
{
	if (UNION_CASE(FnDecl, n, root))
		pushVariable(rs, n->var);
}

static bool resolveNode(Resolve& rs, Ast* root)
{
	if (UNION_CASE(Ident, n, root))
	{
		auto it = rs.bindings.find(n->name.str());

		if (it == rs.bindings.end() || !it->second)
			rs.output->panic(n->location, "Unresolved identifier %s", n->name.str().c_str());

		n->target = it->second->var;
	}
	else if (UNION_CASE(Block, n, root))
	{
		size_t scope = rs.stack.size();

		// Bind all declarations from this scope to allow recursive references
		for (auto& c: n->body)
			resolveDecl(rs, c);

		visitAstInner(root, resolveNode, rs);

		popScope(rs, scope);
	}
	else if (UNION_CASE(Fn, n, root))
	{
		size_t scope = rs.stack.size();

		for (auto& a: n->args)
			pushVariable(rs, a);

		visitAstInner(root, resolveNode, rs);

		popScope(rs, scope);
	}
	else if (UNION_CASE(FnDecl, n, root))
	{
		if (n->body)
		{
			size_t scope = rs.stack.size();

			for (auto& a: n->args)
				pushVariable(rs, a);

			visitAstInner(root, resolveNode, rs);

			popScope(rs, scope);
		}
	}
	else if (UNION_CASE(VarDecl, n, root))
	{
		visitAstInner(root, resolveNode, rs);

		pushVariable(rs, n->var);
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
