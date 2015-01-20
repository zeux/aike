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

	void move(size_t offset = 1)
	{
		index += offset;
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

static Ast* parseExpr(TokenStream& ts);

static Ast* parseBlock(TokenStream& ts)
{
	Array<Ast*> body;

	while (!ts.is(Token::TypeEnd))
		body.push(parseExpr(ts));

	return UNION_NEW(Ast, Block, { body });
}

static Ast* parseFnDecl(TokenStream& ts)
{
	unsigned attributes = 0;

	if (ts.is(Token::TypeIdent, "extern"))
	{
		attributes |= FnAttributeExtern;
		ts.move();
	}

	ts.eat(Token::TypeIdent, "fn");

	Str name = ts.eat(Token::TypeIdent);

	Array<Str> arguments;

	ts.eat(Token::TypeBracket, "(");

	while (!ts.is(Token::TypeBracket, ")"))
	{
		Str argname = ts.eat(Token::TypeIdent);

		ts.eat(Token::TypeAtom, ":");

		Str argtype = ts.eat(Token::TypeIdent);

		arguments.push(argname);
	}

	ts.eat(Token::TypeBracket, ")");

	Ast* body =
		(attributes & FnAttributeExtern)
		? nullptr
		: parseBlock(ts);

	return UNION_NEW(Ast, FnDecl, { name, arguments, attributes, body });
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

	return parseBlock(ts);
}