#include "common.hpp"

#include <stdio.h>

AIKE_EXTERN void println(AikeString str)
{
	printf("%.*s\n", int(str.size), str.data);
}