#include "common.hpp"
#include "gc.hpp"

#include <time.h>

extern "C"
{
	#include "../gjduckgc/gc.h"
}

struct GCHeader
{
	void* type;
};

struct GCHeaderArray
{
	size_t count;
	void* type;
};

void* gcAlloc(size_t size)
{
	void* result = GC_malloc(size);
	if (!result) panic("Out of memory while allocating %lld bytes", static_cast<long long>(size));

	return result;
}

void gcInit()
{
	GC_init();
	GC_disable();
}

AIKE_EXTERN void* gcNew(void* ti, size_t size)
{
	void* block = gcAlloc(sizeof(GCHeader) + size);

	*reinterpret_cast<GCHeader*>(block) = { ti };

	void* result = static_cast<GCHeader*>(block) + 1;

	memset(result, 0, size);

	return result;
}

AIKE_EXTERN void* gcNewArray(void* ti, size_t count, size_t elementSize)
{
	size_t size = count * elementSize;

	void* block = gcAlloc(sizeof(GCHeaderArray) + size);

	*reinterpret_cast<GCHeaderArray*>(block) = { count, ti };

	void* result = static_cast<GCHeaderArray*>(block) + 1;

	memset(result, 0, size);

	return result;
}

AIKE_EXTERN void gcCollect()
{
	GC_enable();
	GC_collect();
	GC_disable();
}