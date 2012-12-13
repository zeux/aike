#pragma once

namespace llvm
{
	class Module;
	class LLVMContext;
}

struct AstBase;

void compile(llvm::LLVMContext& context, llvm::Module* M, AstBase* root);
