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

int main()
{
	std::ifstream in("../tests/simple.aike");
	in.unsetf(std::ios::skipws);
	std::string data;
	std::copy(std::istream_iterator<char>(in), std::istream_iterator<char>(), std::back_inserter(data));

	Lexer lexer = { data, 0 };
	movenext(lexer);

	SynBase* root = parse(lexer);

	dump(std::cout, root);

	Expr* roote = typecheck(root);

	dump(std::cout, roote);

	InitializeNativeTarget();

	LLVMContext context;

	Module* module = new Module("test", context);

	compile(context, module, roote);

	ExecutionEngine* EE = EngineBuilder(module).create();

	outs() << *module;

	outs() << "\n\nOptimized:";

	optimize(context, module, *EE->getDataLayout());

	outs() << *module;
	outs().flush();

	std::vector<GenericValue> noargs;
	GenericValue gv = EE->runFunction(module->getFunction("entrypoint"), noargs);

	outs() << "Result: " << gv.IntVal << "\n";
	delete EE;
	llvm_shutdown();
}
