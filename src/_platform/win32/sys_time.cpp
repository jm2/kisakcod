#include <Windows.h>
#include <timeapi.h>

#include <cstdint>

#include <qcommon/sys_time.h>

std::uint32_t KISAK_CDECL Sys_MillisecondsRaw()
{
    return static_cast<std::uint32_t>(timeGetTime());
}

std::uint32_t KISAK_CDECL Sys_Milliseconds()
{
    static const std::uint32_t timeBase = Sys_MillisecondsRaw();
    return Sys_MillisecondsRaw() - timeBase;
}

void KISAK_CDECL Sys_Sleep(const std::uint32_t msec)
{
    Sleep(static_cast<DWORD>(msec));
}
