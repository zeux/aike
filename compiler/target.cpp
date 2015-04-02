#include "common.hpp"
#include "target.hpp"

#include "llvm/IR/Module.h"
#include "llvm/IR/LegacyPassManager.h"

#include "llvm/Target/TargetMachine.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

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

void targetLink(const string& output, const vector<string>& inputs)
{
}