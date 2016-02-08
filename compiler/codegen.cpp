#include "common.hpp"
#include "codegen.hpp"

#include "ast.hpp"
#include "visit.hpp"
#include "output.hpp"
#include "mangle.hpp"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Intrinsics.h"

#include "llvm/AsmParser/Parser.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include <sstream>

using namespace llvm;

struct FunctionInstance
{
	Function* value;

	Ast::FnDecl* decl;

	FunctionInstance* parent;
	vector<pair<Ty*, Ty*>> generics;
};

struct Codegen
{
	Output* output;
	CodegenOptions options;

	LLVMContext* context;
	Module* module;

	IRBuilder<>* ir;
	DIBuilder* di;

	Constant* builtinTrap;
	Constant* builtinDebugTrap;

	Constant* runtimeNew;
	Constant* runtimeNewArray;

	unordered_map<Variable*, Value*> vars;

	vector<FunctionInstance*> pendingFunctions;
	FunctionInstance* currentFunction;

	vector<DIScope*> debugBlocks;
};

enum CodegenKind
{
	KindValue,
	KindRef,
};

struct CodegenDebugLocation
{
	Codegen& cg;
	DebugLoc debugLoc;

	CodegenDebugLocation(Codegen& cg, const Location& location): cg(cg), debugLoc(cg.ir->getCurrentDebugLocation())
	{
		if (cg.di)
			cg.ir->SetCurrentDebugLocation(DebugLoc::get(location.line + 1, location.column + 1, cg.debugBlocks.back()));
	}

	~CodegenDebugLocation()
	{
		if (cg.di)
			cg.ir->SetCurrentDebugLocation(debugLoc);
	}
};

static Ty* getGenericInstance(Codegen& cg, Ty* type)
{
	FunctionInstance* fn = cg.currentFunction;

	while (fn)
	{
		for (auto& g: fn->generics)
			if (g.first == type)
				return g.second;

		fn = fn->parent;
	}

	UNION_CASE(Generic, t, type);
	assert(t);

	ICE("Generic type %s was not instantiated", t->name.str().c_str());
}

static FunctionInstance* getFunctionInstance(Codegen& cg, Ast::FnDecl* decl)
{
	FunctionInstance* fn = cg.currentFunction;

	while (fn)
	{
		if (fn->decl == decl)
			return fn;

		fn = fn->parent;
	}

	return nullptr;
}

static Ty* tryGetGenericInstance(Codegen& cg, Ty* type)
{
	if (UNION_CASE(Instance, t, type))
		if (t->generic)
			return getGenericInstance(cg, t->generic);

	return nullptr;
}

static Type* codegenType(Codegen& cg, Ty* type)
{
	if (Ty* inst = tryGetGenericInstance(cg, type))
		return codegenType(cg, inst);

	if (UNION_CASE(Void, t, type))
		return Type::getVoidTy(*cg.context);

	if (UNION_CASE(Bool, t, type))
		return Type::getInt1Ty(*cg.context);

	if (UNION_CASE(Integer, t, type))
		return Type::getInt32Ty(*cg.context);

	if (UNION_CASE(Float, t, type))
		return Type::getFloatTy(*cg.context);

	if (UNION_CASE(String, t, type))
	{
		Type* fields[] = { Type::getInt8PtrTy(*cg.context), Type::getInt32Ty(*cg.context) };

		return StructType::get(*cg.context, { fields, 2 });
	}

	if (UNION_CASE(Tuple, t, type))
	{
		vector<Type*> fields;

		for (auto& f: t->fields)
			fields.push_back(codegenType(cg, f));

		return StructType::get(*cg.context, fields);
	}

	if (UNION_CASE(Array, t, type))
	{
		Type* element = codegenType(cg, t->element);
		Type* fields[] = { PointerType::get(element, 0), Type::getInt32Ty(*cg.context) };

		return StructType::get(*cg.context, { fields, 2 });
	}

	if (UNION_CASE(Pointer, t, type))
	{
		Type* element = codegenType(cg, t->element);

		return PointerType::get(element, 0);
	}

	if (UNION_CASE(Function, t, type))
	{
		Type* ret = codegenType(cg, t->ret);

		vector<Type*> args;

		for (auto& a: t->args)
			args.push_back(codegenType(cg, a));

		if (t->varargs)
		{
			Type* any = StructType::get(*cg.context, { Type::getInt8PtrTy(*cg.context), Type::getInt8PtrTy(*cg.context) });

			args.push_back(PointerType::get(any, 0));
			args.push_back(Type::getInt32Ty(*cg.context));
		}

		return PointerType::get(FunctionType::get(ret, args, false), 0);
	}

	if (UNION_CASE(Instance, t, type))
	{
		assert(t->def);

		if (UNION_CASE(Struct, d, t->def))
		{
			string name = mangleType(type, [&](Ty* ty) { return getGenericInstance(cg, ty); });

			if (StructType* st = cg.module->getTypeByName(name))
				return st;

			StructType* result = StructType::create(*cg.context, name);

			vector<Type*> fields;
			for (size_t i = 0; i < d->fields.size; ++i)
				fields.push_back(codegenType(cg, typeMember(type, i)));

			result->setBody(fields);

			return result;
		}

		ICE("Unknown TyDef kind %d", t->def->kind);
	}

	ICE("Unknown Ty kind %d", type->kind);
}

static GlobalVariable* codegenPrepareTypeInfo(Codegen& cg, const string& name, Type* dataType)
{
	GlobalVariable* gv = new GlobalVariable(*cg.module, dataType, /* isConstant= */ true, GlobalValue::PrivateLinkage, nullptr, name);

	gv->setUnnamedAddr(true);

	return gv;
}

static Constant* codegenMakeTypeInfo(Codegen& cg, const string& name, Constant* data)
{
	GlobalVariable* gv = codegenPrepareTypeInfo(cg, name, data->getType());

	gv->setInitializer(data);

	return ConstantExpr::getPointerCast(gv, Type::getInt8PtrTy(*cg.context));
}

static Constant* codegenTypeInfo(Codegen& cg, Ty* type)
{
	if (Ty* inst = tryGetGenericInstance(cg, type))
		return codegenTypeInfo(cg, inst);

	string name = mangleTypeInfo(type, [&](Ty* ty) { return getGenericInstance(cg, ty); });

	if (GlobalVariable* gv = cg.module->getNamedGlobal(name))
		return ConstantExpr::getPointerCast(gv, Type::getInt8PtrTy(*cg.context));

	const DataLayout& layout = cg.module->getDataLayout();

	if (UNION_CASE(Void, t, type))
		return codegenMakeTypeInfo(cg, name, ConstantStruct::getAnon({ cg.ir->getInt32(0) }));

	if (UNION_CASE(Bool, t, type))
		return codegenMakeTypeInfo(cg, name, ConstantStruct::getAnon({ cg.ir->getInt32(1) }));

	if (UNION_CASE(Integer, t, type))
		return codegenMakeTypeInfo(cg, name, ConstantStruct::getAnon({ cg.ir->getInt32(2) }));

	if (UNION_CASE(Float, t, type))
		return codegenMakeTypeInfo(cg, name, ConstantStruct::getAnon({ cg.ir->getInt32(3) }));

	if (UNION_CASE(String, t, type))
		return codegenMakeTypeInfo(cg, name, ConstantStruct::getAnon({ cg.ir->getInt32(4) }));

	if (UNION_CASE(Tuple, t, type))
	{
		StructType* sty = cast<StructType>(codegenType(cg, type));
		const StructLayout* sl = layout.getStructLayout(sty);

		vector<Constant*> fields;
		for (size_t i = 0; i < t->fields.size; ++i)
		{
			Constant* ft = codegenTypeInfo(cg, t->fields[i]);
			Constant* fo = cg.ir->getInt32(sl->getElementOffset(i));

			fields.push_back(ConstantStruct::getAnon({ ft, fo }));
		}

		assert(!fields.empty());

		Constant* fieldArr = ConstantArray::get(ArrayType::get(fields[0]->getType(), fields.size()), fields);

		return codegenMakeTypeInfo(cg, name, ConstantStruct::getAnon({ cg.ir->getInt32(5), cg.ir->getInt32(fields.size()), fieldArr }));
	}

	if (UNION_CASE(Array, t, type))
	{
		int stride = layout.getTypeAllocSize(codegenType(cg, t->element));
		Constant* element = codegenTypeInfo(cg, t->element);

		return codegenMakeTypeInfo(cg, name, ConstantStruct::getAnon({ cg.ir->getInt32(6), element, cg.ir->getInt32(stride) }));
	}

	if (UNION_CASE(Pointer, t, type))
	{
		Constant* element = codegenTypeInfo(cg, t->element);

		return codegenMakeTypeInfo(cg, name, ConstantStruct::getAnon({ cg.ir->getInt32(7), element }));
	}

	if (UNION_CASE(Function, t, type))
		return codegenMakeTypeInfo(cg, name, ConstantStruct::getAnon({ cg.ir->getInt32(8) }));

	if (UNION_CASE(Instance, t, type))
	{
		assert(t->def);

		if (UNION_CASE(Struct, d, t->def))
		{
			Type* fieldType = StructType::get(Type::getInt8PtrTy(*cg.context), Type::getInt8PtrTy(*cg.context), Type::getInt32Ty(*cg.context), nullptr);
			ArrayType* fieldArrType = ArrayType::get(fieldType, d->fields.size);
			Type* dataType = StructType::get(Type::getInt32Ty(*cg.context), Type::getInt8PtrTy(*cg.context), Type::getInt32Ty(*cg.context), fieldArrType, nullptr);

			GlobalVariable* gv = codegenPrepareTypeInfo(cg, name, dataType);

			StructType* sty = cast<StructType>(codegenType(cg, type));
			const StructLayout* sl = layout.getStructLayout(sty);

			vector<Constant*> fields;
			for (size_t i = 0; i < d->fields.size; ++i)
			{
				Constant* fn = cast<Constant>(cg.ir->CreateGlobalStringPtr(d->fields[i].name.str()));
				Constant* ft = codegenTypeInfo(cg, typeMember(type, i));
				Constant* fo = cg.ir->getInt32(sl->getElementOffset(i));

				fields.push_back(ConstantStruct::getAnon({ fn, ft, fo }));
			}

			Constant* fieldArr = ConstantArray::get(fieldArrType, fields);

			Constant* sn = cast<Constant>(cg.ir->CreateGlobalStringPtr(t->name.str()));

			gv->setInitializer(ConstantStruct::getAnon({ cg.ir->getInt32(9), sn, cg.ir->getInt32(fields.size()), fieldArr }));

			return ConstantExpr::getPointerCast(gv, Type::getInt8PtrTy(*cg.context));
		}

		ICE("Unknown TyDef kind %d", t->def->kind);
	}

	ICE("Unknown Ty kind %d", type->kind);
}

static DIType* codegenTypeDebug(Codegen& cg, Ty* type)
{
	assert(cg.di);

	const DataLayout& layout = cg.module->getDataLayout();

	if (Ty* inst = tryGetGenericInstance(cg, type))
		return codegenTypeDebug(cg, inst);

	if (UNION_CASE(Void, t, type))
		return nullptr;

	if (UNION_CASE(Bool, t, type))
		return cg.di->createBasicType("bool", 8, 0, dwarf::DW_ATE_boolean);

	if (UNION_CASE(Integer, t, type))
		return cg.di->createBasicType("int", 32, 0, dwarf::DW_ATE_signed);

	if (UNION_CASE(Float, t, type))
		return cg.di->createBasicType("float", 32, 0, dwarf::DW_ATE_float);

	if (UNION_CASE(String, t, type))
	{
		DIType* ety = cg.di->createBasicType("char", 8, 0, dwarf::DW_ATE_signed_char);
		DIType* pty = cg.di->createPointerType(ety, layout.getPointerSizeInBits());
		DIType* szty = cg.di->createBasicType("int", 32, 0, dwarf::DW_ATE_signed);

		StructType* sty = cast<StructType>(codegenType(cg, type));
		const StructLayout* sl = layout.getStructLayout(sty);

		Metadata* fields[] =
		{
			cg.di->createMemberType(nullptr, "data", nullptr, 0,
				layout.getTypeSizeInBits(sty->getElementType(0)), 0, sl->getElementOffset(0) * 8, 0, pty),
			cg.di->createMemberType(nullptr, "size", nullptr, 0,
				layout.getTypeSizeInBits(sty->getElementType(1)), 0, sl->getElementOffset(1) * 8, 0, szty),
		};

		return cg.di->createStructType(
			nullptr, "string", nullptr, 0,
			sl->getSizeInBits(), 0, 0,
			nullptr, cg.di->getOrCreateArray(fields));
	}

	if (UNION_CASE(Tuple, t, type))
	{
		StructType* sty = cast<StructType>(codegenType(cg, type));
		const StructLayout* sl = layout.getStructLayout(sty);

		vector<Metadata*> fields;

		for (size_t i = 0; i < t->fields.size; ++i)
		{
			DIType* dty = codegenTypeDebug(cg, t->fields[i]);

			fields.push_back(cg.di->createMemberType(
				nullptr, "_" + to_string(i), nullptr, 0,
				layout.getTypeSizeInBits(sty->getElementType(i)),
				0, sl->getElementOffset(i) * 8, 0, dty));
		}

		return cg.di->createStructType(
			nullptr, "tuple", nullptr, 0,
			sl->getSizeInBits(), 0, 0,
			nullptr, cg.di->getOrCreateArray(fields));
	}

	if (UNION_CASE(Array, t, type))
	{
		DIType* ety = codegenTypeDebug(cg, t->element);
		DIType* pty = cg.di->createPointerType(ety, layout.getPointerSizeInBits());
		DIType* szty = cg.di->createBasicType("int", 32, 0, dwarf::DW_ATE_signed);

		StructType* sty = cast<StructType>(codegenType(cg, type));
		const StructLayout* sl = layout.getStructLayout(sty);

		Metadata* fields[] =
		{
			cg.di->createMemberType(nullptr, "data", nullptr, 0,
				layout.getTypeSizeInBits(sty->getElementType(0)), 0, sl->getElementOffset(0) * 8, 0, pty),
			cg.di->createMemberType(nullptr, "size", nullptr, 0,
				layout.getTypeSizeInBits(sty->getElementType(1)), 0, sl->getElementOffset(1) * 8, 0, szty),
		};

		return cg.di->createStructType(
			nullptr, "array", nullptr, 0,
			sl->getSizeInBits(), 0, 0,
			nullptr, cg.di->getOrCreateArray(fields));
	}

	if (UNION_CASE(Pointer, t, type))
	{
		DIType* element = codegenTypeDebug(cg, t->element);

		return cg.di->createPointerType(element, layout.getPointerSizeInBits());
	}

	if (UNION_CASE(Function, t, type))
	{
		vector<Metadata*> args;

		args.push_back(codegenTypeDebug(cg, t->ret));

		for (auto& a: t->args)
			args.push_back(codegenTypeDebug(cg, a));

		return cg.di->createSubroutineType(cg.di->getOrCreateTypeArray(args));
	}

	if (UNION_CASE(Instance, t, type))
	{
		assert(t->def);

		if (UNION_CASE(Struct, d, t->def))
		{
			string name = "dbg." + mangleType(type, [&](Ty* ty) { return getGenericInstance(cg, ty); });
			NamedMDNode* node = cg.module->getOrInsertNamedMetadata(name);

			if (node->getNumOperands())
				return cast<DIType>(node->getOperand(0));

			StructType* ty = cast<StructType>(codegenType(cg, type));
			const StructLayout* sl = layout.getStructLayout(ty);

			DICompositeType* result = cg.di->createStructType(
				nullptr, t->name.str(), nullptr, 0,
				sl->getSizeInBits(), 0, 0,
				nullptr, cg.di->getOrCreateArray({}));

			node->addOperand(result);

			vector<Metadata*> fields;

			for (size_t i = 0; i < d->fields.size; ++i)
			{
				DIType* dty = codegenTypeDebug(cg, typeMember(type, i));

				fields.push_back(cg.di->createMemberType(
					nullptr, d->fields[i].name.str(), nullptr, 0,
					layout.getTypeSizeInBits(ty->getElementType(i)),
					0, sl->getElementOffset(i) * 8, 0, dty));
			}

			cg.di->replaceArrays(result, cg.di->getOrCreateArray(fields));

			return result;
		}

		ICE("Unknown TyDef kind %d", t->def->kind);
	}

	ICE("Unknown Ty kind %d", type->kind);
}

static Ty* finalType(Codegen& cg, Ty* type)
{
	return typeInstantiate(type, [&](Ty* ty) -> Ty* {
		return getGenericInstance(cg, ty);
	});
}

static Ty* finalType(Codegen& cg, Ast* node)
{
	return finalType(cg, astType(node));
}

static Value* codegenVoid(Codegen& cg)
{
	return UndefValue::get(Type::getVoidTy(*cg.context));
}

static void codegenVariable(Codegen& cg, Variable* var, Value* value)
{
	cg.vars[var] = value;

	value->setName(var->name.str());
}

static Value* codegenAlloca(Codegen& cg, Type* type, Constant* arraySize = nullptr)
{
	// LLVM has an undocumented convention related to alloca: alloca with
	// constant size are treated as compile-time stack allocation only if
	// the instruction is in the first basic block. Thus performing alloca
	// in other blocks can cause stack leaks and incorrect debug info.
	// Work around this restriction by inserting all allocas (in reverse order)
	// in the first basic block.
	BasicBlock* ib = cg.ir->GetInsertBlock();
	BasicBlock* bb = &ib->getParent()->getEntryBlock();

	Instruction* inst = new AllocaInst(type, arraySize);

	bb->getInstList().insert(bb->getFirstInsertionPt(), inst);
	cg.ir->SetInstDebugLocation(inst);

	return inst;
}

static void codegenTrapIf(Codegen& cg, Value* cond, bool debug = false)
{
	Function* func = cg.ir->GetInsertBlock()->getParent();

	BasicBlock* trapbb = BasicBlock::Create(*cg.context, "trap");
	BasicBlock* afterbb = BasicBlock::Create(*cg.context, "after");

	cg.ir->CreateCondBr(cond, trapbb, afterbb);

	func->getBasicBlockList().push_back(trapbb);
	cg.ir->SetInsertPoint(trapbb);

	cg.ir->CreateCall(debug ? cg.builtinDebugTrap : cg.builtinTrap);
	cg.ir->CreateUnreachable();

	func->getBasicBlockList().push_back(afterbb);
	cg.ir->SetInsertPoint(afterbb);
}

static Value* codegenNew(Codegen& cg, Ty* type)
{
	Type* elementType = codegenType(cg, type);
	Type* pointerType = PointerType::get(elementType, 0);

	Constant* typeInfo = codegenTypeInfo(cg, type);

	// TODO: refactor + fix int32/size_t
	Value* elementSize = cg.ir->CreateIntCast(ConstantExpr::getSizeOf(elementType), cg.ir->getInt32Ty(), false);
	Value* rawPtr = cg.ir->CreateCall(cg.runtimeNew, { typeInfo, elementSize });
	Value* ptr = cg.ir->CreateBitCast(rawPtr, pointerType);

	return ptr;
}

static Value* codegenNewArr(Codegen& cg, Ty* type, Value* count)
{
	Type* elementType = codegenType(cg, type);
	Type* pointerType = PointerType::get(elementType, 0);

	Constant* typeInfo = codegenTypeInfo(cg, type);

	// TODO: refactor + fix int32/size_t
	Value* elementSize = cg.ir->CreateIntCast(ConstantExpr::getSizeOf(elementType), cg.ir->getInt32Ty(), false);
	Value* rawPtr = cg.ir->CreateCall(cg.runtimeNewArray, { typeInfo, count, elementSize });
	Value* ptr = cg.ir->CreateBitCast(rawPtr, pointerType);

	return ptr;
}

static Value* codegenNewArrEmpty(Codegen& cg, Ty* type)
{
	Type* elementType = codegenType(cg, type);
	Type* pointerType = PointerType::get(elementType, 0);

	return Constant::getNullValue(pointerType);
}

static Value* codegenExpr(Codegen& cg, Ast* node, CodegenKind kind = KindValue);

static Value* codegenCommon(Codegen& cg, Ast::Common* n, CodegenKind kind)
{
	return nullptr;
}

static Value* codegenLiteralVoid(Codegen& cg, Ast::LiteralVoid* n, CodegenKind kind)
{
	return codegenVoid(cg);
}

static Value* codegenLiteralBool(Codegen& cg, Ast::LiteralBool* n, CodegenKind kind)
{
	return cg.ir->getInt1(n->value);
}

static Value* codegenLiteralInteger(Codegen& cg, Ast::LiteralInteger* n, CodegenKind kind)
{
	return ConstantInt::getSigned(cg.ir->getInt32Ty(), n->value);
}

static Value* codegenLiteralFloat(Codegen& cg, Ast::LiteralFloat* n, CodegenKind kind)
{
	return ConstantFP::get(cg.ir->getFloatTy(), n->value);
}

static Value* codegenLiteralString(Codegen& cg, Ast::LiteralString* n, CodegenKind kind)
{
	CodegenDebugLocation dbg(cg, n->location);

	Type* type = codegenType(cg, UNION_NEW(Ty, String, {}));

	Value* result = UndefValue::get(type);

	Value* string = cg.ir->CreateGlobalStringPtr(StringRef(n->value.data, n->value.size));

	result = cg.ir->CreateInsertValue(result, string, 0);
	result = cg.ir->CreateInsertValue(result, cg.ir->getInt32(n->value.size), 1);

	return result;
}

static Value* codegenLiteralTuple(Codegen& cg, Ast::LiteralTuple* n, CodegenKind kind)
{
	CodegenDebugLocation dbg(cg, n->location);

	Type* type = codegenType(cg, n->type);

	Value* result = UndefValue::get(type);

	for (size_t i = 0; i < n->fields.size; ++i)
		result = cg.ir->CreateInsertValue(result, codegenExpr(cg, n->fields[i]), i);

	return result;
}

static Value* codegenLiteralArray(Codegen& cg, Ast::LiteralArray* n, CodegenKind kind)
{
	CodegenDebugLocation dbg(cg, n->location);

	UNION_CASE(Array, ta, n->type);
	assert(ta);

	Value* ptr =
		n->elements.size
		? codegenNewArr(cg, ta->element, cg.ir->getInt32(n->elements.size))
		: codegenNewArrEmpty(cg, ta->element);

	for (size_t i = 0; i < n->elements.size; ++i)
	{
		Value* expr = codegenExpr(cg, n->elements[i]);

		cg.ir->CreateStore(expr, cg.ir->CreateConstInBoundsGEP1_32(expr->getType(), ptr, i));
	}

	Type* type = codegenType(cg, n->type);

	Value* result = UndefValue::get(type);

	result = cg.ir->CreateInsertValue(result, ptr, 0);
	result = cg.ir->CreateInsertValue(result, cg.ir->getInt32(n->elements.size), 1);

	return result;
}

static Value* codegenLiteralStruct(Codegen& cg, Ast::LiteralStruct* n, CodegenKind kind)
{
	CodegenDebugLocation dbg(cg, n->location);

	UNION_CASE(Instance, ti, n->type);
	assert(ti);

	UNION_CASE(Struct, td, ti->def);
	assert(td);

	Type* type = codegenType(cg, n->type);

	Value* result = UndefValue::get(type);

	vector<bool> fields(td->fields.size);

	for (auto& f: n->fields)
	{
		assert(f.first.index >= 0);

		fields[f.first.index] = true;
	}

	for (size_t i = 0; i < fields.size(); ++i)
		if (!fields[i])
		{
			assert(td->fields[i].expr);

			Value* expr = codegenExpr(cg, td->fields[i].expr);

			result = cg.ir->CreateInsertValue(result, expr, i);
		}

	for (auto& f: n->fields)
	{
		assert(f.first.index >= 0);

		Value* expr = codegenExpr(cg, f.second);

		result = cg.ir->CreateInsertValue(result, expr, f.first.index);
	}

	return result;
}

static Value* codegenFunctionDecl(Codegen& cg, Ast::FnDecl* decl, int id, Ty* type, const Arr<Ty*>& tyargs)
{
	FunctionInstance* parent = getFunctionInstance(cg, decl->parent);

	string parentName = parent ? string(parent->value->getName()) : decl->module ? mangleModule(decl->module->name) : "";
	string name = mangleFn(decl->var->name, id, type, tyargs, [&](Ty* ty) { return getGenericInstance(cg, ty); }, parentName);

	if (Function* fun = cg.module->getFunction(name))
		return fun;

	FunctionType* funty = cast<FunctionType>(cast<PointerType>(codegenType(cg, type))->getElementType());
	Function* fun = Function::Create(funty, GlobalValue::InternalLinkage, name, cg.module);

	vector<pair<Ty*, Ty*>> inst;
	assert(tyargs.size == decl->tyargs.size);

	for (size_t i = 0; i < tyargs.size; ++i)
	{
		Ty* ft = finalType(cg, tyargs[i]);

		inst.push_back(make_pair(decl->tyargs[i], ft));
	}

	cg.pendingFunctions.push_back(new FunctionInstance { fun, decl, parent, inst });

	return fun;
}

static Value* codegenIdent(Codegen& cg, Ast::Ident* n, CodegenKind kind)
{
	CodegenDebugLocation dbg(cg, n->location);

	assert(n->targets.size == 1);
	Variable* target = n->targets[0];

	if (target->kind == Variable::KindFunction)
	{
		UNION_CASE(FnDecl, decl, target->fn);
		assert(decl && decl->var == target);

		return codegenFunctionDecl(cg, decl, 0, n->type, n->tyargs);
	}
	else if (target->kind == Variable::KindVariable)
	{
		auto it = cg.vars.find(target);
		assert(it != cg.vars.end());

		if (kind != KindRef)
			return cg.ir->CreateLoad(it->second);
		else
			return it->second;
	}
	else if (target->kind == Variable::KindValue || target->kind == Variable::KindArgument)
	{
		auto it = cg.vars.find(target);
		assert(it != cg.vars.end());

		return it->second;
	}
	else
	{
		ICE("Unknown Variable kind %d", target->kind);
	}
}

static Value* codegenMember(Codegen& cg, Ast::Member* n, CodegenKind kind)
{
	CodegenDebugLocation dbg(cg, n->location);

	assert(n->field.index >= 0);

	Value* expr = codegenExpr(cg, n->expr, kind);

	if (kind == KindRef)
		return cg.ir->CreateStructGEP(cast<PointerType>(expr->getType())->getElementType(), expr, n->field.index);
	else
		return cg.ir->CreateExtractValue(expr, n->field.index);
}

static Value* codegenBlock(Codegen& cg, Ast::Block* n, CodegenKind kind)
{
	Value* result = codegenVoid(cg);

	for (auto& e: n->body)
		result = codegenExpr(cg, e);

	return result;
}

static Value* codegenModule(Codegen& cg, Ast::Module* n, CodegenKind kind)
{
	return codegenExpr(cg, n->body);
}

static Value* codegenCall(Codegen& cg, Ast::Call* n, CodegenKind kind)
{
	CodegenDebugLocation dbg(cg, n->location);

	Value* expr = codegenExpr(cg, n->expr);

	UNION_CASE(Function, tf, astType(n->expr));
	assert(tf);

	vector<Value*> args;

	if (tf->varargs)
	{
		assert(n->args.size >= tf->args.size);

		size_t baseArgs = tf->args.size;
		size_t extraArgs = n->args.size - baseArgs;

		Type* any = StructType::get(*cg.context, { Type::getInt8PtrTy(*cg.context), Type::getInt8PtrTy(*cg.context) });

		Value* extraArray = codegenAlloca(cg, any, cg.ir->getInt32(extraArgs));

		for (size_t i = 0; i < baseArgs; ++i)
			args.push_back(codegenExpr(cg, n->args[i]));

		for (size_t i = 0; i < extraArgs; ++i)
		{
			Value* av = codegenExpr(cg, n->args[baseArgs + i]);
			Value* at = codegenTypeInfo(cg, astType(n->args[baseArgs + i]));

			Value* ap = codegenAlloca(cg, av->getType());

			cg.ir->CreateStore(av, ap);

			Value* app = cg.ir->CreatePointerCast(ap, Type::getInt8PtrTy(*cg.context));

			cg.ir->CreateStore(at, cg.ir->CreateConstInBoundsGEP2_32(any, extraArray, i, 0));
			cg.ir->CreateStore(app, cg.ir->CreateConstInBoundsGEP2_32(any, extraArray, i, 1));
		}

		args.push_back(extraArray);
		args.push_back(cg.ir->getInt32(extraArgs));
	}
	else
	{
		assert(n->args.size == tf->args.size);

		for (auto& a: n->args)
			args.push_back(codegenExpr(cg, a));
	}

	Value* ret = cg.ir->CreateCall(expr, args);

	if (ret->getType()->isVoidTy())
		return codegenVoid(cg);
	else
		return ret;
}

static Value* codegenIndex(Codegen& cg, Ast::Index* n, CodegenKind kind)
{
	CodegenDebugLocation dbg(cg, n->location);

	Value* expr = codegenExpr(cg, n->expr);
	Value* index = codegenExpr(cg, n->index);

	Value* ptr = cg.ir->CreateExtractValue(expr, 0);
	Value* size = cg.ir->CreateExtractValue(expr, 1);

	Value* cond = cg.ir->CreateICmpUGE(index, size);

	codegenTrapIf(cg, cond);

	if (kind == KindRef)
		return cg.ir->CreateInBoundsGEP(ptr, index);
	else
		return cg.ir->CreateLoad(cg.ir->CreateInBoundsGEP(ptr, index));
}

static Value* codegenAssign(Codegen& cg, Ast::Assign* n, CodegenKind kind)
{
	CodegenDebugLocation dbg(cg, n->location);

	Value* left = codegenExpr(cg, n->left, KindRef);
	Value* right = codegenExpr(cg, n->right);

	cg.ir->CreateStore(right, left);

	return codegenVoid(cg);
}

static Value* codegenUnary(Codegen& cg, Ast::Unary* n, CodegenKind kind)
{
	CodegenDebugLocation dbg(cg, n->location);

	Value* expr = codegenExpr(cg, n->expr);

	switch (n->op)
	{
	case UnaryOpNot:
		return cg.ir->CreateNot(expr);

	case UnaryOpDeref:
		if (kind == KindRef)
			return expr;
		else
			return cg.ir->CreateLoad(expr);

	case UnaryOpNew:
	{
		Value* ptr = codegenNew(cg, astType(n->expr));

		cg.ir->CreateStore(expr, ptr);

		return ptr;
	}

	default:
		ICE("Unknown UnaryOp %d", n->op);
	}
}

static Value* codegenBinaryAndOr(Codegen& cg, Ast::Binary* n)
{
	CodegenDebugLocation dbg(cg, n->location);

	Function* func = cg.ir->GetInsertBlock()->getParent();

	Value* left = codegenExpr(cg, n->left);

	BasicBlock* currentbb = cg.ir->GetInsertBlock();
	BasicBlock* nextbb = BasicBlock::Create(*cg.context, "next");
	BasicBlock* afterbb = BasicBlock::Create(*cg.context, "after");

	if (n->op == BinaryOpAnd)
		cg.ir->CreateCondBr(left, nextbb, afterbb);
	else if (n->op == BinaryOpOr)
		cg.ir->CreateCondBr(left, afterbb, nextbb);
	else
		ICE("Unknown BinaryOp %d", n->op);

	func->getBasicBlockList().push_back(nextbb);
	cg.ir->SetInsertPoint(nextbb);

	Value* right = codegenExpr(cg, n->right);
	cg.ir->CreateBr(afterbb);
	nextbb = cg.ir->GetInsertBlock();

	func->getBasicBlockList().push_back(afterbb);
	cg.ir->SetInsertPoint(afterbb);

	PHINode* pn = cg.ir->CreatePHI(left->getType(), 2);

	pn->addIncoming(right, nextbb);
	pn->addIncoming(left, currentbb);

	return pn;
}

static Value* codegenBinary(Codegen& cg, Ast::Binary* n, CodegenKind kind)
{
	assert(n->op == BinaryOpAnd || n->op == BinaryOpOr);

	return codegenBinaryAndOr(cg, n);
}

static Value* codegenIf(Codegen& cg, Ast::If* n, CodegenKind kind)
{
	CodegenDebugLocation dbg(cg, n->location);

	Value* cond = codegenExpr(cg, n->cond);

	Function* func = cg.ir->GetInsertBlock()->getParent();

	if (n->elsebody)
	{
		BasicBlock* thenbb = BasicBlock::Create(*cg.context, "then");
		BasicBlock* elsebb = BasicBlock::Create(*cg.context, "else");
		BasicBlock* endbb = BasicBlock::Create(*cg.context, "ifend");

		cg.ir->CreateCondBr(cond, thenbb, elsebb);

		func->getBasicBlockList().push_back(thenbb);
		cg.ir->SetInsertPoint(thenbb);

		Value* thenbody = codegenExpr(cg, n->thenbody);
		cg.ir->CreateBr(endbb);
		thenbb = cg.ir->GetInsertBlock();

		func->getBasicBlockList().push_back(elsebb);
		cg.ir->SetInsertPoint(elsebb);

		Value* elsebody = codegenExpr(cg, n->elsebody);
		cg.ir->CreateBr(endbb);
		elsebb = cg.ir->GetInsertBlock();

		func->getBasicBlockList().push_back(endbb);
		cg.ir->SetInsertPoint(endbb);

		if (thenbody->getType()->isVoidTy())
		{
			return codegenVoid(cg);
		}
		else
		{
			PHINode* pn = cg.ir->CreatePHI(thenbody->getType(), 2);

			pn->addIncoming(thenbody, thenbb);
			pn->addIncoming(elsebody, elsebb);

			return pn;
		}
	}
	else
	{
		BasicBlock* thenbb = BasicBlock::Create(*cg.context, "then");
		BasicBlock* endbb = BasicBlock::Create(*cg.context, "ifend");

		cg.ir->CreateCondBr(cond, thenbb, endbb);

		func->getBasicBlockList().push_back(thenbb);
		cg.ir->SetInsertPoint(thenbb);

		Value* thenbody = codegenExpr(cg, n->thenbody);
		cg.ir->CreateBr(endbb);

		func->getBasicBlockList().push_back(endbb);
		cg.ir->SetInsertPoint(endbb);

		return codegenVoid(cg);
	}
}

static Value* codegenFor(Codegen& cg, Ast::For* n, CodegenKind kind)
{
	CodegenDebugLocation dbg(cg, n->location);

	Function* func = cg.ir->GetInsertBlock()->getParent();

	BasicBlock* entrybb = cg.ir->GetInsertBlock();
	BasicBlock* loopbb = BasicBlock::Create(*cg.context, "loop");
	BasicBlock* endbb = BasicBlock::Create(*cg.context, "forend");

	Value* expr = codegenExpr(cg, n->expr);

	Value* size = cg.ir->CreateExtractValue(expr, 1);

	cg.ir->CreateCondBr(cg.ir->CreateICmpSGT(size, cg.ir->getInt32(0)), loopbb, endbb);

	func->getBasicBlockList().push_back(loopbb);
	cg.ir->SetInsertPoint(loopbb);

	PHINode* index = cg.ir->CreatePHI(Type::getInt32Ty(*cg.context), 2);

	index->addIncoming(cg.ir->getInt32(0), entrybb);

	Value* var = cg.ir->CreateInBoundsGEP(cg.ir->CreateExtractValue(expr, 0), index);

	codegenVariable(cg, n->var, var);

	if (n->index)
		codegenVariable(cg, n->index, index);

	codegenExpr(cg, n->body);

	Value* next = cg.ir->CreateAdd(index, cg.ir->getInt32(1));

	BasicBlock* loopendbb = cg.ir->GetInsertBlock();

	cg.ir->CreateCondBr(cg.ir->CreateICmpSLT(next, size), loopbb, endbb);

	index->addIncoming(next, loopendbb);

	func->getBasicBlockList().push_back(endbb);
	cg.ir->SetInsertPoint(endbb);

	return codegenVoid(cg);
}

static Value* codegenWhile(Codegen& cg, Ast::While* n, CodegenKind kind)
{
	CodegenDebugLocation dbg(cg, n->location);

	Function* func = cg.ir->GetInsertBlock()->getParent();

	BasicBlock* entrybb = cg.ir->GetInsertBlock();
	BasicBlock* loopbb = BasicBlock::Create(*cg.context, "loop");
	BasicBlock* bodybb = BasicBlock::Create(*cg.context, "whilebody");
	BasicBlock* endbb = BasicBlock::Create(*cg.context, "whileend");

	cg.ir->CreateBr(loopbb);

	func->getBasicBlockList().push_back(loopbb);
	cg.ir->SetInsertPoint(loopbb);

	Value* expr = codegenExpr(cg, n->expr);

	cg.ir->CreateCondBr(expr, bodybb, endbb);

	func->getBasicBlockList().push_back(bodybb);
	cg.ir->SetInsertPoint(bodybb);

	codegenExpr(cg, n->body);

	cg.ir->CreateBr(loopbb);

	func->getBasicBlockList().push_back(endbb);
	cg.ir->SetInsertPoint(endbb);

	return codegenVoid(cg);
}

static Value* codegenFn(Codegen& cg, Ast::Fn* n, CodegenKind kind)
{
	CodegenDebugLocation dbg(cg, n->location);

	UNION_CASE(FnDecl, decl, n->decl);

	return codegenFunctionDecl(cg, decl, n->id, decl->var->type, Arr<Ty*>());
}

static Value* codegenLLVM(Codegen& cg, Ast::LLVM* n, CodegenKind kind)
{
	return nullptr;
}

static Value* codegenFnDecl(Codegen& cg, Ast::FnDecl* n, CodegenKind kind)
{
	return codegenVoid(cg);
}

static Value* codegenVarDecl(Codegen& cg, Ast::VarDecl* n, CodegenKind kind)
{
	CodegenDebugLocation dbg(cg, n->var->location);

	Value* expr = codegenExpr(cg, n->expr);

	Value* storage = codegenAlloca(cg, expr->getType());
	storage->setName(n->var->name.str());

	cg.ir->CreateStore(expr, storage);

	codegenVariable(cg, n->var, storage);

	if (cg.di && cg.options.debugInfo >= 2)
	{
		DIFile* file = cg.di->createFile(n->var->location.source, StringRef());

		DIType* dty = codegenTypeDebug(cg, n->var->type);
		DILocalVariable* dvar = cg.di->createAutoVariable(
			cg.debugBlocks.back(), n->var->name.str(), file, n->var->location.line + 1, dty,
			/* alwaysPreserve= */ false, /* flags= */ 0);

		DebugLoc dloc = DebugLoc::get(n->var->location.line + 1, n->var->location.column + 1, cg.debugBlocks.back());

		cg.di->insertDeclare(storage, dvar, cg.di->createExpression(), dloc, cg.ir->GetInsertBlock());
	}

	return codegenVoid(cg);
}

static Value* codegenTyDecl(Codegen& cg, Ast::TyDecl* n, CodegenKind kind)
{
	return codegenVoid(cg);
}

static Value* codegenImport(Codegen& cg, Ast::Import* n, CodegenKind kind)
{
	return codegenVoid(cg);
}

static Value* codegenExpr(Codegen& cg, Ast* root, CodegenKind kind)
{
#define CALL(name, ...) else if (UNION_CASE(name, n, root)) return codegen##name(cg, n, kind);

	if (false) ;
	UD_AST(CALL)
	else ICE("Unknown Ast kind %d", root->kind);

#undef CALL
}

static vector<Value*> getFunctionArguments(Function* f)
{
	vector<Value*> result;

	for (Function::arg_iterator ait = f->arg_begin(); ait != f->arg_end(); ++ait)
		result.push_back(&*ait);

	return result;
}

static void codegenFunctionExtern(Codegen& cg, const FunctionInstance& inst)
{
	assert(!inst.decl->body);

	Constant* external = cg.module->getOrInsertFunction(inst.decl->var->name.str(), inst.value->getFunctionType());

	vector<Value*> args = getFunctionArguments(inst.value);

	BasicBlock* bb = BasicBlock::Create(*cg.context, "entry", inst.value);
	cg.ir->SetInsertPoint(bb);

	Value* ret = cg.ir->CreateCall(external, args);

	if (ret->getType()->isVoidTy())
		cg.ir->CreateRetVoid();
	else
		cg.ir->CreateRet(ret);
}

static void codegenFunctionBuiltin(Codegen& cg, const FunctionInstance& inst)
{
	assert(!inst.decl->body);

	vector<Value*> args = getFunctionArguments(inst.value);

	BasicBlock* bb = BasicBlock::Create(*cg.context, "entry", inst.value);
	cg.ir->SetInsertPoint(bb);

	Str name = inst.decl->var->name;

	if (name == "sizeof" && inst.generics.size() == 1 && args.size() == 0)
	{
		Type* type = codegenType(cg, inst.generics[0].second);
		Value* ret = cg.ir->CreateIntCast(ConstantExpr::getSizeOf(type), cg.ir->getInt32Ty(), false);

		cg.ir->CreateRet(ret);
	}
	else if (name == "newarr" && inst.generics.size() == 1 && args.size() == 1)
	{
		Type* type = codegenType(cg, UNION_NEW(Ty, Array, { inst.generics[0].second }));

		Value* count = args[0];

		Value* ret = UndefValue::get(type);

		ret = cg.ir->CreateInsertValue(ret, codegenNewArr(cg, inst.generics[0].second, count), 0);
		ret = cg.ir->CreateInsertValue(ret, count, 1);

		cg.ir->CreateRet(ret);
	}
	else if (name == "length" && inst.generics.size() == 1 && args.size() == 1)
	{
		Value* array = args[0];
		Value* ret = cg.ir->CreateExtractValue(array, 1);

		cg.ir->CreateRet(ret);
	}
	else if (name == "assert" && args.size() == 1)
	{
		Value* expr = args[0];

		codegenTrapIf(cg, cg.ir->CreateNot(expr), /* debug= */ true);

		cg.ir->CreateRetVoid();
	}
	else
		cg.output->panic(inst.decl->var->location, "Unknown builtin function %s", name.str().c_str());
}

static string llvmGetTypeName(Type* type)
{
	string buffer;
	raw_string_ostream ss(buffer);
	ss << *type;
	return ss.str();
}

static pair<string, string> splitDeclarationsFromAssembly(const string& code)
{
	stringstream ss(code);

	string decl;
	string ir;

	string line;
	while (getline(ss, line))
	{
		string::size_type ns = line.find_first_not_of(' ');

		if (line.compare(ns == string::npos ? 0 : ns, 8, "declare ") == 0)
			decl += line + "\n";
		else
			ir += line + "\n";
	}

	return make_pair(decl, ir);
}

static void codegenFunctionLLVM(Codegen& cg, const FunctionInstance& inst)
{
	assert(inst.decl->body);

	UNION_CASE(LLVM, ll, inst.decl->body);
	assert(ll);

	FunctionType* funty = inst.value->getFunctionType();
	string name = "llvm_" + inst.value->getName().str();

	auto p = splitDeclarationsFromAssembly(ll->code.str());

	string code;

	code += p.first;

	code += "define internal ";
	code += llvmGetTypeName(funty->getReturnType());
	code += " @";
	code += name;
	code += "(";

	for (size_t i = 0; i < funty->getNumParams(); ++i)
	{
		if (i != 0)
			code += ", ";
		code += llvmGetTypeName(funty->getParamType(i));
	}

	code += ") {\n";
	code += "entry:\n";

	size_t prefixLength = code.length();

	code += p.second;
	code += "\n}\n";

	auto membuffer = MemoryBuffer::getMemBuffer(code);

	SMDiagnostic err;
	if (parseAssemblyInto(membuffer->getMemBufferRef(), *cg.module, err))
	{
		Location location = ll->location;

		size_t offset = err.getLoc().getPointer() - membuffer->getBufferStart();

		if (offset >= prefixLength)
		{
			location.offset += 1; // skip quote
			location.offset += offset - prefixLength;
			location.length -= offset - prefixLength;

			assert(err.getLineNo() >= 2);
			location.line += err.getLineNo() - 2;

			if (err.getLineNo() > 2)
				location.column = err.getColumnNo() - 1;
		}

		cg.output->panic(location, "Error parsing LLVM: %s", err.getMessage().str().c_str());
	}

	Function* fun = cast<Function>(cg.module->getFunction(name));

	vector<BasicBlock*> bbs;
	for (auto& bb: fun->getBasicBlockList())
		bbs.push_back(&bb);

	for (auto* bb: bbs)
	{
		bb->removeFromParent();
		bb->insertInto(inst.value);
	}

	auto valit = inst.value->arg_begin();

	for (auto it = fun->arg_begin(); it != fun->arg_end(); ++it)
	{
		it->replaceAllUsesWith(&*valit);
		valit++;
	}

	fun->eraseFromParent();
}

static void codegenFunctionBody(Codegen& cg, const FunctionInstance& inst)
{
	assert(inst.decl->body);

	vector<Value*> args = getFunctionArguments(inst.value);

	for (size_t i = 0; i < inst.decl->args.size; ++i)
		codegenVariable(cg, inst.decl->args[i], args[i]);

	BasicBlock* bb = BasicBlock::Create(*cg.context, "entry", inst.value);
	cg.ir->SetInsertPoint(bb);

	if (cg.di && cg.options.debugInfo >= 2)
	{
		DIFile* file = cg.di->createFile(inst.decl->var->location.source, StringRef());

		for (size_t i = 0; i < inst.decl->args.size; ++i)
		{
			Variable* var = inst.decl->args[i];
			Value* storage = args[i];

			DIType* dty = codegenTypeDebug(cg, var->type);
			DILocalVariable* dvar = cg.di->createParameterVariable(
				cg.debugBlocks.back(), var->name.str(), i + 1, file, var->location.line + 1, dty,
				/* alwaysPreserve= */ false, /* flags= */ 0);

			DebugLoc dloc = DebugLoc::get(var->location.line + 1, var->location.column + 1, cg.debugBlocks.back());

			cg.di->insertDeclare(storage, dvar, cg.di->createExpression(), dloc, cg.ir->GetInsertBlock());
		}
	}

	Value* ret = codegenExpr(cg, inst.decl->body);

	// Reset debug location for ret instruction.
	// This is not ideal since for simple returns (i.e. a function that returns a constant),
	// there will be no meaningful location. Unfortunately, the location for the last statement
	// is hard to get manually, and is not part of IR state because of CodegenDebugLocation dtor.
	cg.ir->SetCurrentDebugLocation(DebugLoc());

	if (ret->getType()->isVoidTy())
		cg.ir->CreateRetVoid();
	else
		cg.ir->CreateRet(ret);
}

static void codegenFunctionImpl(Codegen& cg, const FunctionInstance& inst)
{
	CodegenDebugLocation dbg(cg, inst.decl->var->location);

	if (inst.decl->attributes & FnAttributeExtern)
		codegenFunctionExtern(cg, inst);
	else if (inst.decl->attributes & FnAttributeBuiltin)
		codegenFunctionBuiltin(cg, inst);
	else if (inst.decl->body && inst.decl->body->kind == Ast::KindLLVM)
		codegenFunctionLLVM(cg, inst);
	else
		codegenFunctionBody(cg, inst);

	if (inst.decl->attributes & FnAttributeInline)
		inst.value->addFnAttr(Attribute::AlwaysInline);
}

static void codegenFunction(Codegen& cg, const FunctionInstance& inst)
{
	assert(inst.value->empty());

	if (cg.di)
	{
		const Location& loc = inst.decl->var->location;

		DIFile* file = cg.di->createFile(loc.source, StringRef());

		DISubroutineType* fty =
			(cg.options.debugInfo >= 2 && inst.decl->var->type)
			? cast<DISubroutineType>(codegenTypeDebug(cg, inst.decl->var->type))
			: cg.di->createSubroutineType(cg.di->getOrCreateTypeArray({}));

		DISubprogram* func = cg.di->createFunction(
			cg.debugBlocks.back(), inst.value->getName(), inst.value->getName(), file, loc.line + 1, fty,
			/* isLocalToUnit= */ false, /* isDefinition= */ true, loc.line + 1);

		inst.value->setSubprogram(func);

		cg.debugBlocks.push_back(func);

		codegenFunctionImpl(cg, inst);

		cg.debugBlocks.pop_back();
	}
	else
	{
		codegenFunctionImpl(cg, inst);
	}
}

static void codegenPrepare(Codegen& cg)
{
	cg.builtinTrap = Intrinsic::getDeclaration(cg.module, Intrinsic::trap);
	cg.builtinDebugTrap = Intrinsic::getDeclaration(cg.module, Intrinsic::debugtrap);

	cg.runtimeNew = cg.module->getOrInsertFunction("gcNew", Type::getInt8PtrTy(*cg.context), Type::getInt8PtrTy(*cg.context), Type::getInt32Ty(*cg.context), nullptr);
	cg.runtimeNewArray = cg.module->getOrInsertFunction("gcNewArray", Type::getInt8PtrTy(*cg.context), Type::getInt8PtrTy(*cg.context), Type::getInt32Ty(*cg.context), Type::getInt32Ty(*cg.context), nullptr);
}

llvm::Value* codegen(Output& output, Ast* root, llvm::Module* module, const CodegenOptions& options)
{
	llvm::LLVMContext* context = &module->getContext();

	IRBuilder<> ir(*context);
	DIBuilder di(*module);

	Codegen cg = { &output, options, context, module, &ir, options.debugInfo ? &di : nullptr };

	codegenPrepare(cg);

	UNION_CASE(Module, rootModule, root);
	assert(rootModule);

	Location entryLocation = rootModule->location;

	if (cg.di)
	{
		DIBuilder::DebugEmissionKind kind = (options.debugInfo > 1) ? DIBuilder::FullDebug : DIBuilder::LineTablesOnly;

		// It's necessary to use "." as the directory instead of an empty string for debug info to work on OSX
		DICompileUnit* cu = cg.di->createCompileUnit(dwarf::DW_LANG_C,
			entryLocation.source, ".", "aikec", /* isOptimized= */ false, StringRef(), 0, StringRef(), kind);
		cg.debugBlocks.push_back(cu);
	}

	string entryName = rootModule->name.str() + ".entry";

	FunctionType* entryType = FunctionType::get(Type::getVoidTy(*cg.context), false);
	Function* entry = Function::Create(entryType, GlobalValue::InternalLinkage, entryName, module);

	Variable* entryVar = new Variable { Variable::KindFunction, Str(entryName.c_str()), nullptr, entryLocation, nullptr };
	Ast::FnDecl* entryDecl = new Ast::FnDecl { nullptr, Location(), entryVar, Arr<Ty*>(), Arr<Variable*>(), 0, root };

	cg.pendingFunctions.push_back(new FunctionInstance { entry, entryDecl });

	while (!cg.pendingFunctions.empty())
	{
		FunctionInstance* inst = cg.pendingFunctions.back();

		cg.pendingFunctions.pop_back();

		cg.currentFunction = inst;

		codegenFunction(cg, *inst);

		cg.currentFunction = nullptr;
	}

	if (cg.di)
		di.finalize();

	return entry;
}

void codegenMain(llvm::Module* module, const vector<llvm::Value*>& entries)
{
	LLVMContext& context = module->getContext();
	IRBuilder<> ir(context);

	Function* main = cast<Function>(module->getOrInsertFunction("aikeMain", Type::getVoidTy(context), nullptr));

	BasicBlock* mainbb = BasicBlock::Create(context, "entry", main);
	ir.SetInsertPoint(mainbb);

	for (auto& e: entries)
		ir.CreateCall(e);

	ir.CreateRetVoid();

	Function* entry = cast<Function>(module->getOrInsertFunction("main", Type::getInt32Ty(context), nullptr));

	BasicBlock* entrybb = BasicBlock::Create(context, "entry", entry);
	ir.SetInsertPoint(entrybb);

	Constant* runtimeEntry = module->getOrInsertFunction("aikeEntry", Type::getInt32Ty(context), main->getType(), nullptr);

	Value* ret = ir.CreateCall(runtimeEntry, main);

	ir.CreateRet(ret);
}