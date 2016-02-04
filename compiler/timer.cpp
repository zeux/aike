#include "common.hpp"
#include "timer.hpp"

#include <chrono>

static unsigned long long now()
{
	using namespace chrono;

	return duration_cast<nanoseconds>(high_resolution_clock::now().time_since_epoch()).count();
}

Timer::Timer(): lasttime(0)
{
	checkpoint();
}

void Timer::checkpoint()
{
	lasttime = now();
}

void Timer::checkpoint(const char* name)
{
	unsigned long long time = now();

	Pass& p = passes[name];

	if (!p.index)
		p.index = passes.size();

	p.count++;
	p.time += time - lasttime;

	lasttime = time;
}

void Timer::dump()
{
	vector<const char*> indices(passes.size());

	for (auto& p: passes)
		indices[p.second.index - 1] = p.first.c_str();

	for (auto& n: indices)
	{
		Pass& p = passes[n];

		printf("%-20s %d calls, %d msec\n", n, p.count, int(p.time / 1000000));
	}
}