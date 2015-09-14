#pragma once

namespace llvm
{
	class Module;
	class Value;
}

struct Output;
struct Ast;

struct CodegenOptions
{
	int debugInfo;
};

llvm::Value* codegen(Output& output, Ast* root, llvm::Module* module, const CodegenOptions& options);

void codegenMain(llvm::Module* module, const vector<llvm::Value*>& entries);