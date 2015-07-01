#include "common.hpp"

#include "signal.hpp"
#include "scheduler.hpp"

AIKE_EXTERN int aike_main(int moduleCount, void (**moduleArray)())
{
	installSignalHandler();

	for (int module = 0; module < moduleCount; ++module)
		spawn(moduleArray[module]);

	schedulerRun();

	return 0;
}