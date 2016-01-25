#include "common.hpp"
#include "target.hpp"

#include "llvm/IR/Module.h"
#include "llvm/IR/LegacyPassManager.h"

#include "llvm/Target/TargetMachine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetRegistry.h"

#include <unistd.h>
#include <fstream>

#ifdef AIKE_USE_LLD
#include "lld/Driver/Driver.h"
#endif

using namespace llvm;

#ifdef AIKE_USE_LLD
using namespace lld;
#endif

static CodeGenOpt::Level getCodeGenOptLevel(int optimizationLevel)
{
	if (optimizationLevel >= 3)
		return CodeGenOpt::Aggressive;
	else if (optimizationLevel >= 2)
		return CodeGenOpt::Default;
	else if (optimizationLevel >= 1)
		return CodeGenOpt::Less;
	else
		return CodeGenOpt::None;
}

static unique_ptr<TargetMachine> createTargetMachine(const string& triple, CodeGenOpt::Level optimizationLevel)
{
	string error;
	const Target* target = TargetRegistry::lookupTarget(triple, error);

	if (!target)
		panic("Can't find target for triple %s: %s", triple.c_str(), error.c_str());

	TargetOptions options;

	return unique_ptr<TargetMachine>(target->createTargetMachine(
		triple, StringRef(), StringRef(), options, Reloc::Default, CodeModel::Default, optimizationLevel));
}

string targetHostTriple()
{
#if defined(__APPLE__)
	return "x86_64-apple-darwin";
#elif defined(__linux__)
	return "x86_64-pc-linux-gnu";
#else
#error Unsupported platform
#endif
}

DataLayout targetDataLayout(const string& triple)
{
	return createTargetMachine(triple, CodeGenOpt::Default)->createDataLayout();
}

static string assemble(const string& triple, Module* module, int optimizationLevel, TargetMachine::CodeGenFileType type)
{
	unique_ptr<TargetMachine> machine = createTargetMachine(triple, getCodeGenOptLevel(optimizationLevel));

	SmallVector<char, 0> buffer;
	raw_svector_ostream rs(buffer);

	legacy::PassManager pm;

	machine->addPassesToEmitFile(pm, rs, type);

	pm.run(*module);

	return rs.str();
}

string targetAssembleBinary(const string& triple, Module* module, int optimizationLevel)
{
	return assemble(triple, module, optimizationLevel, TargetMachine::CGFT_ObjectFile);
}

string targetAssembleText(const string& triple, Module* module, int optimizationLevel)
{
	return assemble(triple, module, optimizationLevel, TargetMachine::CGFT_AssemblyFile);
}

static string targetMakeFolder(const string& outputPath)
{
	string outputName = outputPath;

	string::size_type slash = outputName.find_last_of('/');
	if (slash != string::npos) outputName.erase(0, slash + 1);

	string result = "/tmp/aikec-" + outputName + "-XXXXXX";

	return mkdtemp(&result[0]);
}

static vector<string> targetDumpObjects(const string& outputPath, const vector<string>& inputs)
{
	string tempPath = targetMakeFolder(outputPath);

	vector<string> files;

	for (auto& data: inputs)
	{
		string file = tempPath + "/input" + to_string(files.size()) + ".o";

		ofstream of(file, ios::out | ios::binary);
		of.write(data.data(), data.size());

		files.push_back(file);
	}

	return files;
}

static void targetLinkFillArgs(const Triple& triple, vector<const char*>& args)
{
	if (triple.getOS() == Triple::Linux)
	{
		args.push_back("-dynamic-linker");
		args.push_back("/lib64/ld-linux-x86-64.so.2");
		args.push_back("/usr/lib/x86_64-linux-gnu/crt1.o");
		args.push_back("-lc");
	}
	else if (triple.getOS() == Triple::Darwin)
	{
		args.push_back("-arch");
		args.push_back("x86_64");

		args.push_back("-macosx_version_min");
		args.push_back("10.10");

		args.push_back("-lSystem");
	}
}

static void targetLinkLD(const Triple& triple, const string& ld, const string& outputPath, const vector<string>& inputs, const string& runtimePath)
{
	// Note that we never remove the objects we saved
	// This is required for debug info to work on OSX: linker puts references to object files in the executable
	// We could run dsymutil which moves debug info to .dSYM bundle and then delete the files but for now we just keep them around
	vector<string> files = targetDumpObjects(outputPath, inputs);

	vector<const char*> args;

	targetLinkFillArgs(triple, args);

	args.push_back("-o");
	args.push_back(outputPath.c_str());

	for (auto& file: files)
		args.push_back(file.c_str());

	args.push_back(runtimePath.c_str());

	string command = ld;

	for (auto& arg: args)
	{
		command += " ";
		command += arg;
	}

	int rc = system(command.c_str());

	if (rc != 0)
		panic("Error linking output: %s returned %d", command.c_str(), rc);
}

#ifdef AIKE_USE_LLD
static void targetLinkLLD(const Triple& triple, const string& outputPath, const vector<string>& inputs, const string& runtimePath)
{
	vector<string> files = targetDumpObjects(outputPath, inputs);

	vector<const char*> args;

	args.push_back("lld");
	targetLinkFillArgs(triple, args);

	args.push_back("-o");
	args.push_back(outputPath.c_str());

	for (auto& file: files)
		args.push_back(file.c_str());

	args.push_back(runtimePath.c_str());

	bool result = false;

	if (triple.getObjectFormat() == Triple::MachO)
		result = DarwinLdDriver::linkMachO(args);
	else if (triple.getObjectFormat() == Triple::ELF)
		result = (elf2::link(args), true); // elf2 does not have error handling support yet

	if (!result)
		panic("Error linking output");
}
#endif

static const char* getSystemLinker()
{
	// ld.gold is much faster than ld.bfd but ld is not always symlinked to ld.gold
	if (access("/usr/bin/ld.gold", X_OK) == 0)
		return "/usr/bin/ld.gold";

	return "/usr/bin/ld";
}

void targetLink(const string& triple, const string& outputPath, const vector<string>& inputs, const string& runtimePath, bool debugInfo)
{
#ifdef AIKE_USE_LLD
	// lld does not support debug maps for OSX
	if (!debugInfo)
		return targetLinkLLD(Triple(triple), outputPath, inputs, runtimePath);
#endif

	targetLinkLD(Triple(triple), getSystemLinker(), outputPath, inputs, runtimePath);
}