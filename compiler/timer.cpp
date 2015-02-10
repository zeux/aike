#include "common.hpp"
#include "timer.hpp"

#include <mach/mach_time.h>

static unsigned long long now()
{
	unsigned long long value = mach_absolute_time();

    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);

    return value * timebase.numer / timebase.denom;
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