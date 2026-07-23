#pragma once

#include <cstddef>
#include <cstdint>

#include <universal/platform_compat.h>

// Process service -- portable launch, wait, terminate, signal-park, and
// crash-freeze API. Each backend selects the native primitive that best
// matches the contract documented in the corresponding section below; the
// header does not expose any platform-specific type or include.
//
// All handle-taking operations treat a null handle as InvalidArgument. The
// caller stores a null SysProcessHandle value on entry to Sys_ProcessLaunch;
// on success the backend writes a non-null handle and the caller passes that
// handle to all subsequent operations. Sys_ProcessRelease resets the
// caller's pointer to null on destruction.

struct SysProcess;
using SysProcessHandle = SysProcess *;

enum class SysProcessLaunchStatus : std::uint8_t
{
    Started,
    InvalidArgument,
    SpawnFailed,
};

enum class SysProcessWaitStatus : std::uint8_t
{
    Exited,
    TimedOut,
    InvalidHandle,
};

enum class SysProcessTerminateStatus : std::uint8_t
{
    Signaled,
    InvalidHandle,
    Unsupported,
};

// Launches a child process running `executablePath` with `arguments`. The
// arguments string is a single shell-style command line; the backend is
// responsible for any platform-specific quoting. The portable semantics
// match the POSIX shell's word-splitting rules for the empty-string case.
// On success *outHandle is set to a non-null SysProcessHandle and the
// caller is responsible for invoking Sys_ProcessRelease.
//
// The backend searches PATH for `executablePath`; callers needing exact
// resolution should pass an absolute path. The child inherits the caller's
// stdio, environment, and signal mask -- redirected I/O is layered on top
// by the engine rather than baked into the service.
SysProcessLaunchStatus KISAK_CDECL Sys_ProcessLaunch(
    const char *executablePath,
    const char *arguments,
    SysProcessHandle *outHandle);

// Waits up to `timeoutMs` for `handle` to exit. Returns Exited when the
// child terminated normally and TimedOut when the timeout expires with
// the child still running. The function does not reap the child on
// TimedOut -- the caller may call it again with a larger timeout or
// invoke Sys_ProcessTerminate to ask the backend to force termination.
SysProcessWaitStatus KISAK_CDECL Sys_ProcessWait(
    SysProcessHandle handle,
    std::uint32_t timeoutMs);

// Signals the child to terminate. POSIX raises SIGTERM then escalates
// to SIGKILL; Win32 calls TerminateProcess with `exitCode`. The function
// is best-effort -- it returns Signaled when the signal was delivered
// without waiting for the child to actually exit. Callers that need the
// child to be reaped should follow up with Sys_ProcessWait / Sys_ProcessRelease.
SysProcessTerminateStatus KISAK_CDECL Sys_ProcessTerminate(
    SysProcessHandle handle,
    std::uint32_t exitCode);

// Captures the child's exit status when it has terminated normally. The
// function returns true only when the child has exited and a captured
// status is available; on a still-running child, signaled child, or
// invalid handle it returns false without modifying *exitCode.
bool KISAK_CDECL Sys_ProcessGetExitCode(
    SysProcessHandle handle,
    std::uint32_t *exitCode);

// Releases the backend resources associated with *handle and resets the
// caller's pointer to null. Safe to call with a null handle or null
// pointer; both are treated as no-ops.
void KISAK_CDECL Sys_ProcessRelease(SysProcessHandle *handle);

// Signal-park service. POSIX backends temporarily block a caller-supplied
// set of signals inside the calling thread; nested parks for disjoint
// sets stack and the matching UnPark releases only its own set. Re-entry
// for the same set is rejected with AlreadyParked. Win32 has no
// first-class signal concept and the backend returns Unsupported.
enum class SysSignalParkStatus : std::uint8_t
{
    Parked,
    AlreadyParked,
    Unparked,
    InvalidArgument,
    Unsupported,
    Failed,
};

[[nodiscard]] SysSignalParkStatus KISAK_CDECL Sys_SignalPark(
    const int *signals,
    std::size_t signalCount) noexcept;

[[nodiscard]] SysSignalParkStatus KISAK_CDECL Sys_SignalUnPark(
    const int *signals,
    std::size_t signalCount) noexcept;

// Whole-process crash-freeze. The macOS backend installs a Mach
// exception handler, parks the calling thread via thread_suspend, and
// blocks until the process is terminated by an external signal. On
// non-macOS hosts the backend returns Unsupported; per-thread freeze
// is owned by a separate API. The process is never resumed by design;
// the contract is that the process terminates while frozen.
enum class SysProcessFreezeStatus : std::uint8_t
{
    Frozen,
    Unsupported,
    Failed,
};

[[nodiscard]] SysProcessFreezeStatus KISAK_CDECL Sys_ProcessFreezeForCrash() noexcept;
