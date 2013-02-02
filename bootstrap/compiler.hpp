#pragma once

#include "llvmaike.hpp"

struct Expr;

void compile(LLVMContextRef context, LLVMModuleRef module, LLVMTargetDataRef targetData, Expr* root, bool generateDebugInfo);
