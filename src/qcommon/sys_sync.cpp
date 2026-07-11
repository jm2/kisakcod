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
std::uint32_t Sys_ReadFastCriticalSectionCount(const volatile std::uint32_t *count)
{
    volatile std::uint32_t *const mutableCount =
        const_cast<volatile std::uint32_t *>(count);
#if defined(_MSC_VER)
    return static_cast<std::uint32_t>(InterlockedCompareExchange(
        reinterpret_cast<volatile long *>(mutableCount), 0, 0));
#else
    return static_cast<std::uint32_t>(InterlockedCompareExchange(mutableCount, 0, 0));
#endif
}

void Sys_ValidateFastCriticalSection(const FastCriticalSection *critSect)
{
    iassert(critSect);
    if (!critSect)
        std::abort();
}
}

bool KISAK_CDECL Sys_IsWriteLocked(const FastCriticalSection *critSect)
{
    Sys_ValidateFastCriticalSection(critSect);
    return Sys_ReadFastCriticalSectionCount(&critSect->writeCount) != 0;
}

void KISAK_CDECL Sys_LockRead(FastCriticalSection *critSect)
{
    Sys_ValidateFastCriticalSection(critSect);

    InterlockedIncrement(&critSect->readCount);
    while (Sys_IsWriteLocked(critSect))
        Sys_Sleep(0);
}

void KISAK_CDECL Sys_UnlockRead(FastCriticalSection *critSect)
{
    Sys_ValidateFastCriticalSection(critSect);

    const std::uint32_t readCount =
        Sys_ReadFastCriticalSectionCount(&critSect->readCount);
    iassert(readCount > 0);
    if (readCount == 0)
        std::abort();

    InterlockedDecrement(&critSect->readCount);
}

void KISAK_CDECL Sys_LockWrite(FastCriticalSection *critSect)
{
    Sys_ValidateFastCriticalSection(critSect);

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
    Sys_ValidateFastCriticalSection(critSect);

    const std::uint32_t writeCount =
        Sys_ReadFastCriticalSectionCount(&critSect->writeCount);
    iassert(writeCount > 0);
    if (writeCount == 0)
        std::abort();

    InterlockedDecrement(&critSect->writeCount);
}
