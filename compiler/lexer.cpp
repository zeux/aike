#include "common.hpp"
#include "lexer.hpp"

Tokens tokenize(const string& data)
{
	Tokens result;

	result.data.reset(new char[data.size()]);
	memcpy(result.data.get(), data.c_str(), data.size());

	return result;
}