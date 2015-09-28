#include "common.hpp"
#include "signal.hpp"

#ifdef AIKE_OS_UNIX
#include "backtrace.hpp"
#include "scheduler.hpp"

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

void installSignalHandler()
{
	struct sigaction new_action;

	new_action.sa_sigaction = signalHandler;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = SA_SIGINFO;

	sigaction(SIGILL, &new_action, NULL);
	sigaction(SIGSEGV, &new_action, NULL);
	sigaction(SIGFPE, &new_action, NULL);
	sigaction(SIGTRAP, &new_action, NULL);
}
#endif
