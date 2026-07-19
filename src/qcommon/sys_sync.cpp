#include <cstdlib>

#include "sys_sync.h"

#include "sys_time.h"

#include <universal/assertive.h>
#include <universal/sys_atomic.h>

namespace
{
std::uint32_t Sys_ReadFastCriticalSectionCount(const volatile std::uint32_t *count)
{
    return Sys_AtomicLoad(count);
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

    Sys_AtomicIncrement(&critSect->readCount);
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

    Sys_AtomicDecrement(&critSect->readCount);
}

bool KISAK_CDECL Sys_TryLockWrite(FastCriticalSection *critSect)
{
    Sys_ValidateFastCriticalSection(critSect);

    if (Sys_ReadFastCriticalSectionCount(&critSect->readCount) != 0)
        return false;

    if (Sys_AtomicIncrement(&critSect->writeCount) != 1)
    {
        Sys_AtomicDecrement(&critSect->writeCount);
        return false;
    }
    if (Sys_ReadFastCriticalSectionCount(&critSect->readCount) == 0)
        return true;

    Sys_AtomicDecrement(&critSect->writeCount);
    return false;
}

void KISAK_CDECL Sys_LockWrite(FastCriticalSection *critSect)
{
    while (!Sys_TryLockWrite(critSect))
        Sys_Sleep(0);
}

void KISAK_CDECL Sys_UnlockWrite(FastCriticalSection *critSect)
{
    Sys_ValidateFastCriticalSection(critSect);

    const std::uint32_t writeCount =
        Sys_ReadFastCriticalSectionCount(&critSect->writeCount);
    iassert(writeCount > 0);
    if (writeCount == 0)
        std::abort();

    Sys_AtomicDecrement(&critSect->writeCount);
}
