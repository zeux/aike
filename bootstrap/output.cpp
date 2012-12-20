#include "output.hpp"

#include <cstdarg>
#include <stdexcept>

void errorf(const char* format, ...)
{
	fprintf(stderr, "ERROR: ");

	va_list al;
	va_start(al, format);
	vfprintf(stderr, format, al);
	va_end(al);

	fprintf(stderr, "\n");

	throw std::runtime_error("Error");
}