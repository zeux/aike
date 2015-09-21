#pragma once

namespace llvm
{
	class TargetMachine;
	class Module;
}

string targetHostTriple();
llvm::TargetMachine* targetCreate(const string& triple, int optimizationLevel);

string targetAssembleBinary(llvm::TargetMachine* target, llvm::Module* module);
string targetAssembleText(llvm::TargetMachine* target, llvm::Module* module);

void targetLink(const string& triple, const string& outputPath, const vector<string>& inputs, const string& runtimePath, bool debugInfo);