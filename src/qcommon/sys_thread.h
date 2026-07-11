#pragma once

#include <cstdint>

#include <universal/platform_compat.h>

struct SysThread;
using SysThreadHandle = SysThread *;
using SysThreadEntry = void (KISAK_CDECL *)(void *);

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
