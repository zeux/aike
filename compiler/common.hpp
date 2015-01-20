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

#define UNION_CASE(kindname, var, value) \
	auto var = ((value)->kind == (value)->Kind##kindname) \
		? &(value)->data##kindname \
		: nullptr

#define UNION_MAKE(type, kindname, ...) \
		([&]() -> type { \
			type __result = { type::Kind##kindname, 0 }; \
			__result.data##kindname = { __VA_ARGS__ }; \
			return __result; \
		})()

#define UNION_NEW(type, kindname, ...) \
		([&]() -> type* { \
			type* __result = new type { type::Kind##kindname, 0 }; \
			__result->data##kindname = { __VA_ARGS__ }; \
			return __result; \
		})()