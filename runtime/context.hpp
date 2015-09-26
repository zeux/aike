#pragma once

#if defined(AIKE_ABI_AMD64)
struct Context
{
	uint64_t rbx;
	uint64_t rbp;
	uint64_t rsp;
	uint64_t r12;
	uint64_t r13;
	uint64_t r14;
	uint64_t r15;
	uint64_t rip;
};
#else
#error Unknown ABI
#endif

extern "C" bool contextCapture(Context* context) asm("contextCapture");
extern "C" void contextResume(Context* context) asm("contextResume");
extern "C" void contextCreate(Context* context, void (*entry)(), void* stack, size_t size) asm("contextCreate");