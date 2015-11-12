#include "common.hpp"
#include "modules.hpp"

#include "visit.hpp"

static bool moduleGatherImportsNode(function<void (Str, Location)>& f, Ast* root)
{
	if (UNION_CASE(Module, n, root))
	{
		for (auto& i: n->autoimports)
			f(i, n->location);
	}
	else if (UNION_CASE(Import, n, root))
	{
		f(n->name, n->location);
	}

	return false;
}

void moduleGatherImports(Ast* root, function<void (Str, Location)> f)
{
	visitAst(root, moduleGatherImportsNode, f);
}
