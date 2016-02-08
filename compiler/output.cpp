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

static void strprintfv(string& result, const char* format, va_list args)
{
	char buf[4096];
	int c = vsnprintf(buf, sizeof(buf), format, args);
	assert(c >= 0);

	result += buf;
}

static void strprintf(string& result, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	strprintfv(result, format, args);
	va_end(args);
}

static string print(Output* output, Location loc, const char* format, va_list args)
{
	string result;

	strprintf(result, "%s(%d,%d): ", loc.source, loc.line + 1, loc.column + 1);
	strprintfv(result, format, args);
	result.append("\n");

	auto it = output->sources.find(loc.source);

	if (it != output->sources.end())
	{
		Str contents = it->second;

		assert(loc.offset + loc.length <= contents.size);

		auto line = findLine(contents, loc.offset);

		result.append("\n\t");
		result.append(contents.data + line.first, line.second - line.first);

		result.append("\n\t");
		result.append(loc.offset - line.first, ' ');

		result.append(max(size_t(1), min(loc.length, line.second - loc.offset)), '^');

		result.append("\n");
	}

	return result;
}

void Output::panic(Location loc, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	messages.push_back(print(this, loc, format, args));
	va_end(args);

	flush();
	exit(1);
}

void Output::error(Location loc, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	messages.push_back(print(this, loc, format, args));
	va_end(args);

	errors++;
}

void Output::warning(Location loc, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	messages.push_back(print(this, loc, format, args));
	va_end(args);

	warnings++;
}

void Output::flush()
{
	for (auto& m: messages)
		fputs(m.c_str(), stderr);

	messages.clear();
}