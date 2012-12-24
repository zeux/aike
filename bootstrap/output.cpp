#include "output.hpp"

#include <cstdarg>
#include <stdexcept>

#include "location.hpp"

void errorf(const Location& location, const char* format, ...)
{
	fprintf(stderr, "ERROR: ");

	va_list al;
	va_start(al, format);
	vfprintf(stderr, format, al);
	va_end(al);

	fprintf(stderr, "\n");

	if (location.lineStart)
	{
		const char *lineEnd = strchr(location.lineStart, '\n');

		fprintf(stderr, "\n");

		fprintf(stderr, "at line %d, column %d\n", location.line + 1, location.column + 1);

		if (lineEnd)
			fprintf(stderr, "%.*s\n", lineEnd - location.lineStart, location.lineStart);
		else
			fprintf(stderr, "%s\n", location.lineStart);

		for (size_t i = 0; i < location.column; i++)
			fprintf(stderr, " ");
		for (size_t i = 0; i < location.length; i++)
			fprintf(stderr, "^");
		fprintf(stderr, "\n");
	}

	throw std::runtime_error("Error");
}
