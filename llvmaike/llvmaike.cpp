#include "llvmaike.hpp"

#include <vector>

#include "llvm/Assembly/Parser.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Host.h"

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

void LLVMAikeFatalErrorHandler(void *user_data, const std::string& reason)
{
	throw std::exception(reason.c_str());
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

const char* LLVMAikeParseAssemblyString(const char* text, LLVMContextRef context, LLVMModuleRef module)
{
	llvm::SMDiagnostic err;

	if (!llvm::ParseAssemblyString(text, (llvm::Module*)module, err, *(llvm::LLVMContext*)context))
	{
		std::string error = err.getMessage().str() + " at '" + err.getLineContents().str() + "'";
		return strdup(error.c_str());
	}

	return 0;
}

std::string LLVMAikeGetTypeNameHelper(LLVMContextRef context, LLVMTypeRef type)
{
	switch(LLVMGetTypeKind(type))
	{
	case LLVMVoidTypeKind:
		return "void";
	case LLVMHalfTypeKind:
		return "half";
	case LLVMFloatTypeKind:
		return "float";
	case LLVMDoubleTypeKind:
		return "double";
	case LLVMLabelTypeKind:
		return "label";
	case LLVMIntegerTypeKind:
		if(type == LLVMInt1TypeInContext(context))
			return "i1";
		if(type == LLVMInt8TypeInContext(context))
			return "i8";
		if(type == LLVMInt16TypeInContext(context))
			return "i16";
		if(type == LLVMInt32TypeInContext(context))
			return "i32";
		if(type == LLVMInt64TypeInContext(context))
			return "i64";
		break;
	case LLVMFunctionTypeKind:
		{
			std::string comp_name = LLVMAikeGetTypeNameHelper(context, LLVMGetReturnType(type)) + " (";

			std::vector<LLVMTypeRef> args(LLVMCountParamTypes(type));
			LLVMGetParamTypes(type, args.data());
			for (size_t i = 0; i < args.size(); ++i)
				comp_name += (i == 0 ? "" : ", ") + LLVMAikeGetTypeNameHelper(context, args[i]);

			return comp_name + ")";
		}
		break;
	case LLVMStructTypeKind:
		if (const char *name = LLVMGetStructName(type))
		{
			return "%" + std::string(name);
		}else{
			std::string comp_name = "{ ";

			std::vector<LLVMTypeRef> members(LLVMCountStructElementTypes(type));
			LLVMGetStructElementTypes(type, members.data());
			for (size_t i = 0; i < members.size(); ++i)
				comp_name += (i == 0 ? "" : ", ") + LLVMAikeGetTypeNameHelper(context, members[i]);

			return comp_name + " }";
		}
		break;
	case LLVMPointerTypeKind:
		return LLVMAikeGetTypeNameHelper(context, LLVMGetElementType(type)) + "*";
	}

	return "unknown";
}

const char* LLVMAikeGetTypeName(LLVMTypeRef type)
{
	std::string result = LLVMAikeGetTypeNameHelper(LLVMGetTypeContext(type), type);

	return strdup(result.c_str());
}

const char* LLVMAikeGetHostTriple()
{
	std::string result = llvm::sys::getDefaultTargetTriple();

	return strdup(result.c_str());
}

const char* LLVMAikeGetHostCPU()
{
 	std::string result = llvm::sys::getHostCPUName();

	return strdup(result.c_str());
}

void LLVMAikeInit()
{
	LLVMLinkInJIT();
	LLVMLinkInInterpreter();

	LLVMInitializeNativeTarget();

	llvm::InitializeNativeTargetAsmPrinter();
}
