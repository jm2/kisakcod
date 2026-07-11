#pragma once

#include <cstdint>

#include <qcommon/sys_event.h>
#include <qcommon/sys_thread.h>
#include <qcommon/sys_time.h>
#include <qcommon/thread_context.h>

enum ThreadOwner : std::int32_t
{                                       // ...
    THREAD_OWNER_NONE = 0x0,
    THREAD_OWNER_DATABASE = 0x1,
    THREAD_OWNER_CINEMATICS = 0x2,
};

using SysThreadContextEntry = void (KISAK_CDECL *)(std::uint32_t);

std::uint32_t __cdecl Sys_GetCpuCount();
void __cdecl Sys_InitMainThread();
char __cdecl Sys_SpawnRenderThread(SysThreadContextEntry function);
char __cdecl Sys_SpawnDatabaseThread(SysThreadContextEntry function);
void __cdecl Sys_SuspendDatabaseThread(ThreadOwner owner);
void __cdecl Sys_ResumeDatabaseThread(ThreadOwner owner);
bool __cdecl Sys_HaveSuspendedDatabaseThread(ThreadOwner owner);
void __cdecl Sys_WaitDatabaseThread();
bool __cdecl Sys_SpawnWorkerThread(
    SysThreadContextEntry function,
    std::uint32_t threadIndex);
void __cdecl Sys_SetWorkerThreadActive(
    std::uint32_t workerIndex,
    bool active);
void __cdecl Sys_WorkerThreadPausePoint(ThreadContext_t threadContext);
void *__cdecl Sys_RendererSleep();
int __cdecl Sys_RendererReady();
void __cdecl Sys_RenderCompleted();
void __cdecl Sys_FrontEndSleep();
void __cdecl Sys_WakeRenderer(void* data);
void __cdecl Sys_NotifyRenderer();
void __cdecl Sys_DatabaseCompleted();
void __cdecl Sys_WaitStartDatabase();
bool __cdecl Sys_IsDatabaseReady();
void __cdecl Sys_SyncDatabase();
void __cdecl Sys_WakeDatabase();
void __cdecl Sys_NotifyDatabase();
void __cdecl Sys_DatabaseCompleted2();
bool __cdecl Sys_IsDatabaseReady2();
void __cdecl Sys_WakeDatabase2();
bool __cdecl Sys_FinishRenderer();
int __cdecl Sys_IsMainThreadReady();
void __cdecl Sys_EndLoadThreadPriorities();
void __cdecl Sys_WaitForMainThread();
void __cdecl Sys_StopRenderer();
void __cdecl Sys_StartRenderer();
bool __cdecl Sys_IsRenderThread();
bool __cdecl Sys_IsDatabaseThread();
bool __cdecl Sys_IsMainThread();
#ifdef KISAK_SP
bool KISAK_CDECL Sys_IsServerThread();
#endif
void __cdecl Sys_SetValue(int valueIndex, void* data);
void* __cdecl Sys_GetValue(int valueIndex);
void __cdecl Sys_WaitForWorkerCmd();
void __cdecl Sys_SetWorkerCmdEvent();
void __cdecl Sys_ResetWorkerCmdEvent();
int __cdecl Sys_WaitBackendEvent();
void __cdecl Sys_SetUpdateSpotLightEffectEvent();
void __cdecl Sys_ResetUpdateSpotLightEffectEvent();
void __cdecl Sys_WaitUpdateNonDependentEffectsCompleted();
void __cdecl Sys_SetUpdateNonDependentEffectsEvent();
void __cdecl Sys_ResetUpdateNonDependentEffectsEvent();
void __cdecl Sys_FreezeOtherThreadsForCrash();
void __cdecl Sys_ReleaseThreadOwnership();
char __cdecl Sys_SpawnCinematicsThread(SysThreadContextEntry function);
bool __cdecl Sys_WaitForCinematicsThreadOutstandingRequestEventTimeout(
    std::uint32_t timeoutMsec);
void __cdecl Sys_SetCinematicsThreadOutstandingRequestEvent();
void __cdecl Sys_ResetCinematicsThreadOutstandingRequestEvent();
bool __cdecl Sys_WaitForCinematicsHostOutstandingRequestEventTimeout(
    std::uint32_t timeoutMsec);
void __cdecl Sys_SetCinematicsHostOutstandingRequestEvent();
void __cdecl Sys_ResetCinematicsHostOutstandingRequestEvent();

int __cdecl Sys_IsRendererReady();
void __cdecl Sys_BeginLoadThreadPriorities();

#ifdef KISAK_SP
int KISAK_CDECL Sys_WaitStartServer(std::uint32_t timeout);
void KISAK_CDECL Sys_InitServerEvents();
void KISAK_CDECL Sys_ClientMessageReceived();
void KISAK_CDECL Sys_ClearClientMessage();
int KISAK_CDECL Sys_SpawnServerThread(SysThreadContextEntry function);
void KISAK_CDECL Sys_WaitClientMessageReceived();
void KISAK_CDECL Sys_ServerSnapshotCompleted();
bool KISAK_CDECL Sys_WaitServerSnapshot();
void KISAK_CDECL Sys_AllowSendClientMessages();
void KISAK_CDECL Sys_DisallowSendClientMessages();
int KISAK_CDECL Sys_CanSendClientMessages();
void KISAK_CDECL Sys_ServerCompleted();
int KISAK_CDECL Sys_ServerTimeout();
void KISAK_CDECL Sys_WakeServer();
void KISAK_CDECL Sys_SleepServer();
bool KISAK_CDECL Sys_WaitServer();
void KISAK_CDECL Sys_SetServerTimeout(int timeout);
bool KISAK_CDECL Sys_WaitForSaveHistoryDone();
int KISAK_CDECL Sys_SpawnServerDemoThread(SysThreadContextEntry function);
void KISAK_CDECL Sys_SetSaveHistoryEvent();
void KISAK_CDECL Sys_WaitForSaveHistory();
void KISAK_CDECL Sys_SetSaveHistoryDoneEvent();
#endif


enum WinThreadLock : std::int32_t
{                                       // ...
    THREAD_LOCK_NONE = 0x0,
    THREAD_LOCK_MINIMAL = 0x1,
    THREAD_LOCK_ALL = 0x2,
};
void __cdecl Win_SetThreadLock(WinThreadLock threadLock);
WinThreadLock __cdecl Win_GetThreadLock();
void KISAK_CDECL Win_UpdateThreadLock();
