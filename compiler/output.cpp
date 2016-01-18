#include "common.hpp"
#include "output.hpp"

#include <stdarg.h>

static pair<size_t, size_t> findLine(const Str& data, size_t offset)
{
	size_t begin = offset;

	while (begin > 0 && data[begin - 1] != '\n')
		begin--;

	size_t end = offset;

	while (end < data.size && data[end] != '\r' && data[end] != '\n')
		end++;

	return make_pair(begin, end);
}

static void print(FILE* file, Output* output, Location loc, const char* format, va_list args)
{
	fprintf(file, "%s(%d,%d): ", loc.source, loc.line + 1, loc.column + 1);
	vfprintf(file, format, args);
	fputc('\n', file);

	auto it = output->sources.find(loc.source);

	if (it != output->sources.end())
	{
		Str contents = it->second;

		assert(loc.offset + loc.length <= contents.size);

		auto line = findLine(contents, loc.offset);

		fputc('\n', file);

		fputc('\t', file);

		for (size_t i = line.first; i < line.second; ++i)
			fputc(contents[i], file);

		fputc('\n', file);

		fputc('\t', file);

		for (size_t i = line.first; i < loc.offset; ++i)
			fputc(' ', file);

		if (loc.length == 0)
		{
			fputc('^', file);
		}
		else
		{
			for (size_t i = loc.offset; i < line.second && i < loc.offset + loc.length; ++i)
				fputc('^', file);
		}

		fputc('\n', file);
	}
}

void Output::panic(Location loc, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	print(stderr, this, loc, format, args);
	va_end(args);

	exit(1);
}

void Output::error(Location loc, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	print(stderr, this, loc, format, args);
	va_end(args);

	errors++;
}

void Output::warning(Location loc, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	print(stderr, this, loc, format, args);
	va_end(args);

	warnings++;
}