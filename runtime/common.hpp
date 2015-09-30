#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cassert>

#if defined(__APPLE__)
	#define AIKE_OS_UNIX
	#define AIKE_OS_MAC
#elif defined(__linux__)
	#define AIKE_OS_UNIX
	#define AIKE_OS_LINUX
#elif defined(_WIN32)
	#define AIKE_OS_WINDOWS
#else
	#error Unknown platform
#endif

#if defined(__x86_64__)
	#define AIKE_ABI_AMD64
#else
	#error Unknown architecture
#endif

#ifdef AIKE_OS_WINDOWS
	#define AIKE_EXTERN extern "C" __declspec(dllexport)
#else
	#define AIKE_EXTERN extern "C" __attribute__ ((visibility("default")))
#endif

#ifdef __GNUC__
#define ATTR_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#define ATTR_NORETURN __attribute__((noreturn))
#else
#define ATTR_PRINTF(fmt, args)
#define ATTR_NORETURN
#endif

ATTR_NORETURN ATTR_PRINTF(1, 2) void panic(const char* format, ...);
ATTR_NORETURN void panicError(const char* file, int line, const char* expr);

#define check(x) ((x) ? (void)0 : panicError(__FILE__, __LINE__, #x))