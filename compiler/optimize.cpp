#include "common.hpp"
#include "optimize.hpp"

#include "llvm/PassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

using namespace llvm;

void optimize(Module* module, int level)
{
	PassManagerBuilder pmb;

	pmb.OptLevel = level;

	pmb.Inliner = (level > 1) ? createFunctionInliningPass(level, 0) : createAlwaysInlinerPass();

	pmb.LoopVectorize = level > 2;
	pmb.SLPVectorize = level > 2;

	FunctionPassManager fpm(module);
	pmb.populateFunctionPassManager(fpm);

	PassManager pm;
	pmb.populateModulePassManager(pm);

	fpm.doInitialization();
	for (auto& f: *module)
		fpm.run(f);
	fpm.doFinalization();

	pm.run(*module);
}