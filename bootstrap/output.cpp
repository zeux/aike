#include "output.hpp"

#include <cstdarg>

void errorf(const Location& location, const char* format, ...)
{
	va_list al;

	va_start(al, format);
#ifdef _MSC_VER
	int count = _vscprintf(format, al);
#else
	int count = vsnprintf(NULL, 0, format, al);
#endif
	va_end(al);

	std::string error;
	error.resize(count);

	va_start(al, format);
	vsnprintf(&error[0], error.size(), format, al);
	va_end(al);

	throw ErrorAtLocation(location, error);
}
