#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#if defined(__APPLE__)
	#define AIKE_UNIX
	#define AIKE_MAC
#elif defined(__linux__)
	#define AIKE_UNIX
	#define AIKE_LINUX
#elif defined(_WIN32)
	#define AIKE_WINDOWS
#else
	#error Unknown platform
#endif

#ifdef AIKE_WINDOWS
	#define AIKE_EXTERN extern "C" __declspec(dllexport)
#else
	#define AIKE_EXTERN extern "C" __attribute__ ((visibility("default")))
#endif

struct AikeString
{
	const char* data;
	size_t size;
};

template <typename T> struct AikeArray
{
	T* data;
	size_t size;
};