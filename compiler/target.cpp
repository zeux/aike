#include "common.hpp"
#include "target.hpp"

#include "llvm/IR/Module.h"
#include "llvm/IR/LegacyPassManager.h"

#include "llvm/Target/TargetMachine.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetRegistry.h"

#include <unistd.h>
#include <fstream>

#ifdef AIKE_USE_LLD
#include "lld/ReaderWriter/MachOLinkingContext.h"
#include "lld/ReaderWriter/ELFLinkingContext.h"
#include "lld/ReaderWriter/PECOFFLinkingContext.h"
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

TargetMachine* targetCreate(int optimizationLevel)
{
	string triple = sys::getDefaultTargetTriple();
	string error;
	const Target* target = TargetRegistry::lookupTarget(triple, error);

	if (!target)
		panic("Can't find target for triple %s: %s", triple.c_str(), error.c_str());

	TargetOptions options;

	return target->createTargetMachine(triple, "", "", options, Reloc::Default, CodeModel::Default, getCodeGenOptLevel(optimizationLevel));
}

static string assemble(TargetMachine* target, Module* module, TargetMachine::CodeGenFileType type)
{
	string result;

	raw_string_ostream rs(result);
	formatted_raw_ostream frs(rs);

	legacy::PassManager pm;

	target->addPassesToEmitFile(pm, frs, type);

	pm.run(*module);

	frs.flush();

	return result;
}

string targetAssembleBinary(TargetMachine* target, Module* module)
{
	return assemble(target, module, TargetMachine::CGFT_ObjectFile);
}

string targetAssembleText(TargetMachine* target, Module* module)
{
	return assemble(target, module, TargetMachine::CGFT_AssemblyFile);
}

static void targetLinkFillArgs(vector<const char*>& args)
{
	args.push_back("-arch");
	args.push_back("x86_64");

#if defined(__APPLE__)
	args.push_back("-macosx_version_min");
	args.push_back("10.10");

	args.push_back("-lSystem");
#elif defined(__linux__)
	args.push_back("-dynamic-linker");
	args.push_back("/lib64/ld-linux-x86-64.so.2");
	args.push_back("/usr/lib/x86_64-linux-gnu/crt1.o");
	args.push_back("-lc");
#else
#error Unsupported platform
#endif
}

static void targetLinkLD(const string& ld, const string& outputPath, const vector<string>& inputs, const string& runtimePath)
{
	vector<string> inputFiles;

	for (auto& data: inputs)
	{
		string file = outputPath + "-in" + to_string(inputFiles.size()) + ".o";

		ofstream of(file, ios::out | ios::binary);
		of.write(data.data(), data.size());

		inputFiles.push_back(file);
	}

	vector<const char*> args;

	targetLinkFillArgs(args);

	args.push_back("-o");
	args.push_back(outputPath.c_str());

	for (auto& file: inputFiles)
		args.push_back(file.c_str());

	args.push_back(runtimePath.c_str());

	string command = ld;

	for (auto& arg: args)
	{
		command += " ";
		command += arg;
	}

	int rc = system(command.c_str());

	for (auto& file: inputFiles)
		unlink(file.c_str());

	if (rc != 0)
		panic("Error linking output: %s returned %d", command.c_str(), rc);
}

#ifdef AIKE_USE_LLD
static void targetLinkLLD(const string& outputPath, const vector<string>& inputs, const string& runtimePath)
{
	class CustomDriver: public Driver
	{
	public:
		static bool link(int argc, const char* argv[], const vector<string>& inputs)
		{
		#if defined(__APPLE__)
			unique_ptr<MachOLinkingContext> ctx(new MachOLinkingContext());
			if (!DarwinLdDriver::parse(argc, argv, *ctx))
				return false;

			ctx->registry().addSupportMachOObjects(*ctx);
		#elif defined(__linux__)
			unique_ptr<ELFLinkingContext> ctx;
			if (!GnuLdDriver::parse(argc, argv, ctx) || !ctx)
				return false;

			ctx->registry().addSupportELFObjects(ctx->mergeCommonStrings(), ctx->targetHandler());
			ctx->registry().addSupportELFDynamicSharedObjects(ctx->useShlibUndefines(), ctx->targetHandler());
		#elif defined(_WIN32)
			unique_ptr<PECOFFLinkingContext> ctx(new PECOFFLinkingContext());
			if (!WinLinkDriver::parse(argc, argv, *ctx))
				return false;

			ctx->registry().addSupportCOFFObjects(*ctx);
			ctx->registry().addSupportCOFFImportLibraries(*ctx);
		#else
		#error Unsupported platform
		#endif

			ctx->registry().addSupportArchives(ctx->logInputFiles());
			ctx->registry().addSupportNativeObjects();
			ctx->registry().addSupportYamlFiles();

			for (auto& i: inputs)
			{
				auto mb = MemoryBuffer::getMemBuffer(i, "input");

				vector<unique_ptr<File>> files;
				if (error_code ec = ctx->registry().parseFile(move(mb), files))
					return false;

				for (unique_ptr<File>& file: files)
					ctx->getInputGraph().addInputElement(unique_ptr<FileNode>(new SimpleFileNode("input", std::move(file))));
			}

			return Driver::link(*ctx);
		}
	};

	std::vector<const char*> args;

	args.push_back("lld");
	targetLinkFillArgs(args);

	args.push_back("-o");
	args.push_back(outputPath.c_str());

	args.push_back(runtimePath.c_str());

	if (!CustomDriver::link(args.size(), args.data(), inputs))
		panic("Error linking output");
}
#endif

void targetLink(const string& outputPath, const vector<string>& inputs, const string& runtimePath)
{
#ifdef AIKE_USE_LLD
	targetLinkLLD(outputPath, inputs, runtimePath);
#else
	targetLinkLD("/usr/bin/ld", outputPath, inputs, runtimePath);
#endif
}