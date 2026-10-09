// posix_sys_tests.cpp: the POSIX system layer every POSIX entry point shares
// (_platform/posix/posix_sys.cpp): the sysEvent_t queue the headless server's
// and the client's Sys_GetEvent read through Posix_DequeueEvent, and the
// command line Posix_BuildCommandLine hands Com_Init.
//
// The engine boundary is weak: --gc-sections drops the engine code no check
// reaches, so only what the queue and the builder call needs a stub.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include <_platform/posix/posix_sys.h>
#include <qcommon/qcommon.h>
#include <qcommon/sys_sync.h>
#include <qcommon/sys_time.h>
#include <qcommon/threads.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

// Payloads the queue freed, and with which tag.
int g_freed = 0;
int g_lastFreeTag = -1;

void *Payload()
{
    return std::malloc(4);
}

std::string BuildLine(std::initializer_list<const char *> args, std::size_t size)
{
    std::vector<char *> argv;
    for (const char *arg : args)
        argv.push_back(const_cast<char *>(arg));
    std::vector<char> out(size, '#');
    Posix_BuildCommandLine(static_cast<int>(argv.size()), argv.data(), out.data(), out.size());
    return std::string(out.data());
}
} // namespace

// Engine boundary.
WEAK void Z_Free(void *ptr, int tag)
{
    std::free(ptr);
    ++g_freed;
    g_lastFreeTag = tag;
}
WEAK uint32_t KISAK_CDECL Sys_Milliseconds() { return 1234; }
WEAK void KISAK_CDECL Sys_EnterCriticalSection(int) {}
WEAK void KISAK_CDECL Sys_LeaveCriticalSection(int) {}
WEAK void Com_Printf(int, const char *, ...) {}

int main()
{
    // Events come back in the order they were queued, with their fields.
    Sys_QueEvent(10, SE_KEY, 1, 2, 0, nullptr);
    Sys_QueEvent(0, SE_CHAR, 3, 4, 0, nullptr);
    sysEvent_t ev{};
    CHECK(Posix_DequeueEvent(&ev) && ev.evTime == 10 && ev.evType == SE_KEY && ev.evValue == 1 && ev.evValue2 == 2);
    // A zero time is stamped with Sys_Milliseconds.
    CHECK(Posix_DequeueEvent(&ev) && ev.evTime == 1234 && ev.evType == SE_CHAR && ev.evValue == 3);
    CHECK(!Posix_DequeueEvent(&ev));

    // A full queue drops its oldest event and frees that payload with the
    // event tag (10, as win32/win_main.cpp frees queued payloads).
    for (int i = 0; i < MAX_QUED_EVENTS + 1; ++i)
        Sys_QueEvent(static_cast<uint32_t>(100 + i), SE_KEY, i, 0, 4, Payload());
    CHECK(g_freed == 1 && g_lastFreeTag == 10);
    CHECK(Posix_DequeueEvent(&ev) && ev.evValue == 1);
    std::free(ev.evPtr);

    // Shutdown frees every payload still queued.
    Sys_ShutdownEvents();
    CHECK(g_freed == MAX_QUED_EVENTS);
    CHECK(!Posix_DequeueEvent(&ev));

    // The command line: no executable name, whitespace quoted.
    CHECK(BuildLine({"KisakCOD", "+set", "fs_basepath", "/srv/cod 4", "+map", "mp_crash"}, 128)
        == "+set fs_basepath \"/srv/cod 4\" +map mp_crash");
    CHECK(BuildLine({"KisakCOD"}, 16).empty());
    // An argument that does not fit is dropped with everything after it.
    CHECK(BuildLine({"KisakCOD", "+set", "a_very_long_argument", "+x"}, 16) == "+set");

    if (g_failures == 0)
        std::printf("posix sys: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
