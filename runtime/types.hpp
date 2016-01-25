#pragma once

union TypeInfo;

struct AikeString
{
	const char* data;
	int size;
};

template <typename T> struct AikeArray
{
	T* data;
	int size;
};

struct AikeAny
{
	TypeInfo* type;
	void* value;
};