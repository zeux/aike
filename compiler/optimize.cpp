#include "common.hpp"
#include "optimize.hpp"

#include "llvm/PassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

void optimize(llvm::Module* module, int level)
{
	llvm::PassManagerBuilder pmb;

	pmb.OptLevel = level;
	pmb.Inliner = (level > 1) ? llvm::createFunctionInliningPass(level, 0) : llvm::createAlwaysInlinerPass();
	pmb.LoopVectorize = level > 2;
	pmb.SLPVectorize = level > 2;

	llvm::FunctionPassManager fpm(module);
	pmb.populateFunctionPassManager(fpm);

	llvm::PassManager pm;
	pmb.populateModulePassManager(pm);

	fpm.doInitialization();
	for (auto& f: *module)
		fpm.run(f);
	fpm.doFinalization();

	pm.run(*module);
}