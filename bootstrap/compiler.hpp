#pragma once

namespace llvm
{
	class Module;
	class LLVMContext;
}

struct SynBase;

void compile(llvm::LLVMContext& context, llvm::Module* module, SynBase* root);
