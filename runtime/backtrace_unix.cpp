#include "common.hpp"

#include "backtrace.hpp"

#ifdef AIKE_OS_UNIX
#include <cxxabi.h>
#include <dlfcn.h>
#include <unwind.h>

struct BacktraceData
{
	FILE* file;
	unsigned int flags;
	int frame;
};

static _Unwind_Reason_Code dumpBacktraceCallback(_Unwind_Context* context, void* _data)
{
	auto data = static_cast<BacktraceData*>(_data);

	uintptr_t ip = _Unwind_GetIP(context);

	fprintf(data->file, "#%02d: %016lx", data->frame, ip);

	Dl_info di;
	if (dladdr(reinterpret_cast<void*>(ip), &di) && di.dli_fname && di.dli_sname)
	{
		const char* fname_slash = strrchr(di.dli_fname, '/');
		char* sname_dem = abi::__cxa_demangle(di.dli_sname, 0, 0, 0);

		fprintf(data->file, " %s`%s + %ld",
			fname_slash ? fname_slash + 1 : di.dli_fname,
			sname_dem ? sname_dem : di.dli_sname,
			ip - reinterpret_cast<uintptr_t>(di.dli_saddr));
	}

	fprintf(data->file, "\n");

	data->frame++;

	return _URC_NO_REASON;
}

void backtraceDump(FILE* file, unsigned int flags)
{
	BacktraceData data = { file, flags, 0 };

	auto result = _Unwind_Backtrace(dumpBacktraceCallback, &data);

	if (result != _URC_END_OF_STACK)
		fprintf(file, "unwind failed: %d\n", result);
}
#endif