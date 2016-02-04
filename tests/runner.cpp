#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <dirent.h>
#include <sys/stat.h>

#include <string>
#include <vector>
#include <sstream>

using namespace std;

int system(const char* command, string& output)
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

TestType parseTest(const char* path, string& output, string& extraFlags)
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

string sanitizeErrors(const string& output, const string& source)
{
	istringstream iss(output);
	string result;

	string line;
	while (getline(iss, line))
	{
		if (line.compare(0, source.length(), source) != 0)
			continue;

		result += line.substr(source.length());
		result += "\n";
	}

	return result;
}

enum class TestResult
{
	Pass,
	Fail,
	XFail
};

TestResult runTest(const string& source, const string& target, const string& compiler, const string& extraFlags)
{
	// parse expected test results
	string expectedOutput;
	string testFlags;
	TestType testType = parseTest(source.c_str(), expectedOutput, testFlags);

	// build command line
	string command = compiler;

	command += extraFlags;
	command += testFlags;
	command += " ";
	command += source;
	command += " -o ";
	command += target;
	command += " 2>&1";

	if (testType == TestType::Ok)
	{
		string output;
		int rc = system(command.c_str(), output);

		if (rc != 0)
		{
			fprintf(stderr, "Test %s failed: compilation failed with code %d\n", source.c_str(), rc);
			fprintf(stderr, "Errors:\n%s", output.c_str());
			return TestResult::Fail;
		}

		int re = system(target.c_str(), output);

		if (re != 0)
		{
			fprintf(stderr, "Test %s failed: running failed with code %d\n", source.c_str(), re);
			fprintf(stderr, "Output:\n%s", output.c_str());
			return TestResult::Fail;
		}

		if (output != expectedOutput)
		{
			fprintf(stderr, "Test %s failed: output mismatch\n", source.c_str());
			fprintf(stderr, "Expected output:\n%s", expectedOutput.c_str());
			fprintf(stderr, "Actual output:\n%s", output.c_str());
			return TestResult::Fail;
		}

		return TestResult::Pass;
	}
	else if (testType == TestType::Fail)
	{
		string output;
		int rc = system(command.c_str(), output);

		if (rc == 0)
		{
			fprintf(stderr, "Test %s failed: compilation should have failed but did not\n", source.c_str());
			if (!output.empty())
				fprintf(stderr, "Output:\n%s", output.c_str());
			return TestResult::Fail;
		}

		string errors = sanitizeErrors(output, source);

		if (errors != expectedOutput)
		{
			fprintf(stderr, "Test %s failed: error output mismatch\n", source.c_str());
			fprintf(stderr, "Expected errors:\n%s", expectedOutput.c_str());
			fprintf(stderr, "Actual errors:\n%s", errors.c_str());
			return TestResult::Fail;
		}

		return TestResult::Pass;
	}
	else if (testType == TestType::XFail)
	{
		string output;
		int rc = system(command.c_str(), output);

		if (rc == 0)
		{
			fprintf(stderr, "Test %s failed: compilation should have failed but did not\n", source.c_str());
			return TestResult::Fail;
		}

		return TestResult::XFail;
	}
	else
	{
		fprintf(stderr, "Test %s failed: no valid test output detected\n", source.c_str());
		return TestResult::Fail;
	}
}

string joinPath(const string& left, const string& right)
{
	string result = left;

	if (!result.empty() && result.back() != '/')
		result += '/';

	result += right;

	return result;
}

void gatherFilesRec(vector<string>& result, const string& base, const string& rpath)
{
	string path = joinPath(base, rpath);

	DIR* dir = opendir(path.c_str());
	if (!dir) return;

	while (dirent* entry = readdir(dir))
	{
		if (entry->d_name[0] == '.')
			continue;

		string epath = joinPath(rpath, entry->d_name);

		if (entry->d_type == DT_DIR)
			gatherFilesRec(result, base, epath);
		else
			result.push_back(epath);
	}

	closedir(dir);
}

void createPathRec(const string& path)
{
	string copy = path;

	for (size_t i = 1; i < copy.size(); ++i)
		if (copy[i] == '/')
		{
			copy[i] = 0;

			mkdir(copy.c_str(), 0755);

			copy[i] = '/';
		}
}

struct Stats
{
	unsigned int total, passed, failed, xfail;
};

Stats runTests(const string& sourcePath, const string& targetPath, const string& compiler, const string& extraFlags)
{
	vector<string> files;
	gatherFilesRec(files, sourcePath, "");

	Stats stats = {};

	for (auto& f: files)
	{
		if (f.rfind(".aike") + 5 != f.length())
			continue;

		string source = joinPath(sourcePath, f);
		string target = joinPath(targetPath, f.substr(0, f.length() - 5));

		createPathRec(target);

		stats.total++;

		switch (runTest(source, target, compiler, extraFlags))
		{
		case TestResult::Pass:
			stats.passed++;
			break;

		case TestResult::Fail:
			stats.failed++;
			break;

		case TestResult::XFail:
			stats.xfail++;
			break;

		default:
			assert(false);
		}
	}

	return stats;
}

int main(int argc, char** argv)
{
	if (argc < 4)
	{
		fprintf(stderr, "Usage: %s [test.aike] [test.aike.o] [aikec-path] [aikec-flags]\n", argv[0]);
		return 1;
	}

	// get options
	string source = argv[1];
	string target = argv[2];
	string compiler = argv[3];
	string extraFlags;

	for (int i = 4; i < argc; ++i)
	{
		extraFlags += " ";
		extraFlags += argv[i];
	}

	if (source.back() != '/')
		return runTest(source, target, compiler, extraFlags) == TestResult::Fail;

	Stats stats = runTests(source, target, compiler, extraFlags);

	if (stats.failed != 0)
		printf("FAILURE: %u out of %u tests failed.\n", stats.failed, stats.total);
	else
		printf("Success: %u tests passed.\n", stats.total);

	if (stats.xfail != 0)
		printf("%u tests failed as expected\n", stats.xfail);

	return stats.failed != 0;
}
