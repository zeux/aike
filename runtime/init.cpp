#include "common.hpp"

#include "signal.hpp"

AIKE_EXTERN void aike_init()
{
	installSignalHandler();
}