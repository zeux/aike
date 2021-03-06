#include "common.hpp"

#include "output.hpp"

#include "tokenize.hpp"
#include "parse.hpp"
#include "resolve.hpp"
#include "modules.hpp"
#include "typecheck.hpp"
#include "codegen.hpp"
#include "transform.hpp"
#include "target.hpp"
#include "dump.hpp"
#include "timer.hpp"
#include "ast.hpp"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"

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
	bool coverage;
	bool compileOnly;
	bool disablePrelude;

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
			else if (arg.str().compare(0, 2, "-O") == 0)
				result.optimize = (arg == "-O") ? 2 : atoi(arg.str().c_str() + 2);
			else if (arg.str().compare(0, 2, "-g") == 0)
				result.debugInfo = (arg == "-g") ? 2 : atoi(arg.str().c_str() + 2);
			else if (arg == "-coverage")
				result.coverage = true;
			else if (arg == "-noprelude")
				result.disablePrelude = true;
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

	if (result.coverage)
		result.debugInfo = max(result.debugInfo, 1);

	return result;
}

pair<bool, Str> readFile(const char* path)
{
	FILE* file = fopen(path, "rb");
	if (!file) return { false, Str() };

	fseek(file, 0, SEEK_END);
	long length = ftell(file);
	fseek(file, 0, SEEK_SET);

	char* result = new char[length];
	fread(result, 1, length, file);
	int error = ferror(file);
	fclose(file);

	return { error == 0, Str(result, length) };
}

Str getModuleName(const char* path)
{
	const char* fs = strrchr(path, '/');
	const char* bs = strrchr(path, '\\');
	const char* slash = (fs && bs) ? min(fs, bs) : fs ? fs : bs;

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

	if (path.compare(0, 4, "std/") == 0)
		return "library/" + path.substr(4) + ".aike";
	else
		return path + ".aike";
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

	if (output.errors)
		return nullptr;

	timer.checkpoint("resolveNames");

	int fixpoint;

	do
	{
		fixpoint = 0;

		timer.checkpoint();

		fixpoint += typeckPropagate(output, root);

		if (output.errors)
			return nullptr;

		timer.checkpoint("typeckPropagate");

		fixpoint += resolveMembers(output, root);

		if (output.errors)
			return nullptr;

		timer.checkpoint("resolveMembers");
	}
	while (fixpoint != 0);

	if (options.dumpAst)
	{
		dump(root);
	}

	timer.checkpoint();

	typeckVerify(output, root);

	if (output.errors)
		return nullptr;

	timer.checkpoint("typeckVerify");

	llvm::Value* entry = codegen(output, root, module, { options.debugInfo });

	if (output.errors)
		return nullptr;

	timer.checkpoint("codegen");

	return entry;
}

bool compileModules(vector<llvm::Value*>& entries, Timer& timer, Output& output, llvm::Module* module, const Options& options)
{
	vector<Ast*> modules;
	unordered_map<Str, unsigned int> readyModules;

	ModuleResolver resolver;

	resolver.lookup = [&](Str name) -> Ast* {
		auto it = readyModules.find(name);
		if (it == readyModules.end()) return nullptr;

		return modules[it->second];
	};

	struct PendingModule
	{
		Str name;
		Location import;
		string path;
	};

	deque<PendingModule> pendingModules;

	timer.checkpoint("startup");

	for (auto& file: options.inputs)
		pendingModules.push_back({ getModuleName(file.c_str()), Location(), file });

	while (!pendingModules.empty())
	{
		auto pm = pendingModules.front();
		pendingModules.pop_front();

		if (readyModules.count(pm.name))
			continue;

		const char* source = strdup(pm.path.c_str());

		auto contents = readFile(source);

		if (!contents.first)
		{
			output.error(pm.import, "Cannot find module %s", pm.name.str().c_str());
			return false;
		}

		output.sources[source] = contents.second;

		Ast* root = parseModule(timer, output, source, contents.second, pm.name, options);

		if (!options.disablePrelude)
		{
			if (UNION_CASE(Module, m, root))
				if (pm.name != "std.prelude")
					m->autoimports.push(Str("std.prelude"));
		}

		moduleGatherImports(root, [&](Str name, Location location) {
			pendingModules.push_back({ name, location, getModulePath(name) });
		});

		readyModules[pm.name] = modules.size();
		modules.push_back(root);
	}

	vector<unsigned int> moduleOrder = moduleSort(output, modules);

	for (auto& i: moduleOrder)
	{
		llvm::Value* entrypoint = compileModule(timer, output, module, modules[i], &resolver, options);

		if (!entrypoint)
			return false;

		entries.push_back(entrypoint);
	}

	return output.errors == 0;
}

string getRuntimePath(const string& compilerPath)
{
	string::size_type slash = compilerPath.find_last_of('/');

	string path = (slash == string::npos) ? "" : compilerPath.substr(0, slash + 1);

	return path + "aike-runtime.so";
}

int main(int argc, const char** argv)
{
	Options options = parseOptions(argc, argv);

	Timer timer;

	Output output;

	targetInitialize();

	string triple = options.triple.empty() ? targetHostTriple() : options.triple;

	llvm::LLVMContext context;

	llvm::Module* module = new llvm::Module("main", context);

	module->setTargetTriple(triple);
	module->setDataLayout(targetDataLayout(triple));

	if (options.debugInfo)
	{
		module->addModuleFlag(llvm::Module::Warning, "Debug Info Version", llvm::DEBUG_METADATA_VERSION);

		if (llvm::Triple(triple).isOSDarwin())
			module->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 2);
	}

	vector<llvm::Value*> entries;

	if (!compileModules(entries, timer, output, module, options))
	{
		output.flush();
		return 1;
	}

	codegenMain(module, entries);

	timer.checkpoint();

	assert(!verifyModule(*module, &llvm::errs()));

	timer.checkpoint("verify");

	transformOptimize(module, options.optimize);

	timer.checkpoint("optimize");

	for (auto& fun: *module)
		fun.addFnAttr("no-frame-pointer-elim", "true");

	if (options.debugInfo)
	{
		transformMergeDebugInfo(module);

		timer.checkpoint("debuginfo");
	}

	if (options.coverage)
	{
		transformCoverage(module);

		timer.checkpoint("coverage");
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
			string runtimePath = getRuntimePath(argv[0]);

			timer.checkpoint();

			targetLink(triple, options.output, { result }, runtimePath, options.debugInfo);

			timer.checkpoint("link");
		}
	}

	output.flush();

	if (options.time)
	{
		timer.dump();
	}

	llvm::PrintStatistics();
	llvm::TimerGroup::printAll(llvm::outs());
}