#pragma once

#include "string.hpp"
#include "location.hpp"

struct Output
{
	unordered_map<const char*, Str> sources;
	vector<string> messages;
	int errors = 0;
	int warnings = 0;

	ATTR_NORETURN ATTR_PRINTF(3, 4) void panic(Location loc, const char* format, ...);

	ATTR_PRINTF(3, 4) void error(Location loc, const char* format, ...);
	ATTR_PRINTF(3, 4) void warning(Location loc, const char* format, ...);

	ATTR_PRINTF(2, 3) void error(const char* format, ...);
	ATTR_PRINTF(2, 3) void warning(const char* format, ...);

	void flush();
};