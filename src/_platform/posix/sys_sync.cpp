#include <pthread.h>

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
pthread_mutex_t s_criticalSections[CRITSECT_COUNT];
pthread_once_t s_criticalSectionsInitOnce = PTHREAD_ONCE_INIT;

void Sys_CheckPthreadResult(const int result)
{
    if (result != 0)
        std::abort();
    iassert(result == 0);
}

void Sys_ValidateCriticalSection(const int critSect)
{
    const bool valid = critSect >= 0 && critSect < CRITSECT_COUNT;
    iassert(valid);
    if (!valid)
        std::abort();
}

void Sys_InitializeCriticalSectionsOnce()
{
    pthread_mutexattr_t attributes;
    Sys_CheckPthreadResult(pthread_mutexattr_init(&attributes));
    Sys_CheckPthreadResult(
        pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE));

    for (int critSect = 0; critSect < CRITSECT_COUNT; ++critSect)
        Sys_CheckPthreadResult(pthread_mutex_init(&s_criticalSections[critSect], &attributes));

    Sys_CheckPthreadResult(pthread_mutexattr_destroy(&attributes));
}
}

void KISAK_CDECL Sys_InitializeCriticalSections()
{
    Sys_CheckPthreadResult(pthread_once(
        &s_criticalSectionsInitOnce,
        Sys_InitializeCriticalSectionsOnce));
}

void KISAK_CDECL Sys_EnterCriticalSection(const int critSect)
{
    PROF_SCOPED("Sys_EnterCriticalSection");

    Sys_ValidateCriticalSection(critSect);
    Sys_InitializeCriticalSections();

    Sys_CheckPthreadResult(pthread_mutex_lock(&s_criticalSections[critSect]));
}

void KISAK_CDECL Sys_LeaveCriticalSection(const int critSect)
{
    Sys_ValidateCriticalSection(critSect);
    Sys_InitializeCriticalSections();

    Sys_CheckPthreadResult(pthread_mutex_unlock(&s_criticalSections[critSect]));
}
