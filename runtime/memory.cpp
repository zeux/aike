#include "common.hpp"
#include "memory.hpp"

void* aikeNew(size_t size)
{
	// TODO: panic on null result
	return calloc(1, size);
}

void* aikeNewArray(size_t count, size_t size)
{
	// TODO: panic on overflow
	return calloc(count, size);
}