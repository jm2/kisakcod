// posix_main.cpp: POSIX headless entry point. The process entry, the
// console-driven Sys_GetEvent pump and the clipboard of the headless dedicated
// server; the shared system layer it runs on is posix_sys.cpp. It replaces the
// window, message-pump and console-window half of win32/win_main.cpp -- a
// headless server has no HWND, no DirectInput and no splash screen
// (docs/design/PLATFORM_POSIX.md, NOW row 13).

#include "posix_sys.h"

#include <chrono>
#include <qcommon/sys_local.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <unistd.h>

#include <qcommon/cmd.h>
#include <qcommon/net_local.h>
#include <qcommon/qcommon.h>
#include <qcommon/sys_console.h>
#include <qcommon/sys_sync.h>
#include <qcommon/sys_time.h>
#include <qcommon/threads.h>
#include <script/scr_stringlist.h>
#include <universal/com_memory.h>
#include <universal/q_parse.h>
#include <universal/q_shared.h>
#include <universal/timing.h>

#if defined(__linux__)
#include <sys/utsname.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#endif

// Terminal mode and quit-signal hooks owned by posix_syscon.cpp.
void Sys_ConsoleInitTerminal();
void Sys_ConsoleShutdownTerminal();
void Sys_InstallQuitSignalHandlers();

namespace
{
char sys_cmdline[1024];
} // namespace

// Pumps console input, then returns the next queued event. When the queue is
// empty the returned record is zero-filled with the current time, which is the
// "idle frame" signal Com_EventLoop treats as no work.
static sysEvent_t *Posix_GetEvent(sysEvent_t *result)
{
    Sys_EnterCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
    if (Posix_EventQueueEmpty())
    {
        const char *line = Sys_ConsoleInput();
        if (line)
        {
            const size_t length = strnlen(line, SYS_CONSOLE_MAX_LINE_LENGTH + 1);
            char *payload = reinterpret_cast<char *>(Com_AllocEvent(static_cast<int>(length) + 1));
            I_strncpyz(payload, line, static_cast<int>(length) + 1);
            Sys_LeaveCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
            Sys_QueEvent(0, SE_CONSOLE, 0, 0, static_cast<int>(length) + 1, payload);
            Sys_EnterCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
        }
    }

    Sys_LeaveCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
    sysEvent_t ev;
    if (!Posix_DequeueEvent(&ev))
    {
        std::memset(&ev, 0, sizeof(ev));
        ev.evTime = static_cast<int>(Sys_Milliseconds());
    }
    *result = ev;
    return result;
}

sysEvent_t *__cdecl Sys_GetEvent(sysEvent_t *result)
{
    return Posix_GetEvent(result);
}

void __cdecl Sys_LoadingKeepAlive()
{
    sysEvent_t result;
    sysEvent_t ev;
    do
    {
        ev = *Posix_GetEvent(&result);
    } while (ev.evType);
}

void Sys_In_Restart_f()
{
    // No input subsystem on a headless server.
}

//=============================================================================
// Clipboard
//=============================================================================

char *__cdecl Sys_GetClipboardData()
{
    return nullptr;
}

int __cdecl Sys_SetClipboardData(const char *text)
{
    (void)text;
    return 0;
}

/*
==================
main

The headless dedicated entry point: bring up the engine, then run Com_Frame
until Sys_Quit terminates the process. The frame loop yields like the Win32
dedicated path so an idle server does not spin a core. SIGINT and SIGTERM
request the quit a typed quit runs, from the start of main on.
==================
*/
int main(int argc, char **argv)
{
    Sys_InstallQuitSignalHandlers();
    Sys_InitializeCriticalSections();
    Sys_InitMainThread();
    Posix_DetectCpu();

    Posix_BuildCommandLine(argc, argv, sys_cmdline, sizeof(sys_cmdline));

    Sys_ConsoleInitTerminal();
    atexit(Sys_ConsoleShutdownTerminal);

    Com_InitParse();
    Dvar_Init();
    InitTiming();
    Sys_Milliseconds();

    Com_Init(sys_cmdline);
    Posix_PrintWorkingDir();

    while (true)
    {
        // A dedicated server has no frame budget to hit, so yield exactly like
        // the Win32 dedicated path and let Com_Frame drive the tick.
        Sys_Sleep(5);
        // A pending SIGINT or SIGTERM, even one from Com_Init, becomes the
        // quit this frame runs.
        Cbuf_AddRequestedQuit();
        Com_Frame();
    }
}
