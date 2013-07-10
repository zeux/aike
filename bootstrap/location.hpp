#pragma once

#include <stddef.h>

struct SourceFile
{
	const char* path;
	const char* contents;
	size_t length;

	SourceFile(): path(0), contents(0), length(0)
	{
	}

	SourceFile(const char* path, const char* contents, size_t length): path(path), contents(contents), length(length)
	{
	}
};

struct Location
{
	SourceFile file;

	size_t lineOffset;

	size_t line;
	size_t column;

	size_t length;

	Location(): lineOffset(0), line(0), column(0), length(0)
	{
	}

	Location(const SourceFile& file, size_t lineOffset, size_t line, size_t column, size_t length): file(file), lineOffset(lineOffset), line(line), column(column), length(length)
	{
	}
};
