#include "common.hpp"
#include "lexer.hpp"

#include "output.hpp"

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

static vector<Line> parseLines(Output& output, const char* source, const Str& data)
{
	vector<Line> result;

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
				output.panic(Location(source, result.size(), indent, offset, 1), "Source files can't have tabs");

			offset++;
		}

		result.push_back({indent, start});

		if (offset < data.size)
			offset++;
	}

	return result;
}

static Location getLocation(const char* source, const vector<Line>& lines, size_t offset, size_t length)
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

static vector<Token> parseTokens(Output& output, const char* source, const Str& data, const vector<Line>& lines)
{
	vector<Token> result;

	size_t offset = 0;

	while (offset < data.size)
	{
		while (offset < data.size && isspace(data[offset]))
			offset++;

		if (offset < data.size)
		{
			if (isidentstart(data[offset]))
				result.push_back({Token::TypeIdent, scan(data, offset, isident)});
			else if (isdigit(data[offset]))
				result.push_back({Token::TypeNumber, scan(data, offset, isnumber)});
			else if (data[offset] == '"' || data[offset] == '\'')
			{
				char start = data[offset];
				offset++;
				result.push_back({start == '"' ? Token::TypeString : Token::TypeCharacter, scan(data, offset, [=](char ch) { return ch != start; })});
				offset++;
			}
			else if (isbracket(data[offset]))
			{
				result.push_back({Token::TypeBracket, Str(data.data + offset, 1)});
				offset++;
			}
			else if (isatom(data[offset]))
			{
				result.push_back({Token::TypeAtom, scan(data, offset, isatom)});
			}
			else
			{
				Location loc = getLocation(source, lines, offset, 1);

				if (inrange(data[offset], 0, 32))
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

static void matchBrackets(Output& output, vector<Token>& tokens)
{
	vector<size_t> brackets;

	for (size_t i = 0; i < tokens.size(); ++i)
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

}