#pragma once

#include <cstdint>
#include <cassert>
#include <cstring>

#include <vector>
#include <string>
#include <unordered_map>

#include <memory>
#include <utility>

#include <algorithm>
#include <functional>

using namespace std;

#ifdef __GNUC__
#define ATTR_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define ATTR_PRINTF(fmt, args)
#endif

ATTR_PRINTF(1, 2) void panic(const char* format, ...);

#include "string.hpp"
#include "array.hpp"
#include "union.hpp"