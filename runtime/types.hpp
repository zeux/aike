#pragma once

union TypeInfo;

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

struct AikeAny
{
	TypeInfo* type;
	void* value;
};