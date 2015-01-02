#include "common.hpp"

#include <stdio.h>
#include <stdlib.h>

void panic(const char* format, ...)
{
	char error[1024];

	va_list args;
	va_start(args, format);
	vsnprintf(error, sizeof(error), format, args);
	va_end(args);

	error[sizeof(error) - 1] = 0;

	fprintf(stderr, "%s\n", error);
	abort();
}