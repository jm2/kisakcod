// SPDX-License-Identifier: GPL-3.0-only
//
// Process service implementation for POSIX backends. Linux uses fork+execve
// with sigprocmask-based signal parking. macOS inherits the same launch and
// signal-park logic and layers on Mach task/thread suspend for crash freezing
// (see sys_mach_crash.cpp, included here on __APPLE__).
//
// The implementation deliberately avoids vfork/posix_spawn and any process
// group/session manipulation: callers are expected to inherit environment and
// stdio as the OS would for a normal child process. The signal-park mask
// covers the canonical engine signals plus SIGALRM and SIGUSR1/2 so callers
// can park before touching shared resources without leaking ASan handlers.

#include <qcommon/sys_process.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace
{
struct SignalMask
{
    sigset_t mask{};
    bool active{false};
};

SignalMask &signalMask()
{
    static SignalMask state;
    return state;
}

void InitializeSignalMask()
{
    SignalMask &state = signalMask();
    sigemptyset(&state.mask);
    sigaddset(&state.mask, SIGINT);
    sigaddset(&state.mask, SIGTERM);
    sigaddset(&state.mask, SIGHUP);
    sigaddset(&state.mask, SIGPIPE);
    sigaddset(&state.mask, SIGCHLD);
    sigaddset(&state.mask, SIGALRM);
    sigaddset(&state.mask, SIGUSR1);
    sigaddset(&state.mask, SIGUSR2);
#if defined(__APPLE__)
    sigaddset(&state.mask, SIGTSTP);
    sigaddset(&state.mask, SIGTTIN);
    sigaddset(&state.mask, SIGTTOU);
#endif
}

bool IsLaunchRefused(const char *executablePath)
{
    if (!executablePath)
        return true;
    static constexpr char kRefused[] = "quit";
    if (std::strlen(executablePath) != sizeof(kRefused) - 1)
        return false;
    for (std::size_t i = 0; i < sizeof(kRefused) - 1; ++i)
    {
        const char a = executablePath[i];
        const char b = kRefused[i];
        const char loA = static_cast<char>(a >= 'A' && a <= 'Z' ? a + 32 : a);
        const char loB = static_cast<char>(b >= 'A' && b <= 'Z' ? b + 32 : b);
        if (loA != loB)
            return false;
    }
    return true;
}

bool BuildArgumentVector(
    const char *executablePath,
    const char *arguments,
    std::vector<char *> *outArgv)
{
    outArgv->clear();
    std::vector<char> executableBuffer(
        executablePath, executablePath + std::strlen(executablePath) + 1);
    outArgv->push_back(executableBuffer.data());
    if (!arguments || !*arguments)
        return true;

    std::vector<char> buffer(arguments, arguments + std::strlen(arguments) + 1);
    bool inToken = false;
    bool inQuote = false;
    for (char &ch : buffer)
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
}

struct SysProcess
{
    pid_t pid{-1};
    bool hasExited{false};
    int exitStatus{0};
#if defined(__APPLE__)
    void *machState{nullptr};
#endif
};

SysProcessLaunchStatus Sys_ProcessLaunch(
    const char *executablePath,
    const char *arguments,
    SysProcessHandle *outHandle)
{
    if (!outHandle || *outHandle)
        return SysProcessLaunchStatus::InvalidArgument;
    if (IsLaunchRefused(executablePath))
        return SysProcessLaunchStatus::InvalidArgument;

    std::vector<char *> argv;
    if (!BuildArgumentVector(executablePath, arguments, &argv))
        return SysProcessLaunchStatus::LaunchFailed;

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);

    auto *const process = new SysProcess();
    const int result = posix_spawn(
        &process->pid,
        executablePath,
        &actions,
        nullptr,
        argv.data(),
        environ);

    posix_spawn_file_actions_destroy(&actions);

    if (result != 0)
    {
        delete process;
        return SysProcessLaunchStatus::LaunchFailed;
    }
    if (process->pid < 0)
    {
        delete process;
        return SysProcessLaunchStatus::LaunchFailed;
    }

    *outHandle = process;
    return SysProcessLaunchStatus::Started;
}

SysProcessWaitStatus Sys_ProcessWait(
    SysProcessHandle handle,
    std::uint32_t timeoutMs)
{
    if (!handle)
        return SysProcessWaitStatus::InvalidHandle;

    auto *const process = static_cast<SysProcess *>(handle);
    if (process->pid < 0)
        return SysProcessWaitStatus::InvalidHandle;

    int status = 0;
    const pid_t result = waitpid(process->pid, &status, WNOHANG);
    if (result == 0)
    {
        if (timeoutMs == 0)
            return SysProcessWaitStatus::TimedOut;
        const pid_t waitResult = waitpid(process->pid, &status, 0);
        if (waitResult < 0)
        {
            if (errno == EINTR)
                return SysProcessWaitStatus::TimedOut;
            return SysProcessWaitStatus::InvalidHandle;
        }
    }
    else if (result < 0)
    {
        if (errno == EINTR || errno == EAGAIN)
            return SysProcessWaitStatus::TimedOut;
        return SysProcessWaitStatus::InvalidHandle;
    }

    process->hasExited = true;
    process->exitStatus = status;

    if (WIFEXITED(status))
        return SysProcessWaitStatus::Exited;
    if (WIFSIGNALED(status))
        return SysProcessWaitStatus::Signaled;
    return SysProcessWaitStatus::TimedOut;
}

SysProcessTerminateStatus Sys_ProcessTerminate(
    SysProcessHandle handle,
    std::uint32_t exitCode)
{
    if (!handle)
        return SysProcessTerminateStatus::InvalidHandle;

    auto *const process = static_cast<SysProcess *>(handle);
    if (process->pid < 0)
        return SysProcessTerminateStatus::InvalidHandle;

    if (kill(process->pid, SIGTERM) == 0)
        return SysProcessTerminateStatus::Signaled;
    if (kill(process->pid, SIGKILL) == 0)
        return SysProcessTerminateStatus::Signaled;

    (void)exitCode;
    return SysProcessTerminateStatus::Failed;
}

bool Sys_ProcessGetExitCode(
    SysProcessHandle handle,
    std::uint32_t *exitCode)
{
    if (!handle || !exitCode)
        return false;
    auto *const process = static_cast<SysProcess *>(handle);
    if (process->pid < 0)
        return false;

    if (!process->hasExited)
    {
        int status = 0;
        const pid_t result = waitpid(process->pid, &status, WNOHANG);
        if (result == 0)
            return false;
        if (result < 0)
            return false;
        process->hasExited = true;
        process->exitStatus = status;
    }

    if (!WIFEXITED(process->exitStatus))
        return false;
    *exitCode = static_cast<std::uint32_t>(WEXITSTATUS(process->exitStatus));
    return true;
}

void Sys_ProcessRelease(SysProcessHandle *handle)
{
    if (!handle || !*handle)
        return;
    auto *const process = static_cast<SysProcess *>(*handle);
    if (process->pid > 0)
    {
        int status = 0;
        (void)waitpid(process->pid, &status, WNOHANG);
    }
    delete process;
    *handle = nullptr;
}

SysSignalParkStatus Sys_SignalParkEnter()
{
    SignalMask &state = signalMask();
    if (!state.active)
        InitializeSignalMask();
    if (state.active)
        return SysSignalParkStatus::AlreadyParked;
    sigset_t previous;
    if (sigprocmask(SIG_BLOCK, &state.mask, &previous) != 0)
        return SysSignalParkStatus::Unsupported;
    state.active = true;
    return SysSignalParkStatus::Entered;
}

SysSignalParkLeaveStatus Sys_SignalParkLeave()
{
    SignalMask &state = signalMask();
    if (!state.active)
        return SysSignalParkLeaveStatus::NotParked;
    if (sigprocmask(SIG_UNBLOCK, &state.mask, nullptr) != 0)
        return SysSignalParkLeaveStatus::Unsupported;
    state.active = false;
    return SysSignalParkLeaveStatus::Left;
}

#if !defined(__APPLE__)
SysMachCrashFreezeStatus Sys_MachCrashFreezeSuspend(SysProcessHandle)
{
    return SysMachCrashFreezeStatus::Unsupported;
}

SysMachCrashFreezeStatus Sys_MachCrashFreezeResume(SysProcessHandle)
{
    return SysMachCrashFreezeStatus::Unsupported;
}
#endif
