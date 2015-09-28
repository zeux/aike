#pragma once

AIKE_EXTERN void spawn(void (*fn)());
AIKE_EXTERN void yield();

void schedulerRun();

bool schedulerGetStack(void** stack, size_t* stackSize);