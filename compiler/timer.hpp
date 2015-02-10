#pragma once

struct Timer
{
	struct Pass
	{
		unsigned int index;
		unsigned int count;
		unsigned long long time;

		Pass(): index(0), count(0), time(0)
		{
		}
	};

	unordered_map<string, Pass> passes;
	unsigned long long lasttime;

	Timer();

	void checkpoint();
	void checkpoint(const char* name);

	void dump();
};