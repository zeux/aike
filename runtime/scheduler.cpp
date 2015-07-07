#include "common.hpp"
#include "scheduler.hpp"

#include "context.hpp"

#include <sys/mman.h>

struct Coro
{
	Context context;

	void (*fn)();

	void* stack;
	size_t stackSize;

	Coro* prev;
	Coro* next;
};

static Coro* readyHead;
static Coro* readyTail;

static Coro* current;
static Coro* cleanup;

static Context worker;

static void coroQueue(Coro* coro)
{
	coro->next = 0;

	if (readyTail)
	{
		coro->prev = readyTail;
		readyTail->next = coro;
		readyTail = coro;
	}
	else
	{
		assert(readyHead == 0);
		coro->prev = 0;
		readyHead = readyTail = coro;
	}
}

static void coroDispatch()
{
	if (readyHead)
	{
		Coro* c = readyHead;
		assert(!c->prev);

		if (c->next)
		{
			c->next->prev = 0;
			readyHead = c->next;
			c->next = 0;
		}
		else
		{
			assert(readyTail == c);
			readyHead = readyTail = 0;
		}

		assert(!current);
		current = c;

		contextResume(&c->context);
	}
}

static void coroReturn()
{
	contextResume(&worker);
}

static void coroEntry()
{
	Coro* coro = current;

	current->fn();
	current = 0;

	assert(!cleanup);
	cleanup = coro;

	coroReturn();
}

void spawn(void (*fn)())
{
	Coro* coro = static_cast<Coro*>(malloc(sizeof(Coro)));

	coro->fn = fn;

	coro->stackSize = 64*1024;
	coro->stack = mmap(0, coro->stackSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

	contextCreate(&coro->context, coroEntry, coro->stack, coro->stackSize);

	coroQueue(coro);
}

void yield()
{
	assert(current);

	coroQueue(current);

	if (contextCapture(&current->context))
	{
		current = 0;

		coroReturn();
	}
}

void schedulerRun()
{
	contextCapture(&worker);

	if (cleanup)
	{
		munmap(cleanup->stack, cleanup->stackSize);
		free(cleanup);
		cleanup = 0;
	}

	coroDispatch();
}