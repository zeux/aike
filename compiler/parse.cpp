#include "common.hpp"
#include "parse.hpp"

#include "ast.hpp"
#include "tokenize.hpp"
#include "output.hpp"

const Token kEnd = { Token::TypeEnd };

struct TokenStream
{
	Output* output;
	const Tokens* tokens;
	size_t index;

	const Token& get(size_t offset = 0) const
	{
		return index + offset < tokens->tokens.size ? tokens->tokens[index + offset] : kEnd;
	}

	void move()
	{
		assert(!is(Token::TypeEnd));

		index++;
	}

	bool is(Token::Type type)
	{
		const Token& t = get();

		return (t.type == type);
	}

	bool is(Token::Type type, const char* data)
	{
		const Token& t = get();

		return (t.type == type && t.data == data);
	}

	void expect(Token::Type type)
	{
		if (!is(type))
		{
			const Token& t = get();

			output->panic(t.location, "Expected %s, got '%.*s'", tokenTypeName(type), int(t.data.size), t.data.data);
		}
	}

	void expect(Token::Type type, const char* data)
	{
		if (!is(type, data))
		{
			const Token& t = get();

			output->panic(t.location, "Expected '%s', got '%.*s'", data, int(t.data.size), t.data.data);
		}
	}

	Str eat(Token::Type type)
	{
		expect(type);

		Str result = get().data;

		move();

		return result;
	}

	void eat(Token::Type type, const char* data)
	{
		expect(type, data);

		move();
	}
};

static int getLineIndent(const TokenStream& ts, const Location& loc)
{
	return ts.tokens->lines[loc.line].indent;
}

static bool isFirstOnLine(const TokenStream& ts, const Location& loc)
{
	return loc.column == ts.tokens->lines[loc.line].indent;
}

static Ty* parseType(TokenStream& ts)
{
	if (ts.is(Token::TypeIdent, "string"))
	{
		ts.move();
		return UNION_NEW(Ty, String, {});
	}

	ts.output->panic(ts.get().location, "Expected type");
}

static Ast* parseExpr(TokenStream& ts);

static Ast* parseBlock(TokenStream& ts, const Location* indent)
{
	Array<Ast*> body;

	if (isFirstOnLine(ts, ts.get().location))
	{
		int startIndent = indent ? getLineIndent(ts, *indent) : 0;
		int firstIndent = getLineIndent(ts, ts.get().location);

		if (indent && firstIndent <= startIndent)
			ts.output->panic(ts.get().location, "Invalid indentation: expected >%d, got %d", startIndent, firstIndent);

		while (!ts.is(Token::TypeEnd))
		{
			if (!isFirstOnLine(ts, ts.get().location))
				ts.output->panic(ts.get().location, "Expected newline");

			if (indent)
			{
				int lineIndent = getLineIndent(ts, ts.get().location);

				if (lineIndent == startIndent)
					break;

				if (lineIndent != firstIndent)
					ts.output->panic(ts.get().location, "Invalid indentation: expected %d, got %d", startIndent, lineIndent);
			}

			body.push(parseExpr(ts));
		}
	}
	else
	{
		body.push(parseExpr(ts));
	}

	return UNION_NEW(Ast, Block, { body });
}

static Ast* parseFnDecl(TokenStream& ts)
{
	Location indent = ts.get().location;

	unsigned attributes = 0;

	if (ts.is(Token::TypeIdent, "extern"))
	{
		attributes |= FnAttributeExtern;
		ts.move();
	}

	ts.eat(Token::TypeIdent, "fn");

	Str name = ts.eat(Token::TypeIdent);

	Array<pair<Str, Ty*>> arguments;

	ts.eat(Token::TypeBracket, "(");

	while (!ts.is(Token::TypeBracket, ")"))
	{
		Str argname = ts.eat(Token::TypeIdent);

		ts.eat(Token::TypeAtom, ":");

		Ty* type = parseType(ts);

		arguments.push(make_pair(argname, type));
	}

	ts.eat(Token::TypeBracket, ")");

	Ty* ret;

	if (ts.is(Token::TypeAtom, ":"))
	{
		ts.move();
		ret = parseType(ts);
	}
	else
		ret = UNION_NEW(Ty, Void, {});

	Ast* body =
		(attributes & FnAttributeExtern)
		? nullptr
		: parseBlock(ts, &indent);

	return UNION_NEW(Ast, FnDecl, { name, arguments, ret, attributes, body });
}

static Ast* parseCall(TokenStream& ts, Ast* expr)
{
	ts.eat(Token::TypeBracket, "(");

	Array<Ast*> arguments;

	while (!ts.is(Token::TypeBracket, ")"))
	{
		arguments.push(parseExpr(ts));

		if (!ts.is(Token::TypeBracket, ")"))
			ts.eat(Token::TypeAtom, ",");
	}

	ts.eat(Token::TypeBracket, ")");

	return UNION_NEW(Ast, Call, { expr, arguments });
}

static Ast* parseTerm(TokenStream& ts)
{
	if (ts.is(Token::TypeString))
		return UNION_NEW(Ast, LiteralString, { ts.eat(Token::TypeString) });

	if (ts.is(Token::TypeIdent))
		return UNION_NEW(Ast, Ident, { ts.eat(Token::TypeIdent) });

	auto t = ts.get();

	ts.output->panic(t.location, "Unexpected token '%.*s'", int(t.data.size), t.data.data);
}

static Ast* parseExpr(TokenStream& ts)
{
	if (ts.is(Token::TypeIdent, "extern") || ts.is(Token::TypeIdent, "fn"))
		return parseFnDecl(ts);

	Ast* term = parseTerm(ts);

	if (ts.is(Token::TypeBracket, "("))
		return parseCall(ts, term);

	return term;
}

Ast* parse(Output& output, const Tokens& tokens)
{
	TokenStream ts = { &output, &tokens, 0 };

	Ast* result = parseBlock(ts, nullptr);

	ts.expect(Token::TypeEnd);

	return result;
}