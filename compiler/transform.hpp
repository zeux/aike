#pragma once

namespace llvm
{
	class Module;
}

void transformMergeDebugInfo(llvm::Module* module);
void transformOptimize(llvm::Module* module, int level);
