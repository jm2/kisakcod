#pragma once

#include <cstddef>
#include <cstdint>

#include <universal/platform_compat.h>

// Process service -- launch + wait + bounded terminate. All three backends
// (POSIX fork/exec, Win32 CreateProcess, macOS posix_spawn) inherit stdio
// from the caller; callers needing redirected I/O layer it on top. The
// argv vector must contain the executable name as argv[0] and be
// null-terminated. environ may be null, in which case the caller's environ
// is inherited.
enum class SysProcessLaunchStatus : std::uint8_t
{
    Completed = 0,
    SpawnFailed,
    InvalidArgument,
    Unavailable,
    WaitFailed,
    TimedOut,
    TerminatedByTimeout,
};

struct SysProcessLaunchResult
{
    SysProcessLaunchStatus status = SysProcessLaunchStatus::Unavailable;
    int exitCode = 0;
};

// Launch and wait up to msec for the child. UINT32_MAX is reserved for the
// no-timeout overload and is rejected here. exitCode is meaningful only
// when status == Completed; for any other status exitCode is zero. SpawnFailed
// means the backend failed to create the child; WaitFailed means wait
// failed; TimedOut means msec elapsed without the child exiting. The engine
// is expected to call Sys_ProcessTerminate on a TimedOut result.
[[nodiscard]] SysProcessLaunchResult KISAK_CDECL Sys_ProcessLaunchAndWait(
    const char *utf8Path,
    char *const *argv,
    char *const *environ,
    std::uint32_t msec) noexcept;

// Signal park service -- temporarily block signal delivery inside the
// calling thread (POSIX pthread_sigmask, macOS pthread_sigmask) until a
// matching Sys_SignalUnPark. On Win32 signals are not a first-class concept
// and the backend returns Unsupported.
//
// Re-entrancy on the same signal set is rejected with AlreadyParked. Nested
// parks for disjoint sets are stacked; the matching UnPark releases only
// its own set. signalCount==0 with signals==nullptr is a no-op that returns
// Parked so a caller that has no signals to block can still enter the park
// boundary.
enum class SysSignalParkStatus : std::uint8_t
{
    Parked = 0,
    AlreadyParked,
    Unparked,
    AlreadyUnparked,
    Unsupported,
    InvalidArgument,
    Failed,
};

[[nodiscard]] SysSignalParkStatus KISAK_CDECL Sys_SignalPark(
    const int *signals,
    std::size_t signalCount) noexcept;

[[nodiscard]] SysSignalParkStatus KISAK_CDECL Sys_SignalUnPark(
    const int *signals,
    std::size_t signalCount) noexcept;

// Crash freeze -- terminal entry point for the engine's post-mortem handler.
// Freezes the entire process so the attached debugger or crash dumper can
// take a snapshot. Has no resume counterpart by design; the process is
// expected to terminate while frozen.
//
// macOS installs a Mach exception handler on the current task via
// task_set_exception_ports and parks the faulting thread through
// thread_suspend so Mach-level post-mortem tooling can attach. On non-macOS
// hosts Sys_ProcessFreezeForCrash returns Unsupported. Win32/POSIX use
// separate per-thread freeze APIs (Sys_ThreadForceSuspendForCrash) instead,
// and Sys_ProcessFreezeForCrash on those backends returns Unsupported.
enum class SysProcessFreezeStatus : std::uint8_t
{
    Frozen = 0,
    AlreadyFrozen,
    Unsupported,
    PermissionDenied,
    Failed,
};

[[nodiscard]] SysProcessFreezeStatus KISAK_CDECL Sys_ProcessFreezeForCrash() noexcept;