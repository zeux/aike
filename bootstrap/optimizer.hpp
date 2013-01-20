#pragma once

#include "llvmaike.hpp"

void optimize(LLVMContextRef context, LLVMModuleRef module, LLVMTargetDataRef targetData);
