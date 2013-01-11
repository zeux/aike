#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/Interpreter.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/TypeBuilder.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/PassManager.h"
#include "llvm/Target/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <ctime>

#include <Windows.h>

#include "lexer.hpp"
#include "parser.hpp"
#include "typecheck.hpp"
#include "compiler.hpp"
#include "optimizer.hpp"
#include "dump.hpp"
#include "output.hpp"

enum DebugFlags
{
	DebugParse = 1,
	DebugAST = 2,
	DebugTypedAST = 4,
	DebugCode = 8,
	DebugObject = 16,
};

#if defined(__linux)
	#define AIKE_EXTERN extern "C" __attribute__ ((visibility("default")))
#else
	#define AIKE_EXTERN extern "C" __declspec(dllexport)
#endif

std::ostream* gOutput;

AIKE_EXTERN
void print(int value)
{
	*gOutput << value << "\n";
}

void dumpError(std::ostream& os, const std::string& path, const Location& location, const std::string& error, bool outputErrorLocation)
{
	os << path << "(" << (location.line + 1) << "," << (location.column + 1) << "): " << error << "\n";

	if (location.lineStart && outputErrorLocation)
	{
		const char *lineEnd = strchr(location.lineStart, '\n');

		os << "\n";

		if (lineEnd)
			os << std::string(location.lineStart, lineEnd);
		else
			os << location.lineStart;

		os << "\n";

		for (size_t i = 0; i < location.column; i++)
			os << ' ';

		for (size_t i = 0; i < location.length; i++)
			os << '^';

		os << "\n";
	}
}

std::string readFile(const std::string& path)
{
	std::ifstream in(path);
	in.unsetf(std::ios::skipws);
	std::string data;
	std::copy(std::istream_iterator<char>(in), std::istream_iterator<char>(), std::back_inserter(data));

	return data;
}

std::string extractCommentBlock(const std::string& data, const std::string& name)
{
	std::string::size_type pos = data.find("// " + name + ":");
	if (pos == std::string::npos) return "";

	std::string result;

	std::string::size_type newline = data.find('\n', pos);

	while (newline != std::string::npos)
	{
		std::string::size_type nextline = data.find('\n', newline + 1);

		if (nextline != std::string::npos && data.compare(newline, 4, "\n// ") == 0)
		{
			result += std::string(data.begin() + newline + 4, data.begin() + nextline);
			result += "\n";

			newline = nextline;
		}
		else
		{
			break;
		}
	}

	return result;
}

const llvm::Target* getTarget(const std::string& triple)
{
	std::string error;
	const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, error);

	if (!target)
	{
		std::cout << error << std::endl;
		assert(false);
	}

	return target;
}

llvm::CodeGenOpt::Level getCodeGenOptLevel(unsigned int optimizationLevel)
{
	switch (optimizationLevel)
	{
	case 0: return llvm::CodeGenOpt::None;
	case 1: return llvm::CodeGenOpt::Less;
	case 2: return llvm::CodeGenOpt::Default;
	case 3: return llvm::CodeGenOpt::Aggressive;
	default: return llvm::CodeGenOpt::Aggressive;
	}
}

std::string getHostFeatures()
{
	llvm::StringMap<bool> features;
	std::string result;

	if (llvm::sys::getHostCPUFeatures(features))
	{
		for (llvm::StringMap<bool>::const_iterator it = features.begin(); it != features.end(); ++it)
		{
			if (result.empty()) result += ',';
			result += it->second ? '+' : '-';
			result += it->first();
		}
	}

	return result;
}

void compileModuleToObject(llvm::Module* module, const std::string& path, unsigned int optimizationLevel)
{
	std::string triple = llvm::sys::getDefaultTargetTriple();
	std::string cpu = llvm::sys::getHostCPUName();
	std::string features = getHostFeatures();

	const llvm::Target* target = getTarget(triple);

	llvm::TargetOptions options;
	llvm::CodeGenOpt::Level codeGenOptLevel = getCodeGenOptLevel(optimizationLevel);

	llvm::TargetMachine* tm = target->createTargetMachine(triple, cpu, features, options, llvm::Reloc::Default, llvm::CodeModel::Default, codeGenOptLevel);

	llvm::PassManager pm;

	pm.add(new llvm::TargetLibraryInfo(llvm::Triple(triple)));
	tm->addAnalysisPasses(pm);
	pm.add(new llvm::DataLayout(*tm->getDataLayout()));

	std::ofstream out(path, std::ios::out | std::ios::binary);
	llvm::raw_os_ostream os(out);
	llvm::formatted_raw_ostream fos(os);

	tm->addPassesToEmitFile(pm, fos, llvm::TargetMachine::CGFT_ObjectFile);

	pm.run(*module);
}

bool runCode(const std::string& path, const std::string& data, std::ostream& output, std::ostream& errors, unsigned int debugFlags, unsigned int optimizationLevel, bool outputErrorLocation)
{
	gOutput = &output;

	Lexer lexer = { data, 0 };
	movenext(lexer);

	bool result = true;

	try
	{
		SynBase* synRoot = parse(lexer);

		if (debugFlags & DebugParse)
			dump(std::cout, synRoot);

		Expr* root = resolve(synRoot);

		if (debugFlags & DebugAST)
			dump(std::cout, root);

		Type* rootType = typecheck(root);

		if (debugFlags & DebugTypedAST)
			dump(std::cout, root);

		llvm::LLVMContext context;

		llvm::Module* module = new llvm::Module("test", context);

		llvm::Function::Create(llvm::FunctionType::get(llvm::Type::getInt8PtrTy(context), llvm::Type::getInt32Ty(context), false), llvm::Function::ExternalLinkage, "malloc", module);

		compile(context, module, root);

		llvm::ExecutionEngine* EE = llvm::EngineBuilder(module).create();

		if (optimizationLevel > 0)
			optimize(context, module, *EE->getDataLayout());

		if (debugFlags & DebugCode)
		{
			llvm::outs() << *module;
			llvm::outs().flush();
		}

		if (debugFlags & DebugObject)
		{
			compileModuleToObject(module, "../output.obj", optimizationLevel);
		}

		std::vector<llvm::GenericValue> noargs;
		llvm::GenericValue gv = EE->runFunction(module->getFunction("entrypoint"), noargs);

		output << gv.IntVal.getSExtValue() << "\n";

		delete EE;
	}
	catch (const ErrorAtLocation& e)
	{
		dumpError(errors, path, e.location, e.error, outputErrorLocation);
		result = false;
	}
	catch (const std::exception& e)
	{
		errors << "Unknown exception: " << e.what() << "\n";
		result = false;
	}

	gOutput = NULL;

	return result;
}

bool runTest(const std::string& path, unsigned int debugFlags, unsigned int optimizationLevel)
{
	std::string data = readFile(path);

	if (data.compare(0, 8, "// SKIP\n") == 0)
		return true;

	std::string expectedOutput = extractCommentBlock(data, "OUTPUT");
	std::string expectedErrors = extractCommentBlock(data, "ERRORS");

	std::ostringstream output, errors;

	bool result = runCode("", data, output, errors, debugFlags, optimizationLevel, /* outputErrorLocation= */ false);

	if (output.str() != expectedOutput || errors.str() != expectedErrors || result != expectedErrors.empty())
	{
		std::cout << "\n" << path << " FAILED (O" << optimizationLevel << ")\n";
		std::cout << "Expected output:\n";
		std::cout << expectedOutput;
		std::cout << "Actual output:\n";
		std::cout << output.str();
		std::cout << "Expected errors:\n";
		std::cout << expectedErrors;
		std::cout << "Actual errors:\n";
		std::cout << errors.str();

		return false;
	}

	return true;
}

void findFilesRecursive(std::vector<std::string>& result, const std::string& path, const std::string& prefix)
{
	WIN32_FIND_DATAA data;
	HANDLE h = FindFirstFileA((path + "/*").c_str(), &data);

	if (h != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (data.cFileName[0] != '.')
			{
				if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
					findFilesRecursive(result, path + "/" + data.cFileName, prefix + data.cFileName + "/");
				else
					result.push_back(prefix + data.cFileName);
			}
		}
		while (FindNextFileA(h, &data));

		FindClose(h);
	}
}

unsigned int parseDebugFlags(int argc, char** argv)
{
	unsigned int result = 0;

	for (size_t i = 1; i < argc; ++i)
	{
		if (strcmp(argv[i], "--debug-parse") == 0)
			result |= DebugParse;
		else if (strcmp(argv[i], "--debug-ast") == 0)
			result |= DebugAST;
		else if (strcmp(argv[i], "--debug-tast") == 0)
			result |= DebugTypedAST;
		else if (strcmp(argv[i], "--debug-code") == 0)
			result |= DebugCode;
		else if (strcmp(argv[i], "--debug-object") == 0)
			result |= DebugObject;
		else if (strcmp(argv[i], "--debug") == 0)
			result |= DebugParse | DebugAST | DebugTypedAST | DebugCode;
	}

	return result;
}

unsigned int parseOptimizationLevel(int argc, char** argv)
{
	for (size_t i = 1; i < argc; ++i)
		if (argv[i][0] == '-' && argv[i][1] == 'O' && isdigit(argv[i][2]))
			return atoi(argv[i] + 2);

	return 0;
}

std::string parseTestName(int argc, char** argv)
{
	for (size_t i = 1; i < argc; ++i)
		if (argv[i][0] != '-')
			return argv[i];

	return "";
}

int main(int argc, char** argv)
{
	llvm::InitializeNativeTarget();
	llvm::InitializeNativeTargetAsmPrinter();

	SetCurrentDirectoryA("../tests");

	unsigned int debugFlags = parseDebugFlags(argc, argv);
	unsigned int optimizationLevel = parseOptimizationLevel(argc, argv);
	std::string testName = parseTestName(argc, argv);

	if (testName.empty())
	{
		std::vector<std::string> files;
		findFilesRecursive(files, ".", "");

		clock_t start = clock();

		size_t total = 0, passed = 0;

		for (size_t i = 0; i < files.size(); ++i)
			if (files[i].rfind(".aike") == files[i].length() - 5)
			{
				total++;
				passed += runTest(files[i], debugFlags, optimizationLevel);
			}

		if (total == passed)
			std::cout << "Success: " << total << " tests passed in " << ((clock() - start) * 1000 / CLOCKS_PER_SEC) << " ms.\n";
		else
			std::cout << "FAILURE: " << (total - passed) << " out of " << total << " tests failed.\n";
	}
	else
	{
		runCode(testName, readFile(testName), std::cout, std::cerr, debugFlags, optimizationLevel, /* outputErrorLocation= */ true);
	}

	llvm::llvm_shutdown();
}
