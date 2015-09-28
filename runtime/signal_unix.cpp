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

const int kSignalActions[] = { SIGILL, SIGSEGV, SIGFPE, SIGTRAP };

void signalSetup()
{
	struct sigaction action;

	action.sa_sigaction = signalHandler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = SA_SIGINFO;

	for (int id: kSignalActions)
		sigaction(id, &action, NULL);
}

void signalTeardown()
{
	for (int id: kSignalActions)
		sigaction(id, NULL, NULL);
}
#endif
