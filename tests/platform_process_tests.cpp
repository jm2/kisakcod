#include <qcommon/sys_process.h>

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
const char *gCheckStage = "startup";

void SetCheckStage(const char *const stage)
{
    gCheckStage = stage ? stage : "unknown";
}

bool Check(const bool condition)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: platform process stage: %s\n", gCheckStage);
    }
    return condition;
}

#if !defined(_WIN32)
// Spawn a child that prints a single integer (its exit code) and exits with
// the same value. Implemented as a posix_spawn of /bin/sh so the test does
// not have to ship its own helper binary.
struct ExitChild
{
    pid_t pid = -1;
    int exitCode = 0;
    bool spawnable = false;
};

bool StartExitChild(
    const int exitCode,
    ExitChild &out)
{
    out.pid = -1;
    out.exitCode = exitCode;
    out.spawnable = false;

    // Pipe so the child can declare its exit code to the parent.
    int syncFds[2] = { -1, -1 };
    if (::pipe(syncFds) != 0)
        return false;

    const pid_t pid = ::fork();
    if (pid < 0)
    {
        ::close(syncFds[0]);
        ::close(syncFds[1]);
        return false;
    }
    if (pid == 0)
    {
        ::close(syncFds[0]);
        char buffer[32];
        const int length = std::snprintf(
            buffer, sizeof(buffer), "%d\n", exitCode);
        if (length > 0)
            (void)::write(syncFds[1], buffer, static_cast<std::size_t>(length));
        ::close(syncFds[1]);
        std::_Exit(exitCode);
    }
    ::close(syncFds[1]);

    char buffer[32] = {};
    ssize_t received = 0;
    while (received < static_cast<ssize_t>(sizeof(buffer)) - 1)
    {
        const ssize_t chunk = ::read(
            syncFds[0],
            buffer + received,
            sizeof(buffer) - 1 - static_cast<std::size_t>(received));
        if (chunk < 0)
        {
            if (errno == EINTR)
                continue;
            ::close(syncFds[0]);
            (void)::waitpid(pid, nullptr, 0);
            return false;
        }
        if (chunk == 0)
            break;
        received += chunk;
    }
    ::close(syncFds[0]);

    const int declared = std::atoi(buffer);
    if (declared != exitCode)
    {
        (void)::waitpid(pid, nullptr, 0);
        return false;
    }

    out.pid = pid;
    out.spawnable = true;
    return true;
}

bool WaitForExitChild(
    const ExitChild &child,
    const std::uint32_t msec)
{
    if (!child.spawnable)
        return false;
    constexpr std::uint32_t kPollMillis = 5;
    std::uint32_t elapsed = 0;
    while (elapsed <= msec)
    {
        int status = 0;
        const pid_t rc = ::waitpid(child.pid, &status, WNOHANG);
        if (rc == child.pid)
            return true;
        if (rc < 0)
            return false;
        timespec request{
            static_cast<time_t>(kPollMillis / 1'000U),
            static_cast<long>(kPollMillis % 1'000U) * 1'000'000L,
        };
        while (::nanosleep(&request, &request) != 0)
        {
            if (errno != EINTR)
                return false;
        }
        elapsed += kPollMillis;
    }
    return false;
}
#endif

bool TestInvalidArgument()
{
    SetCheckStage("launch/invalid-argument");
    char arg0[] = "ignored";
    char *argv[] = { arg0, nullptr };
    const SysProcessLaunchResult nullPath = Sys_ProcessLaunchAndWait(
        nullptr, argv, nullptr, 100);
    if (!Check(nullPath.status == SysProcessLaunchStatus::InvalidArgument))
        return false;

    const SysProcessLaunchResult nullArgv = Sys_ProcessLaunchAndWait(
        "/bin/true", nullptr, nullptr, 100);
    if (!Check(nullArgv.status == SysProcessLaunchStatus::InvalidArgument))
        return false;

    const SysProcessLaunchResult emptyPath = Sys_ProcessLaunchAndWait(
        "", argv, nullptr, 100);
    if (!Check(emptyPath.status == SysProcessLaunchStatus::InvalidArgument))
        return false;

    const SysProcessLaunchResult timeoutSentinel = Sys_ProcessLaunchAndWait(
        "/bin/true", argv, nullptr, UINT32_MAX);
    if (!Check(timeoutSentinel.status == SysProcessLaunchStatus::InvalidArgument))
        return false;

    return true;
}

#if !defined(_WIN32)
bool TestSuccessfulLaunch()
{
    SetCheckStage("launch/success");
    ExitChild child;
    if (!Check(StartExitChild(0, child)))
        return false;

    char arg0[] = "sh";
    char arg1[] = "-c";
    char arg2[] = "exit 7";
    char *argv[] = { arg0, arg1, arg2, nullptr };
    const SysProcessLaunchResult result = Sys_ProcessLaunchAndWait(
        "/bin/sh", argv, nullptr, 5000);
    if (!Check(result.status == SysProcessLaunchStatus::Completed))
        return false;
    if (!Check(result.exitCode == 7))
        return false;
    (void)WaitForExitChild(child, 1000);
    return true;
}

bool TestSpawnFailure()
{
    SetCheckStage("launch/spawn-failure");
    char arg0[] = "definitely-not-a-real-executable";
    char *argv[] = { arg0, nullptr };
    const SysProcessLaunchResult result = Sys_ProcessLaunchAndWait(
        "/nonexistent/path/that/should/never/exist",
        argv,
        nullptr,
        100);
    return Check(result.status == SysProcessLaunchStatus::SpawnFailed);
}

bool TestTimeout()
{
    SetCheckStage("launch/timeout");
    char arg0[] = "sh";
    char arg1[] = "-c";
    char arg2[] = "sleep 2";
    char *argv[] = { arg0, arg1, arg2, nullptr };
    const SysProcessLaunchResult result = Sys_ProcessLaunchAndWait(
        "/bin/sh", argv, nullptr, 50);
    return Check(result.status == SysProcessLaunchStatus::TimedOut);
}

bool TestSignalParkRoundTrip()
{
    SetCheckStage("signal-park/round-trip");
    int signals[] = { SIGUSR1 };
    const SysSignalParkStatus park = Sys_SignalPark(
        signals, 1);
    if (!Check(park == SysSignalParkStatus::Parked))
        return false;

    sigset_t current{};
    for (auto &word : current.__val)
        word = 0;
    if (!Check(pthread_sigmask(SIG_BLOCK, nullptr, &current) == 0))
    {
        (void)Sys_SignalUnPark(signals, 1);
        return false;
    }
    if (!Check(sigismember(&current, SIGUSR1) == 1))
    {
        (void)Sys_SignalUnPark(signals, 1);
        return false;
    }

    const SysSignalParkStatus already = Sys_SignalPark(
        signals, 1);
    if (!Check(already == SysSignalParkStatus::AlreadyParked))
    {
        (void)Sys_SignalUnPark(signals, 1);
        return false;
    }

    const SysSignalParkStatus unpark = Sys_SignalUnPark(signals, 1);
    if (!Check(unpark == SysSignalParkStatus::Unparked))
        return false;

    for (auto &word : current.__val)
        word = 0;
    if (!Check(pthread_sigmask(SIG_BLOCK, nullptr, &current) == 0))
        return false;
    return Check(sigismember(&current, SIGUSR1) == 0);
}

bool TestSignalParkRejectsInvalidSignals()
{
    SetCheckStage("signal-park/invalid-signals");
    int badSignals[] = { SIGKILL };
    const SysSignalParkStatus kill = Sys_SignalPark(
        badSignals, 1);
    if (!Check(kill == SysSignalParkStatus::InvalidArgument))
        return false;

    int belowZero[] = { -1 };
    const SysSignalParkStatus negative = Sys_SignalPark(
        belowZero, 1);
    if (!Check(negative == SysSignalParkStatus::InvalidArgument))
        return false;

    int nonZeroCount[] = { SIGUSR1 };
    const SysSignalParkStatus nonZeroNullStatus = Sys_SignalPark(
        nonZeroCount, 0);
    return Check(nonZeroNullStatus == SysSignalParkStatus::InvalidArgument);
}

bool TestSignalParkEmptyNoOp()
{
    SetCheckStage("signal-park/empty-no-op");
    const SysSignalParkStatus park = Sys_SignalPark(nullptr, 0);
    if (!Check(park == SysSignalParkStatus::Parked))
        return false;
    const SysSignalParkStatus unpark = Sys_SignalUnPark(nullptr, 0);
    return Check(unpark == SysSignalParkStatus::Unparked);
}

bool TestCrashFreezeUnsupportedOnLinux()
{
    SetCheckStage("process-freeze/linux-unsupported");
    const SysProcessFreezeStatus status = Sys_ProcessFreezeForCrash();
    return Check(status == SysProcessFreezeStatus::Unsupported);
}
#else
bool TestSuccessfulLaunch() { return Check(true); }
bool TestSpawnFailure() { return Check(true); }
bool TestTimeout() { return Check(true); }
bool TestSignalParkRoundTrip() { return Check(true); }
bool TestSignalParkRejectsInvalidSignals() { return Check(true); }
bool TestSignalParkEmptyNoOp() { return Check(true); }
bool TestCrashFreezeUnsupportedOnLinux() { return Check(true); }
#endif

bool TestWin32SignalParkUnsupported()
{
    SetCheckStage("signal-park/win32-unsupported");
#if defined(_WIN32)
    int signals[] = { 2 };
    const SysSignalParkStatus park = Sys_SignalPark(signals, 1);
    if (!Check(park == SysSignalParkStatus::Unsupported))
        return false;
    const SysSignalParkStatus unpark = Sys_SignalUnPark(signals, 1);
    return Check(unpark == SysSignalParkStatus::Unsupported);
#else
    (void)0;
    return Check(true);
#endif
}
}

int main()
{
    if (!TestInvalidArgument())
        return 1;
    if (!TestSuccessfulLaunch())
        return 1;
    if (!TestSpawnFailure())
        return 1;
    if (!TestTimeout())
        return 1;
    if (!TestSignalParkRoundTrip())
        return 1;
    if (!TestSignalParkRejectsInvalidSignals())
        return 1;
    if (!TestSignalParkEmptyNoOp())
        return 1;
    if (!TestWin32SignalParkUnsupported())
        return 1;
    if (!TestCrashFreezeUnsupportedOnLinux())
        return 1;
    return 0;
}