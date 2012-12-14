#pragma once

namespace llvm
{
	class Module;
	class LLVMContext;
	class DataLayout;
}

void optimize(llvm::LLVMContext& context, llvm::Module* module, const llvm::DataLayout& layout);
