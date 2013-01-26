#include "optimizer.hpp"

#include "llvmaike.hpp"

void optimize(LLVMContextRef context, LLVMModuleRef module, LLVMTargetDataRef targetData)
{
	LLVMPassManagerRef fpm = LLVMCreateFunctionPassManagerForModule(module);

	LLVMAddTargetData(targetData, fpm);

	LLVMAddBasicAliasAnalysisPass(fpm);
	LLVMAddPromoteMemoryToRegisterPass(fpm);
	LLVMAddInstructionCombiningPass(fpm);
	LLVMAddReassociatePass(fpm);
	LLVMAddGVNPass(fpm);
	LLVMAddCFGSimplificationPass(fpm);
	LLVMAddConstantPropagationPass(fpm);
	LLVMAddSCCPPass(fpm);
	LLVMAddTailCallEliminationPass(fpm);
	LLVMAddLICMPass(fpm);

	LLVMPassManagerRef pm = LLVMCreatePassManager();

	LLVMAddFunctionInliningPass(pm);
	LLVMAddFunctionAttrsPass(pm);
	LLVMAddIPConstantPropagationPass(pm);
	LLVMAddDeadArgEliminationPass(pm);
	
	LLVMInitializeFunctionPassManager(fpm);

	for (LLVMFunctionRef fi = LLVMGetFirstFunction(module); fi; fi = LLVMGetNextFunction(fi))
		LLVMRunFunctionPassManager(fpm, fi);

	LLVMRunPassManager(pm, module);

	for (LLVMFunctionRef fi = LLVMGetFirstFunction(module); fi; fi = LLVMGetNextFunction(fi))
		LLVMRunFunctionPassManager(fpm, fi);

	LLVMFinalizeFunctionPassManager(fpm);
}