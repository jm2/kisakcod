// posix_client_main.cpp: the POSIX MP client entry point (KISAK_CLIENT_SDL3).
// It is the windowed counterpart of posix_main.cpp: the same shared system
// layer (posix_sys.cpp) and terminal console, plus WinMain's client half.
// SDL3 owns the window, its event pump and the clipboard; the event queue and
// Com_Frame loop are unchanged (docs/design/CLIENT.md).

#include "posix_sys.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>

#include <client/cl_sdl3.h>
#include <gfx_d3d/r_init.h>
#include <qcommon/cmd.h>
#include <qcommon/qcommon.h>
#include <qcommon/sys_console.h>
#include <qcommon/sys_local.h>
#include <qcommon/sys_sync.h>
#include <qcommon/sys_time.h>
#include <qcommon/threads.h>
#include <universal/com_memory.h>
#include <universal/q_parse.h>
#include <universal/q_shared.h>
#include <universal/timing.h>

// Terminal mode and quit-signal hooks owned by posix_syscon.cpp.
void Sys_ConsoleInitTerminal();
void Sys_ConsoleShutdownTerminal();
void Sys_InstallQuitSignalHandlers();

namespace
{
// Win_GetEvent's payload tag; Sys_QueEvent's consumers free with it.
constexpr int kClipboardTag = 10;

char sys_cmdline[1024];

// Win_GetEvent's order: pump the window, then the console, then hand back the
// oldest queued event or an empty one stamped with the current time.
sysEvent_t *Posix_ClientGetEvent(sysEvent_t *result)
{
    Sys_EnterCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
    if (Posix_EventQueueEmpty())
    {
        Sys_LeaveCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
        CL_SdlPumpEvents();
        const char *line = Sys_ConsoleInput();
        if (line)
        {
            const size_t length = strnlen(line, SYS_CONSOLE_MAX_LINE_LENGTH + 1);
            char *payload = static_cast<char *>(Com_AllocEvent(static_cast<int>(length) + 1));
            I_strncpyz(payload, line, static_cast<int>(length) + 1);
            Sys_QueEvent(0, SE_CONSOLE, 0, 0, static_cast<int>(length) + 1, payload);
        }
    }
    else
    {
        Sys_LeaveCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
    }

    sysEvent_t ev;
    if (!Posix_DequeueEvent(&ev))
    {
        std::memset(&ev, 0, sizeof(ev));
        ev.evTime = static_cast<int>(Sys_Milliseconds());
    }
    *result = ev;
    return result;
}
} // namespace

sysEvent_t *__cdecl Sys_GetEvent(sysEvent_t *result)
{
    return Posix_ClientGetEvent(result);
}

void __cdecl Sys_LoadingKeepAlive()
{
    sysEvent_t result;
    while (Posix_ClientGetEvent(&result)->evType)
    {
    }
    R_CheckLostDevice();
}

void Sys_In_Restart_f()
{
    IN_Shutdown();
    IN_Init();
}

//=============================================================================
// Clipboard
//=============================================================================

// Like win_main.cpp: a Z_Malloc'd copy (tag 10) cut at the first line break,
// or null when the clipboard holds no text.
char *__cdecl Sys_GetClipboardData()
{
    char *text = SDL_GetClipboardText();
    if (!text || !*text)
    {
        SDL_free(text);
        return nullptr;
    }
    const size_t size = std::strlen(text) + 1;
    char *data = static_cast<char *>(Z_Malloc(static_cast<int>(size), "Sys_GetClipboardData", kClipboardTag));
    I_strncpyz(data, text, static_cast<int>(size));
    SDL_free(text);
    data[std::strcspn(data, "\n\r\b")] = '\0';
    return data;
}

int __cdecl Sys_SetClipboardData(const char *text)
{
    return SDL_SetClipboardText(text) ? 1 : 0;
}

/*
==================
main

WinMain's client path without the splash window: bring up the engine, queue
readStats, then run Com_Frame, sleeping while the window is minimised.
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
    if (!com_dedicated->current.integer)
        Cbuf_AddText(0, "readStats\n");
    Posix_PrintWorkingDir();

    while (true)
    {
        if (CL_SdlIsMinimized() || com_dedicated->current.integer)
            Sys_Sleep(5);
        Cbuf_AddRequestedQuit();
        Com_Frame();
    }
}
