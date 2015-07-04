#include "common.hpp"
#include "scheduler.hpp"

#include "context.hpp"

#include <sys/mman.h>

struct Coro
{
	Context context;

	void (*fn)();

	Coro* prev;
	Coro* next;
};

static Coro* readyHead;
static Coro* readyTail;

static Coro* current;

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
	current->fn();
	current = 0;

	coroReturn();
}

void spawn(void (*fn)())
{
	Coro* coro = new Coro();

	coro->fn = fn;

	size_t sz = 64*1024;
	void* stack = mmap(0, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

	contextCreate(&coro->context, coroEntry, stack, sz);

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

	coroDispatch();
}