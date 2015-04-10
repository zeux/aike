#include "common.hpp"

#include "output.hpp"

#include "tokenize.hpp"
#include "parse.hpp"
#include "resolve.hpp"
#include "typecheck.hpp"
#include "codegen.hpp"
#include "optimize.hpp"
#include "target.hpp"
#include "dump.hpp"
#include "timer.hpp"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"

#include "llvm/Target/TargetMachine.h"

#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Timer.h"

#include "llvm/ADT/Statistic.h"

#include <fstream>

struct Options
{
	vector<string> inputs;
	string output;

	int optimize;
	bool debugInfo;

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
			else if (arg == "-g")
				result.debugInfo = true;
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
			else if (arg.str().compare(0, 6, "--llvm") == 0)
			{
				string opt = arg.str().substr(6);

				const char* opts[] = { argv[0], opt.c_str() };
				llvm::cl::ParseCommandLineOptions(2, opts);
			}
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

int main(int argc, const char** argv)
{
	Options options = parseOptions(argc, argv);

	Timer timer;

	Output output;

	llvm::InitializeNativeTarget();
	llvm::InitializeNativeTargetAsmPrinter();

	llvm::TargetMachine* machine = targetCreate(options.optimize);

	llvm::LLVMContext context;

	llvm::Module* module = new llvm::Module("main", context);

#if LLVM_VERSION_MAJOR * 10 + LLVM_VERSION_MINOR < 36
	module->setDataLayout(machine->getDataLayout());
#endif

	vector<llvm::Value*> entries;

	timer.checkpoint("startup");

	for (auto& file: options.inputs)
	{
		const char* source = strdup(file.c_str());

		Str contents = readFile(source);

		output.sources[source] = contents;

		timer.checkpoint();

		Tokens tokens = tokenize(output, source, contents);

		timer.checkpoint("tokenize");

		Ast* root = parse(output, tokens);

		timer.checkpoint("parse");

		if (options.dumpParse)
		{
			dump(root);
		}

		timer.checkpoint();

		resolveNames(output, root);

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

		entries.push_back(codegen(output, root, module, { options.debugInfo }));

		timer.checkpoint("codegen");
	}

	codegenMain(module, entries);

	timer.checkpoint();

	assert(!verifyModule(*module, &llvm::errs()));

	timer.checkpoint("verify");

	optimize(module, options.optimize);

	timer.checkpoint("optimize");

	if (options.dumpLLVM)
	{
		module->print(llvm::outs(), 0);
	}

	if (options.dumpAsm)
	{
		string result = targetAssembleText(machine, module);

		puts(result.c_str());
	}

	if (!options.output.empty())
	{
		timer.checkpoint();

		string result = targetAssembleBinary(machine, module);

		timer.checkpoint("assemble");

		std::ofstream of(options.output, std::ios::out | std::ios::binary);
		of.write(result.c_str(), result.size());
	}

	if (options.time)
	{
		timer.dump();
	}

	llvm::PrintStatistics();
	llvm::TimerGroup::printAll(llvm::outs());
}