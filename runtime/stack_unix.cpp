#include "common.hpp"
#include "stack.hpp"

#ifdef AIKE_OS_UNIX
#include <sys/mman.h>

static const size_t kPageSize = 4096;

void* stackCreate(size_t stackSize)
{
	stackSize = (stackSize + kPageSize - 1) & ~(kPageSize - 1);

	void* ret = mmap(0, stackSize + kPageSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (!ret) return ret;

	check(mprotect(ret, kPageSize, PROT_NONE) == 0);

	return static_cast<char*>(ret) + kPageSize;
}

void stackDestroy(void* stack, size_t stackSize)
{
	assert(stack);

	stackSize = (stackSize + kPageSize - 1) & ~(kPageSize - 1);

	check(munmap(static_cast<char*>(stack) - kPageSize, stackSize + kPageSize) == 0);
}
#endif