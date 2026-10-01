#pragma once

// The autoconfigure CPU benchmark: the best of 1000 timed runs of a float and
// LCG loop, scaled to retail-normalized GHz. Sys_SetAutoConfigureGHz
// (win_configure.cpp) multiplies it by the core-count factor. InitTiming must
// have run, since the result is in units of msecPerRawTimerTick.
long double __cdecl Sys_BenchmarkGHz();
