#include "common.hpp"
#include "modules.hpp"

#include "ast.hpp"

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

static pair<Str, Location> findCircularDependencyRec(const pair<Str, Location>& import, const unordered_map<Str, ModuleData>& modules, unordered_set<Str>& visited)
{
	if (visited.count(import.first))
		return import;

	visited.insert(import.first);

	auto it = modules.find(import.first);
	assert(it != modules.end());

	for (auto& i: it->second.imports)
	{
		auto im = findCircularDependencyRec(i, modules, visited);
		if (im.first.size)
			return im;
	}

	visited.erase(import.first);

	return make_pair(Str(), Location());
}

static pair<Str, Location> findCircularDependency(const pair<Str, Location>& import, const unordered_map<Str, ModuleData>& modules)
{
	unordered_set<Str> visited;

	return findCircularDependencyRec(import, modules, visited);
}

static bool moduleIsReady(const ModuleData& module, const unordered_set<Str>& visited)
{
	for (auto& i: module.imports)
		if (visited.count(i.first) == 0)
			return false;

	return true;
}

static vector<unsigned int> moduleSort(Output& output, const unordered_map<Str, ModuleData>& modules)
{
	vector<unsigned int> result;

	vector<const ModuleData*> pending;
	for (auto& m: modules)
		pending.push_back(&m.second);

	// make sure output order is stable
	sort(pending.begin(), pending.end(), [](const ModuleData* lhs, const ModuleData* rhs) { return lhs->name < rhs->name; });

	unordered_set<Str> visited;

	while (!pending.empty())
	{
		size_t write = 0;

		for (size_t i = 0; i < pending.size(); ++i)
		{
			const ModuleData* m = pending[i];

			if (moduleIsReady(*m, visited))
			{
				result.push_back(m->index);
				visited.insert(m->name);
			}
			else
			{
				pending[write++] = m;
			}
		}

		if (write == pending.size())
		{
			auto import = findCircularDependency(make_pair(pending[0]->name, Location()), modules);
			assert(import.first.size);

			output.error(import.second, "Circular dependency detected: module %s transitively imports itself", import.first.str().c_str());

			// break the cycle to proceed with the sorting
			visited.insert(import.first);
		}

		pending.resize(write);
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
		{
			output.error(m->location, "Duplicate module name %s", m->name.str().c_str());
			continue;
		}

		vector<pair<Str, Location>> imports;
		moduleGatherImports(root, [&](Str name, Location location) { imports.push_back(make_pair(name, location)); });

		moduleMap[m->name] = { m->name, unsigned(i), imports };
	}

	return moduleSort(output, moduleMap);
}
