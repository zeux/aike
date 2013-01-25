#include "lexer.hpp"

#include <cassert>

#include "output.hpp"

inline char peekch(const Lexer& lexer, size_t offset = 0)
{
	return lexer.position + offset < lexer.file.length ? lexer.file.contents[lexer.position + offset] : 0;
}

inline Location getlocation(const Lexer& lexer, size_t length)
{
	return Location(lexer.file, lexer.line_start_pos, lexer.line + 1, lexer.position - lexer.line_start_pos + 1, length);
}

inline void consume(Lexer& lexer)
{
	if (peekch(lexer) == '\t')
	{
		lexer.current.location = getlocation(lexer, 1);
		errorf(lexer.current.location, "Source file must not contain tabs");
	}
	else if (peekch(lexer) == '\n')
	{
		lexer.line_start_pos = lexer.position + 1;
		lexer.line++;
	}

	assert(lexer.position < lexer.file.length);
	lexer.position++;
}

Lexeme readnumber(Lexer& lexer, int base)
{
	long long result = 0;

	while (char ch = peekch(lexer))
	{
		if (isdigit(ch))
		{
			if (ch - '0' >= base) return LexUnknown;

			result = result * base + (ch - '0');
			consume(lexer);
		}
		else if (ch >= 'a' && ch <= 'f')
		{
			if (base != 16) return LexUnknown;

			result = result * base + (ch - 'a' + 10);
			consume(lexer);
		}
		else if (ch >= 'A' && ch <= 'F')
		{
			if (base != 16) return LexUnknown;

			result = result * base + (ch - 'a' + 10);
			consume(lexer);
		}
		else if (ch == '_')
		{
			consume(lexer);
		}
		else if (isalpha(ch))
		{
			return LexUnknown;
		}
		else
		{
			return Lexeme(LexNumber, result);
		}
	}

	return Lexeme(LexNumber, result);
}

inline bool isidentstart(char ch)
{
	return isalpha(ch) || ch == '_';
}

inline bool isident(char ch)
{
	return isalnum(ch) || ch == '_';
}

Lexeme readident(Lexer& lexer)
{
	std::string data;

	while (isident(peekch(lexer)))
	{
		data += peekch(lexer);
		consume(lexer);
	}

	if (data == "let" || data == "match" || data == "with" || data == "if" || data == "then" || data == "else" || data == "llvm" || data == "extern" || data == "fun" || data == "for" || data == "in" || data == "do" || data == "true" || data == "false" || data == "type")
		return Lexeme(LexKeyword, data);
	else
		return Lexeme(LexIdentifier, data);
}

Lexeme readnext(Lexer& lexer)
{
	switch (peekch(lexer))
	{
	case 0: return LexEOF;
	case ',': return consume(lexer), LexComma;
	case '.': return consume(lexer), (peekch(lexer) == '.' ? (consume(lexer), LexPointPoint) : LexPoint);
	case '(': return consume(lexer), LexOpenBrace;
	case ')': return consume(lexer), LexCloseBrace;
	case '[': return consume(lexer), LexOpenBracket;
	case ']': return consume(lexer), LexCloseBracket;
	case '{': return consume(lexer), LexOpenCurlyBrace;
	case '}': return consume(lexer), LexCloseCurlyBrace;
	case '=': return consume(lexer), (peekch(lexer) == '=' ? (consume(lexer), LexEqualEqual) : LexEqual);
	case '+': return consume(lexer), LexPlus;
	case '-': return consume(lexer), (peekch(lexer) == '>' ? (consume(lexer), LexArrow) : LexMinus);
	case ':': return consume(lexer), (peekch(lexer) == '=' ? (consume(lexer), LexColonEqual) : LexColon);
	case ';': return consume(lexer), LexSemicolon;
	case '<': return consume(lexer), (peekch(lexer) == '=' ? (consume(lexer), LexLessEqual) : LexLess);
	case '>': return consume(lexer), (peekch(lexer) == '=' ? (consume(lexer), LexGreaterEqual) : LexGreater);
	case '!': return consume(lexer), (peekch(lexer) == '=' ? (consume(lexer), LexNotEqual) : LexExclamation);
	case '|': return consume(lexer), LexPipe;
	case '*': return consume(lexer), LexMultiply;
	case '/': return consume(lexer), LexDivide;
	case '#': return consume(lexer), LexSharp;
	}

	if (isdigit(peekch(lexer)))
	{
		if (peekch(lexer) == '0')
		{
			consume(lexer);

			if (peekch(lexer) == 'x')
				return consume(lexer), readnumber(lexer, 16);
			else if (peekch(lexer) == 'b')
				return consume(lexer), readnumber(lexer, 2);
			else if (isdigit(peekch(lexer)))
				// can handle oct here, but we'd rather not...
				return LexUnknown;
			else if (isalpha(peekch(lexer)))
				// HUH?
				return LexUnknown;
			else
				return Lexeme(LexNumber, 0);
		}
		else
			return readnumber(lexer, 10);
	}
	else if (peekch(lexer) == '\'')
	{
		consume(lexer);

		if (isidentstart(peekch(lexer)) && peekch(lexer, 1) != '\'')
		{
			std::string data;
			
			while (isident(peekch(lexer)))
			{
				data += peekch(lexer);
				consume(lexer);
			}

			return Lexeme(LexIdentifierGeneric, data);
		}
		else
		{
			std::string data;

			while (peekch(lexer) && peekch(lexer) != '\'')
			{
				data += peekch(lexer);
				consume(lexer);
			}

			consume(lexer);

			return Lexeme(LexCharacter, data);
		}
	}
	else if (peekch(lexer) == '\"')
	{
		consume(lexer);

		std::string data;

		while (peekch(lexer) && peekch(lexer) != '\"')
		{
			data += peekch(lexer);
			consume(lexer);
		}

		consume(lexer);

		return Lexeme(LexString, data);
	}
	else if (isidentstart(peekch(lexer)))
	{
		return readident(lexer);
	}

	return LexUnknown;
}

void movenext(Lexer& lexer)
{
	while (isspace(peekch(lexer)) || peekch(lexer) == '/')
	{
		while (peekch(lexer) != '\n' && isspace(peekch(lexer)))
			consume(lexer);

		if (peekch(lexer) == '\n')
			consume(lexer);

		if (peekch(lexer) == '/')
		{
			if (peekch(lexer, 1) == '/')
			{
				while (peekch(lexer) && peekch(lexer) != '\n')
					consume(lexer);
			}
			else
			{
				break;
			}
		}
	}

	Location location = getlocation(lexer, 0);

	size_t position = lexer.position;
	lexer.current = readnext(lexer);

	lexer.current.location = Location(location.file, location.lineOffset, location.line, location.column, lexer.position - position);
}

Lexer capturestate(const Lexer& lexer)
{
	Lexer result;
	result.position = lexer.position;
	result.line_start_pos = lexer.line_start_pos;
	result.line = lexer.line;
	result.current = lexer.current;
	return result;
}

void restorestate(Lexer& lexer, const Lexer& state)
{
	lexer.position = state.position;
	lexer.line_start_pos = state.line_start_pos;
	lexer.line = state.line;
	lexer.current = state.current;
}
