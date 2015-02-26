#include "common.hpp"
#include "memory.hpp"

void* aike_new(size_t size)
{
	// TODO: panic on null result
	return calloc(1, size);
}

void* aike_newarr(size_t count, size_t size)
{
	// TODO: panic on overflow
	return calloc(count, size);
}