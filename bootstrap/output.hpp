#pragma once

struct Location;

#ifdef _MSC_VER
__declspec(noreturn)
#endif
void errorf(const Location& location, const char* format, ...);