#pragma once

#include <cstdint>
#include <cassert>
#include <cstring>

#include <vector>
#include <string>
#include <memory>
#include <algorithm>

#include <unordered_map>

using namespace std;

#ifdef __GNUC__
#define ATTR_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define ATTR_PRINTF(fmt, args)
#endif

ATTR_PRINTF(1, 2) void panic(const char* format, ...);