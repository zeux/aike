#include "common.hpp"

#include "signal.hpp"
#include "scheduler.hpp"

AIKE_EXTERN int aikeEntry(void (*main)())
{
	signalSetup();

	spawn(main);

	schedulerRun();

	signalTeardown();

	return 0;
}