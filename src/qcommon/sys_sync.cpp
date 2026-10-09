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

// The fast critical sections this thread holds. Each section is released on
// the thread that took it, so the count is exact for every thread.
thread_local std::uint32_t t_heldFastCriticalSections = 0;
}

bool KISAK_CDECL Sys_HoldsFastCriticalSection()
{
    return t_heldFastCriticalSections != 0;
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
    ++t_heldFastCriticalSections;
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
    --t_heldFastCriticalSections;
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
    {
        ++t_heldFastCriticalSections; Sys_NoteWriteLock(critSect, true);
        return true;
    }

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

    Sys_AtomicDecrement(&critSect->writeCount); Sys_NoteWriteLock(critSect, false);
    --t_heldFastCriticalSections;
}

namespace
{
// The sections this thread holds for write. A thread nests few: the asset
// hash, say, inside a registry window.
constexpr std::uint32_t kWriteHeldSlots = 8;
thread_local const FastCriticalSection *t_writeHeld[kWriteHeldSlots] = {};
thread_local std::uint32_t t_writeHeldCount = 0;
} // namespace

void KISAK_CDECL Sys_NoteWriteLock(const FastCriticalSection *critSect, const bool held)
{
    if (held)
    {
        if (t_writeHeldCount < kWriteHeldSlots)
            t_writeHeld[t_writeHeldCount] = critSect;
        ++t_writeHeldCount;
        return;
    }
    const std::uint32_t tracked = t_writeHeldCount < kWriteHeldSlots ? t_writeHeldCount : kWriteHeldSlots;
    for (std::uint32_t index = tracked; index-- > 0;)
    {
        if (t_writeHeld[index] == critSect)
        {
            for (std::uint32_t next = index + 1; next < tracked; ++next)
                t_writeHeld[next - 1] = t_writeHeld[next];
            break;
        }
    }
    if (t_writeHeldCount)
        --t_writeHeldCount;
}

bool KISAK_CDECL Sys_HoldsWriteLock(const FastCriticalSection *critSect)
{
    const std::uint32_t tracked = t_writeHeldCount < kWriteHeldSlots ? t_writeHeldCount : kWriteHeldSlots;
    for (std::uint32_t index = 0; index < tracked; ++index)
    {
        if (t_writeHeld[index] == critSect)
            return true;
    }
    return false;
}
