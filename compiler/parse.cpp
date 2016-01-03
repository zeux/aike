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

	Ast* body = parseBlock(ts, &start);

	Variable* var = new Variable { Variable::KindFunction, Str(), sig.first, start };
	Ast* decl = UNION_NEW(Ast, FnDecl, { var, Arr<Ty*>(), sig.second, 0, body });

	return UNION_NEW(Ast, Fn, { start, int(ts.index), decl });
}

static Ast* parseFnBody(TokenStream& ts, const Location* indent)
{
	if (ts.is(Token::TypeIdent, "llvm"))
	{
		ts.move();

		auto code = ts.eat(Token::TypeString);

		return UNION_NEW(Ast, LLVM, { code.location, code.data });
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
	Ast* result = UNION_NEW(Ast, FnDecl, { var, tysig, sig.second, attributes, body });

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

	return UNION_NEW(Ast, VarDecl, { new Variable { Variable::KindVariable, name.data, type, name.location }, expr });
}

static Ast* parseStructDecl(TokenStream& ts)
{
	Location indent = ts.get().location;

	ts.eat(Token::TypeIdent, "struct");

	auto name = ts.eat(Token::TypeIdent);

	auto tysig = parseTypeSignature(ts);

	Arr<StructField> fields;

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

	return UNION_NEW(Ast, TyDecl, { name.data, name.location, def });
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

	return UNION_NEW(Ast, Import, { Str::copy(path.c_str()), location });
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

	return UNION_NEW(Ast, Call, { expr, args, Location(start, end) });
}

static Ast* parseIndex(TokenStream& ts, Ast* expr)
{
	Location start = ts.get().location;

	ts.eat(Token::TypeBracket, "[");

	auto index = parseExpr(ts);

	ts.eat(Token::TypeBracket, "]");

	return UNION_NEW(Ast, Index, { expr, index, start });
}

static Ast* parseIdent(TokenStream& ts)
{
	auto name = ts.eat(Token::TypeIdent);

	auto tyargs = parseTypeArguments(ts);

	return UNION_NEW(Ast, Ident, { name.data, name.location, nullptr, tyargs });
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

		return UNION_NEW(Ast, Member, { expr, name.location, nullptr, field });
	}
}

static Ast* parseAssign(TokenStream& ts, Ast* expr)
{
	Location location = ts.get().location;

	ts.eat(Token::TypeAtom, "=");

	Ast* value = parseExpr(ts);

	return UNION_NEW(Ast, Assign, { location, expr, value });
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

	return UNION_NEW(Ast, If, { cond, thenbody, elsebody, start });
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
	Ast* body = parseBlock(ts, &start);

	return UNION_NEW(Ast, For, { start, var, index, expr, body });
}

static Ast* parseWhile(TokenStream& ts)
{
	Location start = ts.get().location;

	ts.eat(Token::TypeIdent, "while");

	Ast* expr = parseExpr(ts);
	Ast* body = parseBlock(ts, &start);

	return UNION_NEW(Ast, While, { start, expr, body });
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

	Ty* ty = UNION_NEW(Ty, Array, { UNION_NEW(Ty, Unknown, {}) });

	return UNION_NEW(Ast, LiteralArray, { start, ty, elements });
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
			expr = UNION_NEW(Ast, Ident, { fname.data, fname.location, nullptr });

		FieldRef field = { fname.data, fname.location, -1 };

		// TODO: verify name uniqueness
		fields.push(make_pair(field, expr));

		if (!ts.is(Token::TypeBracket, "}"))
			ts.eat(Token::TypeAtom, ",");
	}

	ts.eat(Token::TypeBracket, "}");

	Ty* ty = name.data.size == 0 ? UNION_NEW(Ty, Unknown, {}) : UNION_NEW(Ty, Instance, { name.data, name.location, tyargs });

	return UNION_NEW(Ast, LiteralStruct, { name.data, start, ty, fields });
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

			return UNION_NEW(Ast, LiteralInteger, { valueInteger, value.location });
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

			return UNION_NEW(Ast, LiteralFloat, { valueDouble, value.location });
		}
	}

	ts.output->panic(value.location, "Invalid number literal '%s'", contents.c_str());
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
		return parseNumber(ts);
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
		ts.move();

		Ast* result = parseExpr(ts);

		ts.eat(Token::TypeBracket, ")");

		return result;
	}

	auto t = ts.get();

	ts.output->panic(t.location, "Unexpected token '%s'", t.data.str().c_str());
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
		Ast* ident = UNION_NEW(Ast, Ident, { Str(def.opname), location, nullptr });

		return UNION_NEW(Ast, Call, { ident, { expr }, location });
	}
	else
		return UNION_NEW(Ast, Unary, { def.op, expr, location });
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
		Ast* ident = UNION_NEW(Ast, Ident, { Str(def.opname), location, nullptr });

		return UNION_NEW(Ast, Call, { ident, { left, right }, location });
	}
	else
		return UNION_NEW(Ast, Binary, { def.op, left, right, location });
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

	return UNION_NEW(Ast, Module, { moduleName, tokens.tokens[0].location, result });
}