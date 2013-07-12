#pragma once

#ifdef _MSC_VER
#	pragma warning(push)
#	pragma warning(disable: 4146 4244 4800)
#endif

#include "llvm-c/Core.h"
#include "llvm-c/ExecutionEngine.h"
#include "llvm-c/Target.h"
#include "llvm-c/TargetMachine.h"
#include "llvm-c/Analysis.h"
#include "llvm-c/Transforms/Scalar.h"
#include "llvm-c/Transforms/IPO.h"

#ifdef _MSC_VER
#	pragma warning(pop)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef LLVMValueRef LLVMFunctionRef;
typedef LLVMValueRef LLVMPHIRef;
typedef LLVMTypeRef LLVMStructTypeRef;
typedef LLVMTypeRef LLVMFunctionTypeRef;
typedef LLVMValueRef LLVMMetadataRef;
typedef LLVMValueRef LLVMInstructionRef;

LLVMTypeRef LLVMGetContainedType(LLVMTypeRef type, size_t index);

LLVMValueRef LLVMBuildCall1(LLVMBuilderRef builder, LLVMFunctionRef function, LLVMValueRef arg);
LLVMValueRef LLVMBuildCall2(LLVMBuilderRef builder, LLVMFunctionRef function, LLVMValueRef a, LLVMValueRef b);

LLVMFunctionRef LLVMGetOrInsertFunction(LLVMModuleRef module, const char* name, LLVMFunctionTypeRef type);

LLVMValueRef LLVMBuildMemCpy(LLVMBuilderRef builder, LLVMContextRef context, LLVMModuleRef module, LLVMValueRef Dst, LLVMValueRef Src, LLVMValueRef Size, unsigned Align, bool isVolatile);

int LLVMAikeIsInstruction(LLVMValueRef value);

int LLVMAikeVerifyFunction(LLVMValueRef function);

const char* LLVMAikeGetTypeName(LLVMTypeRef type);

LLVMTargetMachineRef LLVMAikeCreateTargetMachine(LLVMTargetRef T, LLVMCodeGenOptLevel Level);

void LLVMAikeInit();

#ifdef __cplusplus
}
#endif
