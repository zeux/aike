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

pair<Ast*, llvm::Value*> compileModule(Timer& timer, Output& output, llvm::Module* module, const char* source, const Str& contents, const Str& moduleName, ModuleResolver* moduleResolver, const Options& options)
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

	timer.checkpoint();

	resolveNames(output, root, moduleResolver);

	timer.checkpoint("resolveNames");

	typeckInstantiate(output, root);

	timer.checkpoint("instantiate");

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

	timer.checkpoint();

	typeckVerify(output, root);

	timer.checkpoint("typeckVerify");

	if (options.dumpAst)
	{
		dump(root);
	}

	timer.checkpoint();

	llvm::Value* entry = codegen(output, root, module, { options.debugInfo });

	timer.checkpoint("codegen");

	return make_pair(root, entry);
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

	ModuleResolver resolver = {};
	vector<llvm::Value*> entries;

	timer.checkpoint("startup");

	{
		const char* source = "library/prelude.aike";

		Str contents = readFile(source);

		output.sources[source] = contents;

		Str moduleName = getModuleName(source);

		auto p = compileModule(timer, output, module, source, contents, moduleName, &resolver, options);

		resolver.prelude = p.first;
		entries.push_back(p.second);
	}

	for (auto& file: options.inputs)
	{
		const char* source = strdup(file.c_str());

		Str contents = readFile(source);

		output.sources[source] = contents;

		Str moduleName = getModuleName(source);

		auto p = compileModule(timer, output, module, source, contents, moduleName, &resolver, options);

		entries.push_back(p.second);
	}

	codegenMain(module, entries);

	timer.checkpoint();

	assert(!verifyModule(*module, &llvm::errs()));

	timer.checkpoint("verify");

	optimize(module, options.optimize);

	timer.checkpoint("optimize");

	if (options.debugInfo)
	{
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