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

			output->panic(t.location, "Expected %s, got '%s'", tokenTypeName(type), t.data.str().c_str());
		}
	}

	void expect(Token::Type type, const char* data)
	{
		if (!is(type, data))
		{
			const Token& t = get();

			output->panic(t.location, "Expected '%s', got '%s'", data, t.data.str().c_str());
		}
	}

	Token eat(Token::Type type)
	{
		expect(type);

		Token result = get();

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
	if (ts.is(Token::TypeIdent, "_"))
	{
		ts.move();
		return UNION_NEW(Ty, Unknown, {});
	}

	if (ts.is(Token::TypeIdent, "void"))
	{
		ts.move();
		return UNION_NEW(Ty, Void, {});
	}

	if (ts.is(Token::TypeIdent, "bool"))
	{
		ts.move();
		return UNION_NEW(Ty, Bool, {});
	}

	if (ts.is(Token::TypeIdent, "int"))
	{
		ts.move();
		return UNION_NEW(Ty, Integer, {});
	}

	if (ts.is(Token::TypeIdent, "string"))
	{
		ts.move();
		return UNION_NEW(Ty, String, {});
	}

	if (ts.is(Token::TypeIdent, "fn"))
	{
		ts.move();

		ts.eat(Token::TypeBracket, "(");

		Array<Ty*> args;

		while (!ts.is(Token::TypeBracket, ")"))
		{
			args.push(parseType(ts));

			if (!ts.is(Token::TypeBracket, ")"))
				ts.eat(Token::TypeAtom, ",");
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

		return UNION_NEW(Ty, Function, { args, ret });
	}

	if (ts.is(Token::TypeIdent))
	{
		auto name = ts.eat(Token::TypeIdent);

		return UNION_NEW(Ty, Instance, { name.data, nullptr });
	}

	ts.output->panic(ts.get().location, "Expected type");
}

template <typename F>
static void parseIndent(TokenStream& ts, const Location* indent, bool allowSingleLine, F f)
{
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
					ts.output->panic(ts.get().location, "Invalid indentation: expected %d, got %d", firstIndent, lineIndent);
			}

			f();
		}
	}
	else
	{
		if (allowSingleLine)
			f();
		else
			ts.output->panic(ts.get().location, "Expected newline");
	}
}

static Ast* parseExpr(TokenStream& ts);

static Ast* parseBlock(TokenStream& ts, const Location* indent)
{
	Array<Ast*> body;

	parseIndent(ts, indent, /* allowSingleLine= */ true, [&]() { body.push(parseExpr(ts)); });

	return UNION_NEW(Ast, Block, { body });
}

static pair<Ty*, Array<Variable*>> parseFnSignature(TokenStream& ts, bool requireTypes)
{
	Array<Variable*> args;
	Array<Ty*> argtys;

	ts.eat(Token::TypeBracket, "(");

	while (!ts.is(Token::TypeBracket, ")"))
	{
		auto argname = ts.eat(Token::TypeIdent);

		Ty* type;

		if (ts.is(Token::TypeAtom, ":") || requireTypes)
		{
			ts.eat(Token::TypeAtom, ":");

			type = parseType(ts);
		}
		else
			type = UNION_NEW(Ty, Unknown, {});

		args.push(new Variable { argname.data, type, argname.location });
		argtys.push(type);

		if (!ts.is(Token::TypeBracket, ")"))
			ts.eat(Token::TypeAtom, ",");
	}

	ts.eat(Token::TypeBracket, ")");

	Ty* ret;

	if (ts.is(Token::TypeAtom, ":"))
	{
		ts.move();
		ret = parseType(ts);
	}
	else if (requireTypes)
		ret = UNION_NEW(Ty, Void, {});
	else
		ret = UNION_NEW(Ty, Unknown, {});

	Ty* ty = UNION_NEW(Ty, Function, { argtys, ret });

	return make_pair(ty, args);
}

static Ast* parseFn(TokenStream& ts)
{
	Location indent = ts.get().location;

	ts.eat(Token::TypeIdent, "fn");

	auto sig = parseFnSignature(ts, /* requireTypes= */ false);

	Ast* body = parseBlock(ts, &indent);

	return UNION_NEW(Ast, Fn, { sig.first, indent, sig.second, body });
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

	auto name = ts.eat(Token::TypeIdent);

	auto sig = parseFnSignature(ts, /* requireTypes= */ true);

	Ast* body =
		(attributes & FnAttributeExtern)
		? nullptr
		: parseBlock(ts, &indent);

	return UNION_NEW(Ast, FnDecl, { new Variable { name.data, sig.first, name.location }, sig.second, attributes, body });
}

static Ast* parseVarDecl(TokenStream& ts)
{
	ts.eat(Token::TypeIdent, "var");

	auto name = ts.eat(Token::TypeIdent);

	Ty* type;

	if (ts.is(Token::TypeAtom, ":"))
	{
		ts.move();
		type = parseType(ts);
	}
	else
		type = UNION_NEW(Ty, Unknown, {});

	ts.eat(Token::TypeAtom, "=");

	Ast* expr = parseExpr(ts);

	return UNION_NEW(Ast, VarDecl, { new Variable { name.data, type, name.location }, expr });
}

static Ast* parseStructDecl(TokenStream& ts)
{
	Location indent = ts.get().location;

	ts.eat(Token::TypeIdent, "struct");

	auto name = ts.eat(Token::TypeIdent);

	Array<pair<Str, Ty*>> fields;

	parseIndent(ts, &indent, /* allowSingleLine= */ false, [&]() {
		auto fname = ts.eat(Token::TypeIdent);

		ts.eat(Token::TypeAtom, ":");

		auto ty = parseType(ts);

		fields.push(make_pair(fname.data, ty));
	});

	TyDef* def = UNION_NEW(TyDef, Struct, { fields });

	return UNION_NEW(Ast, TyDecl, { name.data, name.location, def });
}

static Ast* parseCall(TokenStream& ts, Ast* expr, Location start)
{
	ts.eat(Token::TypeBracket, "(");

	Array<Ast*> args;

	while (!ts.is(Token::TypeBracket, ")"))
	{
		args.push(parseExpr(ts));

		if (!ts.is(Token::TypeBracket, ")"))
			ts.eat(Token::TypeAtom, ",");
	}

	Location end = ts.get().location;

	ts.eat(Token::TypeBracket, ")");

	return UNION_NEW(Ast, Call, { expr, args, Location(start, end) });
}

static Ast* parseIndex(TokenStream& ts, Ast* expr)
{
	ts.eat(Token::TypeAtom, ".");

	auto name = ts.eat(Token::TypeIdent);

	return UNION_NEW(Ast, Index, { expr, name.data, name.location });
}

static Ast* parseIf(TokenStream& ts)
{
	Location start = ts.get().location;

	ts.eat(Token::TypeIdent, "if");

	Ast* cond = parseExpr(ts);
	Ast* thenbody = parseBlock(ts, &start);
	Ast* elsebody = nullptr;

	if (ts.is(Token::TypeIdent, "else"))
	{
		int ifIndent = getLineIndent(ts, start);
		int elseIndent = getLineIndent(ts, ts.get().location);

		if (ifIndent != elseIndent)
			ts.output->panic(ts.get().location, "Invalid indentation: expected %d, got %d", ifIndent, elseIndent);

		ts.eat(Token::TypeIdent);

		elsebody = parseBlock(ts, &start);
	}

	return UNION_NEW(Ast, If, { cond, thenbody, elsebody });
}

static Ast* parseLiteralStruct(TokenStream& ts)
{
	auto name = ts.is(Token::TypeIdent) ? ts.eat(Token::TypeIdent) : Token();

	Array<pair<Str, Ast*>> fields;

	ts.eat(Token::TypeBracket, "{");

	while (!ts.is(Token::TypeBracket, "}"))
	{
		auto fname = ts.eat(Token::TypeIdent);

		Ast* expr;

		if (ts.is(Token::TypeAtom, "="))
		{
			ts.move();

			expr = parseExpr(ts);
		}
		else
			expr = UNION_NEW(Ast, Ident, { fname.data, fname.location, nullptr });

		fields.push(make_pair(fname.data, expr));

		if (!ts.is(Token::TypeBracket, "}"))
			ts.eat(Token::TypeAtom, ",");
	}

	ts.eat(Token::TypeBracket, "}");

	return UNION_NEW(Ast, LiteralStruct, { name.data, name.location, UNION_NEW(Ty, Unknown, {}), fields });
}

static Ast* parseTerm(TokenStream& ts)
{
	if (ts.is(Token::TypeIdent, "true"))
	{
		auto value = ts.eat(Token::TypeIdent);

		return UNION_NEW(Ast, LiteralBool, { true, value.location });
	}

	if (ts.is(Token::TypeIdent, "false"))
	{
		auto value = ts.eat(Token::TypeIdent);

		return UNION_NEW(Ast, LiteralBool, { false, value.location });
	}

	if (ts.is(Token::TypeNumber))
	{
		auto value = ts.eat(Token::TypeNumber);

		return UNION_NEW(Ast, LiteralNumber, { value.data, value.location });
	}

	if (ts.is(Token::TypeString))
	{
		auto value = ts.eat(Token::TypeString);

		return UNION_NEW(Ast, LiteralString, { value.data, value.location });
	}

	if (ts.is(Token::TypeBracket, "{") || (ts.is(Token::TypeIdent) && ts.get(1).type == Token::TypeBracket && ts.get(1).data == "{"))
	{
		return parseLiteralStruct(ts);
	}

	if (ts.is(Token::TypeIdent))
	{
		auto name = ts.eat(Token::TypeIdent);

		return UNION_NEW(Ast, Ident, { name.data, name.location, nullptr });
	}

	auto t = ts.get();

	ts.output->panic(t.location, "Unexpected token '%s'", t.data.str().c_str());
}

static Ast* parseExpr(TokenStream& ts)
{
	if (ts.is(Token::TypeIdent, "extern"))
		return parseFnDecl(ts);

	if (ts.is(Token::TypeIdent, "fn"))
		return ts.get(1).type == Token::TypeIdent ? parseFnDecl(ts) : parseFn(ts);

	if (ts.is(Token::TypeIdent, "var"))
		return parseVarDecl(ts);

	if (ts.is(Token::TypeIdent, "struct"))
		return parseStructDecl(ts);

	if (ts.is(Token::TypeIdent, "if"))
		return parseIf(ts);

	Location start = ts.get().location;

	Ast* term = parseTerm(ts);

	for (;;)
	{
		if (ts.is(Token::TypeBracket, "("))
			term = parseCall(ts, term, start);
		else if (ts.is(Token::TypeAtom, "."))
			term = parseIndex(ts, term);
		else
			break;
	}

	return term;
}

Ast* parse(Output& output, const Tokens& tokens)
{
	TokenStream ts = { &output, &tokens, 0 };

	Ast* result = parseBlock(ts, nullptr);

	ts.expect(Token::TypeEnd);

	return result;
}