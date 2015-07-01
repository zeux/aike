// Need to define this before all other includes to get correct definition of ucontext_t
#define _XOPEN_SOURCE

#include "common.hpp"
#include "scheduler.hpp"

#include <ucontext.h>
#include <sys/mman.h>

struct Coro
{
	ucontext_t uc;

	void (*fn)();

	Coro* prev;
	Coro* next;
};

static Coro* readyHead;
static Coro* readyTail;

static Coro* current;

static ucontext_t worker;

static void queueCoro(Coro* coro)
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

void spawn(void (*fn)())
{
	Coro* coro = new Coro();

	getcontext(&coro->uc);

	size_t sz = 64*1024;

	coro->uc.uc_stack.ss_sp = mmap(0, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	coro->uc.uc_stack.ss_size = sz;
	coro->uc.uc_stack.ss_flags = 0;

	coro->uc.uc_link = &worker;

	makecontext(&coro->uc, fn, 0);

	queueCoro(coro);
}

void yield()
{
	assert(current);

	queueCoro(current);

	swapcontext(&current->uc, &worker);
}

void schedulerRun()
{
	getcontext(&worker);

	while (readyHead)
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

		swapcontext(&worker, &c->uc);

		current = 0;
	}
}