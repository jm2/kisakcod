#pragma once

#include <cstdint>

#include <universal/platform_compat.h>

std::uint32_t KISAK_CDECL Sys_Milliseconds();
std::uint32_t KISAK_CDECL Sys_MillisecondsRaw();
void KISAK_CDECL Sys_Sleep(std::uint32_t msec);

// Raw tick counter behind the retail __rdtsc profiling sites. Only
// differences are meaningful; universal/timing.cpp calibrates this same
// counter into msecPerRawTimerTick. x86 reads the TSC, AArch64 the virtual
// counter CNTVCT_EL0, anything else the monotonic clock. MSVC x86 expands to
// the retail __rdtsc() tokens, so the Windows x86 baseline codegen is
// unchanged even at /Od.
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#define Sys_CycleCounter() __rdtsc()
#elif defined(__i386__) || defined(__x86_64__)
inline unsigned long long Sys_CycleCounter()
{
    return __builtin_ia32_rdtsc();
}
#elif defined(__aarch64__)
inline unsigned long long Sys_CycleCounter()
{
    unsigned long long ticks;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(ticks));
    return ticks;
}
#else
#include <chrono>
inline unsigned long long Sys_CycleCounter()
{
    return static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
}
#endif
