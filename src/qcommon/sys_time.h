#pragma once

#include <cstdint>

#include <universal/platform_compat.h>

std::uint32_t KISAK_CDECL Sys_Milliseconds();
std::uint32_t KISAK_CDECL Sys_MillisecondsRaw();
void KISAK_CDECL Sys_Sleep(std::uint32_t msec);
