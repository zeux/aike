#pragma once

namespace llvm
{
	class Module;
	class LLVMContext;
	class DataLayout;
}

struct Expr;

void compile(llvm::LLVMContext& context, llvm::Module* module, llvm::DataLayout* layout, Expr* root);
