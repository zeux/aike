#pragma once

namespace llvm
{
	class LLVMContext;
	class Module;
}

struct Output;
struct Ast;

void codegen(Output& output, Ast* root, llvm::LLVMContext* context, llvm::Module* module);
