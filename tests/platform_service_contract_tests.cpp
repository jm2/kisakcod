#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <qcommon/sys_event.h>
#include <qcommon/sys_sync.h>
#include <qcommon/sys_thread.h>
#include <qcommon/sys_time.h>
#include <qcommon/sys_worker_gate.h>
#include <qcommon/threads.h>

static_assert(std::is_same_v<std::underlying_type_t<CriticalSection>, std::int32_t>);
static_assert(sizeof(CriticalSection) == sizeof(std::int32_t));
static_assert(std::is_standard_layout_v<FastCriticalSection>);
static_assert(sizeof(FastCriticalSection) == 8);
static_assert(offsetof(FastCriticalSection, readCount) == 0);
static_assert(offsetof(FastCriticalSection, writeCount) == 4);
static_assert(std::is_pointer_v<SysEventHandle>);
static_assert(std::is_same_v<SysEventHandle, SysEvent *>);
static_assert(sizeof(SysEventHandle) == sizeof(void *));
static_assert(std::is_pointer_v<SysThreadHandle>);
static_assert(std::is_same_v<SysThreadHandle, SysThread *>);
static_assert(sizeof(SysThreadHandle) == sizeof(void *));
static_assert(std::is_pointer_v<SysWorkerGateHandle>);
static_assert(std::is_same_v<SysWorkerGateHandle, SysWorkerGate *>);
static_assert(sizeof(SysWorkerGateHandle) == sizeof(void *));
static_assert(std::is_same_v<
    std::underlying_type_t<ThreadContext_t>,
    std::int32_t>);
static_assert(std::is_same_v<
    std::underlying_type_t<ThreadOwner>,
    std::int32_t>);
static_assert(std::is_same_v<
    std::underlying_type_t<WinThreadLock>,
    std::int32_t>);
static_assert(std::is_same_v<
    std::remove_extent_t<decltype(threadId)>,
    std::uint32_t>);
static_assert(std::is_same_v<
    std::remove_extent_t<decltype(threadHandle)>,
    void *>);

using MillisecondsFunction = std::uint32_t (KISAK_CDECL *)();
using SleepFunction = void (KISAK_CDECL *)(std::uint32_t);
using InitializeCriticalSectionsFunction = void (KISAK_CDECL *)();
using CriticalSectionFunction = void (KISAK_CDECL *)(int);
using FastCriticalSectionFunction = void (KISAK_CDECL *)(FastCriticalSection *);
using FastCriticalSectionQueryFunction =
    bool (KISAK_CDECL *)(const FastCriticalSection *);
using CreateEventFunction =
    void (KISAK_CDECL *)(bool, bool, SysEventHandle *);
using EventFunction = void (KISAK_CDECL *)(SysEventHandle *);
using WaitEventTimeoutFunction =
    bool (KISAK_CDECL *)(SysEventHandle *, std::uint32_t);
using ThreadCaptureFunction =
    bool (KISAK_CDECL *)(const char *, SysThreadHandle *);
using ThreadCreateFunction = bool (KISAK_CDECL *)(
    SysThreadEntry,
    void *,
    const char *,
    SysThreadHandle *);
using ThreadActionFunction = void (KISAK_CDECL *)(SysThreadHandle);
using ThreadQueryFunction = bool (KISAK_CDECL *)(SysThreadHandle);
using ThreadJoinTimeoutFunction =
    bool (KISAK_CDECL *)(SysThreadHandle, std::uint32_t);
using ThreadDestroyFunction = void (KISAK_CDECL *)(SysThreadHandle *);
using ExpectedThreadEntry = void (KISAK_CDECL *)(void *);
using WorkerGateHandleFunction = void (KISAK_CDECL *)(SysWorkerGateHandle *);
using WorkerGateActionFunction = void (KISAK_CDECL *)(SysWorkerGateHandle);
using WorkerGateQueryFunction = bool (KISAK_CDECL *)(SysWorkerGateHandle);

static_assert(std::is_same_v<decltype(&Sys_Milliseconds), MillisecondsFunction>);
static_assert(std::is_same_v<decltype(&Sys_MillisecondsRaw), MillisecondsFunction>);
static_assert(std::is_same_v<decltype(&Sys_Sleep), SleepFunction>);
static_assert(std::is_same_v<
    decltype(&Sys_InitializeCriticalSections),
    InitializeCriticalSectionsFunction>);
static_assert(std::is_same_v<decltype(&Sys_EnterCriticalSection), CriticalSectionFunction>);
static_assert(std::is_same_v<decltype(&Sys_LeaveCriticalSection), CriticalSectionFunction>);
static_assert(std::is_same_v<decltype(&Sys_LockRead), FastCriticalSectionFunction>);
static_assert(std::is_same_v<decltype(&Sys_UnlockRead), FastCriticalSectionFunction>);
static_assert(std::is_same_v<decltype(&Sys_LockWrite), FastCriticalSectionFunction>);
static_assert(std::is_same_v<decltype(&Sys_UnlockWrite), FastCriticalSectionFunction>);
static_assert(std::is_same_v<
    decltype(&Sys_IsWriteLocked),
    FastCriticalSectionQueryFunction>);
static_assert(std::is_same_v<decltype(&Sys_CreateEvent), CreateEventFunction>);
static_assert(std::is_same_v<decltype(&Sys_DestroyEvent), EventFunction>);
static_assert(std::is_same_v<decltype(&Sys_SetEvent), EventFunction>);
static_assert(std::is_same_v<decltype(&Sys_ResetEvent), EventFunction>);
static_assert(std::is_same_v<decltype(&Sys_WaitForSingleObject), EventFunction>);
static_assert(std::is_same_v<
    decltype(&Sys_WaitForSingleObjectTimeout),
    WaitEventTimeoutFunction>);
static_assert(std::is_same_v<SysThreadEntry, ExpectedThreadEntry>);
static_assert(std::is_same_v<
    decltype(&Sys_ThreadCaptureCurrent),
    ThreadCaptureFunction>);
static_assert(std::is_same_v<
    decltype(&Sys_ThreadCreateSuspended),
    ThreadCreateFunction>);
static_assert(std::is_same_v<decltype(&Sys_ThreadStart), ThreadActionFunction>);
static_assert(std::is_same_v<decltype(&Sys_ThreadIsCurrent), ThreadQueryFunction>);
static_assert(std::is_same_v<
    decltype(&Sys_ThreadJoinTimeout),
    ThreadJoinTimeoutFunction>);
static_assert(std::is_same_v<decltype(&Sys_ThreadJoin), ThreadActionFunction>);
static_assert(std::is_same_v<decltype(&Sys_ThreadDestroy), ThreadDestroyFunction>);
static_assert(std::is_same_v<
    decltype(&Sys_WorkerGateCreate),
    WorkerGateHandleFunction>);
static_assert(std::is_same_v<
    decltype(&Sys_WorkerGateDestroy),
    WorkerGateHandleFunction>);
static_assert(std::is_same_v<
    decltype(&Sys_WorkerGateActivate),
    WorkerGateQueryFunction>);
static_assert(std::is_same_v<
    decltype(&Sys_WorkerGateRequestPause),
    WorkerGateQueryFunction>);
static_assert(std::is_same_v<
    decltype(&Sys_WorkerGateWaitPaused),
    WorkerGateActionFunction>);
static_assert(std::is_same_v<
    decltype(&Sys_WorkerGatePausePoint),
    WorkerGateActionFunction>);
static_assert(std::is_same_v<
    decltype(&Sys_WorkerGateIsActive),
    WorkerGateQueryFunction>);

#if defined(KISAK_MP)
static_assert(CRITSECT_CONSOLE == 0x0);
static_assert(CRITSECT_SYS_EVENT_QUEUE == 0xA);
static_assert(CRITSECT_FATAL_ERROR == 0xC);
static_assert(CRITSECT_CINEMATIC_TARGET_CHANGE == 0x13);
static_assert(CRITSECT_CBUF == 0x15);
static_assert(CRITSECT_COUNT == 0x16);
#elif defined(KISAK_SP)
static_assert(CRITSECT_CONSOLE == 0x0);
static_assert(CRITSECT_SOUND_ALLOC == 0x4);
static_assert(CRITSECT_SCRIPT_STRING == 0x13);
static_assert(CRITSECT_CBUF == 0x1F);
static_assert(CRITSECT_SYS_EVENT_QUEUE == 0x20);
static_assert(CRITSECT_FATAL_ERROR == 0x21);
static_assert(CRITSECT_GPU_FENCE == 0x22);
static_assert(CRITSECT_COUNT == 0x23);
#else
#error "Platform service contract tests require KISAK_MP or KISAK_SP"
#endif

int main()
{
	return 0;
}
