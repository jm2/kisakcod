// SPDX-License-Identifier: GPL-3.0-only
//
// platform_crash_tests.cpp — exercises the crash-freeze and signal-park
// surfaces of the portable Sys_Process* API. On macOS the Mach freeze path
// is exercised against a real subprocess; on other platforms the contract
// validates that the API returns Unsupported rather than misbehaving.

#include <qcommon/sys_process.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

bool TestMachCrashUnsupportedOnNonMacos()
{
#if defined(__APPLE__)
    SysProcessHandle handle{nullptr};
    if (Sys_ProcessLaunch(SleepExecutable(), SleepArguments(), &handle)
        != SysProcessLaunchStatus::Started)
    {
        if (handle)
            Sys_ProcessRelease(&handle);
        return true;
    }

    const SysMachCrashFreezeStatus suspendResult =
        Sys_MachCrashFreezeSuspend(handle);
    if (suspendResult != SysMachCrashFreezeStatus::Frozen
        && suspendResult != SysMachCrashFreezeStatus::PermissionDenied)
    {
        std::fprintf(stderr,
            "Mach freeze returned unexpected status %d\n",
            static_cast<int>(suspendResult));
        Sys_ProcessRelease(&handle);
        return false;
    }

    if (suspendResult == SysMachCrashFreezeStatus::Frozen)
    {
        const SysMachCrashFreezeStatus resumeResult =
            Sys_MachCrashFreezeResume(handle);
        if (resumeResult != SysMachCrashFreezeStatus::Frozen)
        {
            std::fprintf(stderr,
                "Mach resume returned unexpected status %d\n",
                static_cast<int>(resumeResult));
            Sys_ProcessTerminate(handle, 1);
            Sys_ProcessRelease(&handle);
            return false;
        }
    }
    Sys_ProcessTerminate(handle, 1);
    Sys_ProcessRelease(&handle);
    return true;
#else
    SysMachCrashFreezeStatus suspendResult =
        Sys_MachCrashFreezeSuspend(nullptr);
    if (suspendResult != SysMachCrashFreezeStatus::Unsupported)
    {
        std::fprintf(stderr,
            "Mach suspend on null returned %d, expected Unsupported\n",
            static_cast<int>(suspendResult));
        return false;
    }
    SysMachCrashFreezeStatus resumeResult =
        Sys_MachCrashFreezeResume(nullptr);
    if (resumeResult != SysMachCrashFreezeStatus::Unsupported)
    {
        std::fprintf(stderr,
            "Mach resume on null returned %d, expected Unsupported\n",
            static_cast<int>(resumeResult));
        return false;
    }
    SysProcessHandle handle{nullptr};
    if (Sys_ProcessLaunch(SleepExecutable(), SleepArguments(), &handle)
        != SysProcessLaunchStatus::Started)
    {
        if (handle)
            Sys_ProcessRelease(&handle);
        return true;
    }
    suspendResult = Sys_MachCrashFreezeSuspend(handle);
    if (suspendResult != SysMachCrashFreezeStatus::Unsupported)
    {
        std::fprintf(stderr,
            "Mach suspend on real handle returned %d, expected Unsupported\n",
            static_cast<int>(suspendResult));
        Sys_ProcessTerminate(handle, 1);
        Sys_ProcessRelease(&handle);
        return false;
    }
    Sys_ProcessTerminate(handle, 1);
    Sys_ProcessRelease(&handle);
    return true;
#endif
}

bool TestSignalParkIsolationFromCrash()
{
    const SysSignalParkStatus enter = Sys_SignalParkEnter();
    if (enter == SysSignalParkStatus::Unsupported)
        return true;
    if (enter != SysSignalParkStatus::Entered)
        return false;

    SysProcessHandle handle{nullptr};
    if (Sys_ProcessLaunch(SleepExecutable(), SleepArguments(), &handle)
        != SysProcessLaunchStatus::Started)
    {
        if (handle)
            Sys_ProcessRelease(&handle);
        (void)Sys_SignalParkLeave();
        return false;
    }
    Sys_ProcessTerminate(handle, 1);
    Sys_ProcessRelease(&handle);

    const SysSignalParkLeaveStatus leave = Sys_SignalParkLeave();
    return leave == SysSignalParkLeaveStatus::Left;
}

bool TestMachCrashNullHandleSafe()
{
    const SysMachCrashFreezeStatus suspendResult =
        Sys_MachCrashFreezeSuspend(nullptr);
    if (suspendResult != SysMachCrashFreezeStatus::Unsupported
        && suspendResult != SysMachCrashFreezeStatus::Failed)
    {
        std::fprintf(stderr,
            "suspend on null returned unexpected status %d\n",
            static_cast<int>(suspendResult));
        return false;
    }
    const SysMachCrashFreezeStatus resumeResult =
        Sys_MachCrashFreezeResume(nullptr);
    if (resumeResult != SysMachCrashFreezeStatus::Unsupported
        && resumeResult != SysMachCrashFreezeStatus::Failed)
    {
        std::fprintf(stderr,
            "resume on null returned unexpected status %d\n",
            static_cast<int>(resumeResult));
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
        {"null-handle", &TestMachCrashNullHandleSafe},
        {"platform-supported", &TestMachCrashUnsupportedOnNonMacos},
        {"signal-park-isolation", &TestSignalParkIsolationFromCrash},
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
        std::fprintf(stderr, "%d platform-crash test failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
