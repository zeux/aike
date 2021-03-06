#include "common.hpp"
#include "parse.hpp"

#include "ast.hpp"
#include "tokenize.hpp"
#include "output.hpp"

#include <cerrno>

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

			output->panic(t.location, "Expected %s, got %s", tokenName(type).c_str(), tokenName(t).c_str());
		}
	}

	void expect(Token::Type type, const char* data)
	{
		if (!is(type, data))
		{
			const Token& t = get();

			output->panic(t.location, "Expected '%s', got %s", data, tokenName(t).c_str());
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

static Arr<Ty*> parseTypeArguments(TokenStream& ts);

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

	if (ts.is(Token::TypeIdent, "float"))
	{
		ts.move();
		return UNION_NEW(Ty, Float, {});
	}

	if (ts.is(Token::TypeIdent, "string"))
	{
		ts.move();
		return UNION_NEW(Ty, String, {});
	}

	if (ts.is(Token::TypeBracket, "("))
	{
		ts.eat(Token::TypeBracket, "(");

		Arr<Ty*> fields;

		while (!ts.is(Token::TypeBracket, ")"))
		{
			fields.push(parseType(ts));

			if (!ts.is(Token::TypeBracket, ")"))
				ts.eat(Token::TypeAtom, ",");
		}

		ts.eat(Token::TypeBracket, ")");

		if (fields.size == 0)
			return UNION_NEW(Ty, Void, {});
		else if (fields.size == 1)
			return fields[0];
		else
			return UNION_NEW(Ty, Tuple, { fields });
	}

	if (ts.is(Token::TypeBracket, "["))
	{
		ts.eat(Token::TypeBracket, "[");

		Ty* element = parseType(ts);

		ts.eat(Token::TypeBracket, "]");

		return UNION_NEW(Ty, Array, { element });
	}

	if (ts.is(Token::TypeAtom, "*"))
	{
		ts.move();

		Ty* element = parseType(ts);

		return UNION_NEW(Ty, Pointer, { element });
	}

	if (ts.is(Token::TypeIdent, "fn"))
	{
		ts.move();

		ts.eat(Token::TypeBracket, "(");

		Arr<Ty*> args;
		bool varargs = false;

		while (!ts.is(Token::TypeBracket, ")"))
		{
			if (ts.is(Token::TypeAtom, "..."))
			{
				ts.move();

				varargs = true;
				break;
			}

			args.push(parseType(ts));

			if (!ts.is(Token::TypeBracket, ")"))
				ts.eat(Token::TypeAtom, ",");
		}

		ts.eat(Token::TypeBracket, ")");
		ts.eat(Token::TypeAtom, ":");

		Ty* ret = parseType(ts);

		return UNION_NEW(Ty, Function, { args, ret, varargs });
	}

	if (ts.is(Token::TypeIdent))
	{
		auto name = ts.eat(Token::TypeIdent);
		auto tysig = parseTypeArguments(ts);

		return UNION_NEW(Ty, Instance, { name.data, name.location, tysig });
	}

	ts.output->panic(ts.get().location, "Expected type");
}

template <typename F>
static void parseIndent(TokenStream& ts, const Location* indent, F f)
{
	int startIndent = indent ? getLineIndent(ts, *indent) : 0;
	int firstIndent = getLineIndent(ts, ts.get().location);

	if (indent && firstIndent <= startIndent)
		ts.output->panic(ts.get().location, "Invalid indentation: expected >%d, got %d", startIndent, firstIndent);

	while (!ts.is(Token::TypeEnd))
	{
		f();

		if (ts.is(Token::TypeEnd))
			break;

		// For nested blocks it could be that the newline already got consumed by f()
		// so we handle that case as well as the case where f() did not consume it.
		if (ts.is(Token::TypeLine))
			ts.move();
		else if (ts.get(-1).type != Token::TypeLine)
			ts.expect(Token::TypeLine);

		if (indent)
		{
			int lineIndent = getLineIndent(ts, ts.get().location);

			if (lineIndent <= startIndent)
				break;

			if (lineIndent != firstIndent)
				ts.output->panic(ts.get().location, "Invalid indentation: expected %d, got %d", firstIndent, lineIndent);
		}
	}
}

static Ast* parseExpr(TokenStream& ts);

static Ast* parseBlock(TokenStream& ts, const Location* indent)
{
	Arr<Ast*> body;

	parseIndent(ts, indent, [&]() { body.push(parseExpr(ts)); });

	return UNION_NEW(Ast, Block, { nullptr, Location(), body });
}

static Ast* parseBlockExpr(TokenStream& ts, const Location* indent)
{
	if (ts.is(Token::TypeLine))
	{
		ts.move();

		return parseBlock(ts, indent);
	}
	else
	{
		Ast* result = parseExpr(ts);

		// parseBlockExpr reads newline after blocks so we do this here as well to match the behavior
		// This is important for if-else blocks where the if body is an expr but else is on the next line
		if (ts.is(Token::TypeLine))
			ts.move();

		return result;
	}
}

static Arr<Ty*> parseTypeSignature(TokenStream& ts)
{
	if (!ts.is(Token::TypeAtom, "<"))
		return Arr<Ty*>();

	Arr<Ty*> args;

	ts.eat(Token::TypeAtom, "<");

	while (!ts.is(Token::TypeAtom, ">"))
	{
		auto name = ts.eat(Token::TypeIdent);

		// TODO: verify name uniqueness
		args.push(UNION_NEW(Ty, Generic, { name.data, name.location }));

		if (!ts.is(Token::TypeAtom, ">"))
			ts.eat(Token::TypeAtom, ",");
	}

	ts.eat(Token::TypeAtom, ">");

	return args;
}

static Arr<Ty*> parseTypeArguments(TokenStream& ts)
{
	if (!ts.is(Token::TypeAtom, ".<"))
		return Arr<Ty*>();

	Arr<Ty*> args;

	ts.eat(Token::TypeAtom, ".<");

	while (!ts.is(Token::TypeAtom, ">"))
	{
		args.push(parseType(ts));

		if (!ts.is(Token::TypeAtom, ">"))
			ts.eat(Token::TypeAtom, ",");
	}

	ts.eat(Token::TypeAtom, ">");

	return args;
}

static Ty* parseTypeAscription(TokenStream& ts)
{
	if (ts.is(Token::TypeAtom, ":"))
	{
		ts.move();

		return parseType(ts);
	}
	else
		return UNION_NEW(Ty, Unknown, {});
}

static pair<Ty*, Arr<Variable*>> parseFnSignature(TokenStream& ts)
{
	Arr<Variable*> args;
	Arr<Ty*> argtys;
	bool varargs = false;

	ts.eat(Token::TypeBracket, "(");

	while (!ts.is(Token::TypeBracket, ")"))
	{
		if (ts.is(Token::TypeAtom, "..."))
		{
			ts.move();

			varargs = true;
			break;
		}

		auto argname = ts.eat(Token::TypeIdent);

		Ty* type = parseTypeAscription(ts);

		args.push(new Variable { Variable::KindArgument, argname.data, type, argname.location });
		argtys.push(type);

		if (!ts.is(Token::TypeBracket, ")"))
			ts.eat(Token::TypeAtom, ",");
	}

	ts.eat(Token::TypeBracket, ")");

	Ty* ret = parseTypeAscription(ts);

	Ty* ty = UNION_NEW(Ty, Function, { argtys, ret, varargs });

	return make_pair(ty, args);
}

static Ast* parseFn(TokenStream& ts)
{
	Location start = ts.get().location;

	ts.eat(Token::TypeIdent, "fn");

	auto sig = parseFnSignature(ts);

	Ast* body = parseBlockExpr(ts, &start);

	Variable* var = new Variable { Variable::KindFunction, Str(), sig.first, start };
	Ast* decl = UNION_NEW(Ast, FnDecl, { nullptr, start, var, Arr<Ty*>(), sig.second, 0, body });

	return UNION_NEW(Ast, Fn, { nullptr, start, int(ts.index), decl });
}

static Ast* parseFnBody(TokenStream& ts, const Location* indent)
{
	ts.eat(Token::TypeLine);

	if (ts.is(Token::TypeIdent, "llvm"))
	{
		ts.move();

		auto code = ts.eat(Token::TypeString);

		return UNION_NEW(Ast, LLVM, { nullptr, code.location, code.data });
	}

	return parseBlock(ts, indent);
}

static Ast* parseFnDecl(TokenStream& ts)
{
	Location indent = ts.get().location;

	unsigned attributes = 0;

	if (ts.is(Token::TypeIdent, "inline"))
	{
		attributes |= FnAttributeInline;
		ts.move();
	}

	if (ts.is(Token::TypeIdent, "extern"))
	{
		attributes |= FnAttributeExtern;
		ts.move();
	}

	if (ts.is(Token::TypeIdent, "builtin"))
	{
		attributes |= FnAttributeBuiltin;
		ts.move();
	}

	bool bodyImplicit = (attributes & (FnAttributeExtern | FnAttributeBuiltin)) != 0;

	ts.eat(Token::TypeIdent, "fn");

	auto name = ts.eat(Token::TypeIdent);

	auto tysig = parseTypeSignature(ts);
	auto sig = parseFnSignature(ts);

	Ast* body = bodyImplicit ? nullptr : parseFnBody(ts, &indent);

	Variable* var = new Variable { Variable::KindFunction, name.data, sig.first, name.location };
	Ast* result = UNION_NEW(Ast, FnDecl, { nullptr, Location(), var, tysig, sig.second, attributes, body });

	var->fn = result;

	return result;
}

static Ast* parseVarDecl(TokenStream& ts)
{
	ts.eat(Token::TypeIdent, "var");

	auto name = ts.eat(Token::TypeIdent);

	Ty* type = parseTypeAscription(ts);

	ts.eat(Token::TypeAtom, "=");

	Ast* expr = parseExpr(ts);

	return UNION_NEW(Ast, VarDecl, { nullptr, Location(), new Variable { Variable::KindVariable, name.data, type, name.location }, expr });
}

static Ast* parseStructDecl(TokenStream& ts)
{
	Location indent = ts.get().location;

	ts.eat(Token::TypeIdent, "struct");

	auto name = ts.eat(Token::TypeIdent);

	auto tysig = parseTypeSignature(ts);

	ts.eat(Token::TypeLine);

	Arr<StructField> fields;

	parseIndent(ts, &indent, [&]() {
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

		Ast* expr = nullptr;

		if (ts.is(Token::TypeAtom, "="))
		{
			ts.move();

			expr = parseExpr(ts);
		}

		// TODO: verify name uniqueness
		for (auto& f: fnames)
			fields.push({ f.data, f.location, ty, expr });
	});

	TyDef* def = UNION_NEW(TyDef, Struct, { tysig, fields });

	return UNION_NEW(Ast, TyDecl, { nullptr, name.location, name.data, def });
}

static Ast* parseImport(TokenStream& ts)
{
	ts.eat(Token::TypeIdent, "import");

	auto start = ts.eat(Token::TypeIdent);

	string path = start.data.str();
	Location location = start.location;

	while (ts.is(Token::TypeAtom, ".") && ts.get().location.line == location.line)
	{
		ts.move();

		auto name = ts.eat(Token::TypeIdent);

		path += ".";
		path += name.data.str();
		location = Location(location, name.location);
	}

	return UNION_NEW(Ast, Import, { nullptr, location, Str::copy(path.c_str()) });
}

static Ast* parseCall(TokenStream& ts, Ast* expr, Location start, Ast* self = nullptr)
{
	ts.eat(Token::TypeBracket, "(");

	Arr<Ast*> args;

	if (self)
		args.push(self);

	while (!ts.is(Token::TypeBracket, ")"))
	{
		args.push(parseExpr(ts));

		if (!ts.is(Token::TypeBracket, ")"))
			ts.eat(Token::TypeAtom, ",");
	}

	Location end = ts.get().location;

	ts.eat(Token::TypeBracket, ")");

	return UNION_NEW(Ast, Call, { nullptr, Location(start, end), expr, args });
}

static Ast* parseIndex(TokenStream& ts, Ast* expr)
{
	Location start = ts.get().location;

	ts.eat(Token::TypeBracket, "[");

	auto index = parseExpr(ts);

	ts.eat(Token::TypeBracket, "]");

	return UNION_NEW(Ast, Index, { nullptr, start, expr, index });
}

static Ast* parseIdent(TokenStream& ts)
{
	auto name = ts.eat(Token::TypeIdent);

	auto tyargs = parseTypeArguments(ts);

	return UNION_NEW(Ast, Ident, { nullptr, name.location, name.data, tyargs });
}

static Ast* parseMember(TokenStream& ts, Ast* expr)
{
	ts.eat(Token::TypeAtom, ".");

	auto name = ts.eat(Token::TypeIdent);

	if (ts.is(Token::TypeBracket, "(") || ts.is(Token::TypeAtom, ".<"))
	{
		// backtrack so that we can reuse parseIdent
		ts.index--;

		Ast* member = parseIdent(ts);

		return parseCall(ts, member, name.location, /* self= */ expr);
	}
	else
	{
		FieldRef field = { name.data, name.location, -1 };

		return UNION_NEW(Ast, Member, { nullptr, name.location, expr, field });
	}
}

static Ast* parseAssign(TokenStream& ts, Ast* expr)
{
	Location location = ts.get().location;

	ts.eat(Token::TypeAtom, "=");

	Ast* value = parseExpr(ts);

	return UNION_NEW(Ast, Assign, { nullptr, location, expr, value });
}

static Ast* parseIf(TokenStream& ts)
{
	Location start = ts.get().location;

	ts.eat(Token::TypeIdent, "if");

	Ast* cond = parseExpr(ts);
	Ast* thenbody = parseBlockExpr(ts, &start);
	Ast* elsebody = nullptr;

	if (ts.is(Token::TypeIdent, "else"))
	{
		int ifIndent = getLineIndent(ts, start);
		int elseIndent = getLineIndent(ts, ts.get().location);

		if (ifIndent != elseIndent)
			ts.output->panic(ts.get().location, "Invalid indentation: expected %d, got %d", ifIndent, elseIndent);

		ts.eat(Token::TypeIdent);

		elsebody = parseBlockExpr(ts, &start);
	}

	return UNION_NEW(Ast, If, { nullptr, start, cond, thenbody, elsebody });
}

static Ast* parseFor(TokenStream& ts)
{
	Location start = ts.get().location;

	ts.eat(Token::TypeIdent, "for");

	auto name = ts.eat(Token::TypeIdent);
	Variable* var = new Variable { Variable::KindVariable, name.data, UNION_NEW(Ty, Unknown, {}), name.location };

	Variable* index = nullptr;

	if (ts.is(Token::TypeAtom, ","))
	{
		ts.move();

		auto name = ts.eat(Token::TypeIdent);
		index = new Variable { Variable::KindValue, name.data, UNION_NEW(Ty, Unknown, {}), name.location };
	}

	ts.eat(Token::TypeIdent, "in");

	Ast* expr = parseExpr(ts);
	Ast* body = parseBlockExpr(ts, &start);

	return UNION_NEW(Ast, For, { nullptr, start, var, index, expr, body });
}

static Ast* parseWhile(TokenStream& ts)
{
	Location start = ts.get().location;

	ts.eat(Token::TypeIdent, "while");

	Ast* expr = parseExpr(ts);
	Ast* body = parseBlockExpr(ts, &start);

	return UNION_NEW(Ast, While, { nullptr, start, expr, body });
}

static Ast* parseLiteralArray(TokenStream& ts)
{
	Location start = ts.get().location;

	Arr<Ast*> elements;

	ts.eat(Token::TypeBracket, "[");

	while (!ts.is(Token::TypeBracket, "]"))
	{
		elements.push(parseExpr(ts));

		if (!ts.is(Token::TypeBracket, "]"))
			ts.eat(Token::TypeAtom, ",");
	}

	ts.eat(Token::TypeBracket, "]");

	return UNION_NEW(Ast, LiteralArray, { nullptr, start, elements });
}

static Ast* parseLiteralStruct(TokenStream& ts)
{
	Location start = ts.get().location;

	auto name = ts.is(Token::TypeIdent) ? ts.eat(Token::TypeIdent) : Token();
	auto tyargs = name.data.size == 0 ? Arr<Ty*>() : parseTypeArguments(ts);

	Arr<pair<FieldRef, Ast*>> fields;

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
			expr = UNION_NEW(Ast, Ident, { nullptr, fname.location, fname.data });

		FieldRef field = { fname.data, fname.location, -1 };

		// TODO: verify name uniqueness
		fields.push(make_pair(field, expr));

		if (!ts.is(Token::TypeBracket, "}"))
			ts.eat(Token::TypeAtom, ",");
	}

	ts.eat(Token::TypeBracket, "}");

	Ty* ty = (name.data.size == 0) ? nullptr : UNION_NEW(Ty, Instance, { name.data, name.location, tyargs });

	return UNION_NEW(Ast, LiteralStruct, { ty, start, name.data, fields });
}

static Ast* parseNumber(TokenStream& ts)
{
	auto value = ts.eat(Token::TypeNumber);
	string contents = value.data.str();

	{
		errno = 0;

		char* end = 0;
		long long valueInteger = strtoll(contents.c_str(), &end, 10);

		if (*end == 0)
		{
			if (errno)
				ts.output->panic(value.location, "Invalid integer literal '%s'", contents.c_str());

			return UNION_NEW(Ast, LiteralInteger, { nullptr, value.location, valueInteger });
		}
	}

	{
		errno = 0;

		char* end = 0;
		double valueDouble = strtod(contents.c_str(), &end);

		if (*end == 0)
		{
			if (errno)
				ts.output->panic(value.location, "Invalid floating-point literal '%s'", contents.c_str());

			return UNION_NEW(Ast, LiteralFloat, { nullptr, value.location, valueDouble });
		}
	}

	ts.output->panic(value.location, "Invalid number literal '%s'", contents.c_str());
}

static Ast* parseTerm(TokenStream& ts)
{
	if (ts.is(Token::TypeIdent, "true"))
	{
		auto value = ts.eat(Token::TypeIdent);

		return UNION_NEW(Ast, LiteralBool, { nullptr, value.location, true });
	}

	if (ts.is(Token::TypeIdent, "false"))
	{
		auto value = ts.eat(Token::TypeIdent);

		return UNION_NEW(Ast, LiteralBool, { nullptr, value.location, false });
	}

	if (ts.is(Token::TypeNumber))
	{
		return parseNumber(ts);
	}

	if (ts.is(Token::TypeString))
	{
		auto value = ts.eat(Token::TypeString);

		return UNION_NEW(Ast, LiteralString, { nullptr, value.location, value.data });
	}

	if (ts.is(Token::TypeBracket, "{") || (ts.is(Token::TypeIdent) && ts.get(1).type == Token::TypeBracket && ts.get(1).data == "{"))
	{
		return parseLiteralStruct(ts);
	}

	if (ts.is(Token::TypeBracket, "["))
	{
		return parseLiteralArray(ts);
	}

	if (ts.is(Token::TypeIdent))
	{
		return parseIdent(ts);
	}

	if (ts.is(Token::TypeBracket, "("))
	{
		Location start = ts.get().location;

		ts.move();

		Arr<Ast*> fields;

		while (!ts.is(Token::TypeBracket, ")"))
		{
			fields.push(parseExpr(ts));

			if (!ts.is(Token::TypeBracket, ")"))
				ts.eat(Token::TypeAtom, ",");
		}

		Location end = ts.get().location;

		ts.eat(Token::TypeBracket, ")");

		if (fields.size == 0)
			return UNION_NEW(Ast, LiteralVoid, { nullptr, Location(start, end) });
		else if (fields.size == 1)
			return fields[0];
		else
			return UNION_NEW(Ast, LiteralTuple, { nullptr, Location(start, end), fields });
	}

	auto t = ts.get();

	ts.output->panic(t.location, "Unexpected token %s", tokenName(t).c_str());
}

template <typename Op>
struct OpDef
{
	int priority;
	Op op;
	const char* opname;
};

static OpDef<UnaryOp> parseUnaryOp(TokenStream& ts)
{
	if (ts.is(Token::TypeAtom, "+")) return { 1, UnaryOpPlus, "operatorPlus" };
	if (ts.is(Token::TypeAtom, "-")) return { 1, UnaryOpMinus, "operatorMinus" };

	if (ts.is(Token::TypeIdent, "not")) return { 1, UnaryOpNot, nullptr };
	if (ts.is(Token::TypeAtom, "*")) return { 1, UnaryOpDeref, nullptr };
	if (ts.is(Token::TypeIdent, "new")) return { 1, UnaryOpNew, nullptr };

	return { 0, UnaryOpNot, nullptr };
}

Ast* lowerUnaryOp(const OpDef<UnaryOp>& def, Ast* expr, Location location)
{
	if (def.opname)
	{
		Ast* ident = UNION_NEW(Ast, Ident, { nullptr, location, Str(def.opname) });

		return UNION_NEW(Ast, Call, { nullptr, location, ident, { expr } });
	}
	else
		return UNION_NEW(Ast, Unary, { nullptr, location, def.op, expr });
}

static OpDef<BinaryOp> parseBinaryOp(TokenStream& ts)
{
	if (ts.is(Token::TypeAtom, "*%")) return { 7, BinaryOpMultiplyWrap, "operatorMultiplyWrap" };
	if (ts.is(Token::TypeAtom, "*")) return { 7, BinaryOpMultiply, "operatorMultiply" };
	if (ts.is(Token::TypeAtom, "/")) return { 7, BinaryOpDivide, "operatorDivide" };
	if (ts.is(Token::TypeAtom, "%")) return { 7, BinaryOpModulo, "operatorModulo" };
	if (ts.is(Token::TypeAtom, "+%")) return { 6, BinaryOpAddWrap, "operatorAddWrap" };
	if (ts.is(Token::TypeAtom, "+")) return { 6, BinaryOpAdd, "operatorAdd" };
	if (ts.is(Token::TypeAtom, "-%")) return { 6, BinaryOpSubtractWrap, "operatorSubtractWrap" };
	if (ts.is(Token::TypeAtom, "-")) return { 6, BinaryOpSubtract, "operatorSubtract" };
	if (ts.is(Token::TypeAtom, "<")) return { 5, BinaryOpLess, "operatorLess" };
	if (ts.is(Token::TypeAtom, "<=")) return { 5, BinaryOpLessEqual, "operatorLessEqual" };
	if (ts.is(Token::TypeAtom, ">")) return { 5, BinaryOpGreater, "operatorGreater" };
	if (ts.is(Token::TypeAtom, ">=")) return { 5, BinaryOpGreaterEqual, "operatorGreaterEqual" };
	if (ts.is(Token::TypeAtom, "==")) return { 4, BinaryOpEqual, "operatorEqual" };
	if (ts.is(Token::TypeAtom, "!=")) return { 4, BinaryOpNotEqual, "operatorNotEqual" };

	if (ts.is(Token::TypeIdent, "and")) return { 3, BinaryOpAnd, nullptr };
	if (ts.is(Token::TypeIdent, "or")) return { 2, BinaryOpOr, nullptr };

	return { 0, BinaryOpOr, nullptr };
}

Ast* lowerBinaryOp(const OpDef<BinaryOp>& def, Ast* left, Ast* right, Location location)
{
	if (def.opname)
	{
		Ast* ident = UNION_NEW(Ast, Ident, { nullptr, location, Str(def.opname) });

		return UNION_NEW(Ast, Call, { nullptr, location, ident, { left, right } });
	}
	else
		return UNION_NEW(Ast, Binary, { nullptr, location, def.op, left, right });
}

static Ast* parsePrimary(TokenStream& ts)
{
	auto uop = parseUnaryOp(ts);

	if (uop.priority)
	{
		Location start = ts.get().location;

		ts.move();

		Ast* expr = parsePrimary(ts);

		return lowerUnaryOp(uop, expr, start);
	}

	if (ts.is(Token::TypeIdent, "extern") || ts.is(Token::TypeIdent, "builtin") || ts.is(Token::TypeIdent, "inline"))
		return parseFnDecl(ts);

	if (ts.is(Token::TypeIdent, "fn"))
		return ts.get(1).type == Token::TypeIdent ? parseFnDecl(ts) : parseFn(ts);

	if (ts.is(Token::TypeIdent, "var"))
		return parseVarDecl(ts);

	if (ts.is(Token::TypeIdent, "struct"))
		return parseStructDecl(ts);

	if (ts.is(Token::TypeIdent, "import"))
		return parseImport(ts);

	if (ts.is(Token::TypeIdent, "if"))
		return parseIf(ts);

	if (ts.is(Token::TypeIdent, "for"))
		return parseFor(ts);

	if (ts.is(Token::TypeIdent, "while"))
		return parseWhile(ts);

	Location start = ts.get().location;

	Ast* term = parseTerm(ts);

	for (;;)
	{
		if (ts.is(Token::TypeBracket, "("))
			term = parseCall(ts, term, start);
		else if (ts.is(Token::TypeBracket, "["))
			term = parseIndex(ts, term);
		else if (ts.is(Token::TypeAtom, "."))
			term = parseMember(ts, term);
		else
			break;
	}

	// TODO: is this the right place?
	if (ts.is(Token::TypeAtom, "="))
		term = parseAssign(ts, term);

	return term;
}

Ast* parseExprClimb(TokenStream& ts, Ast* left, int limit)
{
	auto op = parseBinaryOp(ts);

	while (op.priority && op.priority >= limit)
	{
		Location start = ts.get().location;

		ts.move();

		Ast* right = parsePrimary(ts);

		auto nextop = parseBinaryOp(ts);

		while (nextop.priority && nextop.priority > op.priority)
		{
			right = parseExprClimb(ts, right, nextop.priority);

			nextop = parseBinaryOp(ts);
		}

		left = lowerBinaryOp(op, left, right, start);

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

Ast* parse(Output& output, const Tokens& tokens, const Str& moduleName)
{
	Ast* result = parse(output, tokens);

	if (tokens.tokens.size == 0)
		return result;

	return UNION_NEW(Ast, Module, { nullptr, tokens.tokens[0].location, moduleName, result });
}