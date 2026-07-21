#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include <qcommon/sys_process.h>

#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_init.h>
#include <mach/thread_act.h>
#include <mach/task.h>
#include <mach/exception.h>
#include <pthread.h>
#endif

extern char **environ;

namespace
{
// posix_spawn_file_actions_t is unused on every supported backend; stdio is
// inherited from the caller so the engine can layer piped I/O on top of the
// process service without conflicting with the terminal API.

bool SpawnChild(
    const char *utf8Path,
    char *const *argv,
    char *const *childEnviron,
    pid_t &outChild)
{
    outChild = -1;
    if (!utf8Path || !utf8Path[0] || !argv || !argv[0])
    {
        errno = EINVAL;
        return false;
    }

    // posix_spawnp searches PATH; we use it for parity with the test harness
    // rather than execve-with-absolute-path. The argv terminator is part of
    // the contract documented in sys_process.h.
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0)
        return false;

    int spawned = -1;
    const int rc = posix_spawnp(
        &spawned,
        utf8Path,
        &actions,
        nullptr,
        argv,
        childEnviron);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0)
    {
        errno = rc;
        return false;
    }
    outChild = spawned;
    return true;
}

bool WaitForChild(
    const pid_t child,
    const std::uint32_t msec,
    int &outStatus)
{
    outStatus = 0;
    if (msec == UINT32_MAX)
    {
        // The header reserves UINT32_MAX for the no-timeout overload; we
        // reject it here so callers that need it route through a future
        // overload instead of waiting forever on a poisoned handle.
        errno = EINVAL;
        return false;
    }

    // A bounded wait via waitpid(WNOHANG) in a poll loop with a coarse timer
    // so the engine can stay responsive inside the terminal API.
    constexpr std::uint32_t kPollMillis = 10;
    std::uint32_t elapsed = 0;
    while (elapsed < msec)
    {
        int status = 0;
        const pid_t rc = waitpid(child, &status, WNOHANG);
        if (rc == child)
        {
            if (WIFEXITED(status))
                outStatus = WEXITSTATUS(status);
            else if (WIFSIGNALED(status))
                outStatus = 128 + WTERMSIG(status);
            return true;
        }
        if (rc < 0)
            return false;
        const std::uint32_t remaining = msec - elapsed;
        const std::uint32_t slice = remaining < kPollMillis
            ? remaining
            : kPollMillis;
        timespec request{
            static_cast<time_t>(slice / 1'000U),
            static_cast<long>(slice % 1'000U) * 1'000'000L,
        };
        while (nanosleep(&request, &request) != 0)
        {
            if (errno != EINTR)
                return false;
        }
        elapsed += slice;
    }
    return false;
}
}

SysProcessLaunchResult KISAK_CDECL Sys_ProcessLaunchAndWait(
    const char *utf8Path,
    char *const *argv,
    char *const *environIn,
    std::uint32_t msec) noexcept
{
    SysProcessLaunchResult result{};
    if (!utf8Path || !utf8Path[0] || !argv || !argv[0])
    {
        result.status = SysProcessLaunchStatus::InvalidArgument;
        return result;
    }
    if (msec == UINT32_MAX)
    {
        result.status = SysProcessLaunchStatus::InvalidArgument;
        return result;
    }

    char *const *childEnviron = environIn ? environIn : ::environ;
    pid_t child = -1;
    if (!SpawnChild(utf8Path, argv, childEnviron, child))
    {
        result.status = SysProcessLaunchStatus::SpawnFailed;
        return result;
    }

    int exitStatus = 0;
    if (!WaitForChild(child, msec, exitStatus))
    {
        result.status = SysProcessLaunchStatus::TimedOut;
        result.exitCode = 0;
        return result;
    }

    result.status = SysProcessLaunchStatus::Completed;
    result.exitCode = exitStatus;
    return result;
}

namespace
{
// Per-thread stack of parked signal sets. The header contract says parks
// for disjoint sets stack; nested parks for the same set return
// AlreadyParked. We track the stack inside a thread-local vector so the
// boundary stays free of engine locks. The vector lives as a
// std::unique_ptr so its destructor runs at thread exit without an explicit
// cleanup call.
struct ThreadParkedSets
{
    std::vector<sigset_t> sets;
};

thread_local std::unique_ptr<ThreadParkedSets> tParkedSets;

bool SignalIsValid(const int sig)
{
    // POSIX signal numbers above SIGRTMIN are reserved for libc; below 0
    // is never valid. SIGKILL/SIGSTOP cannot be blocked, so we reject them
    // here so a caller that tries to park them gets InvalidArgument rather
    // than a silent pthread_sigmask failure.
    if (sig <= 0 || sig >= SIGRTMIN)
        return false;
    return sig != SIGKILL && sig != SIGSTOP;
}

ThreadParkedSets &ParkedSets()
{
    if (!tParkedSets)
        tParkedSets.reset(new ThreadParkedSets());
    return *tParkedSets;
}
}

SysSignalParkStatus KISAK_CDECL Sys_SignalPark(
    const int *signals,
    std::size_t signalCount) noexcept
{
    if (signalCount == 0)
    {
        if (!signals)
            return SysSignalParkStatus::Parked;
        return SysSignalParkStatus::InvalidArgument;
    }
    if (!signals)
        return SysSignalParkStatus::InvalidArgument;

    sigset_t requested{};
    // sigemptyset on glibc only clears the bits up to _NSIG/8 and leaves the
    // rest of the sigset_t struct uninitialized on the stack. Zero the whole
    // struct up front so the equality check against an earlier park does not
    // get fooled by stack-pointer garbage in the tail.
    for (auto &word : requested.__val)
        word = 0;
    sigemptyset(&requested);
    for (std::size_t i = 0; i < signalCount; ++i)
    {
        if (!SignalIsValid(signals[i]))
            return SysSignalParkStatus::InvalidArgument;
        if (sigaddset(&requested, signals[i]) != 0)
            return SysSignalParkStatus::Failed;
    }

    ThreadParkedSets &slot = ParkedSets();
    for (const sigset_t &previous : slot.sets)
    {
        if (std::memcmp(&previous, &requested, sizeof(sigset_t)) == 0)
            return SysSignalParkStatus::AlreadyParked;
    }

    if (pthread_sigmask(SIG_BLOCK, &requested, nullptr) != 0)
        return SysSignalParkStatus::Failed;
    try
    {
        slot.sets.push_back(requested);
    }
    catch (const std::bad_alloc &)
    {
        // Roll back the mask so a later call sees a clean park stack.
        pthread_sigmask(SIG_UNBLOCK, &requested, nullptr);
        return SysSignalParkStatus::Failed;
    }
    return SysSignalParkStatus::Parked;
}

SysSignalParkStatus KISAK_CDECL Sys_SignalUnPark(
    const int *signals,
    std::size_t signalCount) noexcept
{
    if (signalCount == 0)
    {
        if (!signals)
            return SysSignalParkStatus::Unparked;
        return SysSignalParkStatus::InvalidArgument;
    }
    if (!signals)
        return SysSignalParkStatus::InvalidArgument;
    if (!tParkedSets || tParkedSets->sets.empty())
        return SysSignalParkStatus::AlreadyUnparked;

    sigset_t requested{};
    for (auto &word : requested.__val)
        word = 0;
    sigemptyset(&requested);
    for (std::size_t i = 0; i < signalCount; ++i)
    {
        if (!SignalIsValid(signals[i]))
            return SysSignalParkStatus::InvalidArgument;
        if (sigaddset(&requested, signals[i]) != 0)
            return SysSignalParkStatus::Failed;
    }

    // Walk the stack from the top so the matching park that was actually
    // pushed for this exact set is the one we pop, even if disjoint sets
    // were nested between this set and the matching park.
    std::vector<sigset_t> &sets = tParkedSets->sets;
    for (std::size_t index = sets.size(); index > 0; --index)
    {
        const sigset_t &previous = sets[index - 1];
        if (std::memcmp(&previous, &requested, sizeof(sigset_t)) != 0)
            continue;
        if (pthread_sigmask(SIG_UNBLOCK, &requested, nullptr) != 0)
            return SysSignalParkStatus::Failed;
        sets.erase(sets.begin() + (index - 1));
        return SysSignalParkStatus::Unparked;
    }
    return SysSignalParkStatus::AlreadyUnparked;
}

SysProcessFreezeStatus KISAK_CDECL Sys_ProcessFreezeForCrash() noexcept
{
#if defined(__APPLE__)
    // Mach exception + thread_suspend is the macOS contract. We install a
    // dedicated exception port that swallows the fault and parks the current
    // thread so Mach-level post-mortem tooling (lldb --attach, sample) can
    // take a snapshot. The parked thread is never resumed; the process is
    // expected to be terminated while frozen.
    mach_port_t task = mach_task_self();
    exception_mask_t mask = EXC_MASK_ALL;
    mach_port_t handler = MACH_PORT_NULL;
    kern_return_t kr = task_set_exception_ports(
        task,
        mask,
        handler,
        EXCEPTION_DEFAULT,
        THREAD_STATE_NONE);
    if (kr != KERN_SUCCESS)
        return SysProcessFreezeStatus::Failed;

    thread_act_t current = mach_thread_self();
    kr = thread_suspend(current);
    mach_port_deallocate(mach_task_self(), current);
    if (kr != KERN_SUCCESS)
        return SysProcessFreezeStatus::Failed;

    // thread_suspend returns to the caller; the contract is that we never
    // resume. Block on sigsuspend under an empty mask until SIGKILL or
    // process termination closes the process down.
    sigset_t empty{};
    sigemptyset(&empty);
    while (sigsuspend(&empty) != 0 && errno == EINTR)
    {
    }
    _exit(0);
    return SysProcessFreezeStatus::Frozen; // unreachable
#else
    // Per-thread freeze is owned by sys_thread.h's Sys_ThreadForceSuspendForCrash.
    // Whole-process freeze is a macOS-only contract; Linux uses the per-thread
    // primitive and returns Unsupported here so the post-mortem handler can
    // decide whether to fan out across captured handles.
    return SysProcessFreezeStatus::Unsupported;
#endif
}