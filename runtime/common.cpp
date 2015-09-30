#include "common.hpp"

#include <errno.h>

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

void panicError(const char* file, int line, const char* expr)
{
	panic("%s failed (errno %d) at %s:%d", expr, errno, file, line);
}