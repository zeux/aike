#pragma once

#include <stdio.h>

enum BacktraceFlags
{
};

void backtraceDump(FILE* file, unsigned int flags);