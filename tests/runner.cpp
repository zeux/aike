#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <sstream>

int system(const char* command, std::string& output)
{
	FILE* p = popen(command, "r");
	if (!p)
		return -1;

	output.clear();

	while (!feof(p))
	{
		char buf[256];
		size_t bytes = fread(buf, 1, sizeof(buf), p);

		output.append(buf, bytes);
	}

	return pclose(p);
}

enum class TestType
{
	Unknown,
	Ok,
	Fail,
	XFail
};

TestType parseTest(const char* path, std::string& output, std::string& extraFlags)
{
	FILE* f = fopen(path, "r");
	if (!f)
		return TestType::Unknown;

	char line[2048];

	TestType type = TestType::Unknown;
	bool error = false;

	while (fgets(line, sizeof(line), f))
	{
		size_t length = strlen(line);

		// trim the newline
		if (length > 0 && line[length - 1] == '\n')
			line[length - 1] = 0;

		if (line[0] == '#' && line[1] == '#')
		{
			if (strcmp(line, "## OK") == 0)
			{
				error |= (type != TestType::Unknown);
				type = TestType::Ok;
			}
			else if (strcmp(line, "## FAIL") == 0)
			{
				error |= (type != TestType::Unknown);
				type = TestType::Fail;
			}
			else if (strcmp(line, "## XFAIL") == 0)
			{
				error |= (type != TestType::Unknown);
				type = TestType::XFail;
			}
			else if (strncmp(line, "## FLAGS ", 9) == 0)
			{
				extraFlags += (line + 8);
			}
			else
			{
				error = true;
			}
		}
		else if (line[0] == '#' && line[1] == ' ' && type != TestType::Unknown)
		{
			output += line + 2;
			output += "\n";
		}
	}

	fclose(f);

	return error ? TestType::Unknown : type;
}

std::string sanitizeErrors(const std::string& output, const std::string& source)
{
	std::istringstream iss(output);
	std::string result;

	std::string line;
	while (std::getline(iss, line))
	{
		if (line.compare(0, source.length(), source) != 0)
			continue;

		result += line.substr(source.length());
		result += "\n";
	}

	return result;
}

int main(int argc, char** argv)
{
	if (argc < 4)
	{
		fprintf(stderr, "Usage: %s [test.aike] [test.aike.o] [aikec-path] [aikec-flags]\n", argv[0]);
		return 1;
	}

	// get options
	std::string source = argv[1];
	std::string target = argv[2];
	std::string compiler = argv[3];
	std::string extraFlags;

	for (int i = 4; i < argc; ++i)
	{
		extraFlags += " ";
		extraFlags += argv[i];
	}

	// parse expected test results
	std::string expectedOutput;
	std::string testFlags;
	TestType testType = parseTest(source.c_str(), expectedOutput, testFlags);

	// build command line
	std::string command = compiler;

	command += extraFlags;
	command += testFlags;
	command += " ";
	command += source;
	command += " -o ";
	command += target;
	command += " 2>&1";

	if (testType == TestType::Ok)
	{
		std::string output;
		int rc = system(command.c_str(), output);

		if (rc != 0)
		{
			fprintf(stderr, "Test %s failed: compilation failed with code %d\n", source.c_str(), rc);
			fprintf(stderr, "Errors:\n%s", output.c_str());
			return 1;
		}

		int re = system(target.c_str(), output);

		if (re != 0)
		{
			fprintf(stderr, "Test %s failed: running failed with code %d\n", source.c_str(), re);
			fprintf(stderr, "Output:\n%s", output.c_str());
			return 1;
		}

		if (output != expectedOutput)
		{
			fprintf(stderr, "Test %s failed: output mismatch\n", source.c_str());
			fprintf(stderr, "Expected output:\n%s", expectedOutput.c_str());
			fprintf(stderr, "Actual output:\n%s", output.c_str());
			return 1;
		}
	}
	else if (testType == TestType::Fail)
	{
		std::string output;
		int rc = system(command.c_str(), output);

		if (rc == 0)
		{
			fprintf(stderr, "Test %s failed: compilation should have failed but did not\n", source.c_str());
			if (!output.empty())
				fprintf(stderr, "Output:\n%s", output.c_str());
			return 1;
		}

		std::string errors = sanitizeErrors(output, source);

		if (errors != expectedOutput)
		{
			fprintf(stderr, "Test %s failed: error output mismatch\n", source.c_str());
			fprintf(stderr, "Expected errors:\n%s", expectedOutput.c_str());
			fprintf(stderr, "Actual errors:\n%s", errors.c_str());
			return 1;
		}
	}
	else if (testType == TestType::XFail)
	{
		std::string output;
		int rc = system(command.c_str(), output);

		if (rc == 0)
		{
			fprintf(stderr, "Test %s failed: compilation should have failed but did not\n", source.c_str());
			return 1;
		}
	}
	else
	{
		fprintf(stderr, "Test %s failed: no valid test output detected\n", source.c_str());
		return 1;
	}

	return 0;
}
