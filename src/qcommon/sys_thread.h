#pragma once

#include <cstdint>

#include <universal/platform_compat.h>

struct SysThread;
using SysThreadHandle = SysThread *;
using SysThreadEntry = void (KISAK_CDECL *)(void *);

enum class SysThreadPriority : std::uint8_t
{
    BelowNormal,
    Normal,
    AboveNormal,
};

enum class SysThreadPolicyStatus : std::uint8_t
{
    Applied,
    Unsupported,
    PermissionDenied,
    Unavailable,
};

enum class SysThreadCrashFreezeStatus : std::uint8_t
{
    Frozen,
    CurrentThread,
    AlreadyStopped,
    Unsupported,
    Failed,
};

// Captures the calling thread as a non-joinable handle. The output pointer must
// be valid and must contain a null handle.
bool KISAK_CDECL Sys_ThreadCaptureCurrent(
    const char *name,
    SysThreadHandle *outThread);

// Creates a joinable thread whose callback cannot run until Sys_ThreadStart.
// The output pointer must be valid and must contain a null handle.
bool KISAK_CDECL Sys_ThreadCreateSuspended(
    SysThreadEntry entry,
    void *userData,
    const char *name,
    SysThreadHandle *outThread);

// Starts a created thread exactly once.
void KISAK_CDECL Sys_ThreadStart(SysThreadHandle thread);

bool KISAK_CDECL Sys_ThreadIsCurrent(SysThreadHandle thread);

// Returns true only after the created thread has been reaped. Returns false
// only when the finite timeout expires.
bool KISAK_CDECL Sys_ThreadJoinTimeout(
    SysThreadHandle thread,
    std::uint32_t msec);

void KISAK_CDECL Sys_ThreadJoin(SysThreadHandle thread);

// Captured handles may be destroyed immediately. Created handles must first be
// joined. On success, releases all resources and resets the handle to null.
void KISAK_CDECL Sys_ThreadDestroy(SysThreadHandle *thread);

// Returns the number of logical processors currently represented by the
// backend's eligible-processor set. The result is always at least one.
std::uint32_t KISAK_CDECL Sys_ThreadGetEligibleProcessorCount();

SysThreadPolicyStatus KISAK_CDECL Sys_ThreadSetPriority(
    SysThreadHandle thread,
    SysThreadPriority priority);

// Processor ordinals index the backend-owned eligible-processor set; they are
// not native processor numbers or bits in a public affinity mask.
SysThreadPolicyStatus KISAK_CDECL Sys_ThreadPinToEligibleProcessor(
    SysThreadHandle thread,
    std::uint32_t ordinal);

SysThreadPolicyStatus KISAK_CDECL Sys_ThreadClearAffinity(
    SysThreadHandle thread);

// Terminal crash handling only. This deliberately has no resume counterpart.
SysThreadCrashFreezeStatus KISAK_CDECL Sys_ThreadForceSuspendForCrash(
    SysThreadHandle thread);
