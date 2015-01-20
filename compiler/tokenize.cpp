#include "common.hpp"
#include "tokenize.hpp"

#include "output.hpp"

static bool inRange(char ch, char min, char max)
{
	return ch >= min && ch <= max;
}

static bool isSpace(char ch)
{
	return ch == ' ' || ch == '\r' || ch == '\n';
}

static bool isDigit(char ch)
{
	return inRange(ch, '0', '9');
}

static bool isNumber(char ch)
{
	return isDigit(ch) || ch == '_';
}

static bool isIdentStart(char ch)
{
	return inRange(ch, 'a', 'z') || inRange(ch, 'A', 'Z') || ch == '_';
}

static bool isIdent(char ch)
{
	return isIdentStart(ch) || isDigit(ch);
}

static bool isBracket(char ch)
{
	return ch == '(' || ch == ')' || ch == '[' || ch == ']' || ch == '{' || ch == '}';
}

static bool isAtom(char ch)
{
	return
		ch == '!' ||
		ch == '#' || ch == '$' || ch == '%' || ch == '&' ||
		ch == '*' || ch == '+' || ch == ',' || ch == '-' || ch == '.' || ch == '/' ||
		ch == ':' || ch == ';' || ch == '<' || ch == '<' || ch == '=' || ch == '>' || ch == '?' || ch == '@' ||
		ch == '\\' || ch == '^' || ch == '`' || ch == '|' || ch == '~';
}

static Array<Line> parseLines(Output& output, const char* source, const Str& data)
{
	Array<Line> result;

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

		while (offset < data.size && data[offset] != '\n')
		{
			if (data[offset] == '\t')
				output.panic(Location(source, result.size, indent, offset, 1), "Source files can't have tabs");

			offset++;
		}

		result.push({indent, start});

		if (offset < data.size)
			offset++;
	}

	return result;
}

static Location getLocation(const char* source, const Array<Line>& lines, size_t offset, size_t length)
{
	auto it = std::lower_bound(lines.begin(), lines.end(), offset, [](const Line& line, size_t offset) { return line.offset <= offset; });
	assert(it != lines.begin());

	auto line = it - 1;

	return Location(source, line - lines.begin(), offset - line->offset, offset, length);
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

static Array<Token> parseTokens(Output& output, const char* source, const Str& data, const Array<Line>& lines)
{
	Array<Token> result;

	size_t offset = 0;

	while (offset < data.size)
	{
		while (offset < data.size && isspace(data[offset]))
			offset++;

		if (offset < data.size)
		{
			if (isIdentStart(data[offset]))
				result.push({Token::TypeIdent, scan(data, offset, isIdent)});
			else if (isdigit(data[offset]))
				result.push({Token::TypeNumber, scan(data, offset, isNumber)});
			else if (data[offset] == '"' || data[offset] == '\'')
			{
				char start = data[offset];
				offset++;
				result.push({start == '"' ? Token::TypeString : Token::TypeCharacter, scan(data, offset, [=](char ch) { return ch != start; })});
				offset++;
			}
			else if (isBracket(data[offset]))
			{
				result.push({Token::TypeBracket, Str(data.data + offset, 1)});
				offset++;
			}
			else if (isAtom(data[offset]))
			{
				result.push({Token::TypeAtom, scan(data, offset, isAtom)});
			}
			else
			{
				Location loc = getLocation(source, lines, offset, 1);

				if (inRange(data[offset], 0, 32))
					output.panic(loc, "Unknown character %d", data[offset]);
				else
					output.panic(loc, "Unknown character '%c'", data[offset]);
			}
		}
	}

	return result;
}

static const char* getClosingBracket(const Str& open)
{
	assert(open == "{" || open == "(" || open == "[");

	return (open == "{") ? "}" : (open == "(") ? ")" : "]";
}

static void matchBrackets(Output& output, Array<Token>& tokens)
{
	vector<size_t> brackets;

	for (size_t i = 0; i < tokens.size; ++i)
	{
		const Token& t = tokens[i];

		if (t.type == Token::TypeBracket)
		{
			if (t.data == "{" || t.data == "(" || t.data == "[")
				brackets.push_back(i);
			else
			{
				if (brackets.empty())
					output.panic(t.location, "Unmatched closing bracket %s", t.data.str().c_str());

				const Token& open = tokens[brackets.back()];
				const char* close = getClosingBracket(open.data);

				if (t.data != close)
					output.panic(t.location, "Mismatched closing bracket: expected %s to close bracket at (%d,%d)",
						close, open.location.line + 1, open.location.column + 1);

				brackets.pop_back();
			}
		}
	}

	if (!brackets.empty())
	{
		const Token& open = tokens[brackets.back()];
		const char* close = getClosingBracket(open.data);

		output.panic(open.location, "Unmatched opening bracket: expected %s to close but found end of file", close);
	}
}

Tokens tokenize(Output& output, const char* source, const Str& data)
{
	auto lines = parseLines(output, source, data);
	auto tokens = parseTokens(output, source, data, lines);

	for (auto& t: tokens)
		t.location = getLocation(source, lines, t.data.data - data.data, t.data.size);

	matchBrackets(output, tokens);

	return { lines, tokens };
}
