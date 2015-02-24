#pragma once

namespace llvm
{
	class Module;
	class Value;
}

struct Output;
struct Ast;

llvm::Value* codegen(Output& output, Ast* root, llvm::Module* module);

void codegenMain(llvm::Module* module, const vector<llvm::Value*>& entries);