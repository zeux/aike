#include "common.hpp"
#include "debuginfo.hpp"

#include "llvm/IR/Module.h"
#include "llvm/IR/DebugInfoMetadata.h"

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

void debugInfoMerge(Module* module)
{
	NamedMDNode* culist = module->getNamedMetadata("llvm.dbg.cu");
	if (!culist || culist->getNumOperands() == 0)
		return;

	vector<Metadata*> enumTypes, retainedTypes, subprograms, globalVariables, importedEntities;

	for (MDNode* node: culist->operands())
	{
		DICompileUnit* cu = cast<DICompileUnit>(node);

		mergeArray(enumTypes, cu->getEnumTypes());
		mergeArray(retainedTypes, cu->getRetainedTypes());
		mergeArray(subprograms, cu->getSubprograms());
		mergeArray(globalVariables, cu->getGlobalVariables());
		mergeArray(importedEntities, cu->getImportedEntities());
	}

	DICompileUnit* maincu = cast<DICompileUnit>(culist->getOperand(culist->getNumOperands() - 1));
	LLVMContext& context = module->getContext();

	DICompileUnit* mergedcu = DICompileUnit::getDistinct(
		context, maincu->getSourceLanguage(), maincu->getFile(),
		maincu->getProducer(), maincu->isOptimized(), maincu->getFlags(),
		maincu->getRuntimeVersion(), maincu->getSplitDebugFilename(), maincu->getEmissionKind(),
		getArray(context, enumTypes), getArray(context, retainedTypes),
		getArray(context, subprograms), getArray(context, globalVariables),
		getArray(context, importedEntities), maincu->getDWOId());

	culist->dropAllReferences();

	culist->addOperand(mergedcu);
}