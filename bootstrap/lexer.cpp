#include "lexer.hpp"

#include <cassert>

inline char peekch(const Lexer& lexer)
{
	return lexer.position < lexer.data.size() ? lexer.data[lexer.position] : 0;
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

	if (data == "let" || data == "in" || data == "match" || data == "with" || data == "if" || data == "then" || data == "else")
		return Lexeme(LexKeyword, data);
	else
		return Lexeme(LexIdentifier, data);
}

Lexeme readnext(Lexer& lexer)
{
	while (isspace(peekch(lexer)) || peekch(lexer) == '/')
	{
		while (isspace(peekch(lexer))) consume(lexer);

		if (peekch(lexer) == '/')
		{
			consume(lexer);
			if (peekch(lexer) != '/') return LexUnknown;

			while (peekch(lexer) && peekch(lexer) != '\n') consume(lexer);
		}
	}

	switch (peekch(lexer))
	{
	case 0: return LexEOF;
	case ',': return consume(lexer), LexComma;
	case '(': return consume(lexer), LexOpenBrace;
	case ')': return consume(lexer), LexCloseBrace;
	case '=': return consume(lexer), (peekch(lexer) == '=' ? (consume(lexer), LexEqualEqual) : LexEqual);
	case '+': return consume(lexer), LexPlus;
	case '-': return consume(lexer), (peekch(lexer) == '>' ? (consume(lexer), LexArrow) : LexMinus);
	case ':': return consume(lexer), LexColon;
	case ';': return consume(lexer), LexSemicolon;
	case '<': return consume(lexer), (peekch(lexer) == '=' ? (consume(lexer), LexLessEqual) : LexLess);
	case '>': return consume(lexer), (peekch(lexer) == '=' ? (consume(lexer), LexGreaterEqual) : LexGreater);
	case '!': return consume(lexer), (peekch(lexer) == '=' ? (consume(lexer), LexNotEqual) : LexNot);
	case '|': return consume(lexer), LexPipe;
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
	else if (isidentstart(peekch(lexer)))
	{
		return readident(lexer);
	}

	return LexUnknown;
}