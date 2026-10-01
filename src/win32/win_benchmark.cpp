// Sys_BenchmarkGHz, kept apart from win_configure.cpp so the platform tests can
// run it without the engine headers (tests/win_benchmark_tests.cpp).
#include "win_benchmark.h"

#include <Windows.h>

#include <cstdint>

#include <qcommon/sys_time.h>
#include <universal/timing.h>

long double __cdecl Sys_BenchmarkGHz()
{
    uint32_t i; // ecx
    unsigned __int64 v1; // kr00_8
    // Nothing reads the loop's results, so without volatile MSVC deletes the
    // loop on every target and times two back-to-back counter reads. x86 and
    // x64 then report tens of thousands of GHz that jump 33x between runs;
    // the 100 ns steady_clock that MSVC ARM64 reads (qcommon/sys_time.h) does
    // not advance at all, giving inf. Either way Sys_HasInfoChanged sees a
    // different machine on many starts and asks about it. Retail's compiler
    // kept the loop; volatile keeps the work, as in posix_main.cpp's
    // BenchmarkGHz.
    volatile int holdrand; // [esp+10h] [ebp-68h]
    volatile float k; // [esp+2Ch] [ebp-4Ch]
    std::uint64_t start; // [esp+30h] [ebp-48h]
    int priority; // [esp+44h] [ebp-34h]
    unsigned __int64 minTime; // [esp+48h] [ebp-30h]
    uint32_t attempt; // [esp+54h] [ebp-24h]
    float x; // [esp+68h] [ebp-10h]
    float xa; // [esp+68h] [ebp-10h]
    float y; // [esp+6Ch] [ebp-Ch]
    float ya; // [esp+6Ch] [ebp-Ch]
    HANDLE thread; // [esp+70h] [ebp-8h]

    k = 2.5999999f;
    thread = GetCurrentThread();
    priority = GetThreadPriority(thread);
    SetThreadPriority(thread, 15);
    minTime = ~0ull;
    for (attempt = 0; attempt < 0x3E8; ++attempt)
    {
        Sleep(0);
        start = Sys_CycleCounter();
        holdrand = 0;
        x = 0.25f;
        y = 0.75f;
        for (i = 0; i < 0x3E8; ++i)
        {
            xa = static_cast<float>((1.0 - x) * x * k + x);
            ya = static_cast<float>((1.0 - y) * y * k + y);
            x = static_cast<float>((1.0 - xa) * xa * k + xa);
            y = static_cast<float>((1.0 - ya) * ya * k + ya);
            if ((i & 1) != 0)
                holdrand = 0x343FD * (0x343FD * (0x343FD * holdrand + 0x269EC3) + 0x269EC3) + 0x269EC3;
        }
        const volatile float sink = x + y;
        (void)sink;
        v1 = Sys_CycleCounter() - start;
        if (minTime > v1)
            minTime = v1;
    }
    SetThreadPriority(thread, priority);
    return 0.1010328 / ((double)minTime * msecPerRawTimerTick);
}
