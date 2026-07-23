// SPDX-License-Identifier: GPL-3.0-only
//
// Process service implementation for POSIX backends (Linux + macOS).
// Launch uses posix_spawnp with PATH search; wait uses waitpid in a
// bounded poll loop; terminate uses SIGTERM escalated to SIGKILL; the
// signal-park stack is per-thread via pthread_sigmask. Mach crash
// freezing lives in src/_platform/macos/sys_mach_crash.cpp.

#include <qcommon/sys_process.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <memory>
#include <vector>

#include <signal.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

struct SysProcess
{
    pid_t pid{-1};
    bool hasExited{false};
    int exitStatus{0};
};

namespace
{
bool IsValidSignal(const int sig) noexcept
{
    if (sig <= 0 || sig >= SIGRTMIN)
        return false;
    return sig != SIGKILL && sig != SIGSTOP;
}

// Per-thread stack of parked signal sets. The header contract says parks
// for disjoint sets stack and nested parks for the same set return
// AlreadyParked. The stack lives inside a thread-local unique_ptr so
// its destructor runs at thread exit without an explicit cleanup call.
thread_local std::unique_ptr<std::vector<sigset_t>> tParkedSets;

std::vector<sigset_t> &ParkedSets() noexcept
{
    if (!tParkedSets)
        tParkedSets = std::make_unique<std::vector<sigset_t>>();
    return *tParkedSets;
}

bool BuildSet(const int *signals, std::size_t count, sigset_t *out) noexcept
{
    *out = sigset_t{};
    for (auto &word : out->__val)
        word = 0;
    sigemptyset(out);
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!IsValidSignal(signals[i]))
            return false;
        if (sigaddset(out, signals[i]) != 0)
            return false;
    }
    return true;
}

bool BuildArgumentVector(const char *executablePath,
    const char *arguments,
    std::vector<char> *executableBuffer,
    std::vector<char> *argumentBuffer,
    std::vector<char *> *outArgv)
{
    outArgv->clear();
    executableBuffer->clear();
    argumentBuffer->clear();
    if (!executablePath || !executablePath[0])
        return false;

    executableBuffer->assign(
        executablePath, executablePath + std::strlen(executablePath) + 1);
    outArgv->push_back(executableBuffer->data());

    if (!arguments || !*arguments)
    {
        outArgv->push_back(nullptr);
        return true;
    }

    argumentBuffer->assign(
        arguments, arguments + std::strlen(arguments) + 1);
    bool inToken = false;
    bool inQuote = false;
    for (char &ch : *argumentBuffer)
    {
        if (ch == '"')
        {
            inQuote = !inQuote;
            ch = '\0';
            continue;
        }
        if (!inQuote && (ch == ' ' || ch == '\t'))
        {
            ch = '\0';
            inToken = false;
            continue;
        }
        if (!inToken)
        {
            outArgv->push_back(&ch);
            inToken = true;
        }
    }
    outArgv->push_back(nullptr);
    return true;
}

bool SpawnChild(const char *executablePath,
    char *const *argv,
    pid_t *outPid) noexcept
{
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0)
        return false;

    pid_t spawned = -1;
    const int rc = posix_spawnp(
        &spawned,
        executablePath,
        &actions,
        nullptr,
        argv,
        environ);

    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0)
        return false;
    *outPid = spawned;
    return true;
}

bool WaitForChild(pid_t pid, std::uint32_t msec, int *outStatus) noexcept
{
    if (msec == UINT32_MAX)
        return false;

    constexpr std::uint32_t kPollMillis = 10;
    std::uint32_t elapsed = 0;
    while (elapsed < msec)
    {
        int status = 0;
        const pid_t rc = waitpid(pid, &status, WNOHANG);
        if (rc == pid)
        {
            *outStatus = status;
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

bool ReapIfExited(SysProcess *process) noexcept
{
    if (process->hasExited)
        return true;
    int status = 0;
    const pid_t rc = waitpid(process->pid, &status, WNOHANG);
    if (rc == 0)
        return false;
    if (rc < 0)
        return false;
    process->hasExited = true;
    process->exitStatus = status;
    return true;
}
}

SysProcessLaunchStatus KISAK_CDECL Sys_ProcessLaunch(
    const char *executablePath,
    const char *arguments,
    SysProcessHandle *outHandle)
{
    if (!outHandle || *outHandle || !executablePath || !executablePath[0])
        return SysProcessLaunchStatus::InvalidArgument;

    std::vector<char> executableBuffer;
    std::vector<char> argumentBuffer;
    std::vector<char *> argv;
    if (!BuildArgumentVector(executablePath, arguments,
        &executableBuffer, &argumentBuffer, &argv))
        return SysProcessLaunchStatus::InvalidArgument;

    auto process = std::make_unique<SysProcess>();
    if (!SpawnChild(executablePath, argv.data(), &process->pid))
        return SysProcessLaunchStatus::SpawnFailed;

    *outHandle = process.release();
    return SysProcessLaunchStatus::Started;
}

SysProcessWaitStatus KISAK_CDECL Sys_ProcessWait(
    SysProcessHandle handle,
    std::uint32_t timeoutMs)
{
    if (!handle)
        return SysProcessWaitStatus::InvalidHandle;
    if (handle->hasExited)
        return SysProcessWaitStatus::Exited;

    int status = 0;
    if (timeoutMs == 0)
    {
        const pid_t rc = waitpid(handle->pid, &status, WNOHANG);
        if (rc == 0)
            return SysProcessWaitStatus::TimedOut;
        if (rc < 0)
            return SysProcessWaitStatus::InvalidHandle;
        handle->hasExited = true;
        handle->exitStatus = status;
        return SysProcessWaitStatus::Exited;
    }

    if (!WaitForChild(handle->pid, timeoutMs, &status))
        return SysProcessWaitStatus::TimedOut;
    handle->hasExited = true;
    handle->exitStatus = status;
    return SysProcessWaitStatus::Exited;
}

SysProcessTerminateStatus KISAK_CDECL Sys_ProcessTerminate(
    SysProcessHandle handle,
    std::uint32_t exitCode)
{
    if (!handle)
        return SysProcessTerminateStatus::InvalidHandle;
    if (handle->hasExited)
        return SysProcessTerminateStatus::Signaled;

    if (kill(handle->pid, SIGTERM) == 0)
    {
        (void)exitCode;
        return SysProcessTerminateStatus::Signaled;
    }
    if (kill(handle->pid, SIGKILL) == 0)
        return SysProcessTerminateStatus::Signaled;

    return SysProcessTerminateStatus::Unsupported;
}

bool KISAK_CDECL Sys_ProcessGetExitCode(
    SysProcessHandle handle,
    std::uint32_t *exitCode)
{
    if (!handle || !exitCode)
        return false;
    if (!ReapIfExited(handle))
        return false;
    if (!WIFEXITED(handle->exitStatus))
        return false;
    *exitCode = static_cast<std::uint32_t>(WEXITSTATUS(handle->exitStatus));
    return true;
}

void KISAK_CDECL Sys_ProcessRelease(SysProcessHandle *handle)
{
    if (!handle || !*handle)
        return;
    if ((*handle)->pid > 0)
    {
        int status = 0;
        (void)waitpid((*handle)->pid, &status, WNOHANG);
    }
    delete *handle;
    *handle = nullptr;
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
    if (!BuildSet(signals, signalCount, &requested))
        return SysSignalParkStatus::InvalidArgument;

    std::vector<sigset_t> &stack = ParkedSets();
    for (const sigset_t &previous : stack)
    {
        if (std::memcmp(&previous, &requested, sizeof(sigset_t)) == 0)
            return SysSignalParkStatus::AlreadyParked;
    }

    if (pthread_sigmask(SIG_BLOCK, &requested, nullptr) != 0)
        return SysSignalParkStatus::Failed;
    try
    {
        stack.push_back(requested);
    }
    catch (const std::bad_alloc &)
    {
        (void)pthread_sigmask(SIG_UNBLOCK, &requested, nullptr);
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
    if (!tParkedSets || tParkedSets->empty())
        return SysSignalParkStatus::Unparked;

    sigset_t requested{};
    if (!BuildSet(signals, signalCount, &requested))
        return SysSignalParkStatus::InvalidArgument;

    std::vector<sigset_t> &stack = *tParkedSets;
    for (std::size_t index = stack.size(); index > 0; --index)
    {
        const sigset_t &previous = stack[index - 1];
        if (std::memcmp(&previous, &requested, sizeof(sigset_t)) != 0)
            continue;
        if (pthread_sigmask(SIG_UNBLOCK, &requested, nullptr) != 0)
            return SysSignalParkStatus::Failed;
        stack.erase(stack.begin() + (index - 1));
        return SysSignalParkStatus::Unparked;
    }
    return SysSignalParkStatus::Unparked;
}

SysProcessFreezeStatus KISAK_CDECL Sys_ProcessFreezeForCrash() noexcept
{
#if defined(__APPLE__)
    // Mach exception + thread_suspend is the macOS contract. The Mach
    // crash TU in src/_platform/macos owns the actual implementation
    // (task_set_exception_ports + thread_suspend + sigsuspend); including
    // it here keeps a single call site for the engine's post-mortem
    // handler. The function takes no arguments because the contract
    // freezes the calling process, not an arbitrary handle.
    extern SysProcessFreezeStatus Sys_MachProcessFreezeForCrash() noexcept;
    return Sys_MachProcessFreezeForCrash();
#else
    // Per-thread freeze is owned by Sys_ThreadForceSuspendForCrash. On
    // non-macOS hosts Sys_ProcessFreezeForCrash returns Unsupported so
    // the post-mortem handler can decide whether to fan out across
    // captured thread handles instead.
    return SysProcessFreezeStatus::Unsupported;
#endif
}
