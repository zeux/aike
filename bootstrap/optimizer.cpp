#include "optimizer.hpp"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Module.h"
#include "llvm/PassManager.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/IPO.h"

using namespace llvm;

void optimize(LLVMContext& context, Module* module, const DataLayout& layout)
{
	FunctionPassManager fpm(module);

	fpm.add(new DataLayout(layout));
	fpm.add(createBasicAliasAnalysisPass());
	fpm.add(createPromoteMemoryToRegisterPass());
	fpm.add(createInstructionCombiningPass());
	fpm.add(createReassociatePass());
	fpm.add(createGVNPass());
	fpm.add(createCFGSimplificationPass());
	fpm.add(createConstantPropagationPass());
	fpm.add(createTailCallEliminationPass());
	fpm.add(createLICMPass());

	PassManager pm;
	pm.add(createFunctionInliningPass());
	pm.add(createFunctionAttrsPass());

	fpm.doInitialization();

	for (Module::iterator fit = module->begin(); fit != module->end(); ++fit)
		fpm.run(*fit);

	pm.run(*module);

	for (Module::iterator fit = module->begin(); fit != module->end(); ++fit)
		fpm.run(*fit);

	fpm.doFinalization();
}
