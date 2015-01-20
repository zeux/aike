#pragma once

#include "string.hpp"
#include "location.hpp"

struct Output
{
	unordered_map<const char*, Str> sources;

	ATTR_NORETURN ATTR_PRINTF(3, 4) void panic(Location loc, const char* format, ...);
};