#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#ifndef _WIN32
	#define AIKE_EXTERN extern "C" __attribute__ ((visibility("default")))
#else
	#define AIKE_EXTERN extern "C" __declspec(dllexport)
#endif

struct AikeString
{
	const char* data;
	size_t size;
};