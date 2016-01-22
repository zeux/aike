#include "common.hpp"

#include "signal.hpp"
#include "scheduler.hpp"
#include "gc.hpp"

AIKE_EXTERN int aikeEntry(void (*main)())
{
	signalSetup();

	gcInit();

	spawn(main);

	schedulerRun();

	signalTeardown();

	return 0;
}