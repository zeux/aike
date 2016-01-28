#pragma once

namespace llvm
{
	class TargetMachine;
	class Module;
	class DataLayout;
}

void targetInitialize();

string targetHostTriple();

llvm::DataLayout targetDataLayout(const string& triple);

string targetAssembleBinary(const string& triple, llvm::Module* module, int optimizationLevel);
string targetAssembleText(const string& triple, llvm::Module* module, int optimizationLevel);

void targetLink(const string& triple, const string& outputPath, const vector<string>& inputs, const string& runtimePath, bool debugInfo);