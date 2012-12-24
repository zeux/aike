#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/Interpreter.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/TypeBuilder.h"

#include <iostream>
#include <fstream>
#include <string>

#include "lexer.hpp"
#include "parser.hpp"
#include "typecheck.hpp"
#include "compiler.hpp"
#include "optimizer.hpp"
#include "dump.hpp"
#include "output.hpp"

using namespace llvm;

#if defined(__linux)
	#define AIKE_EXTERN extern "C" __attribute__ ((visibility("default")))
#else
	#define AIKE_EXTERN extern "C" __declspec(dllexport)
#endif

AIKE_EXTERN
void print(int value)
{
	printf("Print from aike: %d\n", value);
}

void dumpError(const std::string& file, const Location& location, const std::string& error)
{
	fprintf(stderr, "%s(%d,%d): %s\n", file.c_str(), location.line + 1, location.column + 1, error.c_str());

	if (location.lineStart)
	{
		const char *lineEnd = strchr(location.lineStart, '\n');

		if (lineEnd)
			fprintf(stderr, "%.*s\n", lineEnd - location.lineStart, location.lineStart);
		else
			fprintf(stderr, "%s\n", location.lineStart);

		for (size_t i = 0; i < location.column; i++)
			fprintf(stderr, " ");
		for (size_t i = 0; i < location.length; i++)
			fprintf(stderr, "^");

		fprintf(stderr, "\n");
	}
}

std::string readFile(const std::string& path)
{
	std::ifstream in(path, std::ios::in | std::ios::binary);
	in.unsetf(std::ios::skipws);
	std::string data;
	std::copy(std::istream_iterator<char>(in), std::istream_iterator<char>(), std::back_inserter(data));

	return data;
}

void runTest(const std::string& path)
{
	std::string data = readFile(path);

	Lexer lexer = { data, 0 };
	movenext(lexer);

	try
	{
		SynBase* synRoot = parse(lexer);

		dump(std::cout, synRoot);

		Expr* root = typecheck(synRoot);

		dump(std::cout, root);

		LLVMContext context;

		Module* module = new Module("test", context);

		Function::Create(FunctionType::get(llvm::Type::getInt8PtrTy(context), llvm::Type::getInt32Ty(context), false), Function::ExternalLinkage, "malloc", module);

		compile(context, module, root);

		ExecutionEngine* EE = EngineBuilder(module).create();

		outs() << *module;

		outs() << "\n\nOptimized:";

		optimize(context, module, *EE->getDataLayout());

		outs() << *module;
		outs().flush();

		std::vector<GenericValue> noargs;
		GenericValue gv = EE->runFunction(module->getFunction("entrypoint"), noargs);

		outs() << gv.IntVal << "\n";

		delete EE;
	}
	catch (const ErrorAtLocation& e)
	{
		dumpError(path, e.location, e.error);
	}
	catch (const std::exception& e)
	{
		fprintf(stderr, "Unknown exception: %s\n", e.what());
	}
}

int main(int argc, char** argv)
{
	InitializeNativeTarget();

	runTest("../tests/simple.aike");

	llvm_shutdown();
}
