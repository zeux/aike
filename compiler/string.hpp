#pragma once

struct StringPiece
{
	const char* data;
	size_t size;

	StringPiece(): data(0), size(0)
	{
	}

	explicit StringPiece(const char* string): data(string), size(strlen(string))
	{
	}

	explicit StringPiece(const char* data, size_t size): data(data), size(size)
	{
	}

	bool operator==(const StringPiece& other) const
	{
		return size == other.size && (size == 0 || memcmp(data, other.data, size) == 0);
	}

	bool operator!=(const StringPiece& other) const
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

	string toString() const
	{
		return string(data, size);
	}
};