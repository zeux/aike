#pragma once

struct Location
{
	const char* source;

	int line;
	int column;

	size_t offset;
	size_t length;

	Location(): source(""), line(0), column(0), offset(0), length(0)
	{
	}

	Location(const char* source, int line, int column, size_t offset, size_t length): source(source), line(line), column(column), offset(offset), length(length)
	{
	}

	Location(const Location& lhs, const Location& rhs)
	{
		assert(lhs.source == rhs.source);
		assert(lhs.offset + lhs.length <= rhs.offset);

		source = lhs.source;
		line = lhs.line;
		column = lhs.column;
		offset = lhs.offset;
		length = rhs.offset + rhs.length - lhs.offset;
	}
};