#pragma once

namespace llvm
{
	class Module;
}

struct Output;
struct Ast;

void codegen(Output& output, Ast* root, llvm::Module* module);
