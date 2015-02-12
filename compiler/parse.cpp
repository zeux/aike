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

	if (ts.is(Token::TypeBracket, "["))
	{
		ts.eat(Token::TypeBracket, "[");

		Ty* element = parseType(ts);

		ts.eat(Token::TypeBracket, "]");

		return UNION_NEW(Ty, Array, { element });
	}

	if (ts.is(Token::TypeIdent, "fn"))
	{
		ts.move();

		ts.eat(Token::TypeBracket, "(");

		Arr<Ty*> args;

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

		return UNION_NEW(Ty, Instance, { name.data, name.location, nullptr });
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

				if (lineIndent <= startIndent)
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
	Arr<Ast*> body;

	parseIndent(ts, indent, /* allowSingleLine= */ true, [&]() { body.push(parseExpr(ts)); });

	return UNION_NEW(Ast, Block, { body });
}

static pair<Ty*, Arr<Variable*>> parseFnSignature(TokenStream& ts, bool requireTypes)
{
	Arr<Variable*> args;
	Arr<Ty*> argtys;

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

	return UNION_NEW(Ast, Fn, { int(ts.index), sig.first, indent, sig.second, body });
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

	Arr<pair<Str, Ty*>> fields;

	parseIndent(ts, &indent, /* allowSingleLine= */ false, [&]() {
		vector<Token> fnames;

		for (;;)
		{
			fnames.push_back(ts.eat(Token::TypeIdent));

			if (!ts.is(Token::TypeAtom, ","))
				break;

			ts.move();
		}

		ts.eat(Token::TypeAtom, ":");

		auto ty = parseType(ts);

		// TODO: verify name uniqueness
		for (auto& f: fnames)
			fields.push(make_pair(f.data, ty));
	});

	TyDef* def = UNION_NEW(TyDef, Struct, { fields });

	return UNION_NEW(Ast, TyDecl, { name.data, name.location, def });
}

static Ast* parseCall(TokenStream& ts, Ast* expr, Location start)
{
	ts.eat(Token::TypeBracket, "(");

	Arr<Ast*> args;

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

static Ast* parseMember(TokenStream& ts, Ast* expr)
{
	ts.eat(Token::TypeAtom, ".");

	auto name = ts.eat(Token::TypeIdent);
	Field field = { name.data, name.location, -1 };

	return UNION_NEW(Ast, Member, { expr, name.location, nullptr, field });
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
	Location start = ts.get().location;

	auto name = ts.is(Token::TypeIdent) ? ts.eat(Token::TypeIdent) : Token();

	Arr<pair<Field, Ast*>> fields;

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

		Field field = { fname.data, fname.location, -1 };

		// TODO: verify name uniqueness
		fields.push(make_pair(field, expr));

		if (!ts.is(Token::TypeBracket, "}"))
			ts.eat(Token::TypeAtom, ",");
	}

	ts.eat(Token::TypeBracket, "}");

	Ty* ty = name.data.size == 0 ? UNION_NEW(Ty, Unknown, {}) : UNION_NEW(Ty, Instance, { name.data, name.location, nullptr });

	return UNION_NEW(Ast, LiteralStruct, { name.data, start, ty, fields });
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

	if (ts.is(Token::TypeBracket, "("))
	{
		ts.move();

		Ast* result = parseExpr(ts);

		ts.eat(Token::TypeBracket, ")");

		return result;
	}

	auto t = ts.get();

	ts.output->panic(t.location, "Unexpected token '%s'", t.data.str().c_str());
}

static pair<int, UnaryOp> parseUnaryOp(TokenStream& ts)
{
	if (ts.is(Token::TypeAtom, "+")) return make_pair(1, UnaryOpPlus);
	if (ts.is(Token::TypeAtom, "-")) return make_pair(1, UnaryOpMinus);
	if (ts.is(Token::TypeIdent, "not")) return make_pair(1, UnaryOpNot);

	return make_pair(0, UnaryOpNot);
}

static pair<int, BinaryOp> parseBinaryOp(TokenStream& ts)
{
	if (ts.is(Token::TypeAtom, "*%")) return make_pair(7, BinaryOpMultiplyWrap);
	if (ts.is(Token::TypeAtom, "*")) return make_pair(7, BinaryOpMultiply);
	if (ts.is(Token::TypeAtom, "/")) return make_pair(7, BinaryOpDivide);
	if (ts.is(Token::TypeAtom, "%")) return make_pair(7, BinaryOpModulo);
	if (ts.is(Token::TypeAtom, "+%")) return make_pair(6, BinaryOpAddWrap);
	if (ts.is(Token::TypeAtom, "+")) return make_pair(6, BinaryOpAdd);
	if (ts.is(Token::TypeAtom, "-%")) return make_pair(6, BinaryOpSubtractWrap);
	if (ts.is(Token::TypeAtom, "-")) return make_pair(6, BinaryOpSubtract);
	if (ts.is(Token::TypeAtom, "<")) return make_pair(5, BinaryOpLess);
	if (ts.is(Token::TypeAtom, "<=")) return make_pair(5, BinaryOpLessEqual);
	if (ts.is(Token::TypeAtom, ">")) return make_pair(5, BinaryOpGreater);
	if (ts.is(Token::TypeAtom, ">=")) return make_pair(5, BinaryOpGreaterEqual);
	if (ts.is(Token::TypeAtom, "==")) return make_pair(4, BinaryOpEqual);
	if (ts.is(Token::TypeAtom, "!=")) return make_pair(4, BinaryOpNotEqual);
	if (ts.is(Token::TypeIdent, "and")) return make_pair(3, BinaryOpAnd);
	if (ts.is(Token::TypeIdent, "or")) return make_pair(2, BinaryOpOr);

	return make_pair(0, BinaryOpOr);
}

static Ast* parsePrimary(TokenStream& ts)
{
	auto uop = parseUnaryOp(ts);

	if (uop.first)
	{
		ts.move();

		Ast* expr = parsePrimary(ts);

		return UNION_NEW(Ast, Unary, { uop.second, expr });
	}

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
			term = parseMember(ts, term);
		else
			break;
	}

	return term;
}

Ast* parseExprClimb(TokenStream& ts, Ast* left, int limit)
{
	auto op = parseBinaryOp(ts);

	while (op.first && op.first >= limit)
	{
		ts.move();

		Ast* right = parsePrimary(ts);

		auto nextop = parseBinaryOp(ts);

		while (nextop.first && nextop.first > op.first)
		{
			right = parseExprClimb(ts, right, nextop.first);

			nextop = parseBinaryOp(ts);
		}

		left = UNION_NEW(Ast, Binary, { op.second, left, right });

		op = parseBinaryOp(ts);
	}

	return left;
}

Ast* parseExpr(TokenStream& ts)
{
	return parseExprClimb(ts, parsePrimary(ts), 0);
}

Ast* parse(Output& output, const Tokens& tokens)
{
	TokenStream ts = { &output, &tokens, 0 };

	Ast* result = parseBlock(ts, nullptr);

	ts.expect(Token::TypeEnd);

	return result;
}