#if defined(_MSC_VER)
#include <Windows.h>
#endif

#include <cstdlib>

#include "sys_sync.h"

#include "sys_time.h"

#include <universal/assertive.h>
#include <universal/sys_atomic.h>

namespace
{
std::uint32_t Sys_ReadFastCriticalSectionCount(volatile std::uint32_t *count)
{
#if defined(_MSC_VER)
    return static_cast<std::uint32_t>(InterlockedCompareExchange(
        reinterpret_cast<volatile long *>(count), 0, 0));
#else
    return static_cast<std::uint32_t>(InterlockedCompareExchange(count, 0, 0));
#endif
}
}

void KISAK_CDECL Sys_LockWrite(FastCriticalSection *critSect)
{
    iassert(critSect);
    if (!critSect)
        std::abort();

    while (true)
    {
        if (Sys_ReadFastCriticalSectionCount(&critSect->readCount) == 0)
        {
            if (InterlockedIncrement(&critSect->writeCount) == 1
                && Sys_ReadFastCriticalSectionCount(&critSect->readCount) == 0)
            {
                return;
            }
            InterlockedDecrement(&critSect->writeCount);
        }
        Sys_Sleep(0);
    }
}

void KISAK_CDECL Sys_UnlockWrite(FastCriticalSection *critSect)
{
    iassert(critSect);
    if (!critSect)
        std::abort();

    const std::uint32_t writeCount =
        Sys_ReadFastCriticalSectionCount(&critSect->writeCount);
    iassert(writeCount > 0);
    if (writeCount == 0)
        std::abort();

    InterlockedDecrement(&critSect->writeCount);
}
