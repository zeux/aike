#include "common.hpp"
#include "lexer.hpp"
#include "output.hpp"

#include "llvm/IR/Verifier.h"
#include "llvm/IR/DerivedTypes.h"
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

	bool emitLLVM;
	bool emitAsm;
};

Options parseOptions(int argc, const char** argv)
{
	Options result = {};

	for (int i = 1; i < argc; ++i)
	{
		Str arg = argv[i];

		if (arg.size > 0 && arg[0] == '-')
		{
			if (arg == "-o" && i + 1 < argc)
				result.output = argv[++i];
			else if (arg == "-emit-llvm")
				result.emitLLVM = true;
			else if (arg == "-emit-asm")
				result.emitAsm = true;
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

TargetMachine* createTargetMachine()
{
	string triple = sys::getDefaultTargetTriple();
	string error;
	const Target* target = TargetRegistry::lookupTarget(triple, error);

	if (!target)
		panic("Can't find target for triple %s: %s", triple.c_str(), error.c_str());

	TargetOptions options;

	return target->createTargetMachine(triple, "", "", options, Reloc::Default, CodeModel::Default, CodeGenOpt::Default);
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

	for (auto& file: options.inputs)
	{
		const char* source = strdup(file.c_str());

		Str contents = readFile(source);

		output.sources[source] = contents;

		lexer::Tokens tokens = lexer::tokenize(output, source, contents);

		for (auto t: tokens.tokens)
			printf("{%d,%d,%d %s}, ", t.location.line + 1, t.location.column + 1, int(t.location.length), t.data.str().c_str());
		printf("\n");
	}

	InitializeNativeTarget();
	InitializeNativeTargetAsmPrinter();

	TargetMachine* machine = createTargetMachine();

	LLVMContext context;
	Module* module = new Module("main", context);

	vector<Type*> args;
	FunctionType* ty = FunctionType::get(Type::getInt32Ty(context), args, false);
	Function* fun = Function::Create(ty, Function::ExternalLinkage, "main", module);

	BasicBlock* bb = BasicBlock::Create(context, "entry", fun);

	IRBuilder<> builder(context);
	builder.SetInsertPoint(bb);

	builder.CreateRet(ConstantInt::get(Type::getInt32Ty(context), 0));

	if (options.emitLLVM)
	{
		module->dump();
	}

	if (options.emitAsm)
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