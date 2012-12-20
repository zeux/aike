#pragma once

#ifdef _MSC_VER
__declspec(noreturn)
#endif
void errorf(const char* format, ...);