#pragma once

namespace llvm
{
	class Module;
}

void optimize(llvm::Module* module, int level);