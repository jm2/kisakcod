// win_dpi_awareness_tests.cpp: the Windows clients embed dpi_aware.manifest so
// that Windows does not scale their window or confine the cursor on high-DPI
// displays (#918). This test embeds the same manifest and checks, through the
// engine's Sys_DpiAwarenessName, that the process starts per-monitor v2 aware,
// then that the helper names each awareness a thread can switch to.

#include <cstdio>
#include <cstring>

#include <Windows.h>

#include <win32/win_dpi.h>

namespace
{
int g_failures;

void Expect(const char *const what, const char *const expected)
{
    const char *const actual = Sys_DpiAwarenessName();
    if (std::strcmp(actual, expected) != 0)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s: got \"%s\", want \"%s\"\n", what, actual, expected);
    }
}

void ExpectThread(const DPI_AWARENESS_CONTEXT context, const char *const expected)
{
    if (!SetThreadDpiAwarenessContext(context))
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL SetThreadDpiAwarenessContext for \"%s\"\n", expected);
        return;
    }
    Expect("thread awareness", expected);
}
} // namespace

int main()
{
    Expect("startup awareness from the manifest", "per-monitor v2");

    ExpectThread(DPI_AWARENESS_CONTEXT_UNAWARE, "unaware");
    ExpectThread(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE, "system");
    ExpectThread(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE, "per-monitor");
    ExpectThread(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2, "per-monitor v2");

    if (g_failures)
    {
        std::fprintf(stderr, "win dpi awareness: FAILED\n");
        return 1;
    }
    std::printf("win dpi awareness: ok\n");
    return 0;
}
