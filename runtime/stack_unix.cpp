#include "common.hpp"
#include "stack.hpp"

#ifdef AIKE_OS_UNIX
#include <sys/mman.h>

void* stackCreate(size_t stackSize)
{
	return mmap(0, stackSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
}

void stackDestroy(void* stack, size_t stackSize)
{
	munmap(stack, stackSize);
}
#endif