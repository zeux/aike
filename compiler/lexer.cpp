#include "common.hpp"
#include "lexer.hpp"

namespace lexer {

static bool inrange(char ch, char min, char max)
{
	return ch >= min && ch <= max;
}

static bool isspace(char ch)
{
	return ch == ' ' || ch == '\r' || ch == '\n';
}

static bool isdigit(char ch)
{
	return inrange(ch, '0', '9');
}

static bool isnumber(char ch)
{
	return isdigit(ch) || ch == '_';
}

static bool isidentstart(char ch)
{
	return inrange(ch, 'a', 'z') || inrange(ch, 'A', 'Z') || ch == '_';
}

static bool isident(char ch)
{
	return isidentstart(ch) || isdigit(ch);
}

static bool isbracket(char ch)
{
	return ch == '(' || ch == ')' || ch == '[' || ch == ']' || ch == '{' || ch == '}';
}

static bool isatom(char ch)
{
	return
		ch == '!' ||
		ch == '#' || ch == '$' || ch == '%' || ch == '&' ||
		ch == '*' || ch == '+' || ch == ',' || ch == '-' || ch == '.' || ch == '/' ||
		ch == ':' || ch == ';' || ch == '<' || ch == '<' || ch == '=' || ch == '>' || ch == '?' || ch == '@' ||
		ch == '\\' || ch == '^' || ch == '`' || ch == '|' || ch == '~';
}

Lines lines(const Str& data)
{
	Lines result;

	size_t offset = 0;

	while (offset < data.size)
	{
		size_t start = offset;

		// scan indent
		unsigned int indent = 0;

		while (offset < data.size && data[offset] == ' ')
		{
			offset++;
			indent++;
		}

		if (offset < data.size && data[offset] == '\t')
			panic("(%d, %d): Source files can't have tabs", int(result.lines.size() + 1), indent + 1);

		while (offset < data.size && data[offset] != '\n')
			offset++;

		result.lines.push_back({indent, start, offset});

		if (offset < data.size)
			offset++;
	}

	return result;
}

pair<int, int> getLocation(const Lines& lines, size_t offset)
{
	auto it = std::lower_bound(lines.lines.begin(), lines.lines.end(), offset, [](const Line& line, size_t offset) { return line.end < offset; });

	if (it != lines.lines.end())
		return make_pair(it - lines.lines.begin(), offset - it->start);
	else
		return make_pair(lines.lines.size(), 0);
}

template <typename Fn> static Str scan(const Str& data, size_t& offset, Fn fn)
{
	size_t start = offset;
	size_t end = offset;

	while (end < data.size && fn(data[end]))
		end++;

	offset = end;

	return Str(data.data + start, end - start);
}

Tokens tokenize(const Str& data, const Lines& lines)
{
	Tokens result;

	size_t offset = 0;

	while (offset < data.size)
	{
		while (offset < data.size && isspace(data[offset]))
			offset++;

		if (offset < data.size)
		{
			if (isidentstart(data[offset]))
				result.tokens.push_back({Token::TypeIdent, scan(data, offset, isident)});
			else if (isdigit(data[offset]))
				result.tokens.push_back({Token::TypeNumber, scan(data, offset, isnumber)});
			else if (data[offset] == '"' || data[offset] == '\'')
			{
				char start = data[offset];
				offset++;
				result.tokens.push_back({start == '"' ? Token::TypeString : Token::TypeCharacter, scan(data, offset, [=](char ch) { return ch != start; })});
				offset++;
			}
			else if (isbracket(data[offset]))
			{
				result.tokens.push_back({Token::TypeAtom, Str(data.data + offset, 1)});
				offset++;
			}
			else if (isatom(data[offset]))
			{
				result.tokens.push_back({Token::TypeAtom, scan(data, offset, isatom)});
			}
			else
			{
				auto loc = getLocation(lines, offset);

				if (inrange(data[offset], 0, 32))
					panic("(%d, %d): Unknown character %d", int(loc.first + 1), int(loc.second + 1), data[offset]);
				else
					panic("(%d, %d): Unknown character '%c'", int(loc.first + 1), int(loc.second + 1), data[offset]);
			}
		}
	}

	return result;
}

}