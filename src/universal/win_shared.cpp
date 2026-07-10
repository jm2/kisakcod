#include <Windows.h>

#include <qcommon/sys_time.h>

int initialized_1 = 0;
int sys_timeBase;

uint32_t __cdecl Sys_Milliseconds()
{
    if (!initialized_1)
    {
        sys_timeBase = timeGetTime();
        initialized_1 = 1;
    }
    return timeGetTime() - sys_timeBase;
}

uint32_t __cdecl Sys_MillisecondsRaw()
{
    return timeGetTime();
}
