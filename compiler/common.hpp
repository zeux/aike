#pragma once

#include <cstdint>
#include <cassert>

#include <vector>
#include <string>
#include <memory>

using namespace std;

#ifdef __GNUC__
__attribute__((format(printf, 1, 2)))
#endif
void panic(const char* format, ...);