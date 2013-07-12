#include "llvmaike.hpp"

#include <vector>
#include <stdexcept>

#include "llvm/Assembly/Parser.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/Host.h"
#include "llvm/Target/TargetOptions.h"

LLVMTypeRef LLVMGetContainedType(LLVMTypeRef type, size_t index)
{
	std::vector<LLVMTypeRef> contained((LLVMCountStructElementTypes(type)));
	LLVMGetStructElementTypes(type, contained.data());
	return contained[index];
}

LLVMValueRef LLVMBuildCall1(LLVMBuilderRef builder, LLVMFunctionRef function, LLVMValueRef arg)
{
	LLVMValueRef args[] = {arg};

	return LLVMBuildCall(builder, function, args, 1, "");
}

LLVMValueRef LLVMBuildCall2(LLVMBuilderRef builder, LLVMFunctionRef function, LLVMValueRef a, LLVMValueRef b)
{
	LLVMValueRef args[] = {a, b};

	return LLVMBuildCall(builder, function, args, 2, "");
}

LLVMFunctionRef LLVMGetOrInsertFunction(LLVMModuleRef module, const char* name, LLVMFunctionTypeRef type)
{
	if(LLVMFunctionRef func = LLVMGetNamedFunction(module, name))
		return func;

	return LLVMAddFunction(module, name, type);
}

LLVMValueRef LLVMBuildMemCpy(LLVMBuilderRef builder, LLVMContextRef context, LLVMModuleRef module, LLVMValueRef Dst, LLVMValueRef Src, LLVMValueRef Size, unsigned Align, bool isVolatile)
{
	Dst = LLVMBuildBitCast(builder, Dst, LLVMPointerType(LLVMInt8TypeInContext(context), 0), "");
	Src = LLVMBuildBitCast(builder, Src, LLVMPointerType(LLVMInt8TypeInContext(context), 0), "");

	LLVMValueRef Ops[] = { Dst, Src, Size, LLVMConstInt(LLVMInt32TypeInContext(context), Align, false), LLVMConstInt(LLVMInt1TypeInContext(context), isVolatile, false) };
	LLVMTypeRef Tys[] = { LLVMTypeOf(Ops[0]), LLVMTypeOf(Ops[1]), LLVMTypeOf(Ops[2]), LLVMTypeOf(Ops[3]), LLVMTypeOf(Ops[4]) };

	LLVMFunctionRef function = LLVMGetOrInsertFunction(module, "llvm.memcpy.p0i8.p0i8.i32", LLVMFunctionType(LLVMVoidTypeInContext(context), Tys, 5, false));

	return LLVMBuildCall(builder, function, Ops, 5, "");
}

int LLVMAikeIsInstruction(LLVMValueRef value)
{
	if(llvm::isa<llvm::Instruction>((llvm::Value*)value))
		return 1;

	return 0;
}

void LLVMAikeFatalErrorHandler(void *user_data, const std::string& reason)
{
	throw std::runtime_error(reason.c_str());
}

int LLVMAikeVerifyFunction(LLVMValueRef function)
{
	llvm::remove_fatal_error_handler();
	llvm::install_fatal_error_handler(LLVMAikeFatalErrorHandler, 0);

	try
	{
		if(LLVMVerifyFunction(function, LLVMPrintMessageAction))
			return 0;
	}catch(std::exception &e){
		return 0;
	}

	return 1;
}

LLVMTargetMachineRef LLVMAikeCreateTargetMachine(LLVMTargetRef T, LLVMCodeGenOptLevel Level)
{
	llvm::CodeGenOpt::Level OL;

	switch (Level) {
	case LLVMCodeGenLevelNone:
		OL = llvm::CodeGenOpt::None;
		break;
	case LLVMCodeGenLevelLess:
		OL = llvm::CodeGenOpt::Less;
		break;
	case LLVMCodeGenLevelAggressive:
		OL = llvm::CodeGenOpt::Aggressive;
		break;
	default:
		OL = llvm::CodeGenOpt::Default;
		break;
	}

	std::string triple = llvm::sys::getDefaultTargetTriple();
 	std::string cpu = llvm::sys::getHostCPUName();
    std::string features = "";

	llvm::TargetOptions opt;
    opt.NoFramePointerElim = true;
    opt.NoFramePointerElimNonLeaf = true;

	return llvm::wrap(llvm::unwrap(T)->createTargetMachine(triple, cpu, features, opt, llvm::Reloc::Default, llvm::CodeModel::Default, OL));
}

void LLVMAikeInit()
{
	LLVMLinkInJIT();
	LLVMLinkInInterpreter();

	LLVMInitializeNativeTarget();

	llvm::InitializeNativeTargetAsmPrinter();
}
