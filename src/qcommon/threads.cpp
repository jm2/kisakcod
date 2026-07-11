#include "threads.h"

#include <atomic>
#include <cstdlib>

#include <qcommon/sys_sync.h>
#include <qcommon/sys_worker_gate.h>
#include <universal/assertive.h>
#include <universal/profile.h>
#include <universal/q_shared.h>

#ifndef KISAK_DEDI_HEADLESS
#include <gfx_d3d/rb_drawprofile.h>
#include <gfx_d3d/r_init.h>
#endif
#if defined(KISAK_MP) && !defined(KISAK_DEDI_HEADLESS)
#include <client_mp/client_mp.h>
#elif KISAK_SP
#include <client/client.h>
#endif
#ifndef KISAK_DEDI_HEADLESS
#include <gfx_d3d/rb_backend.h>
#endif

extern float com_timescaleValue;
extern const dvar_t *sys_lockThreads;

// NOTE(mrsteyk): keep in mind this is 4 elements long.
static thread_local void **g_threadLocals;

#ifdef KISAK_SP
int isDoingDatabaseInit;

SysEventHandle wakeServerEvent;
SysEventHandle serverCompletedEvent;
SysEventHandle allowSendClientMessagesEvent;
SysEventHandle serverSnapshotEvent;
SysEventHandle clientMessageReceived;
SysEventHandle g_saveHistoryEvent;
SysEventHandle g_saveHistoryDoneEvent;

static std::atomic<int32_t> g_timeout{0};
#endif

using ThreadFuncFn = SysThreadContextEntry;

struct SysEngineThreadStartRecord
{
    ThreadContext_t threadContext;
    ThreadFuncFn function;
};

static SysEngineThreadStartRecord s_threadStartRecord[THREAD_CONTEXT_COUNT];
static SysThreadHandle threadHandle[THREAD_CONTEXT_COUNT];
static void *g_threadValues[THREAD_CONTEXT_COUNT][4];

static constexpr uint32_t WORKER_THREAD_COUNT = 2;
static SysWorkerGateHandle s_workerThreadGate[WORKER_THREAD_COUNT];

static void KISAK_CDECL Sys_StartThread(ThreadContext_t threadContext);

static int g_databaseThreadOwner;

static std::atomic<void *> smpData{nullptr};

static std::atomic<int32_t> renderPausedCount{0};

static WinThreadLock s_threadLock;

static SysEventHandle renderPausedEvent;
static SysEventHandle renderCompletedEvent;
static SysEventHandle noThreadOwnershipEvent;
static SysEventHandle rendererRunningEvent;
static SysEventHandle backendEvent[2];
static SysEventHandle ackendEvent;
static SysEventHandle updateSpotLightEffectEvent;
static SysEventHandle updateEffectsEvent;

static bool Sys_IsWorkerThreadIndexValid(const uint32_t workerIndex)
{
    if (workerIndex < WORKER_THREAD_COUNT)
        return true;

    iassert(workerIndex < WORKER_THREAD_COUNT);
    return false;
}

static bool Sys_GetWorkerThreadIndex(
    const ThreadContext_t threadContext,
    uint32_t *const workerIndex)
{
    if (threadContext >= THREAD_CONTEXT_WORKER0
        && threadContext <= THREAD_CONTEXT_WORKER1)
    {
        *workerIndex = static_cast<uint32_t>(
            threadContext - THREAD_CONTEXT_WORKER0);
        return true;
    }

    iassert(threadContext >= THREAD_CONTEXT_WORKER0
        && threadContext <= THREAD_CONTEXT_WORKER1);
    return false;
}

static bool __cdecl Sys_IsUsingAnyRenderProfile()
{
#ifdef KISAK_DEDI_HEADLESS
    return false;
#else
    return RB_IsUsingAnyProfile();
#endif
}

static bool __cdecl Sys_IsUsingAdaptiveGpuSync()
{
#ifdef KISAK_DEDI_HEADLESS
    return false;
#else
    return R_IsUsingAdaptiveGpuSync();
#endif
}

#ifdef KISAK_MP
static const char* s_threadNames[THREAD_CONTEXT_COUNT] =
{
    "Main",
    "Backend",
    "Worker0",
    "Worker1",
    "Cinematic",
    "Titleserver",
    "Database",
};
#elif KISAK_SP
static const char *s_threadNames[THREAD_CONTEXT_COUNT] =
{
    "MAIN",
    "BACKEND",
    "WORKER0",
    "WORKER1",
    "WORKER2",
    "SERVER",
    "CINEMATIC",
    "TITLE_SERVER",
    "DATABASE",
    "STREAM",
    "SNDSTREAMPACKETCALLBACK",
    "SERVER_DEMO"
};
#endif

static bool Sys_IsThreadContextValid(const ThreadContext_t threadContext)
{
    const int32_t contextIndex = static_cast<int32_t>(threadContext);
    if (contextIndex >= 0 && contextIndex < THREAD_CONTEXT_COUNT)
        return true;

    iassert(contextIndex >= 0 && contextIndex < THREAD_CONTEXT_COUNT);
    return false;
}

static uint32_t Sys_GetPreferredProcessorOrdinal(
    const uint32_t eligibleProcessorCount,
    const uint32_t preferredSlot)
{
    iassert(eligibleProcessorCount > 1);
    iassert(preferredSlot < 4);

    switch (preferredSlot)
    {
    case 0:
        return 0;
    case 1:
        return eligibleProcessorCount - 1;
    case 2:
        if (eligibleProcessorCount <= 4)
            return 1;
        return 1 + (eligibleProcessorCount - 2) / 3;
    case 3:
        if (eligibleProcessorCount == 4)
            return 2;
        return eligibleProcessorCount - 1 - (eligibleProcessorCount - 2) / 3;
    default:
        iassert(0);
        return 0;
    }
}

static void Sys_ApplyThreadAffinity(const ThreadContext_t threadContext)
{
    if (!Sys_IsThreadContextValid(threadContext))
        return;

    SysThreadHandle const thread = threadHandle[threadContext];
    if (!thread)
        return;

    const uint32_t eligibleProcessorCount =
        Sys_ThreadGetEligibleProcessorCount();
    const uint32_t preferredSlotCount = eligibleProcessorCount < 4
        ? eligibleProcessorCount
        : 4;
    bool shouldPin = false;
    uint32_t preferredSlot = 0;

    switch (threadContext)
    {
    case THREAD_CONTEXT_MAIN:
        shouldPin = s_threadLock != THREAD_LOCK_NONE
            && eligibleProcessorCount > 1;
        preferredSlot = 0;
        break;
    case THREAD_CONTEXT_BACKEND:
        shouldPin = s_threadLock != THREAD_LOCK_NONE
            && eligibleProcessorCount > 1;
        preferredSlot = 1;
        break;
    case THREAD_CONTEXT_DATABASE:
        shouldPin = s_threadLock == THREAD_LOCK_ALL
            && eligibleProcessorCount > 1;
        preferredSlot = eligibleProcessorCount < 3 ? 1 : 2;
        break;
    case THREAD_CONTEXT_CINEMATIC:
        shouldPin = s_threadLock == THREAD_LOCK_ALL
            && eligibleProcessorCount > 1;
        preferredSlot = preferredSlotCount - 1;
        break;
    case THREAD_CONTEXT_WORKER0:
        shouldPin = s_threadLock == THREAD_LOCK_ALL
            && eligibleProcessorCount >= 3;
        preferredSlot = 2;
        break;
    case THREAD_CONTEXT_WORKER1:
        shouldPin = s_threadLock == THREAD_LOCK_ALL
            && eligibleProcessorCount >= 4;
        preferredSlot = 3;
        break;
    default:
        break;
    }

    if (shouldPin)
    {
        const uint32_t ordinal = Sys_GetPreferredProcessorOrdinal(
            eligibleProcessorCount,
            preferredSlot);
        (void)Sys_ThreadPinToEligibleProcessor(thread, ordinal);
    }
    else
    {
        (void)Sys_ThreadClearAffinity(thread);
    }
}

static void Sys_ApplyThreadAffinityToAll()
{
    for (int32_t contextIndex = 0;
         contextIndex < THREAD_CONTEXT_COUNT;
         ++contextIndex)
    {
        Sys_ApplyThreadAffinity(static_cast<ThreadContext_t>(contextIndex));
    }
}

uint32_t __cdecl Sys_GetCpuCount()
{
    return Sys_ThreadGetEligibleProcessorCount();
}

void __cdecl Sys_EndLoadThreadPriorities()
{
    iassert(Sys_IsMainThread());
    if (threadHandle[THREAD_CONTEXT_MAIN])
    {
        (void)Sys_ThreadSetPriority(
            threadHandle[THREAD_CONTEXT_MAIN],
            SysThreadPriority::Normal);
    }
}

int __cdecl Sys_IsRendererReady()
{
    return Sys_WaitForSingleObjectTimeout(&renderCompletedEvent, 0);
}

static void Sys_InitThread(const ThreadContext_t threadContext)
{
    if (!Sys_IsThreadContextValid(threadContext))
        return;

    g_threadLocals = g_threadValues[threadContext];
    Com_InitThreadData(threadContext);
    Profile_InitContext(threadContext);
}

static void KISAK_CDECL Sys_EngineThreadMain(void *const userData)
{
    const SysEngineThreadStartRecord *const startRecord =
        static_cast<const SysEngineThreadStartRecord *>(userData);
    if (!startRecord || !Sys_IsThreadContextValid(startRecord->threadContext))
    {
        iassert(startRecord);
        return;
    }

    const ThreadFuncFn function = startRecord->function;
    if (!function)
    {
        iassert(function);
        return;
    }

    const ThreadContext_t threadContext = startRecord->threadContext;
#ifdef TRACY_ENABLE
    TracyCSetThreadName(s_threadNames[threadContext]);
#endif
    Sys_InitThread(threadContext);
    function(static_cast<uint32_t>(threadContext));
}

static bool Sys_CreateThread(
    const ThreadFuncFn function,
    const ThreadContext_t threadContext)
{
    if (!function || !Sys_IsThreadContextValid(threadContext))
    {
        iassert(function);
        return false;
    }

    SysEngineThreadStartRecord &startRecord = s_threadStartRecord[threadContext];
    if (startRecord.function || threadHandle[threadContext])
    {
        iassert(!startRecord.function);
        iassert(!threadHandle[threadContext]);
        return false;
    }

    startRecord.threadContext = threadContext;
    startRecord.function = function;
    if (!Sys_ThreadCreateSuspended(
            Sys_EngineThreadMain,
            &startRecord,
            s_threadNames[threadContext],
            &threadHandle[threadContext]))
    {
        startRecord = {};
        return false;
    }

    Sys_ApplyThreadAffinity(threadContext);
    return true;
}

void __cdecl Sys_InitMainThread()
{
    if (threadHandle[THREAD_CONTEXT_MAIN])
    {
        iassert(!threadHandle[THREAD_CONTEXT_MAIN]);
        return;
    }

    if (!Sys_ThreadCaptureCurrent(
            s_threadNames[THREAD_CONTEXT_MAIN],
            &threadHandle[THREAD_CONTEXT_MAIN]))
    {
        iassert(threadHandle[THREAD_CONTEXT_MAIN]);
        std::abort();
    }

    Sys_ApplyThreadAffinity(THREAD_CONTEXT_MAIN);
    g_threadLocals = g_threadValues[THREAD_CONTEXT_MAIN];
    Com_InitThreadData(THREAD_CONTEXT_MAIN);
}

char __cdecl Sys_SpawnRenderThread(SysThreadContextEntry function)
{
    Sys_CreateEvent(0, 0, &renderPausedEvent);
    Sys_CreateEvent(1, 1, &renderCompletedEvent);
    Sys_CreateEvent(1, 0, &noThreadOwnershipEvent);
    Sys_CreateEvent(1, 1, &rendererRunningEvent);
    Sys_CreateEvent(0, 0, &backendEvent[1]);
    Sys_CreateEvent(1, 0, backendEvent);
    Sys_CreateEvent(1, 1, &updateSpotLightEffectEvent);
    Sys_CreateEvent(1, 1, &updateEffectsEvent);

    if (!Sys_CreateThread(function, THREAD_CONTEXT_BACKEND))
        return 0;

    Sys_StartThread(THREAD_CONTEXT_BACKEND);

    return 1;
}

static SysEventHandle wakeDatabaseEvent;
static SysEventHandle databaseCompletedEvent;
static SysEventHandle databaseCompletedEvent2;
static SysEventHandle resumedDatabaseEvent;

bool dediRenderHack = false;

char __cdecl Sys_SpawnDatabaseThread(SysThreadContextEntry function)
{
    Sys_CreateEvent(0, 0, &wakeDatabaseEvent);
    Sys_CreateEvent(1, 1, &databaseCompletedEvent);
    Sys_CreateEvent(1, 1, &databaseCompletedEvent2);
    Sys_CreateEvent(1, 1, &resumedDatabaseEvent);

    if (!Sys_CreateThread(function, THREAD_CONTEXT_DATABASE))
        return 0;

    (void)Sys_ThreadSetPriority(
        threadHandle[THREAD_CONTEXT_DATABASE],
        Sys_GetCpuCount() > 1
            ? SysThreadPriority::AboveNormal
            : SysThreadPriority::BelowNormal);
    Sys_StartThread(THREAD_CONTEXT_DATABASE);

    return 1;
}

void __cdecl Sys_SuspendDatabaseThread(ThreadOwner owner)
{
    iassert( owner != THREAD_OWNER_NONE );

    if (g_databaseThreadOwner)
        MyAssertHandler(
            ".\\qcommon\\threads.cpp",
            1061,
            0,
            "%s\n\t(g_databaseThreadOwner) = %i",
            "(g_databaseThreadOwner == THREAD_OWNER_NONE)",
            g_databaseThreadOwner);

    g_databaseThreadOwner = owner;
    Sys_ResetEvent(&resumedDatabaseEvent);
}

void __cdecl Sys_ResumeDatabaseThread(ThreadOwner owner)
{
    iassert( owner != THREAD_OWNER_NONE );
    if (g_databaseThreadOwner != owner)
        MyAssertHandler(
            ".\\qcommon\\threads.cpp",
            1073,
            0,
            "g_databaseThreadOwner == owner\n\t%i, %i",
            g_databaseThreadOwner,
            owner);
    g_databaseThreadOwner = THREAD_OWNER_NONE;
    Sys_SetEvent(&resumedDatabaseEvent);
}

bool __cdecl Sys_HaveSuspendedDatabaseThread(ThreadOwner owner)
{
    return g_databaseThreadOwner == owner;
}

void __cdecl Sys_WaitDatabaseThread()
{
    Sys_WaitForSingleObject(&resumedDatabaseEvent);
}

bool __cdecl Sys_SpawnWorkerThread(
    SysThreadContextEntry function,
    uint32_t threadIndex)
{
    if (!Sys_IsWorkerThreadIndexValid(threadIndex))
        return false;

    const ThreadContext_t threadContext = static_cast<ThreadContext_t>(
        THREAD_CONTEXT_WORKER0 + threadIndex);
    if (threadHandle[threadContext] || s_workerThreadGate[threadIndex])
    {
        MyAssertHandler(
            ".\\qcommon\\threads.cpp",
            1113,
            0,
            "%s\n\t(threadContext) = %i",
            "(!threadHandle[threadContext])",
            threadContext);

        return false;
    }

    Sys_WorkerGateCreate(&s_workerThreadGate[threadIndex]);
    if (Sys_CreateThread(function, threadContext))
        return true;

    Sys_WorkerGateDestroy(&s_workerThreadGate[threadIndex]);
    return false;
}

void __cdecl Sys_SetWorkerThreadActive(
    const uint32_t workerIndex,
    const bool active)
{
    if (!Sys_IsWorkerThreadIndexValid(workerIndex))
        return;

    const ThreadContext_t threadContext = static_cast<ThreadContext_t>(
        THREAD_CONTEXT_WORKER0 + workerIndex);
    SysWorkerGateHandle const gate = s_workerThreadGate[workerIndex];
    if (!gate || !threadHandle[threadContext])
    {
        iassert(gate);
        iassert(threadHandle[threadContext]);
        return;
    }

    if (active)
    {
        if (Sys_WorkerGateActivate(gate))
            Sys_StartThread(threadContext);
        Sys_SetWorkerCmdEvent();
        return;
    }

    if (Sys_WorkerGateRequestPause(gate))
    {
        Sys_SetWorkerCmdEvent();
        Sys_WorkerGateWaitPaused(gate);
    }
}

void __cdecl Sys_WorkerThreadPausePoint(const ThreadContext_t threadContext)
{
    uint32_t workerIndex;
    if (!Sys_GetWorkerThreadIndex(threadContext, &workerIndex))
        return;

    SysWorkerGateHandle const gate = s_workerThreadGate[workerIndex];
    if (!gate)
    {
        iassert(gate);
        return;
    }

    Sys_WorkerGatePausePoint(gate);
}

static void KISAK_CDECL Sys_StartThread(const ThreadContext_t threadContext)
{
    if (!Sys_IsThreadContextValid(threadContext))
        return;

    SysThreadHandle const thread = threadHandle[threadContext];
    if (!thread)
    {
        iassert(thread);
        return;
    }

    Sys_ThreadStart(thread);
}

void *__cdecl Sys_RendererSleep()
{
    return smpData.exchange(nullptr, std::memory_order_seq_cst);
}

int __cdecl Sys_RendererReady()
{
    return smpData.load(std::memory_order_seq_cst) != nullptr;
}

void __cdecl Sys_RenderCompleted()
{
    Sys_SetEvent(&renderCompletedEvent);
    Sys_SetWorkerCmdEvent();
}

void __cdecl Sys_FrontEndSleep()
{
    int32_t newCount; // [esp+0h] [ebp-4h]

    if (!Sys_WaitForSingleObjectTimeout(&noThreadOwnershipEvent, 0))
        MyAssertHandler(
            ".\\qcommon\\threads.cpp",
            1206,
            0,
            "%s",
            "Sys_WaitForSingleObjectTimeout( &noThreadOwnershipEvent, 0 )");
    Sys_WaitForSingleObject(&rendererRunningEvent);
    Sys_ResetEvent(&noThreadOwnershipEvent);
    Sys_SetEvent(&backendEvent[1]);
    newCount = renderPausedCount.fetch_sub(1, std::memory_order_seq_cst) - 1;
    if (newCount != -1 && newCount)
        MyAssertHandler(
            ".\\qcommon\\threads.cpp",
            1212,
            0,
            "%s\n\t(newCount) = %i",
            "((newCount == -1) || (newCount == 0))",
            newCount);
    Sys_WaitForSingleObject(&renderPausedEvent);
}

void __cdecl Sys_WakeRenderer(void* data)
{
    Sys_ResetEvent(&renderCompletedEvent);
    const void *old = smpData.exchange(data, std::memory_order_seq_cst);
    vassert(!old, "old = %p", old);
    (void)old;
    Sys_SetEvent(&backendEvent[1]);
    Sys_SetWorkerCmdEvent();
}

void __cdecl Sys_NotifyRenderer()
{
    if (!backendEvent[1])
        MyAssertHandler(
            ".\\qcommon\\threads.cpp",
            1266,
            0,
            "%s",
            "Sys_IsEventInitialized( backendEvent[BACKEND_EVENT_GENERIC] )");
    Sys_SetEvent(&backendEvent[1]);
}

void __cdecl Sys_DatabaseCompleted()
{
#ifdef KISAK_SP 
    Sys_EnterCriticalSection(CRITSECT_START_SERVER);
    isDoingDatabaseInit = 1;
    Sys_LeaveCriticalSection(CRITSECT_START_SERVER);
    Sys_WaitForSingleObject(&serverCompletedEvent);
#endif
    Sys_SetEvent(&databaseCompletedEvent);
}

void __cdecl Sys_WaitStartDatabase()
{
    Sys_WaitForSingleObject(&wakeDatabaseEvent);
}

bool __cdecl Sys_IsDatabaseReady()
{
    return Sys_WaitForSingleObjectTimeout(&databaseCompletedEvent, 0);
}

void __cdecl Sys_SyncDatabase()
{
    Sys_WaitForSingleObject(&databaseCompletedEvent);
}

void __cdecl Sys_WakeDatabase()
{
    Sys_ResetEvent(&databaseCompletedEvent);
}

void __cdecl Sys_NotifyDatabase()
{
    Sys_SetEvent(&wakeDatabaseEvent);
}

void __cdecl Sys_DatabaseCompleted2()
{
#ifdef KISAK_SP
    Sys_EnterCriticalSection(CRITSECT_START_SERVER);
    isDoingDatabaseInit = 0;
    Sys_LeaveCriticalSection(CRITSECT_START_SERVER);
#endif
    Sys_SetEvent(&databaseCompletedEvent2);
}

bool __cdecl Sys_IsDatabaseReady2()
{
    return Sys_WaitForSingleObjectTimeout(&databaseCompletedEvent2, 0);
}

void __cdecl Sys_WakeDatabase2()
{
    Sys_ResetEvent(&databaseCompletedEvent2);
}

bool __cdecl Sys_FinishRenderer()
{
    return !Sys_WaitForSingleObjectTimeout(&noThreadOwnershipEvent, 0);
}

int __cdecl Sys_IsMainThreadReady()
{
    return Sys_WaitForSingleObjectTimeout(&noThreadOwnershipEvent, 0);
}

void __cdecl Sys_WaitForMainThread()
{
    Sys_WaitForSingleObject(&noThreadOwnershipEvent);
}

void __cdecl Sys_StopRenderer()
{
    int32_t newCount; // [esp+0h] [ebp-4h]

    newCount = renderPausedCount.fetch_add(1, std::memory_order_seq_cst) + 1;
    if (newCount > 1)
        MyAssertHandler(
            ".\\qcommon\\threads.cpp",
            1734,
            0,
            "%s\n\t(newCount) = %i",
            "((newCount == 0) || (newCount == 1))",
            newCount);
    Sys_ResetEvent(&rendererRunningEvent);
    Sys_SetEvent(&renderPausedEvent);
}

void __cdecl Sys_StartRenderer()
{
    Sys_SetEvent(&rendererRunningEvent);
}

bool __cdecl Sys_IsRenderThread()
{
    SysThreadHandle const thread = threadHandle[THREAD_CONTEXT_BACKEND];
    return thread && Sys_ThreadIsCurrent(thread);
}

bool __cdecl Sys_IsDatabaseThread()
{
    SysThreadHandle const thread = threadHandle[THREAD_CONTEXT_DATABASE];
    return thread && Sys_ThreadIsCurrent(thread);
}

bool __cdecl Sys_IsMainThread()
{
    SysThreadHandle const thread = threadHandle[THREAD_CONTEXT_MAIN];
    return thread && Sys_ThreadIsCurrent(thread);
}

#ifdef KISAK_SP
bool KISAK_CDECL Sys_IsServerThread()
{
    SysThreadHandle const thread = threadHandle[THREAD_CONTEXT_SERVER];
    return thread && Sys_ThreadIsCurrent(thread);
}
#endif

void __cdecl Sys_SetValue(int valueIndex, void* data)
{
    //*(uint32_t*)(*(uint32_t*)(*((uint32_t*)NtCurrentTeb()->ThreadLocalStoragePointer + _tls_index) + 4) + 4 * valueIndex) = data;
    g_threadLocals[valueIndex] = data;
}

void* __cdecl Sys_GetValue(int valueIndex)
{
    //return *(void**)(*(uint32_t*)(*((uint32_t*)NtCurrentTeb()->ThreadLocalStoragePointer + _tls_index) + 4) + 4 * valueIndex);
    return g_threadLocals[valueIndex];
}

void __cdecl Sys_WaitForWorkerCmd()
{
    Sys_WaitForSingleObjectTimeout(backendEvent, 1u);
}

void __cdecl Sys_SetWorkerCmdEvent()
{
    Sys_SetEvent(backendEvent);
}

void __cdecl Sys_ResetWorkerCmdEvent()
{
    Sys_ResetEvent(backendEvent);
}

int __cdecl Sys_WaitBackendEvent()
{
    return Sys_WaitForSingleObjectTimeout(&backendEvent[1], 0);
}

void __cdecl Sys_SetUpdateSpotLightEffectEvent()
{
    Sys_SetEvent(&updateSpotLightEffectEvent);
}

void __cdecl Sys_ResetUpdateSpotLightEffectEvent()
{
    Sys_ResetEvent(&updateSpotLightEffectEvent);
}

void __cdecl Sys_WaitUpdateNonDependentEffectsCompleted()
{
    Sys_WaitForSingleObject(&updateEffectsEvent);
}

void __cdecl Sys_SetUpdateNonDependentEffectsEvent()
{
    Sys_SetEvent(&updateEffectsEvent);
}

void __cdecl Sys_ResetUpdateNonDependentEffectsEvent()
{
    Sys_ResetEvent(&updateEffectsEvent);
}

void __cdecl Sys_FreezeOtherThreadsForCrash()
{
    for (int32_t contextIndex = 0;
         contextIndex < THREAD_CONTEXT_COUNT;
         ++contextIndex)
    {
        SysThreadHandle const thread = threadHandle[contextIndex];
        if (!thread || Sys_ThreadIsCurrent(thread))
            continue;

        (void)Sys_ThreadForceSuspendForCrash(thread);
    }
}

void __cdecl Sys_ReleaseThreadOwnership()
{
    if (Sys_WaitForSingleObjectTimeout(&noThreadOwnershipEvent, 0))
        MyAssertHandler(
            ".\\qcommon\\threads.cpp",
            2000,
            0,
            "%s",
            "!Sys_WaitForSingleObjectTimeout( &noThreadOwnershipEvent, 0 )");
    Sys_SetEvent(&noThreadOwnershipEvent);
}

static SysEventHandle g_cinematicsThreadOutstandingRequestEvent;
static SysEventHandle g_cinematicsHostOutstandingRequestEvent;


char __cdecl Sys_SpawnCinematicsThread(SysThreadContextEntry function)
{
    Sys_CreateEvent(1, 1, &g_cinematicsThreadOutstandingRequestEvent);
    Sys_CreateEvent(1, 0, &g_cinematicsHostOutstandingRequestEvent);
    if (!Sys_CreateThread(function, THREAD_CONTEXT_CINEMATIC))
        return 0;
    Sys_StartThread(THREAD_CONTEXT_CINEMATIC);
    return 1;
}

bool __cdecl Sys_WaitForCinematicsThreadOutstandingRequestEventTimeout(uint32_t timeoutMsec)
{
    return Sys_WaitForSingleObjectTimeout(&g_cinematicsThreadOutstandingRequestEvent, timeoutMsec);
}

void __cdecl Sys_SetCinematicsThreadOutstandingRequestEvent()
{
    Sys_SetEvent(&g_cinematicsThreadOutstandingRequestEvent);
}

void __cdecl Sys_ResetCinematicsThreadOutstandingRequestEvent()
{
    Sys_ResetEvent(&g_cinematicsThreadOutstandingRequestEvent);
}

bool __cdecl Sys_WaitForCinematicsHostOutstandingRequestEventTimeout(uint32_t timeoutMsec)
{
    return Sys_WaitForSingleObjectTimeout(&g_cinematicsHostOutstandingRequestEvent, timeoutMsec);
}

void __cdecl Sys_SetCinematicsHostOutstandingRequestEvent()
{
    Sys_SetEvent(&g_cinematicsHostOutstandingRequestEvent);
}

void __cdecl Sys_ResetCinematicsHostOutstandingRequestEvent()
{
    Sys_ResetEvent(&g_cinematicsHostOutstandingRequestEvent);
}

void __cdecl Win_SetThreadLock(WinThreadLock threadLock)
{
    if (threadLock < THREAD_LOCK_NONE || threadLock > THREAD_LOCK_ALL)
    {
        iassert(threadLock >= THREAD_LOCK_NONE && threadLock <= THREAD_LOCK_ALL);
        return;
    }

    if (threadLock == s_threadLock)
        return;

    // Main capture and thread creation apply the current policy directly, so
    // an unchanged enum does not need per-frame native affinity calls.
    s_threadLock = threadLock;
    Sys_ApplyThreadAffinityToAll();
}

WinThreadLock __cdecl Win_GetThreadLock()
{
    return s_threadLock;
}

void KISAK_CDECL Win_UpdateThreadLock()
{
    if (Sys_GetCpuCount() == 1)
    {
        Win_SetThreadLock(THREAD_LOCK_ALL);
    }
    else if (Sys_IsUsingAnyRenderProfile())
    {
        Win_SetThreadLock(THREAD_LOCK_ALL);
    }
    else
    {
        WinThreadLock threadLock = (WinThreadLock)sys_lockThreads->current.integer;
        if (threadLock == THREAD_LOCK_NONE && Sys_IsUsingAdaptiveGpuSync())
            threadLock = THREAD_LOCK_MINIMAL;
        Win_SetThreadLock(threadLock);
    }
}

void __cdecl Sys_BeginLoadThreadPriorities()
{
    iassert(Sys_IsMainThread());
    if (threadHandle[THREAD_CONTEXT_MAIN])
    {
        (void)Sys_ThreadSetPriority(
            threadHandle[THREAD_CONTEXT_MAIN],
            SysThreadPriority::BelowNormal);
    }
}

#ifdef KISAK_SP
int KISAK_CDECL Sys_WaitStartServer(uint32_t timeout)
{
    int v2; // r3
    int v3; // r30

    Sys_EnterCriticalSection(CRITSECT_START_SERVER);
    v2 = Sys_WaitForSingleObjectTimeout(&wakeServerEvent, timeout);
    v3 = v2;
    if (isDoingDatabaseInit)
    {
        v3 = 0;
    }
    else if (v2)
    {
        Sys_ResetEvent(&serverCompletedEvent);
    }
    Sys_LeaveCriticalSection(CRITSECT_START_SERVER);
    return v3;
}

void KISAK_CDECL Sys_InitServerEvents()
{
    Sys_ResetEvent(&wakeServerEvent);
    Sys_ResetEvent(&serverCompletedEvent);
    Sys_SetEvent(&allowSendClientMessagesEvent);
    Sys_ResetEvent(&serverSnapshotEvent);
    Sys_SetEvent(&clientMessageReceived);
    g_timeout.store(0, std::memory_order_seq_cst);
}

void KISAK_CDECL Sys_ClientMessageReceived()
{
    Sys_SetEvent(&clientMessageReceived);
}
void KISAK_CDECL Sys_ClearClientMessage()
{
    Sys_ResetEvent(&clientMessageReceived);
}
int KISAK_CDECL Sys_SpawnServerThread(SysThreadContextEntry function)
{
    Sys_CreateEvent(true, false, &wakeServerEvent);
    Sys_CreateEvent(true, false, &serverCompletedEvent);
    Sys_CreateEvent(true, false, &allowSendClientMessagesEvent);
    Sys_CreateEvent(false, false, &serverSnapshotEvent);
    Sys_CreateEvent(true, true, &clientMessageReceived);
    if (!Sys_CreateThread(function, THREAD_CONTEXT_SERVER))
        return 0;

    Sys_StartThread(THREAD_CONTEXT_SERVER);
    return 1;
}
void KISAK_CDECL Sys_WaitClientMessageReceived()
{
    uint32_t v0; // r8

    PROF_SCOPED("wait receive msg");
    Sys_WaitForSingleObject(&clientMessageReceived);
}
void KISAK_CDECL Sys_ServerSnapshotCompleted()
{
    Sys_SetEvent(&serverSnapshotEvent);
}
bool KISAK_CDECL Sys_WaitServerSnapshot()
{
    PROF_SCOPED("wait snapshot");
    return Sys_WaitForSingleObjectTimeout(&serverSnapshotEvent, 1);
}
void KISAK_CDECL Sys_AllowSendClientMessages()
{
    Sys_SetEvent(&allowSendClientMessagesEvent);
}
void KISAK_CDECL Sys_DisallowSendClientMessages()
{
    Sys_ResetEvent(&allowSendClientMessagesEvent);
}
int KISAK_CDECL Sys_CanSendClientMessages()
{
    return Sys_WaitForSingleObjectTimeout(&allowSendClientMessagesEvent, 0);
}
void KISAK_CDECL Sys_ServerCompleted()
{
    Sys_SetEvent(&serverCompletedEvent);
}
int KISAK_CDECL Sys_ServerTimeout()
{
    const int32_t time = g_timeout.load(std::memory_order_seq_cst);

    if (!time)
    {
        return 1;
    }

    int timeMS = Sys_Milliseconds();
    if (timeMS - g_timeout.load(std::memory_order_seq_cst) >= 0)
    {
        int nextTimeout = timeMS - (int)(-50.0f / com_timescaleValue);

        while (true)
        {
            int32_t current = g_timeout.load(std::memory_order_seq_cst);
            if (current != time)
                break;

            if (g_timeout.compare_exchange_strong(
                    current,
                    nextTimeout,
                    std::memory_order_seq_cst,
                    std::memory_order_seq_cst))
            {
                return 1;
            }
        }

        // timeout modified by someone else
        return 1;
    }


    return 0;
}
void KISAK_CDECL Sys_WakeServer()
{
    Sys_SetEvent(&wakeServerEvent);
}
bool KISAK_CDECL Sys_WaitServer()
{
    PROF_SCOPED("wait server");
    return Sys_WaitForSingleObjectTimeout(&serverCompletedEvent, 1);
}
void KISAK_CDECL Sys_SleepServer()
{
    bool v0; // r30

    //PIXBeginNamedEvent_Copy_NoVarArgs(0xFFFFFFFF, "sleep server");
    v0 = Sys_WaitForSingleObjectTimeout(&wakeServerEvent, 0);
    //PIXEndNamedEvent();
    if (v0)
    {
        Sys_EnterCriticalSection(CRITSECT_START_SERVER);
        Sys_ResetEvent(&wakeServerEvent);
        Sys_LeaveCriticalSection(CRITSECT_START_SERVER);
    }
}
void KISAK_CDECL Sys_SetServerTimeout(int timeout)
{
    int timeMS; // r3

    iassert(timeout >= 0);
    iassert(com_timescaleValue);

    if (timeout)
    {
        //a12 = (int)(float)((float)__SPAIR64__(&a12, timeout) / (float)v13);
        int val = (int)((float)timeout / com_timescaleValue);
        timeMS = Sys_Milliseconds();
        const int32_t currentTimeout =
            g_timeout.load(std::memory_order_seq_cst);
        if (currentTimeout
            && timeMS - currentTimeout < 0
            && currentTimeout - (timeMS + val) <= 0)
        {
            //PIXSetMarker(0xFFFFFFFF, "ignore server timeout: %d", a12);
        }
        else
        {
            g_timeout.store(timeMS + val, std::memory_order_seq_cst);
            //PIXSetMarker(0xFFFFFFFF, "server timeout: %d", a12);
        }
    }
    else
    {
        g_timeout.store(0, std::memory_order_seq_cst);
        //PIXSetMarker(0xFFFFFFFF, "server timeout");
    }
}

bool KISAK_CDECL Sys_WaitForSaveHistoryDone()
{
    return Sys_WaitForSingleObjectTimeout(&g_saveHistoryDoneEvent, 0x7D0u);
}

int KISAK_CDECL Sys_SpawnServerDemoThread(SysThreadContextEntry function)
{
    Sys_CreateEvent(false, false, &g_saveHistoryEvent);
    Sys_CreateEvent(false, false, &g_saveHistoryDoneEvent);
    if (!Sys_CreateThread(function, THREAD_CONTEXT_SERVER_DEMO))
        return 0;

    Sys_StartThread(THREAD_CONTEXT_SERVER_DEMO);
    return 1;
}

void KISAK_CDECL Sys_SetSaveHistoryEvent()
{
    Sys_SetEvent(&g_saveHistoryEvent);
}

void KISAK_CDECL Sys_WaitForSaveHistory()
{
    Sys_WaitForSingleObject(&g_saveHistoryEvent);
}

void KISAK_CDECL Sys_SetSaveHistoryDoneEvent()
{
    Sys_SetEvent(&g_saveHistoryDoneEvent);
}
#endif
