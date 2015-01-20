#include "common.hpp"

#include "signal.hpp"

AIKE_EXTERN void init()
{
	installSignalHandler();
}