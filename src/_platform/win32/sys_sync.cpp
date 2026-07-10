#include <Windows.h>

#include <cstdlib>

#include <qcommon/sys_sync.h>

#include <universal/assertive.h>

#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>
#define PROF_SCOPED(name) ZoneScopedN(name)
#else
#define PROF_SCOPED(name)
#endif

namespace
{
CRITICAL_SECTION s_criticalSections[CRITSECT_COUNT];
INIT_ONCE s_criticalSectionsInitOnce = INIT_ONCE_STATIC_INIT;

void Sys_CheckWin32Result(const BOOL result)
{
    if (!result)
        std::abort();
    iassert(result);
}

void Sys_ValidateCriticalSection(const int critSect)
{
    const bool valid = critSect >= 0 && critSect < CRITSECT_COUNT;
    iassert(valid);
    if (!valid)
        std::abort();
}

BOOL CALLBACK Sys_InitializeCriticalSectionsOnce(
    PINIT_ONCE,
    PVOID,
    PVOID *)
{
    for (int critSect = 0; critSect < CRITSECT_COUNT; ++critSect)
    {
        Sys_CheckWin32Result(InitializeCriticalSectionAndSpinCount(
            &s_criticalSections[critSect],
            0));
    }

    return TRUE;
}
}

void KISAK_CDECL Sys_InitializeCriticalSections()
{
    Sys_CheckWin32Result(InitOnceExecuteOnce(
        &s_criticalSectionsInitOnce,
        Sys_InitializeCriticalSectionsOnce,
        nullptr,
        nullptr));
}

void KISAK_CDECL Sys_EnterCriticalSection(const int critSect)
{
    PROF_SCOPED("Sys_EnterCriticalSection");

    Sys_ValidateCriticalSection(critSect);
    Sys_InitializeCriticalSections();

    EnterCriticalSection(&s_criticalSections[critSect]);
}

void KISAK_CDECL Sys_LeaveCriticalSection(const int critSect)
{
    Sys_ValidateCriticalSection(critSect);
    Sys_InitializeCriticalSections();

    LeaveCriticalSection(&s_criticalSections[critSect]);
}
