#include "common.hpp"
#include "modules.hpp"

#include "output.hpp"
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

struct ModuleData
{
	Str name;
	unsigned int index;

	vector<pair<Str, Location>> imports;
};

static Str findCircularDependencyRec(const Str& module, const unordered_map<Str, ModuleData>& modules, unordered_set<Str>& visited)
{
	if (visited.count(module))
		return module;

	visited.insert(module);

	auto it = modules.find(module);
	assert(it != modules.end());

	for (auto& i: it->second.imports)
	{
		Str im = findCircularDependencyRec(i.first, modules, visited);
		if (im.size)
			return im;
	}

	return Str();
}

static Str findCircularDependency(const Str& module, const unordered_map<Str, ModuleData>& modules)
{
	unordered_set<Str> visited;

	return findCircularDependencyRec(module, modules, visited);
}

static bool moduleIsReady(const ModuleData& module, const unordered_set<Str>& visited)
{
	for (auto& i: module.imports)
		if (visited.count(i.first) == 0)
			return false;

	return true;
}

static vector<const ModuleData*> moduleSort(Output& output, const unordered_map<Str, ModuleData>& modules)
{
	vector<const ModuleData*> result;

	vector<const ModuleData*> pending;
	for (auto& m: modules)
		pending.push_back(&m.second);

	unordered_set<Str> visited;

	while (!pending.empty())
	{
		size_t size = result.size();

		for (auto& p: pending)
			if (moduleIsReady(*p, visited))
			{
				result.push_back(p);
				p = nullptr;
			}

		if (size < result.size())
		{
			pending.erase(remove(pending.begin(), pending.end(), nullptr), pending.end());

			for (size_t i = size; i < result.size(); ++i)
				visited.insert(result[i]->name);

			sort(result.begin() + size, result.end(), [](const ModuleData* lhs, const ModuleData* rhs) { return lhs->name < rhs->name; });
		}
		else
		{
			Str module = findCircularDependency(pending[0]->name, modules);

			output.panic(Location(), "Circular dependency detected: module %s transitively imports itself", module.str().c_str());
		}
	}

	return result;
}

vector<unsigned int> moduleSort(Output& output, const vector<Ast*>& modules)
{
	unordered_map<Str, ModuleData> moduleMap;

	for (size_t i = 0; i < modules.size(); ++i)
	{
		Ast* root = modules[i];

		UNION_CASE(Module, m, root);
		assert(m);

		if (moduleMap.count(m->name))
			output.panic(m->location, "Duplicate module name %s", m->name.str().c_str());

		vector<pair<Str, Location>> imports;
		moduleGatherImports(root, [&](Str name, Location location) { imports.push_back(make_pair(name, location)); });

		moduleMap[m->name] = { m->name, unsigned(i), imports };
	}

	vector<const ModuleData*> order = moduleSort(output, moduleMap);

	vector<unsigned int> result;

	for (auto& m: order)
		result.push_back(m->index);

	return result;
}
