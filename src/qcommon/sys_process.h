#pragma once

#include <cstdint>

#include <universal/platform_compat.h>

// Process service API.
//
// The portable layer exposes a single, opaque handle per child process plus a
// small set of lifecycle operations that mirror the underlying native
// primitives. The implementation backends in src/_platform/<backend>/ provide:
//   * POSIX: fork(2) + execve(2) with sigprocmask(2)-based signal parking.
//   * Win32: CreateProcessW + WaitForSingleObject + TerminateProcess.
//   * macOS: process launch plus Mach task/thread suspend for crash freezing.
//
// All operations that take an outHandle require the caller to pass a pointer
// that holds a null handle on entry; the backend writes a non-null handle on
// success and leaves the pointer untouched on failure. Sys_ProcessRelease
// destroys the native handle and resets the caller's pointer to null.

struct SysProcess;
using SysProcessHandle = SysProcess *;

enum class SysProcessLaunchStatus : std::uint8_t
{
    Started,
    InvalidArgument,
    LaunchFailed,
};

enum class SysProcessWaitStatus : std::uint8_t
{
    Exited,
    Signaled,
    TimedOut,
    InvalidHandle,
};

enum class SysProcessTerminateStatus : std::uint8_t
{
    Signaled,
    InvalidHandle,
    Unsupported,
    Failed,
};

// Launches a child process running `executablePath` with `arguments`.
// `arguments` is a single, already-quoted command line as it should appear on
// the receiving side. The backend refuses to spawn the literal string "quit"
// so a misconfigured caller cannot accidentally terminate the parent.
SysProcessLaunchStatus KISAK_CDECL Sys_ProcessLaunch(
    const char *executablePath,
    const char *arguments,
    SysProcessHandle *outHandle);

// Waits up to `timeoutMs` for `handle` to exit. Returns Exited when the child
// terminated normally, Signaled when it was killed by a signal/term request,
// and TimedOut only when the finite timeout expires.
SysProcessWaitStatus KISAK_CDECL Sys_ProcessWait(
    SysProcessHandle handle,
    std::uint32_t timeoutMs);

// Sends a terminate request. POSIX raises SIGTERM; Win32 calls
// TerminateProcess with `exitCode`. Always returns InvalidHandle on a null
// handle; Unsupported when the backend has no termination primitive.
SysProcessTerminateStatus KISAK_CDECL Sys_ProcessTerminate(
    SysProcessHandle handle,
    std::uint32_t exitCode);

// Fills *exitCode with the captured exit status when the child has exited
// normally and the handle is still valid. Returns false on any other state
// (still running, signaled, invalid handle, etc.).
bool KISAK_CDECL Sys_ProcessGetExitCode(
    SysProcessHandle handle,
    std::uint32_t *exitCode);

// Releases the backend resources, then resets *handle to null. Safe to call
// with a null handle or null pointer; both are treated as no-ops.
void KISAK_CDECL Sys_ProcessRelease(SysProcessHandle *handle);

// Linux signal-park: blocks all synchronous signals that the engine handles
// at user-space boundaries (SIGINT/SIGTERM/SIGHUP/SIGPIPE/SIGCHLD/etc.) for
// the calling thread. On Win32 and platforms without signal-park semantics,
// the call is a no-op and returns Unsupported.
enum class SysSignalParkStatus : std::uint8_t
{
    Entered,
    AlreadyParked,
    Unsupported,
};

enum class SysSignalParkLeaveStatus : std::uint8_t
{
    Left,
    NotParked,
    Unsupported,
};

SysSignalParkStatus KISAK_CDECL Sys_SignalParkEnter();
SysSignalParkLeaveStatus KISAK_CDECL Sys_SignalParkLeave();

// macOS Mach crash-freezing. Suspends every Mach thread in the target task
// without sending an exception, so the kernel keeps the crashed state on ice
// while an out-of-band reporter captures a stack. On non-macOS platforms the
// status returns Unsupported.
enum class SysMachCrashFreezeStatus : std::uint8_t
{
    Frozen,
    PermissionDenied,
    AlreadyFrozen,
    Unsupported,
    Failed,
};

SysMachCrashFreezeStatus KISAK_CDECL Sys_MachCrashFreezeSuspend(
    SysProcessHandle handle);

SysMachCrashFreezeStatus KISAK_CDECL Sys_MachCrashFreezeResume(
    SysProcessHandle handle);
