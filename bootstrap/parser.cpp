#include "parser.hpp"

#include "lexer.hpp"
#include "output.hpp"

#include <exception>
#include <cassert>

inline bool iskeyword(Lexer& lexer, const char* expected)
{
	return lexer.current.type == LexKeyword && lexer.current.contents == expected;
}

inline bool islower(const Location& lhs, const Location& rhs)
{
	return lhs.column < rhs.column;
}

inline bool issameline(const Location& lhs, const Location& rhs)
{
	return lhs.line == rhs.line;
}

SynType* parseType(Lexer& lexer);

SynBase* parseExpr(Lexer& lexer);
SynBase* parseBlock(Lexer& lexer);

SynBase* parseTerm(Lexer& lexer)
{
	if (lexer.current.type == LexOpenBrace)
	{
		Location location = lexer.current.location;

		movenext(lexer);

		if (lexer.current.type == LexCloseBrace)
		{
			SynBase* result = new SynUnit(lexer.current.location);
			movenext(lexer);

			return result;
		}

		std::vector<SynBase*> elements;

		while (lexer.current.type != LexCloseBrace)
		{
			if (!elements.empty())
			{
				if (lexer.current.type != LexComma)
					errorf(lexer.current.location, "Expected ',' after previous tuple element");
				movenext(lexer);
			}

			elements.push_back(parseExpr(lexer));
		}

		movenext(lexer);

		if (elements.size() == 1)
			return elements[0];

		return new SynTupleLiteral(location, elements);
	}
	else if (lexer.current.type == LexOpenBracket)
	{
		Location location = lexer.current.location;

		movenext(lexer);

		std::vector<SynBase*> elements;

		while (lexer.current.type != LexCloseBracket)
		{
			if (!elements.empty())
			{
				if (lexer.current.type != LexComma)
					errorf(lexer.current.location, "Expected ',' after previous array element");
				movenext(lexer);
			}

			elements.push_back(parseExpr(lexer));
		}

		movenext(lexer);

		return new SynArrayLiteral(location, elements);
	}
	else if (lexer.current.type == LexNumber)
	{
		SynBase* result = new SynNumberLiteral(lexer.current.location, lexer.current.number);
		movenext(lexer);
		return result;
	}
	else if (lexer.current.type == LexIdentifier)
	{
		SynBase* result = new SynVariableReference(lexer.current.location, lexer.current.contents);
		movenext(lexer);
		return result;
	}
	else if (iskeyword(lexer, "true"))
	{
		SynBase* result = new SynBooleanLiteral(lexer.current.location, true);
		movenext(lexer);
		return result;
	}
	else if (iskeyword(lexer, "false"))
	{
		SynBase* result = new SynBooleanLiteral(lexer.current.location, false);
		movenext(lexer);
		return result;
	}
	else if (lexer.current.type == LexCharacter)
	{
		if (lexer.current.contents.empty())
			errorf(lexer.current.location, "Character missing");
		if (lexer.current.contents.size() > 1)
			errorf(lexer.current.location, "Multicharacter literals are not supported");

		SynBase* result = new SynCharacterLiteral(lexer.current.location, lexer.current.contents[0]);
		movenext(lexer);
		return result;
	}
	else if (lexer.current.type == LexString)
	{
		Location location = lexer.current.location;

		std::vector<SynBase*> elements;
		for (size_t i = 0; i < lexer.current.contents.size(); ++i)
			elements.push_back(new SynCharacterLiteral(location, lexer.current.contents[i]));

		movenext(lexer);

		return new SynArrayLiteral(location, elements);
	}
	else
	{
		errorf(lexer.current.location, "Unexpected lexeme %d", lexer.current.type);
	}
}

SynUnaryOpType getUnaryOp(Lexeme lexeme)
{
	switch (lexeme.type)
	{
	case LexPlus: return SynUnaryOpPlus;
	case LexMinus: return SynUnaryOpMinus;
	case LexExclamation: return SynUnaryOpRefGet;
	case LexKeyword:
		if (lexeme.contents == "not")
			return SynUnaryOpNot;
	default: return SynUnaryOpUnknown;
	}
}

SynBinaryOpType getBinaryOp(Lexeme lexeme)
{
	switch (lexeme.type)
	{
	case LexPlus: return SynBinaryOpAdd;
	case LexMinus: return SynBinaryOpSubtract;
	case LexMultiply: return SynBinaryOpMultiply;
	case LexDivide: return SynBinaryOpDivide;
	case LexLess: return SynBinaryOpLess;
	case LexLessEqual: return SynBinaryOpLessEqual;
	case LexGreater: return SynBinaryOpGreater;
	case LexGreaterEqual: return SynBinaryOpGreaterEqual;
	case LexEqualEqual: return SynBinaryOpEqual;
	case LexNotEqual: return SynBinaryOpNotEqual;
	case LexColonEqual: return SynBinaryOpRefSet;
	case LexKeyword:
		if (lexeme.contents == "and")
			return SynBinaryOpAnd;
		if (lexeme.contents == "or")
			return SynBinaryOpOr;
	default: return SynBinaryOpUnknown;
	}
}

int getBinaryOpPrecedence(SynBinaryOpType op)
{
	switch (op)
	{
	case SynBinaryOpMultiply: return 7;
	case SynBinaryOpDivide: return 7;
	case SynBinaryOpAdd: return 6;
	case SynBinaryOpSubtract: return 6;
	case SynBinaryOpLess: return 5;
	case SynBinaryOpLessEqual: return 5;
	case SynBinaryOpGreater: return 5;
	case SynBinaryOpGreaterEqual: return 5;
	case SynBinaryOpEqual: return 4;
	case SynBinaryOpNotEqual: return 4;
	case SynBinaryOpAnd: return 3;
	case SynBinaryOpOr: return 2;
	case SynBinaryOpRefSet: return 1;
	default: return 0;
	}
}

SynIdentifier parseIdentifier(Lexer& lexer)
{
	if (lexer.current.type != LexIdentifier)
		errorf(lexer.current.location, "Expected identifier");

	Location location = lexer.current.location;
	std::string name = lexer.current.contents;
	movenext(lexer);

	return SynIdentifier(name, location);
}

std::vector<SynTypeGeneric*> parseGenericTypeList(Lexer& lexer)
{
	if (lexer.current.type != LexLess)
		return std::vector<SynTypeGeneric*>();

	movenext(lexer);

	std::vector<SynTypeGeneric*> list;

	while (lexer.current.type != LexGreater)
	{
		if (lexer.current.type != LexIdentifierGeneric)
			errorf(lexer.current.location, "Expected generic identifier");

		SynIdentifier var(lexer.current.contents, lexer.current.location);

		list.push_back(new SynTypeGeneric(var));

		movenext(lexer);

		if (lexer.current.type == LexComma)
			movenext(lexer);
		else if (lexer.current.type == LexGreater)
			;
		else
			errorf(lexer.current.location, "Expected ',' or '>'");
	}

	movenext(lexer);

	return list;
}

std::vector<SynType*> parseGenericInstantiation(Lexer& lexer)
{
	if (lexer.current.type != LexLess)
		return std::vector<SynType*>();

	movenext(lexer);

	std::vector<SynType*> list;

	while (lexer.current.type != LexGreater)
	{
		list.push_back(parseType(lexer));

		if (lexer.current.type == LexComma)
			movenext(lexer);
		else if (lexer.current.type == LexGreater)
			;
		else
			errorf(lexer.current.location, "Expected ',' or '>'");
	}

	movenext(lexer);

	return list;
}

SynType* parseTypeBraced(Lexer& lexer)
{
	assert(lexer.current.type == LexOpenBrace);
	movenext(lexer);

	std::vector<SynType*> list;

	if (lexer.current.type != LexCloseBrace)
	{
		list.push_back(parseType(lexer));

		while (lexer.current.type == LexComma)
		{
			movenext(lexer);

			list.push_back(parseType(lexer));
		}
	}

	if (lexer.current.type != LexCloseBrace)
		errorf(lexer.current.location, "Expected ')' after '('");
	movenext(lexer);

	if (lexer.current.type == LexArrow)
	{
		movenext(lexer);

		return new SynTypeFunction(parseType(lexer), list);
	}
	else
	{
		if (list.size() != 1)
			return new SynTypeTuple(list);

		return list[0];
	}
}

SynType* parseType(Lexer& lexer)
{
	SynType* type;

	if (lexer.current.type == LexOpenBrace)
	{
		type = parseTypeBraced(lexer);
	}
	else if (lexer.current.type == LexIdentifierGeneric)
	{
		SynIdentifier ident(lexer.current.contents, lexer.current.location);
		movenext(lexer);

		type = new SynTypeGeneric(ident);
	}
	else
	{
		SynIdentifier ident = parseIdentifier(lexer);
		std::vector<SynType*> generics = parseGenericInstantiation(lexer);

		type = new SynTypeIdentifier(ident, generics);
	}
	
	if (lexer.current.type == LexOpenBracket)
	{
		movenext(lexer);

		if (lexer.current.type != LexCloseBracket)
			errorf(lexer.current.location, "Expected ']' after '['");
		movenext(lexer);

		return new SynTypeArray(type);
	}
	else
	{
		return type;
	}
}

SynTypeRecord* parseTypeRecord(Lexer& lexer)
{
	if (lexer.current.type != LexOpenCurlyBrace) errorf(lexer.current.location, "Expected '{'");
	movenext(lexer);

	std::vector<SynTypedVar> members;

	size_t prev_line = lexer.current.location.line;

	while (lexer.current.type != LexCloseCurlyBrace)
	{
		if (!members.empty())
		{
			if (lexer.current.type != LexSemicolon && lexer.current.location.line == prev_line)
				errorf(lexer.current.location, "Expected ';' or a newline after previous type member");
			if (lexer.current.type == LexSemicolon)
				movenext(lexer);
		}

		prev_line = lexer.current.location.line;

		SynIdentifier name = parseIdentifier(lexer);

		if (lexer.current.type != LexColon) errorf(lexer.current.location, "Expected ': type' after member name");
		movenext(lexer);

		SynType* type = parseType(lexer);

		members.push_back(SynTypedVar(name, type));
	}

	movenext(lexer);

	return new SynTypeRecord(members);
}

SynTypedVar parseTypedVar(Lexer& lexer)
{
	SynIdentifier argname = parseIdentifier(lexer);
	SynType* argtype = 0;

	if (lexer.current.type == LexColon)
	{
		movenext(lexer);

		argtype = parseType(lexer);
	}

	return SynTypedVar(argname, argtype);
}

std::vector<SynTypedVar> parseFunctionArguments(Lexer& lexer)
{
	assert(lexer.current.type == LexOpenBrace);
	movenext(lexer);

	std::vector<SynTypedVar> args;

	while (lexer.current.type != LexCloseBrace)
	{
		SynTypedVar var = parseTypedVar(lexer);

		args.push_back(var);

		if (lexer.current.type == LexComma)
			movenext(lexer);
		else if (lexer.current.type == LexCloseBrace)
			;
		else
			errorf(lexer.current.location, "Expected ',' or ')'");
	}

	movenext(lexer);

	return args;
}

SynBase* parseLetFunc(Lexer& lexer, const SynIdentifier& name, const Location& start)
{
	std::vector<SynTypedVar> args = parseFunctionArguments(lexer);

	SynType* rettype = 0;

	if (lexer.current.type == LexColon)
	{
		movenext(lexer);
		
		rettype = parseType(lexer);
	}

	if (lexer.current.type != LexEqual) errorf(lexer.current.location, "Expected '='");

	movenext(lexer);

	if (!islower(start, lexer.current.location))
		errorf(lexer.current.location, "Incorrect identation: this token is offside of context at (%d,%d). Indent this token further.", start.line, start.column);

	return new SynLetFunc(name.location, name, rettype, args, parseBlock(lexer));
}

SynBase* parseExternFunc(Lexer& lexer)
{
	assert(iskeyword(lexer, "extern"));
	movenext(lexer);

	SynIdentifier name = parseIdentifier(lexer);

	std::vector<SynTypedVar> args = parseFunctionArguments(lexer);

	for (size_t i = 0; i < args.size(); ++i)
		if (!args[i].type)
			errorf(args[i].name.location, "Extern function '%s': type declaration missing for argument '%s'", name.name.c_str(), args[i].name.name.c_str());

	if (lexer.current.type != LexColon)
		errorf(name.location, "Extern function '%s': type declaration missing for return type", name.name.c_str());

	movenext(lexer);
	
	SynType* rettype = parseType(lexer);

	return new SynExternFunc(name.location, name, rettype, args);
}

SynBase* parseAnonymousFunc(Lexer& lexer)
{
	Location start = lexer.current.location;

	assert(iskeyword(lexer, "fun"));
	movenext(lexer);

	std::vector<SynTypedVar> args;

	SynType* rettype = 0;

	if (lexer.current.type == LexOpenBrace)
	{
		args = parseFunctionArguments(lexer);

		if (lexer.current.type == LexColon)
		{
			movenext(lexer);
		
			rettype = parseType(lexer);
		}
	}
	else if (lexer.current.type == LexIdentifier)
	{
		args.push_back(SynTypedVar(parseIdentifier(lexer), 0));
	}

	if (lexer.current.type != LexArrow) errorf(lexer.current.location, "Expected '->'");
	movenext(lexer);

	SynBase* body = parseBlock(lexer);

	return new SynLetFunc(start, SynIdentifier("", start), rettype, args, body);
}

SynBase* parseLet(Lexer& lexer)
{
	Location start = lexer.current.location;

	assert(iskeyword(lexer, "let"));
	movenext(lexer);

	std::vector<SynTypedVar> vars;

	Location location = lexer.current.location;

	if (lexer.current.type == LexOpenBrace)
	{
		movenext(lexer);

		while (lexer.current.type != LexCloseBrace)
		{
			if (!vars.empty())
			{
				if (lexer.current.type != LexComma)
					errorf(lexer.current.location, "Expected ',' after previous name");
				movenext(lexer);
			}

			SynIdentifier name = parseIdentifier(lexer);

			SynType* type = 0;

			if (lexer.current.type == LexColon)
			{
				movenext(lexer);

				type = parseType(lexer);
			}
			
			for (size_t i = 0; i < vars.size(); ++i)
			{
				if (name.name != "_" && vars[i].name.name == name.name)
					errorf(name.location, "This name is alredy used in this let expression");
			}

			vars.push_back(SynTypedVar(name, type));
		}

		movenext(lexer);
	}
	else
	{
		SynIdentifier name = parseIdentifier(lexer);

		SynType* type = 0;

		if (lexer.current.type == LexColon)
		{
			movenext(lexer);

			type = parseType(lexer);
		}

		vars.push_back(SynTypedVar(name, type));
	}

	if (lexer.current.type == LexOpenBrace)
	{
		if (vars.size() > 1)
			errorf(lexer.current.location, "unexpected '(' after variable name list");
		else if(vars[0].type)
			errorf(lexer.current.location, "unexpected '(' after variable type");

		return parseLetFunc(lexer, vars[0].name, start);
	}

	if (lexer.current.type != LexEqual) errorf(lexer.current.location, "Expected '='");

	movenext(lexer);

	if (!islower(start, lexer.current.location))
		errorf(lexer.current.location, "Incorrect identation: this token is offside of context at (%d,%d). Indent this token further.", start.line, start.column);

	if (vars.size() == 1)
		return new SynLetVar(location, vars[0], parseBlock(lexer));

	return new SynLetVars(location, vars, parseBlock(lexer));
}

SynBase* parseIfThenElse(Lexer& lexer)
{
	Location location = lexer.current.location;
	assert(iskeyword(lexer, "if"));
	movenext(lexer);

	SynBase* cond = parseExpr(lexer);

	if (!iskeyword(lexer, "then")) errorf(lexer.current.location, "Expected 'then'");
	movenext(lexer);

	SynBase* thenbody = parseBlock(lexer);

	SynBase* elsebody =
		iskeyword(lexer, "else")
		? (movenext(lexer), parseBlock(lexer))
		: new SynUnit(lexer.current.location);

	return new SynIfThenElse(location, cond, thenbody, elsebody);
}

SynBase* parseForInDo(Lexer& lexer)
{
	Location location = lexer.current.location;
	assert(iskeyword(lexer, "for"));
	movenext(lexer);

	SynTypedVar var = parseTypedVar(lexer);

	if (!iskeyword(lexer, "in")) errorf(lexer.current.location, "Expected 'in' after array element name");
	movenext(lexer);

	SynBase* arr = parseExpr(lexer);
	SynBase* end = 0;

	if (lexer.current.type == LexPointPoint)
	{
		movenext(lexer);
		end = parseExpr(lexer);
	}

	if (!iskeyword(lexer, "do")) errorf(lexer.current.location, "Expected 'do' after array expression");
	movenext(lexer);

	if (end)
		return new SynForInRangeDo(location, var, arr, end, parseBlock(lexer));

	return new SynForInDo(location, var, arr, parseBlock(lexer));
}

SynBase* parseWhileDo(Lexer& lexer)
{
	Location location = lexer.current.location;
	assert(iskeyword(lexer, "while"));
	movenext(lexer);

	SynBase* condition = parseExpr(lexer);

	if (!iskeyword(lexer, "do")) errorf(lexer.current.location, "Expected 'do' after condition");
	movenext(lexer);

	return new SynWhileDo(location, condition, parseBlock(lexer));
}

SynMatch* parseMatchPattern(Lexer& lexer)
{
	// Check literal value matches
	if (lexer.current.type == LexMinus || lexer.current.type == LexPlus || lexer.current.type == LexNumber)
	{
		bool negative = lexer.current.type == LexMinus;
		if (lexer.current.type == LexMinus || lexer.current.type == LexPlus)
			movenext(lexer);

		SynMatch* result = new SynMatchNumber(lexer.current.location, lexer.current.number * (negative ? -1 : 1));
		movenext(lexer);
		return result;
	}

	if (iskeyword(lexer, "true"))
	{
		SynMatch* result = new SynMatchBoolean(lexer.current.location, true);
		movenext(lexer);
		return result;
	}

	if (iskeyword(lexer, "false"))
	{
		SynMatch* result = new SynMatchBoolean(lexer.current.location, false);
		movenext(lexer);
		return result;
	}

	if (lexer.current.type == LexCharacter)
	{
		if (lexer.current.contents.empty())
			errorf(lexer.current.location, "Character missing");
		if (lexer.current.contents.size() > 1)
			errorf(lexer.current.location, "Multicharacter literals are not supported");

		SynMatch* result = new SynMatchCharacter(lexer.current.location, lexer.current.contents[0]);
		movenext(lexer);
		return result;
	}

	if (lexer.current.type == LexString)
	{
		Location location = lexer.current.location;

		std::vector<SynMatch*> elements;
		for (size_t i = 0; i < lexer.current.contents.size(); ++i)
			elements.push_back(new SynMatchCharacter(location, lexer.current.contents[i]));

		movenext(lexer);

		return new SynMatchArray(location, elements);
	}

	// Check placeholder, type and member matches
	if (lexer.current.type == LexIdentifier)
	{
		Location location = lexer.current.location;

		// | _ ->
		if (lexer.current.contents == "_")
		{
			movenext(lexer);
			return new SynMatchPlaceholderUnnamed(location);
		}

		SynIdentifier identifier = parseIdentifier(lexer);

		// | vec2 x
		if (lexer.current.type == LexIdentifier)
		{
			SynIdentifier alias = parseIdentifier(lexer);

			return new SynMatchTypeSimple(location, identifier, alias);
		}

		// | vec2(x, y)
		if (lexer.current.type == LexOpenBrace)
		{
			movenext(lexer);

			std::vector<SynIdentifier> arg_names;
			std::vector<SynMatch*> arg_values;
			while (lexer.current.type != LexCloseBrace)
			{
				if (!arg_values.empty())
				{
					if (lexer.current.type != LexComma)
						errorf(lexer.current.location, "Expected ',' after previous member pattern");
					movenext(lexer);
				}

				// Try to parse a named argument
				if (lexer.current.type == LexIdentifier)
				{
					Lexer state = capturestate(lexer);

					SynIdentifier id = parseIdentifier(lexer);

					if (lexer.current.type == LexEqual)
					{
						movenext(lexer);

						arg_names.push_back(id);
					}
					else
					{
						restorestate(lexer, state);
					}
				}

				arg_values.push_back(parseMatchPattern(lexer));
			}

			if (!arg_names.empty() && arg_names.size() != arg_values.size())
				errorf(lexer.current.location, "Named and unnamed function arguments are not allowed to be mixed in a single call");

			movenext(lexer);

			return new SynMatchTypeComplex(location, identifier, arg_values, arg_names);
		}

		SynType* type = 0;

		if (lexer.current.type == LexColon)
		{
			movenext(lexer);

			type = parseType(lexer);
		}

		return new SynMatchPlaceholder(location, SynTypedVar(identifier, type));
	}

	// Check array matches
	if (lexer.current.type == LexOpenBracket)
	{
		Location location = lexer.current.location;

		movenext(lexer);

		std::vector<SynMatch*> elements;
		while (lexer.current.type != LexCloseBracket)
		{
			if (!elements.empty())
			{
				if (lexer.current.type != LexComma)
					errorf(lexer.current.location, "Expected ',' after previous array element");
				movenext(lexer);
			}

			elements.push_back(parseMatchPattern(lexer));
		}

		movenext(lexer);

		return new SynMatchArray(location, elements);
	}

	// Check tuple matches
	if (lexer.current.type == LexOpenBrace)
	{
		Location location = lexer.current.location;

		movenext(lexer);

		std::vector<SynMatch*> elements;
		while (lexer.current.type != LexCloseBrace)
		{
			if (!elements.empty())
			{
				if (lexer.current.type != LexComma)
					errorf(lexer.current.location, "Expected ',' after previous tuple element");
				movenext(lexer);
			}

			elements.push_back(parseMatchPattern(lexer));
		}

		movenext(lexer);

		if (elements.empty())
			errorf(location, "unit cannot be matched");

		if (elements.size() == 1)
			return elements[0];

		return new SynMatchTuple(location, elements);
	}

	errorf(lexer.current.location, "Unexpected lexeme %d", lexer.current.type);
}

SynBase* parseMatchWith(Lexer& lexer)
{
	Location start = lexer.current.location;

	assert(iskeyword(lexer, "match"));
	movenext(lexer);

	SynBase* variable = parseExpr(lexer);

	if (!iskeyword(lexer, "with")) errorf(lexer.current.location, "Expected 'with' after expression");
	movenext(lexer);

	std::vector<SynMatch*> variants;
	std::vector<SynBase*> expressions;

	if (islower(lexer.current.location, start))
		errorf(lexer.current.location, "Incorrect identation: this token is offside of context at (%d,%d). Indent this token further.", start.line, start.column);

	// Allow to skip first '|' as long as the identifier is on the same line
	while (!islower(lexer.current.location, start) && (lexer.current.type == LexPipe || (variants.empty() && issameline(start, lexer.current.location))))
	{
		if (lexer.current.type == LexPipe)
			movenext(lexer);

		Location location = lexer.current.location;

		SynMatch *match = parseMatchPattern(lexer);
		std::vector<SynMatch*> options;

		if (lexer.current.type == LexPipe)
			options.push_back(match);

		while (lexer.current.type == LexPipe)
		{
			movenext(lexer);

			options.push_back(parseMatchPattern(lexer));
		}

		if (!options.empty())
			match = new SynMatchOr(location, options);

		if (iskeyword(lexer, "if"))
		{
			movenext(lexer);
			match = new SynMatchIf(location, match, parseExpr(lexer));
		}

		variants.push_back(match);

		if (lexer.current.type != LexArrow) errorf(lexer.current.location, "Expected '->'");
			movenext(lexer);

		expressions.push_back(parseBlock(lexer));
	}

	return new SynMatchWith(start, variable, variants, expressions);
}

SynBase* parseTypeDefinition(Lexer& lexer, const SynIdentifier& name)
{
	size_t start_line = lexer.current.location.line;

	std::vector<SynTypeGeneric*> generics = parseGenericTypeList(lexer);

	if (lexer.current.type != LexEqual) errorf(lexer.current.location, "Expected '=' after type name");
	movenext(lexer);

	if (lexer.current.type == LexOpenCurlyBrace)
		return new SynRecordDefinition(name.location, name, parseTypeRecord(lexer), generics);

	std::vector<SynTypedVar> members;

	// Allow to skip first '|' as long as the identifier is on the same line
	while (lexer.current.type == LexPipe || (members.empty() && lexer.current.type == LexIdentifier && lexer.current.location.line == start_line))
	{
		if (lexer.current.type == LexPipe)
			movenext(lexer);

		SynIdentifier name = parseIdentifier(lexer);
		SynType* type = 0;

		if (lexer.current.type == LexOpenCurlyBrace)
			type = parseTypeRecord(lexer);
		else if (lexer.current.type == LexIdentifier || lexer.current.type == LexIdentifierGeneric || lexer.current.type == LexOpenBrace)
			type = parseType(lexer);

		members.push_back(SynTypedVar(name, type));
	}

	return new SynUnionDefinition(name.location, name, members, generics);
}

SynBase* parsePrimary(Lexer& lexer)
{
	SynUnaryOpType uop = getUnaryOp(lexer.current);

	if (uop != SynUnaryOpUnknown)
	{
		Location location = lexer.current.location;

		movenext(lexer);

		return new SynUnaryOp(location, uop, parsePrimary(lexer));
	}

	if (iskeyword(lexer, "extern"))
		return parseExternFunc(lexer);

	if (iskeyword(lexer, "type"))
	{
		movenext(lexer);

		SynIdentifier name = parseIdentifier(lexer);

		return parseTypeDefinition(lexer, name);
	}

	if (iskeyword(lexer, "let"))
		return parseLet(lexer);

	if (iskeyword(lexer, "if"))
		return parseIfThenElse(lexer);

	if (iskeyword(lexer, "for"))
		return parseForInDo(lexer);

	if (iskeyword(lexer, "while"))
		return parseWhileDo(lexer);

	if (iskeyword(lexer, "match"))
		return parseMatchWith(lexer);

	if (iskeyword(lexer, "fun"))
		return parseAnonymousFunc(lexer);

	size_t column = lexer.current.location.column;

	SynBase* result = parseTerm(lexer);

	while (((lexer.current.type == LexOpenBrace || lexer.current.type == LexOpenBracket) && lexer.current.location.column > column) ||
		   ((lexer.current.type == LexPoint || lexer.current.type == LexSharp) && lexer.current.location.column >= column))
	{
		if (lexer.current.type == LexOpenBrace)
		{
			Location location = lexer.current.location;
			movenext(lexer);

			std::vector<SynIdentifier> arg_names;
			std::vector<SynBase*> arg_values;

			while (lexer.current.type != LexCloseBrace)
			{
				// Try to parse a named argument
				if (lexer.current.type == LexIdentifier)
				{
					Lexer state = capturestate(lexer);

					SynIdentifier id = parseIdentifier(lexer);

					if (lexer.current.type == LexEqual)
					{
						movenext(lexer);

						if (arg_names.size() != arg_values.size())
							errorf(lexer.current.location, "Named and unnamed function arguments are not allowed to be mixed in a single call");
						arg_names.push_back(id);
						arg_values.push_back(parseExpr(lexer));
					}
					else
					{
						restorestate(lexer, state);

						if (!arg_names.empty())
							errorf(lexer.current.location, "Named and unnamed function arguments are not allowed to be mixed in a single call");
						arg_values.push_back(parseExpr(lexer));
					}
				}
				else
				{
					if (!arg_names.empty())
						errorf(lexer.current.location, "Named and unnamed function arguments are not allowed to be mixed in a single call");
					arg_values.push_back(parseExpr(lexer));
				}

				if (lexer.current.type == LexComma)
					movenext(lexer);
				else if (lexer.current.type == LexCloseBrace)
					;
				else
					errorf(lexer.current.location, "Expected comma or closing brace");
			}

			movenext(lexer);

			result = new SynCall(location, result, arg_values, arg_names);
		}
		else if (lexer.current.type == LexOpenBracket)
		{
			Location location = lexer.current.location;
			movenext(lexer);

			SynBase* index_start = 0;
			SynBase* index_end = 0;
			bool to_end = false;

			if (lexer.current.type == LexCloseBracket) errorf(lexer.current.location, "index or range is expected after '['");

			if (lexer.current.type == LexPointPoint)
				index_start = new SynNumberLiteral(lexer.current.location, 0);
			else
				index_start = parseExpr(lexer);

			if (lexer.current.type == LexPointPoint)
			{
				movenext(lexer);

				if (lexer.current.type == LexCloseBracket)
					to_end = true;
				else
					index_end = parseExpr(lexer);
			}

			if (lexer.current.type != LexCloseBracket) errorf(lexer.current.location, "']' expected after index");
			movenext(lexer);

			if (to_end || index_end)
				result = new SynArraySlice(location, result, index_start, index_end);
			else
				result = new SynArrayIndex(location, result, index_start);
		}
		else if (lexer.current.type == LexPoint)
		{
			movenext(lexer);

			if (lexer.current.type != LexIdentifier) errorf(lexer.current.location, "identifier expected after '.'");

			result = new SynMemberAccess(lexer.current.location, result, parseIdentifier(lexer));
		}
		else if (lexer.current.type == LexSharp)
		{
			movenext(lexer);

			SynIdentifier name = parseIdentifier(lexer);

			std::vector<SynBase*> args;

			args.push_back(result);

			if (lexer.current.type == LexOpenBrace)
			{
				movenext(lexer);

				while (lexer.current.type != LexCloseBrace)
				{
					args.push_back(parseExpr(lexer));

					if (lexer.current.type == LexComma)
						movenext(lexer);
					else if (lexer.current.type == LexCloseBrace)
						;
					else
						errorf(lexer.current.location, "Expected comma or closing brace");
				}

				movenext(lexer);
			}

			result = new SynCall(name.location, new SynVariableReference(name.location, name.name), args);
		}
	}

	return result;
}

SynBase* parseExprClimb(Lexer& lexer, SynBase* left, int limit)
{
	Location location = lexer.current.location;
	SynBinaryOpType op = getBinaryOp(lexer.current);

	while (op != SynBinaryOpUnknown && getBinaryOpPrecedence(op) >= limit)
	{
		movenext(lexer);

		SynBase* right = parsePrimary(lexer);

		SynBinaryOpType nextop = getBinaryOp(lexer.current);

		while (nextop != SynBinaryOpUnknown && getBinaryOpPrecedence(nextop) > getBinaryOpPrecedence(op))
		{
			right = parseExprClimb(lexer, right, getBinaryOpPrecedence(nextop));

			nextop = getBinaryOp(lexer.current);
		}

		left = new SynBinaryOp(location, op, left, right);

		location = lexer.current.location;
		op = getBinaryOp(lexer.current);
	}

	return left;
}

SynBase* parseExpr(Lexer& lexer)
{
	return parseExprClimb(lexer, parsePrimary(lexer), 0);
}

SynBase* parseBlock(Lexer& lexer)
{
	Location start = lexer.current.location;

	SynBase* head = parseExpr(lexer);

	std::vector<SynBase*> exprs;

	exprs.push_back(head);

	while (
		lexer.current.type != LexEOF &&
		lexer.current.type != LexCloseBrace &&
		lexer.current.type != LexCloseBracket &&
		lexer.current.type != LexComma &&
		!islower(lexer.current.location, start) && !issameline(lexer.current.location, start))
		exprs.push_back(parseExpr(lexer));
	
	return new SynBlock(start, exprs);
}

SynBase* parse(Lexer& lexer)
{
	SynBase* code = parseBlock(lexer);

	if(lexer.current.type != LexEOF)
		errorf(lexer.current.location, "Unexpected expression");

	return code;
}
