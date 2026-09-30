#include "timing.h"

#if defined(_WIN32)
// KisakCOD ABI port: the calibration below is the Win32 TSC-vs-QPC contract
// (LARGE_INTEGER / QueryPerformanceCounter / Sleep / __rdtsc). The portable
// split keeps windows.h out of POSIX compositions; they take the portable
// arm that calibrates the same raw tick counter against the monotonic clock.
#include <Windows.h>
#else
#include <chrono>
#endif
#include <qcommon/sys_time.h>
#include <qcommon/threads.h>

long double msecPerRawTimerTick;
double qpc2msec;

#if defined(_WIN32)

double __cdecl SecondsPerTick()
{
    _LARGE_INTEGER tscStop; // [esp+20h] [ebp-30h]
    _LARGE_INTEGER qpcFrequency; // [esp+28h] [ebp-28h] BYREF
    _LARGE_INTEGER qpcStart; // [esp+30h] [ebp-20h] BYREF
    _LARGE_INTEGER tscStart; // [esp+38h] [ebp-18h]
    _LARGE_INTEGER qpcStop; // [esp+40h] [ebp-10h] BYREF
    double secPerTick; // [esp+48h] [ebp-8h]

    Win_SetThreadLock(THREAD_LOCK_ALL);
    Sleep(0);
    tscStart.QuadPart = 0;
    qpcStart.QuadPart = 0;
    qpcStop.QuadPart = 0;
    QueryPerformanceFrequency(&qpcFrequency);
    qpc2msec = 1000.0 / qpcFrequency.QuadPart;
    QueryPerformanceCounter(&qpcStart);
    tscStart.QuadPart = Sys_CycleCounter();
    QueryPerformanceCounter(&qpcStart);
    Sleep(0xFAu);
    tscStop.QuadPart = Sys_CycleCounter();
    QueryPerformanceCounter(&qpcStop);
    secPerTick = (double)(qpcStop.QuadPart - qpcStart.QuadPart)
        / ((double)(tscStop.QuadPart - tscStart.QuadPart)
            * (double)qpcFrequency.QuadPart);
    Win_SetThreadLock(THREAD_LOCK_NONE);
    return secPerTick;
}

#else

double __cdecl SecondsPerTick()
{
    // Portable arm of the TSC-vs-QPC calibration above: measure the raw tick
    // counter (Sys_CycleCounter, which the profilers sample) over a
    // monotonic-clock interval, and keep qpc2msec coherent as milliseconds
    // per reference-clock tick.
    // Sleep becomes Sys_Sleep (the sys_time service); the thread lock and the
    // settle/delay shape of the Win32 body are preserved.
    std::uint64_t tscStart;
    std::uint64_t tscStop;
    std::chrono::steady_clock::time_point refStart;
    std::chrono::steady_clock::time_point refStop;
    double secPerTick;

    Win_SetThreadLock(THREAD_LOCK_ALL);
    Sys_Sleep(0);
    qpc2msec = 1000.0 * (double)std::chrono::steady_clock::period::num
        / (double)std::chrono::steady_clock::period::den;
    tscStart = Sys_CycleCounter();
    refStart = std::chrono::steady_clock::now();
    Sys_Sleep(0xFAu);
    tscStop = Sys_CycleCounter();
    refStop = std::chrono::steady_clock::now();
    secPerTick = std::chrono::duration<double>(refStop - refStart).count()
        / (double)(tscStop - tscStart);
    Win_SetThreadLock(THREAD_LOCK_NONE);
    return secPerTick;
}

#endif

void __cdecl InitTiming()
{
	msecPerRawTimerTick = SecondsPerTick() * 1000.0;
}
