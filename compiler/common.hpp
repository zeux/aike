#pragma once

#include <vector>
#include <string>
#include <memory>
#include <cstdint>

using namespace std;

#ifdef __GNUC__
__attribute__((format(printf, 1, 2)))
#endif
void panic(const char* format, ...);