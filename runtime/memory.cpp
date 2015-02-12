#include "common.hpp"
#include "memory.hpp"

void* aike_new(size_t size)
{
	// TODO: panic on null result
	return malloc(size);
}

void* aike_newarr(size_t count, size_t size)
{
	// TODO: panic on overflow
	return aike_new(count * size);
}