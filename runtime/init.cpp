#include "common.hpp"

#include "signal.hpp"

AIKE_EXTERN int aike_main(int moduleCount, void (**moduleArray)())
{
	installSignalHandler();

	for (int module = 0; module < moduleCount; ++module)
		moduleArray[module]();

	return 0;
}