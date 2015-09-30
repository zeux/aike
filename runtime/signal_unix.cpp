#include "common.hpp"
#include "signal.hpp"

#ifdef AIKE_OS_UNIX
#include "backtrace.hpp"
#include "scheduler.hpp"
#include "stack.hpp"

#include <sys/ucontext.h>
#include <signal.h>

static void signalHandler(int signum, siginfo_t* info, void* data)
{
	fprintf(stderr, "Signal caught: %s", sys_siglist[signum]);

	auto uc = static_cast<ucontext_t*>(data);
	auto mc = uc->uc_mcontext;

#if defined(AIKE_OS_MAC) && defined(AIKE_ABI_AMD64)
	fprintf(stderr, " at %016llx", mc->__ss.__rip);

	if (mc->__es.__err == 4 || mc->__es.__err == 6)
		fprintf(stderr, " (%s %016llx)", mc->__es.__err == 4 ? "load" : "store", mc->__es.__faultvaddr);
#endif

#if defined(AIKE_OS_LINUX) && defined(AIKE_ABI_AMD64)
	fprintf(stderr, " at %016lx", mc.gregs[REG_RIP]);
#endif

	fprintf(stderr, "\n");

	void* stack;
	size_t stackSize;
	if (schedulerGetStack(&stack, &stackSize))
	{
	#if defined(AIKE_OS_MAC) && defined(AIKE_ABI_AMD64)
		backtraceDump(stderr, stack, stackSize, mc->__ss.__rip, mc->__ss.__rbp);
	#endif

	#if defined(AIKE_OS_LINUX) && defined(AIKE_ABI_AMD64)
		backtraceDump(stderr, stack, stackSize, mc.gregs[REG_RIP], mc.gregs[REG_RBP]);
	#endif
	}

	abort();
}

const int kSignalActions[] = { SIGILL, SIGTRAP, SIGFPE, SIGBUS, SIGSEGV };

void signalSetup()
{
	stack_t stack = {};
	stack.ss_size = SIGSTKSZ;
	stack.ss_sp = stackCreate(stack.ss_size);

	check(sigaltstack(&stack, nullptr) == 0);

	struct sigaction action = {};
	action.sa_sigaction = signalHandler;
	action.sa_flags = SA_SIGINFO | SA_ONSTACK;

	for (int id: kSignalActions)
		check(sigaction(id, &action, nullptr) == 0);
}

void signalTeardown()
{
	for (int id: kSignalActions)
		check(sigaction(id, nullptr, nullptr) == 0);

	stack_t stack = {};
	stack.ss_size = MINSIGSTKSZ; // Work around an OSX bug: https://code.google.com/p/nativeclient/issues/detail?id=1053#c1
	stack.ss_flags = SS_DISABLE;

	stack_t oldStack = {};

	check(sigaltstack(&stack, &oldStack) == 0);

	stackDestroy(oldStack.ss_sp, oldStack.ss_size);
}
#endif
