#include "common.hpp"

#include "backtrace.hpp"

#ifdef AIKE_OS_UNIX
#include <cxxabi.h>
#include <dlfcn.h>

static void dumpBacktraceFrame(FILE* file, int frame, uintptr_t ip)
{
	fprintf(file, "#%02d: %016lx", frame, ip);

	Dl_info di;
	if (dladdr(reinterpret_cast<void*>(ip), &di) && di.dli_fname && di.dli_sname)
	{
		const char* fname_slash = strrchr(di.dli_fname, '/');
		char* sname_dem = abi::__cxa_demangle(di.dli_sname, 0, 0, 0);

		fprintf(file, " %s`%s + %ld",
			fname_slash ? fname_slash + 1 : di.dli_fname,
			sname_dem ? sname_dem : di.dli_sname,
			ip - reinterpret_cast<uintptr_t>(di.dli_saddr));
	}

	fprintf(file, "\n");
}

static bool isReadable(void* stack, size_t stackSize, uintptr_t address, size_t addressSize)
{
	return
		address >= reinterpret_cast<uintptr_t>(stack) &&
		(address - reinterpret_cast<uintptr_t>(stack)) + addressSize <= stackSize;
}

void backtraceDump(FILE* file, void* stack, size_t stackSize, uintptr_t ip, uintptr_t fp)
{
	int frame = 0;
	dumpBacktraceFrame(file, frame++, ip);

	uintptr_t stackAlignment = 16;

	while (isReadable(stack, stackSize, fp, sizeof(uintptr_t) * 2) && (fp & (stackAlignment - 1)) == 0)
	{
		uintptr_t* fpd = reinterpret_cast<uintptr_t*>(fp);

		uintptr_t nextip = fpd[1];
		uintptr_t nextfp = fpd[0];

		if (nextip == 0 || nextfp <= fp)
			break;

		dumpBacktraceFrame(file, frame++, nextip);

		fp = nextfp;
	}
}
#endif