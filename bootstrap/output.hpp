#pragma once

struct Location;

#ifdef _MSC_VER
__declspec(noreturn)
#endif
void errorf(const char* format, ...);

#ifdef _MSC_VER
__declspec(noreturn)
#endif
void errorf(Location& location, const char* format, ...);
