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

AIKE_EXTERN AikeArray<void> aike_concat(AikeArray<void> lhs, AikeArray<void> rhs, int elementSize)
{
	void* result = aike_newarr(lhs.size + rhs.size, elementSize);

	memcpy(result, lhs.data, lhs.size * elementSize);
	memcpy(static_cast<char*>(result) + lhs.size * elementSize, rhs.data, rhs.size * elementSize);

	return { result, lhs.size + rhs.size };
}