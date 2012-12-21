#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/Interpreter.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include <iostream>
#include <fstream>
#include <string>

#include "lexer.hpp"
#include "parser.hpp"
#include "compiler.hpp"
#include "optimizer.hpp"

using namespace llvm;

int main()
{
	std::ifstream in("../tests/simple.aike");
	in.unsetf(std::ios::skipws);
	std::string data;
	std::copy(std::istream_iterator<char>(in), std::istream_iterator<char>(), std::back_inserter(data));

	Lexer lexer = { data, 0 };
	movenext(lexer);

	SynBase* root = parse(lexer);

	InitializeNativeTarget();

	LLVMContext context;

	Module* module = new Module("test", context);

    compile(context, module, root);

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
