#include "common.hpp"

#include "types.hpp"

AIKE_EXTERN void println(AikeString str)
{
	printf("%.*s\n", int(str.size), str.data);
}

AIKE_EXTERN void printfn(AikeString format, ...)
{
	char* buffer = static_cast<char*>(alloca(format.size + 2));

	memcpy(buffer, format.data, format.size);
	buffer[format.size + 0] = '\n';
	buffer[format.size + 1] = 0;

	va_list args;
	va_start(args, format);
	vprintf(buffer, args);
	va_end(args);
}