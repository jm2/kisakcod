// SPDX-License-Identifier: GPL-3.0-only
//
// platform_process_tests.cpp — exercises the portable Sys_Process* API on the
// host platform backend. The single test binary is invoked directly so it can
// be branched from ctest based on argv[0] if needed.

#include <qcommon/sys_process.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
const char *checkStage = "startup";

bool Check(const bool condition, const char *const stage)
{
    if (!condition)
    {
        checkStage = stage;
        return false;
    }
    return true;
}

const char *SleepExecutable()
{
#if defined(_WIN32)
    return "cmd";
#else
    return "/bin/sh";
#endif
}

const char *SleepArguments()
{
#if defined(_WIN32)
    return "/c timeout /t 30 /nobreak >NUL";
#else
    return "-c \"sleep 30\"";
#endif
}

const char *TrueCommand()
{
#if defined(_WIN32)
    return "cmd";
#else
    return "/bin/true";
#endif
}

const char *TrueArguments()
{
#if defined(_WIN32)
    return "/c exit 0";
#else
    return nullptr;
#endif
}

const char *QuittableCommand()
{
#if defined(_WIN32)
    return "quit";
#else
    return "quit";
#endif
}

bool TestRefusedLaunch()
{
    SysProcessHandle handle{nullptr};
    const SysProcessLaunchStatus status = Sys_ProcessLaunch(
        QuittableCommand(), nullptr, &handle);
    if (status != SysProcessLaunchStatus::InvalidArgument)
    {
        std::fputs("refused literal \"quit\" was accepted\n", stderr);
        if (handle)
            Sys_ProcessRelease(&handle);
        return false;
    }
    if (handle)
    {
        std::fputs("refused launch populated handle\n", stderr);
        Sys_ProcessRelease(&handle);
        return false;
    }
    return true;
}

bool TestNullArguments()
{
    SysProcessHandle handle{nullptr};
    if (Sys_ProcessLaunch(nullptr, nullptr, &handle)
        != SysProcessLaunchStatus::InvalidArgument)
    {
        std::fputs("null executable accepted\n", stderr);
        if (handle)
            Sys_ProcessRelease(&handle);
        return false;
    }
    if (Sys_ProcessLaunch(TrueCommand(), nullptr, nullptr)
        != SysProcessLaunchStatus::InvalidArgument)
    {
        std::fputs("null outHandle accepted\n", stderr);
        return false;
    }
    SysProcessHandle occupied{reinterpret_cast<SysProcess *>(0x1)};
    if (Sys_ProcessLaunch(TrueCommand(), nullptr, &occupied)
        != SysProcessLaunchStatus::InvalidArgument)
    {
        std::fputs("non-null outHandle accepted\n", stderr);
        return false;
    }
    return true;
}

bool TestLaunchExitZero()
{
    SysProcessHandle handle{nullptr};
    if (Sys_ProcessLaunch(TrueCommand(), TrueArguments(), &handle)
        != SysProcessLaunchStatus::Started)
    {
        std::fputs("failed to launch true-equivalent command\n", stderr);
        if (handle)
            Sys_ProcessRelease(&handle);
        return false;
    }
    if (!handle)
    {
        std::fputs("started handle was null\n", stderr);
        return false;
    }

    bool ok = true;
    if (Sys_ProcessWait(handle, 5000) != SysProcessWaitStatus::Exited)
    {
        std::fputs("wait did not report exited\n", stderr);
        ok = false;
    }

    std::uint32_t exitCode = 0;
    if (ok && !Sys_ProcessGetExitCode(handle, &exitCode))
    {
        std::fputs("exit code unavailable\n", stderr);
        ok = false;
    }
    if (ok && exitCode != 0)
    {
        std::fprintf(stderr, "expected exit code 0, got %u\n",
            static_cast<unsigned>(exitCode));
        ok = false;
    }

    Sys_ProcessRelease(&handle);
    if (handle)
    {
        std::fputs("release did not clear handle\n", stderr);
        ok = false;
    }
    return ok;
}

bool TestLaunchWaitTimeout()
{
    SysProcessHandle handle{nullptr};
    if (Sys_ProcessLaunch(SleepExecutable(), SleepArguments(), &handle)
        != SysProcessLaunchStatus::Started)
    {
        std::fputs("failed to launch sleeper\n", stderr);
        if (handle)
            Sys_ProcessRelease(&handle);
        return false;
    }

    bool ok = true;
    const SysProcessWaitStatus result = Sys_ProcessWait(handle, 50);
    if (result != SysProcessWaitStatus::TimedOut)
    {
        std::fprintf(stderr, "expected TimedOut, got %d\n",
            static_cast<int>(result));
        ok = false;
    }

    if (Sys_ProcessTerminate(handle, 1) != SysProcessTerminateStatus::Signaled)
    {
        std::fputs("terminate did not report Signaled\n", stderr);
        ok = false;
    }
    const SysProcessWaitStatus finalWait = Sys_ProcessWait(handle, 5000);
    if (finalWait != SysProcessWaitStatus::Exited
        && finalWait != SysProcessWaitStatus::Signaled)
    {
        std::fprintf(stderr, "final wait returned %d\n",
            static_cast<int>(finalWait));
        ok = false;
    }

    Sys_ProcessRelease(&handle);
    return ok;
}

bool TestTerminateSignaledStatus()
{
    SysProcessHandle handle{nullptr};
    if (Sys_ProcessLaunch(SleepExecutable(), SleepArguments(), &handle)
        != SysProcessLaunchStatus::Started)
    {
        std::fputs("failed to launch sleeper for terminate test\n", stderr);
        if (handle)
            Sys_ProcessRelease(&handle);
        return false;
    }

    bool ok = true;
    if (Sys_ProcessTerminate(handle, 9) != SysProcessTerminateStatus::Signaled)
    {
        std::fputs("terminate returned non-Signaled\n", stderr);
        ok = false;
    }
    if (Sys_ProcessTerminate(nullptr, 0) != SysProcessTerminateStatus::InvalidHandle)
    {
        std::fputs("terminate accepted null handle\n", stderr);
        ok = false;
    }

    Sys_ProcessRelease(&handle);
    return ok;
}

bool TestReleaseNullIsSafe()
{
    Sys_ProcessRelease(nullptr);
    SysProcessHandle nullHandle{nullptr};
    Sys_ProcessRelease(&nullHandle);
    return true;
}

bool TestSignalParkLifecycle()
{
    const SysSignalParkStatus firstEnter = Sys_SignalParkEnter();
    if (firstEnter == SysSignalParkStatus::Unsupported)
    {
        std::fputs("signal park unsupported on host platform\n", stderr);
        return true;
    }
    if (firstEnter != SysSignalParkStatus::Entered)
    {
        std::fputs("first signal-park enter did not succeed\n", stderr);
        return false;
    }

    const SysSignalParkStatus secondEnter = Sys_SignalParkEnter();
    if (secondEnter != SysSignalParkStatus::AlreadyParked)
    {
        std::fputs("nested signal-park enter did not report AlreadyParked\n",
            stderr);
        (void)Sys_SignalParkLeave();
        return false;
    }

    const SysSignalParkLeaveStatus firstLeave = Sys_SignalParkLeave();
    if (firstLeave != SysSignalParkLeaveStatus::Left)
    {
        std::fputs("first signal-park leave did not report Left\n", stderr);
        return false;
    }

    const SysSignalParkLeaveStatus secondLeave = Sys_SignalParkLeave();
    if (secondLeave != SysSignalParkLeaveStatus::NotParked)
    {
        std::fputs("extra signal-park leave did not report NotParked\n",
            stderr);
        return false;
    }
    return true;
}
}

int main(int argc, char **argv)
{
    struct
    {
        const char *name;
        bool (*fn)();
    } const tests[]
    {
        {"refused-launch", &TestRefusedLaunch},
        {"null-arguments", &TestNullArguments},
        {"launch-exit-zero", &TestLaunchExitZero},
        {"launch-wait-timeout", &TestLaunchWaitTimeout},
        {"terminate-signaled", &TestTerminateSignaledStatus},
        {"release-null-safe", &TestReleaseNullIsSafe},
        {"signal-park-lifecycle", &TestSignalParkLifecycle},
    };

    auto runOne = [&](const char *needle) -> bool
    {
        for (const auto &test : tests)
        {
            if (std::strcmp(test.name, needle) == 0)
                return Check(test.fn(), test.name);
        }
        std::fprintf(stderr, "unknown test selector %s\n", needle);
        return false;
    };

    int failures = 0;
    if (argc > 1)
    {
        if (!runOne(argv[1]))
        {
            std::fprintf(stderr, "FAIL %s (stage=%s)\n",
                argv[1], checkStage);
            ++failures;
        }
    }
    else
    {
        for (const auto &test : tests)
        {
            if (!Check(test.fn(), test.name))
            {
                std::fprintf(stderr, "FAIL %s (stage=%s)\n",
                    test.name, checkStage);
                ++failures;
            }
        }
    }
    if (failures != 0)
    {
        std::fprintf(stderr, "%d platform-process test failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
