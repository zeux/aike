#include "common.hpp"

#include "backtrace.hpp"

#include <sys/ucontext.h>
#include <signal.h>

static void signalHandler(int signum, siginfo_t* info, void* data)
{
	fprintf(stderr, "Signal caught: %s", sys_siglist[signum]);

	auto uc = static_cast<ucontext_t*>(data);
	auto mc = uc->uc_mcontext;

	fprintf(stderr, " at %016llx", mc->__ss.__rip);

	if (mc->__es.__err == 4 || mc->__es.__err == 6)
		fprintf(stderr, " (%s %016llx)", mc->__es.__err == 4 ? "load" : "store", mc->__es.__faultvaddr);

	fprintf(stderr, "\n");

	dumpBacktrace(stderr);
	abort();
}

AIKE_EXTERN void init()
{
	struct sigaction new_action;

	new_action.sa_sigaction = signalHandler;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = SA_SIGINFO;

	sigaction(SIGSEGV, &new_action, NULL);
}