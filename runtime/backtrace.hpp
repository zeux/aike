#pragma once

#include <stdio.h>

void backtraceDump(FILE* file, void* stack, size_t stackSize, uintptr_t ip, uintptr_t fp);