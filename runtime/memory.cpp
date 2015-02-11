#include "common.hpp"
#include "memory.hpp"

void* aike_new(size_t size)
{
	return malloc(size);
}