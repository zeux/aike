#pragma once

#include "llvm-c/Core.h"
#include "llvm-c/Target.h"

typedef LLVMValueRef LLVMFunctionRef;
typedef LLVMValueRef LLVMPHIRef;
typedef LLVMTypeRef LLVMStructTypeRef;
typedef LLVMTypeRef LLVMFunctionTypeRef;

LLVMTypeRef LLVMGetContainedType(LLVMTypeRef type, size_t index);

LLVMValueRef LLVMBuildCall1(LLVMBuilderRef builder, LLVMFunctionRef function, LLVMValueRef arg);
LLVMValueRef LLVMBuildCall2(LLVMBuilderRef builder, LLVMFunctionRef function, LLVMValueRef a, LLVMValueRef b);

LLVMFunctionRef LLVMGetOrInsertFunction(LLVMModuleRef module, const char* name, LLVMFunctionTypeRef type);

LLVMValueRef LLVMBuildMemCpy(LLVMBuilderRef builder, LLVMContextRef context, LLVMModuleRef module, LLVMValueRef Dst, LLVMValueRef Src, LLVMValueRef Size, unsigned Align, bool isVolatile);

const char* LLVMAikeParseAssemblyString(const char* text, LLVMContextRef context, LLVMModuleRef module);
const char* LLVMAikeGetTypeName(LLVMContextRef context, LLVMTypeRef type);