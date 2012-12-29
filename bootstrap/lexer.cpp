#include "lexer.hpp"

#include <cassert>

inline char peekch(const Lexer& lexer)
{
	return lexer.position < lexer.data.size() ? lexer.data[lexer.position] : 0;
}

inline char peekch(const Lexer& lexer, size_t offset)
{
	return lexer.position + offset < lexer.data.size() ? lexer.data[lexer.position + offset] : 0;
}

inline void consume(Lexer& lexer)
{
	assert(lexer.position < lexer.data.size());
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
	case ':': return consume(lexer), LexColon;
	case ';': return consume(lexer), LexSemicolon;
	case '<': return consume(lexer), (peekch(lexer) == '=' ? (consume(lexer), LexLessEqual) : LexLess);
	case '>': return consume(lexer), (peekch(lexer) == '=' ? (consume(lexer), LexGreaterEqual) : LexGreater);
	case '!': return consume(lexer), (peekch(lexer) == '=' ? (consume(lexer), LexNotEqual) : LexNot);
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

		std::string data;

		while (peekch(lexer) && peekch(lexer) != '\'')
		{
			data += peekch(lexer);
			consume(lexer);
		}

		consume(lexer);

		return Lexeme(LexCharacter, data);
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
		{
			consume(lexer);
			lexer.line_start_pos = lexer.position;
			lexer.line++;
		}

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

	size_t column = lexer.position - lexer.line_start_pos;

	lexer.current = readnext(lexer);

	lexer.current.location = Location(lexer.line_start_pos < lexer.data.size() ? &lexer.data[lexer.line_start_pos] : 0, lexer.line, column, lexer.position - lexer.line_start_pos - column);
}
