#include "llvmaike.hpp"

#include <vector>

#include "llvm/Assembly/Parser.h"
#include "llvm/Support/SourceMgr.h"

LLVMTypeRef LLVMGetContainedType(LLVMTypeRef type, size_t index)
{
	std::vector<LLVMTypeRef> contained((LLVMCountStructElementTypes(type)));
	LLVMGetStructElementTypes(type, contained.data());
	return contained[index];
}

LLVMValueRef LLVMBuildCall1(LLVMBuilderRef builder, LLVMFunctionRef function, LLVMValueRef arg)
{
	return LLVMBuildCall(builder, function, &arg, 1, "");
}

LLVMValueRef LLVMBuildCall2(LLVMBuilderRef builder, LLVMFunctionRef function, LLVMValueRef a, LLVMValueRef b)
{
	std::vector<LLVMValueRef> args;
	args.push_back(a);
	args.push_back(b);
	return LLVMBuildCall(builder, function, args.data(), args.size(), "");
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
			return name;
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

const char* LLVMAikeGetTypeName(LLVMContextRef context, LLVMTypeRef type)
{
	std::string result = LLVMAikeGetTypeNameHelper(context, type);

	return strdup(result.c_str());
}