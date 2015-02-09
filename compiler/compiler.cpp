#include "common.hpp"

#include "output.hpp"

#include "tokenize.hpp"
#include "parse.hpp"
#include "resolve.hpp"
#include "typecheck.hpp"
#include "codegen.hpp"
#include "optimize.hpp"
#include "dump.hpp"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/Host.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/PassManager.h"

#include <fstream>

struct Options
{
	vector<string> inputs;
	string output;

	int optimize;

	bool dumpParse;
	bool dumpAst;
	bool dumpLLVM;
	bool dumpAsm;
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
			else if (arg == "--dump-parse")
				result.dumpParse = true;
			else if (arg == "--dump-ast")
				result.dumpAst = true;
			else if (arg == "--dump-llvm")
				result.dumpLLVM = true;
			else if (arg == "--dump-asm")
				result.dumpAsm = true;
			else if (arg[0] == '-' && arg[1] == 'O')
				result.optimize = (arg == "-O") ? 2 : atoi(arg.str().c_str() + 2);
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

using namespace llvm;

CodeGenOpt::Level getCodeGenOptLevel(int optimize)
{
	if (optimize >= 3)
		return CodeGenOpt::Aggressive;
	else if (optimize >= 2)
		return CodeGenOpt::Default;
	else if (optimize >= 1)
		return CodeGenOpt::Less;
	else
		return CodeGenOpt::None;
}

TargetMachine* createTargetMachine(int optimize)
{
	string triple = sys::getDefaultTargetTriple();
	string error;
	const Target* target = TargetRegistry::lookupTarget(triple, error);

	if (!target)
		panic("Can't find target for triple %s: %s", triple.c_str(), error.c_str());

	TargetOptions options;

	return target->createTargetMachine(triple, "", "", options, Reloc::Default, CodeModel::Default, getCodeGenOptLevel(optimize));
}

string getModuleIR(Module* module)
{
	string result;
	raw_string_ostream rs(result);

	module->print(rs, 0);

	return result;
}

string generateModule(TargetMachine* machine, Module* module, TargetMachine::CodeGenFileType type)
{
	string result;

	raw_string_ostream rs(result);
	formatted_raw_ostream frs(rs);

	PassManager pm;

	machine->addPassesToEmitFile(pm, frs, type);

	pm.run(*module);

	frs.flush();

	return result;
}

int main(int argc, const char** argv)
{
	Options options = parseOptions(argc, argv);
	Output output;

	InitializeNativeTarget();
	InitializeNativeTargetAsmPrinter();

	TargetMachine* machine = createTargetMachine(options.optimize);

	LLVMContext context;
	Module* module = new Module("main", context);

	for (auto& file: options.inputs)
	{
		const char* source = strdup(file.c_str());

		Str contents = readFile(source);

		output.sources[source] = contents;

		Tokens tokens = tokenize(output, source, contents);
		Ast* root = parse(output, tokens);

		if (options.dumpParse)
		{
			dump(root);
		}

		resolveNames(output, root);

		int fixpoint;

		do
		{
			fixpoint = 0;
			fixpoint += typeckPropagate(output, root);
			fixpoint += resolveMembers(output, root);
		}
		while (fixpoint != 0);

		typeckVerify(output, root);

		if (options.dumpAst)
		{
			dump(root);
		}

		codegen(output, root, module);
	}

	optimize(module, options.optimize);

	if (options.dumpLLVM)
	{
		string result = getModuleIR(module);

		puts(result.c_str());
	}

	if (options.dumpAsm)
	{
		string result = generateModule(machine, module, TargetMachine::CGFT_AssemblyFile);

		puts(result.c_str());
	}

	if (!options.output.empty())
	{
		string result = generateModule(machine, module, TargetMachine::CGFT_ObjectFile);

		std::ofstream of(options.output, std::ios::out | std::ios::binary);
		of.write(result.c_str(), result.size());
	}
}