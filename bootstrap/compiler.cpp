#include "compiler.hpp"

#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/IRBuilder.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"

using namespace llvm;

void compile(LLVMContext& context, Module* M, AstBase* root)
{
	// Create the add1 function entry and insert this entry into module M.  The
	// function will have a return type of "int" and take an argument of "int".
	// The '0' terminates the list of argument types.
	Function *Add1F =
		cast<Function>(M->getOrInsertFunction("add1", Type::getInt32Ty(context),
		Type::getInt32Ty(context),
		(Type *)0));

	// Add a basic block to the function. As before, it automatically inserts
	// because of the last argument.
	BasicBlock *BB = BasicBlock::Create(context, "EntryBlock", Add1F);

	// Create a basic block builder with default parameters.  The builder will
	// automatically append instructions to the basic block `BB'.
	IRBuilder<> builder(BB);

	// Get pointers to the constant `1'.
	Value *One = builder.getInt32(1);

	// Get pointers to the integer argument of the add1 function...
	assert(Add1F->arg_begin() != Add1F->arg_end()); // Make sure there's an arg
	Argument *ArgX = Add1F->arg_begin();  // Get the arg
	ArgX->setName("AnArg");            // Give it a nice symbolic name for fun.

	// Create the add instruction, inserting it into the end of BB.
	Value *Add = builder.CreateAdd(One, ArgX);

	// Create the return instruction and add it to the basic block
	builder.CreateRet(Add);

	// Now, function add1 is ready.


	// Now we're going to create function `foo', which returns an int and takes no
	// arguments.
	Function *FooF =
		cast<Function>(M->getOrInsertFunction("entrypoint", Type::getInt32Ty(context),
		(Type *)0));

	// Add a basic block to the FooF function.
	BB = BasicBlock::Create(context, "EntryBlock", FooF);

	// Tell the basic block builder to attach itself to the new basic block
	builder.SetInsertPoint(BB);

	// Get pointer to the constant `10'.
	Value *Ten = builder.getInt32(10);

	// Pass Ten to the call to Add1F
	CallInst *Add1CallRes = builder.CreateCall(Add1F, Ten);
	Add1CallRes->setTailCall(true);

	// Create the return instruction and add it to the basic block.
	builder.CreateRet(Add1CallRes);

	// Now we create the JIT.
}