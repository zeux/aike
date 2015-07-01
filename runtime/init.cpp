#include "common.hpp"

#include "signal.hpp"
#include "scheduler.hpp"

AIKE_EXTERN int aike_entry(void (*main)())
{
	installSignalHandler();

	spawn(main);

	schedulerRun();

	return 0;
}