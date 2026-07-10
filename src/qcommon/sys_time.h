#pragma once

#include <cstdint>

#include <universal/platform_compat.h>

std::uint32_t KISAK_CDECL Sys_Milliseconds();
std::uint32_t KISAK_CDECL Sys_MillisecondsRaw();

// The current sleep implementation remains in the single-player thread module
// until the native Win32 and POSIX time backends are introduced.
#if defined(KISAK_SP)
void Sys_Sleep(std::uint32_t msec);
#endif
