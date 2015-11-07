#pragma once

struct Str
{
	const char* data;
	size_t size;

	Str(): data(0), size(0)
	{
	}

	explicit Str(const char* string): data(string), size(strlen(string))
	{
	}

	Str(const char* data, size_t size): data(data), size(size)
	{
	}

	bool operator==(const Str& other) const
	{
		return size == other.size && (size == 0 || memcmp(data, other.data, size) == 0);
	}

	bool operator!=(const Str& other) const
	{
		return !(*this == other);
	}

	bool operator==(const char* other) const
	{
		return (size == 0 || strncmp(other, data, size) == 0) && other[size] == 0;
	}

	bool operator!=(const char* other) const
	{
		return !(*this == other);
	}

	char operator[](size_t i) const
	{
		assert(i < size);
		return data[i];
	}

	string str() const
	{
		return string(data, size);
	}

	static Str copy(const char* string)
	{
		size_t length = strlen(string);
		char* data = new char[length];
		memcpy(data, string, length);
		return Str(data, length);
	}
};

namespace std
{
	template<> struct hash<Str>
	{
		size_t operator()(const Str& str) const
		{
			size_t seed = 0;

			for (size_t i = 0; i < str.size; ++i)
				seed ^= str.data[i] + 0x9e3779b9 + (seed << 6) + (seed >> 2);

			return seed;
		}
	};
}
