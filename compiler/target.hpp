#pragma once

namespace llvm
{
	class TargetMachine;
	class Module;
}

llvm::TargetMachine* targetCreate(int optimizationLevel);

string targetAssembleBinary(llvm::TargetMachine* target, llvm::Module* module);
string targetAssembleText(llvm::TargetMachine* target, llvm::Module* module);

void targetLink(const string& output, const vector<string>& inputs);