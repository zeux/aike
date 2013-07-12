#include "llvmaike.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <ctime>
#include <iterator>
#include <stdexcept>

#include "lexer.hpp"
#include "parser.hpp"
#include "typecheck.hpp"
#include "compiler.hpp"
#include "optimizer.hpp"
#include "dump.hpp"
#include "output.hpp"

#ifdef _MSC_VER
#   define NOMINMAX
#   include <Windows.h>
#else
#   include <dirent.h>
#endif

enum DebugFlags
{
	DebugParse = 1,
	DebugAST = 2,
	DebugTypedAST = 4,
	DebugCode = 8,
};

enum CompileFlags
{
    CompileObject = 1,
    CompileDebugInfo = 2,
};

#if defined(__linux)
	#define AIKE_EXTERN extern "C" __attribute__ ((visibility("default")))
#else
	#define AIKE_EXTERN extern "C" __declspec(dllexport)
#endif

std::ostream* gOutput;

template <typename T> struct AikeArray
{
	T* data;
	int length;
};

AIKE_EXTERN
void printint(int value)
{
	*gOutput << value << "\n";
}

AIKE_EXTERN
void printi(int value)
{
	*gOutput << value;
}

AIKE_EXTERN
void print(AikeArray<char> value)
{
	gOutput->write(value.data, value.length);
}

AIKE_EXTERN
AikeArray<char> string(int value)
{
	std::string result = std::to_string(value);

	AikeArray<char> ret = {strdup(result.c_str()), result.length()};
	return ret;
}

std::string readFile(const std::string& path);

AIKE_EXTERN
AikeArray<char> readfile(AikeArray<char> path)
{
	std::string result = readFile(std::string(path.data, path.data + path.length));

	AikeArray<char> ret = {strdup(result.c_str()), result.length()};
	return ret;
}

void dumpError(std::ostream& os, const std::string& path, const Location& location, const std::string& error, bool outputErrorLocation)
{
	if (outputErrorLocation && location.file.path)
		os << location.file.path;

	os << "(" << location.line << "," << location.column << "): " << error << "\n";

	if (location.file.contents && outputErrorLocation)
	{
		const char* lineStart = location.file.contents + location.lineOffset;
		const char* lineEnd = strchr(lineStart, '\n');

		os << "\n";

		if (lineEnd)
			os << std::string(lineStart, lineEnd);
		else
			os << lineStart;

		os << "\n";

		for (size_t i = 1; i < location.column; i++)
			os << ' ';

		for (size_t i = 0; i < location.length; i++)
			os << '^';

		os << "\n";
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

    result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());

	return result;
}

LLVMCodeGenOptLevel getCodeGenOptLevel(unsigned int optimizationLevel)
{
	switch (optimizationLevel)
	{
	case 0: return LLVMCodeGenLevelNone;
	case 1: return LLVMCodeGenLevelLess;
	case 2: return LLVMCodeGenLevelDefault;
	case 3: return LLVMCodeGenLevelAggressive;
	default: return LLVMCodeGenLevelAggressive;
	}
}

void compileModuleToObject(LLVMModuleRef module, const std::string& path, unsigned int optimizationLevel)
{
	LLVMTargetRef target = LLVMGetNextTarget(LLVMGetFirstTarget());

	LLVMCodeGenOptLevel codeGenOptLevel = getCodeGenOptLevel(optimizationLevel);

	LLVMTargetMachineRef tm = LLVMAikeCreateTargetMachine(target, codeGenOptLevel);

	char* error;

	if (LLVMTargetMachineEmitToFile(tm, module, const_cast<char*>(path.c_str()), LLVMObjectFile, &error))
		throw std::runtime_error(error);
}

bool runCode(const std::string& path, const std::string& data, std::ostream& output, std::ostream& errors, unsigned int compileFlags, unsigned int debugFlags, unsigned int optimizationLevel, bool outputErrorLocation)
{
	gOutput = &output;

	Lexer lexer = { SourceFile(path.c_str(), data.c_str(), data.size()) };
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

		LLVMContextRef context = LLVMContextCreate();
		LLVMModuleRef module = LLVMModuleCreateWithNameInContext("test", context);

		LLVMTypeRef malloc_args[] = {LLVMInt32TypeInContext(context)};
		LLVMFunctionRef malloc_func = LLVMAddFunction(module, "malloc", LLVMFunctionType(LLVMPointerType(LLVMInt8TypeInContext(context), 0), malloc_args, 1, false));
		LLVMSetLinkage(malloc_func, LLVMExternalLinkage);

		compile(context, module, LLVMCreateTargetData(LLVMGetDataLayout(module)), root, (compileFlags & CompileDebugInfo) != 0);

		LLVMExecutionEngineRef ee;
		char* error;

		if (LLVMCreateExecutionEngineForModule(&ee, module, &error))
			throw std::runtime_error(error);

		if (optimizationLevel > 0)
			optimize(context, module, LLVMGetExecutionEngineTargetData(ee));

		if (debugFlags & DebugCode)
		{
			LLVMDumpModule(module);
		}

		if (compileFlags & CompileObject)
		{
			compileModuleToObject(module, "../output.obj", optimizationLevel);
		}
        else
        {
            LLVMGenericValueRef gv = LLVMRunFunction(ee, LLVMGetNamedFunction(module, "entrypoint"), 0, 0);

            output << (int)LLVMGenericValueToInt(gv, true) << "\n";
        }
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

bool runTest(const std::string& path, unsigned int compileFlags, unsigned int debugFlags, unsigned int optimizationLevel)
{
	std::string data = readFile(path);

	if (data.compare(0, 8, "// SKIP\n") == 0)
		return true;

	std::string expectedOutput = extractCommentBlock(data, "OUTPUT");
	std::string expectedErrors = extractCommentBlock(data, "ERRORS");

	std::ostringstream output, errors;

	bool result = runCode(path, data, output, errors, compileFlags, debugFlags, optimizationLevel, /* outputErrorLocation= */ false);

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

void runCompiler(const std::vector<std::string>& sources, unsigned int compileFlags, unsigned int debugFlags, unsigned int optimizationLevel)
{
	gOutput = &std::cout;

	try
	{
		std::vector<SynBase*> synRoots;

		for (size_t i = 0; i < sources.size(); ++i)
		{
			std::string data = readFile(sources[i]);

			Lexer lexer = { SourceFile(strdup(sources[i].c_str()), strdup(data.c_str()), data.size()) };
			movenext(lexer);

			synRoots.push_back(parse(lexer));
		}

		SynBase* synRoot = synRoots.back();

		for (size_t i = synRoots.size() - 1; i > 0; --i)
		{
			SynBase* prevRoot = synRoots[i - 1];

			std::vector<SynBase*> exprs = dynamic_cast<SynBlock*>(prevRoot)->expressions;
			exprs.push_back(synRoot);

			synRoot = new SynBlock(prevRoot->location, exprs);
		}

		if (debugFlags & DebugParse)
			dump(std::cout, synRoot);

		Expr* root = resolve(synRoot);

		if (debugFlags & DebugAST)
			dump(std::cout, root);

		Type* rootType = typecheck(root);

		if (debugFlags & DebugTypedAST)
			dump(std::cout, root);

		LLVMContextRef context = LLVMContextCreate();
		LLVMModuleRef module = LLVMModuleCreateWithNameInContext("test", context);

		LLVMTypeRef malloc_args[] = {LLVMInt32TypeInContext(context)};
		LLVMFunctionRef malloc_func = LLVMAddFunction(module, "malloc", LLVMFunctionType(LLVMPointerType(LLVMInt8TypeInContext(context), 0), malloc_args, 1, false));
		LLVMSetLinkage(malloc_func, LLVMExternalLinkage);

		compile(context, module, LLVMCreateTargetData(LLVMGetDataLayout(module)), root, (compileFlags & CompileDebugInfo) != 0);

		LLVMExecutionEngineRef ee;
		char* error;

		if (LLVMCreateExecutionEngineForModule(&ee, module, &error))
			throw std::runtime_error(error);

		if (optimizationLevel > 0)
			optimize(context, module, LLVMGetExecutionEngineTargetData(ee));

		if (debugFlags & DebugCode)
		{
			LLVMDumpModule(module);
		}

		if (compileFlags & CompileObject)
		{
			compileModuleToObject(module, "../output.obj", optimizationLevel);
		}
        else
        {
            LLVMGenericValueRef gv = LLVMRunFunction(ee, LLVMGetNamedFunction(module, "entrypoint"), 0, 0);

            std::cout << (int)LLVMGenericValueToInt(gv, true) << "\n";
        }
	}
	catch (const ErrorAtLocation& e)
	{
		dumpError(std::cerr, "", e.location, e.error, true);
	}
	catch (const std::exception& e)
	{
		std::cerr << "Unknown exception: " << e.what() << "\n";
	}

	gOutput = NULL;
}

void findFilesRecursive(std::vector<std::string>& result, const std::string& path, const std::string& prefix)
{
#ifdef _MSC_VER
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
#else
    if (DIR* dir = opendir(path.c_str()))
    {
        while (dirent* e = readdir(dir))
        {
            if (e->d_name[0] != '.')
            {
                if (e->d_type & DT_DIR)
					findFilesRecursive(result, path + "/" + e->d_name, prefix + e->d_name + "/");
				else
					result.push_back(prefix + e->d_name);
            }
        }

        closedir(dir);
    }
#endif
}

unsigned int parseDebugFlags(int argc, char** argv)
{
	unsigned int result = 0;

	for (size_t i = 1; i < size_t(argc); ++i)
	{
		if (strcmp(argv[i], "--debug-parse") == 0)
			result |= DebugParse;
		else if (strcmp(argv[i], "--debug-ast") == 0)
			result |= DebugAST;
		else if (strcmp(argv[i], "--debug-tast") == 0)
			result |= DebugTypedAST;
		else if (strcmp(argv[i], "--debug-code") == 0)
			result |= DebugCode;
		else if (strcmp(argv[i], "--debug") == 0)
			result |= DebugParse | DebugAST | DebugTypedAST | DebugCode;
	}

	return result;
}

unsigned int parseCompileFlags(int argc, char** argv)
{
	unsigned int result = 0;

	for (size_t i = 1; i < size_t(argc); ++i)
	{
		if (strcmp(argv[i], "-c") == 0)
			result |= CompileObject;
		else if (strcmp(argv[i], "-g") == 0)
			result |= CompileDebugInfo;
	}

	return result;
}

unsigned int parseOptimizationLevel(int argc, char** argv)
{
	for (size_t i = 1; i < size_t(argc); ++i)
		if (argv[i][0] == '-' && argv[i][1] == 'O' && isdigit(argv[i][2]))
			return atoi(argv[i] + 2);

	return 0;
}

std::string parseTestName(int argc, char** argv)
{
	for (size_t i = 1; i < size_t(argc); ++i)
		if (argv[i][0] != '-' || argv[i][1] == 0)
			return argv[i];

	return "";
}

int main(int argc, char** argv)
{
	LLVMAikeInit();

	unsigned int debugFlags = parseDebugFlags(argc, argv);
	unsigned int compileFlags = parseCompileFlags(argc, argv);
	unsigned int optimizationLevel = parseOptimizationLevel(argc, argv);
	std::string testName = parseTestName(argc, argv);

#ifdef _MSC_VER
    SetCurrentDirectoryA("..");
#endif

	if (testName.empty())
	{
		std::vector<std::string> files;
		findFilesRecursive(files, "tests", "tests/");

		clock_t start = clock();

		size_t total = 0, passed = 0;

		for (size_t i = 0; i < files.size(); ++i)
			if (files[i].rfind(".aike") == files[i].length() - 5)
			{
				total++;
				passed += runTest(files[i], compileFlags, debugFlags, optimizationLevel);
			}

		if (total == passed)
			std::cout << "Success: " << total << " tests passed in " << ((clock() - start) * 1000 / CLOCKS_PER_SEC) << " ms.\n";
		else
			std::cout << "FAILURE: " << (total - passed) << " out of " << total << " tests failed.\n";
	}
	else if (testName == "-")
	{
		std::vector<std::string> sources;

		std::ifstream src("compiler/sources", std::ios::in | std::ios::binary);

		std::string line;
		while (std::getline(src, line))
        {
            line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());

			if (!line.empty())
				sources.push_back("compiler/" + line);
        }

        runCompiler(sources, compileFlags, debugFlags, optimizationLevel);
	}
	else
	{
        std::string fileName = "tests/" + testName;

		runCode(fileName, readFile(fileName), std::cout, std::cerr, compileFlags, debugFlags, optimizationLevel, /* outputErrorLocation= */ true);
	}
}
