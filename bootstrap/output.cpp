#include "output.hpp"

#include <cstdarg>

void errorf(const Location& location, const char* format, ...)
{
	va_list al;

	va_start(al, format);
	int count = _vscprintf(format, al);
	va_end(al);

	std::string error;
	error.resize(count);

	va_start(al, format);
	vsnprintf(&error[0], error.size(), format, al);
	va_end(al);

	throw ErrorAtLocation(location, error);
}
