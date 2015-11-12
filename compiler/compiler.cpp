#include "common.hpp"

#include "output.hpp"

#include "tokenize.hpp"
#include "parse.hpp"
#include "resolve.hpp"
#include "modules.hpp"
#include "typecheck.hpp"
#include "codegen.hpp"
#include "optimize.hpp"
#include "debuginfo.hpp"
#include "target.hpp"
#include "dump.hpp"
#include "timer.hpp"
#include "ast.hpp"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"

#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Timer.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/Triple.h"

#include <fstream>
#include <deque>

struct Options
{
	vector<string> inputs;
	string output;

	string triple;

	int optimize;
	int debugInfo;
	bool compileOnly;

	bool robot;

	bool dumpParse;
	bool dumpAst;
	bool dumpLLVM;
	bool dumpAsm;
	bool time;
};

Options parseOptions(int argc, const char** argv)
{
	Options result = {};

	for (int i = 1; i < argc; ++i)
	{
		Str arg = Str(argv[i]);

		if (arg.size > 0 && arg[0] == '-')
		{
			if (arg == "-o" && i + 1 < argc)
				result.output = argv[++i];
			else if (arg == "-c")
				result.compileOnly = true;
			else if (arg == "--dump-parse")
				result.dumpParse = true;
			else if (arg == "--dump-ast")
				result.dumpAst = true;
			else if (arg == "--dump-llvm")
				result.dumpLLVM = true;
			else if (arg == "--dump-asm")
				result.dumpAsm = true;
			else if (arg == "--time")
				result.time = true;
			else if (arg == "--robot")
				result.robot = true;
			else if (arg.str().compare(0, 2, "-O") == 0)
				result.optimize = (arg == "-O") ? 2 : atoi(arg.str().c_str() + 2);
			else if (arg.str().compare(0, 2, "-g") == 0)
				result.debugInfo = (arg == "-g") ? 2 : atoi(arg.str().c_str() + 2);
			else if (arg.str().compare(0, 6, "--llvm") == 0)
			{
				string opt = arg.str().substr(6);

				const char* opts[] = { argv[0], opt.c_str() };
				llvm::cl::ParseCommandLineOptions(2, opts);
			}
			else if (arg == "-triple" && i + 1 < argc)
				result.triple = argv[++i];
			else
				panic("Unknown argument %s", arg.str().c_str());
		}
		else
		{
			result.inputs.push_back(arg.str());
		}
	}

	return result;
}

Str readFile(const char* path)
{
	FILE* file = fopen(path, "rb");
	if (!file) panic("Can't read file %s: file not found", path);

	fseek(file, 0, SEEK_END);
	long length = ftell(file);
	fseek(file, 0, SEEK_SET);

	char* result = new char[length];
	fread(result, 1, length, file);
	if (ferror(file)) panic("Can't read file %s: I/O error", path);

	fclose(file);

	return Str(result, length);
}

Str getModuleName(const char* path)
{
	const char* fs = strrchr(path, '/');
	const char* bs = strrchr(path, '\\');
	const char* slash = (fs && bs) ? std::min(fs, bs) : fs ? fs : bs;

	const char* dot = strrchr(path, '.');

	if (slash && dot && slash < dot)
		return Str(slash + 1, dot - slash - 1);
	else if (slash)
		return Str(slash);
	else
		return Str(path);
}

string getModulePath(const Str& name)
{
	string path = name.str();

	for (auto& c: path)
		if (c == '.')
			c = '/';

	return "library/" + path + ".aike";
}

struct ModuleData
{
	Str name;
	string path;

	Ast* root;
	vector<Str> imports;

	llvm::Value* entrypoint;
};

bool isModuleReady(const ModuleData& module, const unordered_set<Str>& visited)
{
	for (auto& i: module.imports)
		if (visited.count(i) == 0)
			return false;

	return true;
}

Str findCircularDependencyRec(const Str& module, const unordered_map<Str, ModuleData>& modules, unordered_set<Str>& visited)
{
	if (visited.count(module))
		return module;

	visited.insert(module);

	auto it = modules.find(module);
	assert(it != modules.end());

	for (auto& i: it->second.imports)
	{
		Str im = findCircularDependencyRec(i, modules, visited);
		if (im.size)
			return im;
	}

	return Str();
}

Str findCircularDependency(const Str& module, const unordered_map<Str, ModuleData>& modules)
{
	unordered_set<Str> visited;

	return findCircularDependencyRec(module, modules, visited);
}

vector<ModuleData*> sortModules(Output& output, unordered_map<Str, ModuleData>& modules)
{
	vector<ModuleData*> result;

	vector<ModuleData*> pending;
	for (auto& m: modules)
		pending.push_back(&m.second);

	unordered_set<Str> visited;

	while (!pending.empty())
	{
		size_t size = result.size();

		for (ModuleData*& p: pending)
			if (isModuleReady(*p, visited))
			{
				result.push_back(p);
				p = nullptr;
			}

		if (size < result.size())
		{
			pending.erase(remove(pending.begin(), pending.end(), nullptr), pending.end());

			for (size_t i = size; i < result.size(); ++i)
				visited.insert(result[i]->name);

			sort(result.begin() + size, result.end(), [](ModuleData* lhs, ModuleData* rhs) { return lhs->name < rhs->name; });
		}
		else
		{
			Str module = findCircularDependency(pending[0]->name, modules);

			output.panic(Location(), "Circular dependency detected: module %s transitively imports itself", module.str().c_str());
		}
	}

	return result;
}

Ast* parseModule(Timer& timer, Output& output, const char* source, const Str& contents, const Str& moduleName, const Options& options)
{
	timer.checkpoint();

	Tokens tokens = tokenize(output, source, contents);

	timer.checkpoint("tokenize");

	Ast* root = parse(output, tokens, moduleName);

	timer.checkpoint("parse");

	if (options.dumpParse)
	{
		dump(root);
	}

	return root;
}

llvm::Value* compileModule(Timer& timer, Output& output, llvm::Module* module, Ast* root, ModuleResolver* moduleResolver, const Options& options)
{
	timer.checkpoint();

	resolveNames(output, root, moduleResolver);

	timer.checkpoint("resolveNames");

	int fixpoint;

	do
	{
		fixpoint = 0;

		timer.checkpoint();

		fixpoint += typeckPropagate(output, root);

		timer.checkpoint("typeckPropagate");

		fixpoint += resolveMembers(output, root);

		timer.checkpoint("resolveMembers");
	}
	while (fixpoint != 0);

	if (options.dumpAst)
	{
		dump(root);
	}

	timer.checkpoint();

	typeckVerify(output, root);

	timer.checkpoint("typeckVerify");

	llvm::Value* entry = codegen(output, root, module, { options.debugInfo });

	timer.checkpoint("codegen");

	return entry;
}

int main(int argc, const char** argv)
{
	Options options = parseOptions(argc, argv);

	Timer timer;

	Output output;
	output.robot = options.robot;

	llvm::InitializeNativeTarget();
	llvm::InitializeNativeTargetAsmPrinter();

	string triple = options.triple.empty() ? targetHostTriple() : options.triple;

	llvm::LLVMContext context;

	llvm::Module* module = new llvm::Module("main", context);

	module->setDataLayout(targetDataLayout(triple));

	if (options.debugInfo)
	{
		module->addModuleFlag(llvm::Module::Warning, "Debug Info Version", llvm::DEBUG_METADATA_VERSION);

		if (llvm::Triple(triple).isOSDarwin())
			module->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 2);
	}

	unordered_map<Str, ModuleData> modules;

	ModuleResolver resolver;

	resolver.lookup = [&](Str name) -> Ast* {
		auto it = modules.find(name);
		if (it == modules.end()) return nullptr;

		assert(it->second.entrypoint); // module has to be compiled
		return it->second.root;
	};

	deque<pair<Str, string>> pendingModules;

	timer.checkpoint("startup");

	for (auto& file: options.inputs)
		pendingModules.push_back({ getModuleName(file.c_str()), file });

	while (!pendingModules.empty())
	{
		auto pm = pendingModules.front();
		pendingModules.pop_front();

		Str moduleName = pm.first;
		string modulePath = pm.second;

		if (modules.count(moduleName))
			continue;

		const char* source = strdup(pm.second.c_str());

		Str contents = readFile(source);

		output.sources[source] = contents;

		Ast* root = parseModule(timer, output, source, contents, moduleName, options);

		if (UNION_CASE(Module, m, root))
			if (moduleName != "prelude")
				m->autoimports.push(Str("prelude"));

		vector<Str> imports;
		moduleGatherImports(root, [&](Str name) { imports.push_back(name); });

		modules[moduleName] = { moduleName, pm.second, root, imports };

		for (auto& m: imports)
			pendingModules.push_back({ m, getModulePath(m) });
	}

	vector<ModuleData*> moduleOrder = sortModules(output, modules);
	vector<llvm::Value*> entries;

	for (auto& m: moduleOrder)
	{
		m->entrypoint = compileModule(timer, output, module, m->root, &resolver, options);

		entries.push_back(m->entrypoint);
	}

	codegenMain(module, entries);

	timer.checkpoint();

	assert(!verifyModule(*module, &llvm::errs()));

	timer.checkpoint("verify");

	optimize(module, options.optimize);

	timer.checkpoint("optimize");

	if (options.debugInfo)
	{
		for (auto& fun: *module)
			fun.addFnAttr("no-frame-pointer-elim", "true");

		debugInfoMerge(module);

		timer.checkpoint("debuginfo");
	}

	if (options.dumpLLVM)
	{
		module->print(llvm::outs(), 0);
	}

	if (options.dumpAsm)
	{
		string result = targetAssembleText(triple, module, options.optimize);

		puts(result.c_str());
	}

	if (!options.output.empty())
	{
		timer.checkpoint();

		string result = targetAssembleBinary(triple, module, options.optimize);

		timer.checkpoint("assemble");

		if (options.compileOnly)
		{
			ofstream of(options.output, ios::out | ios::binary);
			of.write(result.c_str(), result.size());
		}
		else
		{
			string compilerPath = argv[0];
			string::size_type compilerPathSlash = compilerPath.find_last_of('/');
			string runtimePath = (compilerPathSlash == string::npos ? "" : compilerPath.substr(0, compilerPathSlash + 1)) + "aike-runtime.so";

			timer.checkpoint();

			targetLink(triple, options.output, { result }, runtimePath, options.debugInfo);

			timer.checkpoint("link");
		}
	}

	if (options.time)
	{
		timer.dump();
	}

	llvm::PrintStatistics();
	llvm::TimerGroup::printAll(llvm::outs());
}