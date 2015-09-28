#pragma once

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