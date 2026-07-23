// SPDX-License-Identifier: GPL-3.0-only
//
// platform_crash_tests.cpp -- exercises the crash-freeze surface of
// the portable Sys_Process* API. On every host, the contract validates
// that the API returns Unsupported rather than misbehaving; the
// macOS-only Mach freeze path is exercised by the platform-process
// tests' freeze-unsupported selector (which is a no-op on macOS).

#include <qcommon/sys_process.h>

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

bool TestProcessFreezeUnsupportedContract()
{
    // The contract is that on every host the function returns a status
    // without crashing the caller. On non-macOS hosts the status is
    // Unsupported; on macOS the function freezes the calling thread
    // and never returns, so this test does not exercise that path.
    const SysProcessFreezeStatus status = Sys_ProcessFreezeForCrash();
#if defined(__APPLE__)
    (void)status;
    return true;
#else
    if (status != SysProcessFreezeStatus::Unsupported)
    {
        std::fprintf(stderr,
            "ProcessFreeze on non-macOS returned %d, expected Unsupported\n",
            static_cast<int>(status));
        return false;
    }
    return true;
#endif
}

bool TestSignalParkRejectsInvalidSignals()
{
    const int sigkill = SIGKILL;
    if (Sys_SignalPark(&sigkill, 1) != SysSignalParkStatus::InvalidArgument)
    {
        std::fputs("signal-park accepted SIGKILL\n", stderr);
        return false;
    }
    const int sigstop = SIGSTOP;
    if (Sys_SignalPark(&sigstop, 1) != SysSignalParkStatus::InvalidArgument)
    {
        std::fputs("signal-park accepted SIGSTOP\n", stderr);
        return false;
    }
    const int bad = -1;
    if (Sys_SignalPark(&bad, 1) != SysSignalParkStatus::InvalidArgument)
    {
        std::fputs("signal-park accepted negative signal\n", stderr);
        return false;
    }
    return true;
}

bool TestSignalParkDisjointStacking()
{
    if (Sys_SignalPark(nullptr, 0) != SysSignalParkStatus::Parked)
    {
        std::fputs("baseline park did not report Parked\n", stderr);
        return false;
    }
    const int sigint = SIGINT;
    const SysSignalParkStatus firstPark = Sys_SignalPark(&sigint, 1);
#if defined(_WIN32)
    if (firstPark != SysSignalParkStatus::Unsupported)
    {
        std::fputs("signal-park on Win32 did not report Unsupported\n",
            stderr);
        (void)Sys_SignalUnPark(nullptr, 0);
        return false;
    }
    (void)Sys_SignalUnPark(nullptr, 0);
    return true;
#else
    if (firstPark != SysSignalParkStatus::Parked)
    {
        std::fputs("disjoint park did not report Parked\n", stderr);
        (void)Sys_SignalUnPark(nullptr, 0);
        return false;
    }
    const int sigterm = SIGTERM;
    if (Sys_SignalPark(&sigterm, 1) != SysSignalParkStatus::Parked)
    {
        std::fputs("second disjoint park did not report Parked\n", stderr);
        (void)Sys_SignalUnPark(&sigterm, 1);
        (void)Sys_SignalUnPark(nullptr, 0);
        return false;
    }
    if (Sys_SignalUnPark(&sigterm, 1) != SysSignalParkStatus::Unparked)
    {
        std::fputs("disjoint un-park did not report Unparked\n", stderr);
        return false;
    }
    if (Sys_SignalUnPark(&sigint, 1) != SysSignalParkStatus::Unparked)
    {
        std::fputs("inner un-park did not report Unparked\n", stderr);
        return false;
    }
    if (Sys_SignalUnPark(nullptr, 0) != SysSignalParkStatus::Unparked)
    {
        std::fputs("outer un-park did not report Unparked\n", stderr);
        return false;
    }
    return true;
#endif
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
        {"freeze-unsupported", &TestProcessFreezeUnsupportedContract},
        {"signal-park-rejects-invalid", &TestSignalParkRejectsInvalidSignals},
        {"signal-park-disjoint-stacking", &TestSignalParkDisjointStacking},
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
        std::fprintf(stderr, "%d platform-crash test failure(s)\n",
            failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
