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
#include <sstream>
#include <string>

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
	DebugCode = 4,
	DebugAll = DebugParse | DebugAST | DebugCode
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

void dumpError(std::ostream& os, const std::string& path, const Location& location, const std::string& error)
{
	os << path << "(" << (location.line + 1) << "," << (location.column + 1) << "): " << error << "\n";

	if (location.lineStart)
	{
		const char *lineEnd = strchr(location.lineStart, '\n');

		if (lineEnd)
			os << std::string(location.lineStart, lineEnd);
		else
			os << location.lineStart;

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

bool runCode(const std::string& path, const std::string& data, std::ostream& output, std::ostream& errors, unsigned int debugFlags, unsigned int optimizationLevel)
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

		Expr* root = typecheck(synRoot);

		if (debugFlags & DebugAST)
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

		std::vector<llvm::GenericValue> noargs;
		llvm::GenericValue gv = EE->runFunction(module->getFunction("entrypoint"), noargs);

		output << gv.IntVal.getSExtValue() << "\n";

		delete EE;
	}
	catch (const ErrorAtLocation& e)
	{
		dumpError(errors, path, e.location, e.error);
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

	std::string expectedOutput = extractCommentBlock(data, "OUTPUT");
	std::string expectedErrors = extractCommentBlock(data, "ERRORS");

	std::ostringstream output, errors;

	bool result = runCode(path, data, output, errors, debugFlags, optimizationLevel);

	if (output.str() != expectedOutput || errors.str() != expectedErrors || result != expectedErrors.empty())
	{
		std::cout << path << " FAILED (O" << optimizationLevel << ")\n";
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
		else if (strcmp(argv[i], "--debug-code") == 0)
			result |= DebugCode;
		else if (strcmp(argv[i], "--debug") == 0)
			result |= DebugAll;
	}

	return result;
}

unsigned int parseOptimizationLevel(int argc, char** argv)
{
	for (size_t i = 1; i < argc; ++i)
		if (argv[i][0] == 'O' && isdigit(argv[i][1]))
			return atoi(argv[i] + 1);

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

	SetCurrentDirectoryA("../tests");

	unsigned int debugFlags = parseDebugFlags(argc, argv);
	unsigned int optimizationLevel = parseOptimizationLevel(argc, argv);
	std::string testName = parseTestName(argc, argv);

	if (testName.empty())
	{
		std::vector<std::string> files;
		findFilesRecursive(files, ".", "");

		size_t total = 0, passed = 0;

		for (size_t i = 0; i < files.size(); ++i)
			if (files[i].rfind(".aike") == files[i].length() - 5)
			{
				total++;
				passed += runTest(files[i], debugFlags, optimizationLevel);
			}

		if (total == passed)
			std::cout << "Success: " << total << " tests passed.\n";
		else
			std::cout << "FAILURE: " << (total - passed) << " out of " << total << " tests failed.\n";
	}
	else
	{
		runTest(testName, debugFlags, optimizationLevel);
	}

	llvm::llvm_shutdown();
}
