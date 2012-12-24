#pragma once

struct Location
{
	const char *lineStart;

	size_t line;
	size_t column;

	size_t length;

	Location(): lineStart(0), line(0), column(0), length(0)
	{
	}

	Location(const char* lineData, size_t line, size_t column, size_t length): lineStart(lineData), line(line), column(column), length(length)
	{
	}
};
