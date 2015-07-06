#include "common.hpp"

#include "signal.hpp"
#include "scheduler.hpp"

AIKE_EXTERN int aikeEntry(void (*main)())
{
	installSignalHandler();

	spawn(main);

	schedulerRun();

	return 0;
}