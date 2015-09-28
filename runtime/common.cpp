#include "common.hpp"

void panic(const char* format, ...)
{
	fprintf(stderr, "PANIC: ");

	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);

	fprintf(stderr, "\n");
	abort();
}