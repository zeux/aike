#include "common.hpp"
#include "lexer.hpp"
#include "output.hpp"

struct Options
{
	vector<string> inputs;
	string output;
};

Options parseOptions(int argc, const char** argv)
{
	Options result = {};

	for (int i = 1; i < argc; ++i)
	{
		Str arg = argv[i];

		if (arg.size > 0 && arg[0] == '-')
		{
			if (arg == "-o" && i + 1 < argc)
				result.output = argv[++i];
			else
				panic("Unknown argument %s", arg.str().c_str());
		}
		else
		{
			result.inputs.push_back(arg.str());
		}
	}

	return result;
}

Str readFile(const char* path)
{
	FILE* file = fopen(path, "rb");
	if (!file) panic("Can't read file %s: file not found", path);

	fseek(file, 0, SEEK_END);
	long length = ftell(file);
	fseek(file, 0, SEEK_SET);

	char* result = new char[length];
	fread(result, 1, length, file);
	if (ferror(file)) panic("Can't read file %s: I/O error", path);

	fclose(file);

	return Str(result, length);
}

int main(int argc, const char** argv)
{
	Options options = parseOptions(argc, argv);
	Output output;

	for (auto& file: options.inputs)
	{
		const char* source = strdup(file.c_str());

		Str contents = readFile(source);

		output.sources[source] = contents;

		lexer::Tokens tokens = lexer::tokenize(output, source, contents);

		for (auto t: tokens.tokens)
			printf("{%d,%d,%d %s}, ", t.location.line + 1, t.location.column + 1, int(t.location.length), t.data.str().c_str());
	}
}