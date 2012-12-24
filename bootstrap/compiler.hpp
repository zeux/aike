#pragma once

namespace llvm
{
	class Module;
	class LLVMContext;
}

struct Expr;

void compile(llvm::LLVMContext& context, llvm::Module* module, Expr* root);
