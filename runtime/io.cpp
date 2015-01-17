#include "common.hpp"

AIKE_EXTERN void println(AikeString str)
{
	printf("%.*s\n", int(str.size), str.data);
}