#pragma once

#include <cstdint>
#include <cassert>
#include <cstring>

#include <array>
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
#define ATTR_NORETURN __attribute__((noreturn))
#else
#define ATTR_PRINTF(fmt, args)
#define ATTR_NORETURN
#endif

ATTR_NORETURN ATTR_PRINTF(1, 2) void panic(const char* format, ...);

#define ICE_STRINGIFY_IMPL(x) #x
#define ICE_STRINGIFY(x) ICE_STRINGIFY_IMPL(x)
#define ICE(...) panic(__FILE__ "(" ICE_STRINGIFY(__LINE__) "): Internal compiler error: " __VA_ARGS__)

#include "string.hpp"
#include "array.hpp"
#include "union.hpp"