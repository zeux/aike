#pragma once

#if defined(AIKE_UNIX)
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

AIKE_EXTERN bool contextCapture(Context* context);
AIKE_EXTERN void contextResume(Context* context);
AIKE_EXTERN void contextCreate(Context* context, void (*entry)(), void* stack, size_t size);