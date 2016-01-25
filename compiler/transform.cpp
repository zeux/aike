#include "common.hpp"
#include "transform.hpp"

#include "llvm/ADT/Triple.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

using namespace llvm;

template <typename T> static void mergeArray(vector<Metadata*>& target, MDTupleTypedArrayWrapper<T> source)
{
	for (auto e: source)
		target.push_back(e);
}

static MDTuple* getArray(LLVMContext& context, const vector<Metadata*>& data)
{
	if (data.empty())
		return nullptr;

	vector<Metadata*> result;
	unordered_set<Metadata*> visited;

	for (auto m: data)
	{
		auto p = visited.insert(m);

		if (p.second)
			result.push_back(m);
	}

	return MDTuple::get(context, result);
}

void transformMergeDebugInfo(Module* module)
{
	NamedMDNode* culist = module->getNamedMetadata("llvm.dbg.cu");
	if (!culist || culist->getNumOperands() == 0)
		return;

	vector<Metadata*> enumTypes, retainedTypes, subprograms, globalVariables, importedEntities, macros;

	for (MDNode* node: culist->operands())
	{
		DICompileUnit* cu = cast<DICompileUnit>(node);

		mergeArray(enumTypes, cu->getEnumTypes());
		mergeArray(retainedTypes, cu->getRetainedTypes());
		mergeArray(subprograms, cu->getSubprograms());
		mergeArray(globalVariables, cu->getGlobalVariables());
		mergeArray(importedEntities, cu->getImportedEntities());
		mergeArray(macros, cu->getMacros());
	}

	DICompileUnit* maincu = cast<DICompileUnit>(culist->getOperand(culist->getNumOperands() - 1));
	LLVMContext& context = module->getContext();

	DICompileUnit* mergedcu = DICompileUnit::getDistinct(
		context, maincu->getSourceLanguage(), maincu->getFile(),
		maincu->getProducer(), maincu->isOptimized(), maincu->getFlags(),
		maincu->getRuntimeVersion(), maincu->getSplitDebugFilename(), maincu->getEmissionKind(),
		getArray(context, enumTypes), getArray(context, retainedTypes),
		getArray(context, subprograms), getArray(context, globalVariables),
		getArray(context, importedEntities), getArray(context, macros),
		maincu->getDWOId());

	culist->dropAllReferences();

	culist->addOperand(mergedcu);
}

void transformOptimize(Module* module, int level)
{
	PassManagerBuilder pmb;

	pmb.OptLevel = level;

	pmb.Inliner = (level > 1) ? createFunctionInliningPass(level, 0) : createAlwaysInlinerPass();

	pmb.LoopVectorize = level > 2;
	pmb.SLPVectorize = level > 2;

	legacy::FunctionPassManager fpm(module);
	pmb.populateFunctionPassManager(fpm);

	legacy::PassManager pm;

	pmb.populateModulePassManager(pm);

	fpm.doInitialization();
	for (auto& f: *module)
		fpm.run(f);
	fpm.doFinalization();

	pm.run(*module);
}

void transformCoverage(Module* module)
{
	auto options = GCOVOptions::getDefault();

	// required to make output compatible with "recent" (4.4+) gcov
	memcpy(options.Version, "404*", 4);
	options.UseCfgChecksum = true;

	// ideally we should be able to just omit function names from gcda but llvm-cov can't read that
	// isOSLinux is a proxy for "does target platform use gcov"
	if (Triple(module->getTargetTriple()).isOSLinux())
		options.FunctionNamesInData = false;

	legacy::PassManager pm;
	pm.add(createGCOVProfilerPass(options));

	pm.run(*module);
}
