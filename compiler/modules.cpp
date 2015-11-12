#include "common.hpp"
#include "modules.hpp"

#include "visit.hpp"

static bool moduleGatherImportsNode(function<void (Str)>& f, Ast* root)
{
	if (UNION_CASE(Module, n, root))
	{
		for (auto& i: n->autoimports)
			f(i);
	}
	else if (UNION_CASE(Import, n, root))
	{
		f(n->name);
	}

	return false;
}

void moduleGatherImports(Ast* root, function<void (Str)> f)
{
	visitAst(root, moduleGatherImportsNode, f);
}
