#include "common.hpp"

#include <stdio.h>
#include <stdlib.h>

void panic(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);

	fputc('\n', stderr);
	abort();
}