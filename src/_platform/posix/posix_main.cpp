// posix_main.cpp: POSIX headless entry, frame loop and engine system layer.
// This is the POSIX counterpart of win32/win_main.cpp for the headless
// dedicated server: the process entry point, the sysEvent_t queue, and the
// Sys_Init/Sys_Quit/Sys_Print boundary the engine calls. It replaces the
// window, message-pump and console-window half of the Win32 file outright --
// a headless server has no HWND, no DirectInput and no splash screen --
// while keeping the queue contract identical so qcommon/common.cpp needs no
// platform branch (docs/design/PLATFORM_POSIX.md, NOW row 13).
//
// Everything here is headless-only. The windowed client keeps win_main.cpp.

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
#endif

SysInfo sys_info;
int client_state;

// Terminal mode hooks owned by posix_syscon.cpp. They are POSIX-only, so they
// stay out of the portable qcommon/sys_local.h surface.
void Sys_ConsoleInitTerminal();
void Sys_ConsoleShutdownTerminal();

// Command registration nodes for the Sys_* console commands. They must outlive
// the registration call, so they sit at file scope exactly as in win_main.cpp.
cmd_function_s Sys_In_Restart_f_VAR;
#ifdef KISAK_MP
cmd_function_s Sys_Net_Restart_f_VAR;
cmd_function_s Sys_Listen_f_VAR;
#endif

namespace
{
// Event payload allocation tag. COD4 dropped memtag_t, so the tag is the raw
// int the retail layer passes; 10 is the value win32/win_main.cpp frees queued
// event payloads with, and the queue must allocate and free with the same one.
constexpr int kEventPayloadTag = 10;

sysEvent_t eventQue[MAX_QUED_EVENTS];
int eventHead = 0;
int eventTail = 0;

char sys_cmdline[1024];

void PrintWorkingDir()
{
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)))
        Com_Printf(16, "Working directory: %s\n", cwd);
}

// Portable CPU description. The retail Win32 path uses CPUID and a measured
// GHz benchmark (win32/win_configure.cpp); a headless server only reports the
// host, so the uname/process-count answer is enough to fill SysInfo and keep
// the boot banner honest.
void DetectCpu()
{
    std::memset(&sys_info, 0, sizeof(sys_info));
    std::snprintf(sys_info.cpuVendor, sizeof(sys_info.cpuVendor), "unknown");

#if defined(__linux__)
    struct utsname names{};
    if (uname(&names) == 0)
    {
        std::snprintf(sys_info.cpuName, sizeof(sys_info.cpuName), "%s %s",
            names.machine, names.release);
    }
#else
    std::snprintf(sys_info.cpuName, sizeof(sys_info.cpuName), "unknown");
#endif

    long processors = sysconf(_SC_NPROCESSORS_ONLN);
    if (processors < 1)
        processors = 1;
    sys_info.logicalCpuCount = static_cast<int>(processors);
    sys_info.physicalCpuCount = static_cast<int>(processors);

    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && pageSize > 0)
    {
        const long long bytes = static_cast<long long>(pages) * pageSize;
        long megabytes = static_cast<long>(bytes / (1024 * 1024));
        if (megabytes > 1024)
            megabytes = 1024; // the retail banner caps the report at 1 GB
        sys_info.sysMB = static_cast<int>(megabytes);
    }

#if defined(__SSE__)
    sys_info.SSE = true;
#else
    sys_info.SSE = false;
#endif
    std::snprintf(sys_info.gpuDescription, sizeof(sys_info.gpuDescription), "headless");
}

[[noreturn]] void TerminateOnFatalError(const char *message)
{
    const char prefix[] = "\nKisakCOD fatal error: ";
    const char newline[] = "\n";
    const char *const safeMessage = message ? message : "Unknown fatal error";
    (void)Sys_ConsoleWrite(
        SysConsoleOutputStream::StandardError, prefix, sizeof(prefix) - 1);
    (void)Sys_ConsoleWrite(
        SysConsoleOutputStream::StandardError, safeMessage, std::strlen(safeMessage));
    (void)Sys_ConsoleWrite(
        SysConsoleOutputStream::StandardError, newline, sizeof(newline) - 1);
    (void)Sys_ConsoleFlush(SysConsoleOutputStream::StandardError);
    std::exit(EXIT_FAILURE);
}
} // namespace

//=============================================================================
// The sysEvent_t queue. Contract matches win32/win_main.cpp exactly so
// Com_EventLoop and Debug_EventLoop read the same record shape.
//=============================================================================

void __cdecl Sys_QueEvent(
    uint32_t timeMs,
    sysEventType_t type,
    int value,
    int value2,
    int ptrLength,
    void *ptr)
{
    Sys_EnterCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
    sysEvent_t *ev = &eventQue[static_cast<unsigned int>(eventHead) & MASK_QUED_EVENTS];
    if (eventHead - eventTail >= MAX_QUED_EVENTS)
    {
        Com_Printf(16, "Sys_QueEvent: overflow\n");
        if (ev->evPtr)
            Z_Free(ev->evPtr, kEventPayloadTag);
        ++eventTail;
    }
    ++eventHead;
    if (!timeMs)
        timeMs = Sys_Milliseconds();
    ev->evTime = static_cast<int>(timeMs);
    ev->evType = type;
    ev->evValue = value;
    ev->evValue2 = value2;
    ev->evPtrLength = ptrLength;
    ev->evPtr = ptr;
    Sys_LeaveCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
}

void Sys_ShutdownEvents()
{
    Sys_EnterCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
    while (eventHead > eventTail)
    {
        sysEvent_t *ev = &eventQue[static_cast<unsigned int>(eventTail++) & MASK_QUED_EVENTS];
        if (ev->evPtr)
            Z_Free(ev->evPtr, kEventPayloadTag);
    }
    Sys_LeaveCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
}

// Pumps console input, then returns the next queued event. When the queue is
// empty the returned record is zero-filled with the current time, which is the
// "idle frame" signal Com_EventLoop treats as no work.
static sysEvent_t *Posix_GetEvent(sysEvent_t *result)
{
    Sys_EnterCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
    if (eventHead <= eventTail)
    {
        const char *line = Sys_ConsoleInput();
        if (line)
        {
            const size_t length = std::strlen(line);
            char *payload = reinterpret_cast<char *>(Com_AllocEvent(static_cast<int>(length) + 1));
            I_strncpyz(payload, line, static_cast<int>(length) + 1);
            Sys_LeaveCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
            Sys_QueEvent(0, SE_CONSOLE, 0, 0, static_cast<int>(length) + 1, payload);
            Sys_EnterCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
        }
    }

    sysEvent_t ev;
    std::memset(&ev, 0, sizeof(ev));
    if (eventHead > eventTail)
    {
        ev = eventQue[static_cast<unsigned int>(eventTail++) & MASK_QUED_EVENTS];
    }
    else
    {
        ev.evTime = static_cast<int>(Sys_Milliseconds());
    }
    Sys_LeaveCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
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

//=============================================================================
// Process lifetime
//=============================================================================

void __cdecl Sys_Print(const char *msg)
{
    if (!msg)
        return;
    Conbuf_AppendTextInMainThread(msg);
}

void Sys_SetErrorText(const char *buf)
{
    (void)buf;
}

void __cdecl Sys_OutOfMemErrorInternal(const char *filename, int line)
{
    char buffer[512];
    Com_sprintf(
        buffer,
        sizeof(buffer),
        "Out of memory allocating in %s at line %d\n",
        filename ? filename : "<unknown>",
        line);
    TerminateOnFatalError(buffer);
}

[[noreturn]] void Sys_Error(const char *error, ...)
{
    char string[4096];
    va_list va;
    va_start(va, error);
    std::vsnprintf(string, sizeof(string), error ? error : "", va);
    va_end(va);

    Conbuf_AppendTextInMainThread("\n\n");
    Conbuf_AppendTextInMainThread(string);
    Conbuf_AppendTextInMainThread("\n");
    TerminateOnFatalError(string);
}

void __cdecl Sys_NormalExit()
{
    Sys_ShutdownEvents();
}

void __cdecl Sys_Quit()
{
    Sys_EnterCriticalSection(CRITSECT_COM_ERROR);
    Sys_DestroyConsole();
    Sys_NormalExit();
    RefreshQuitOnErrorCondition();
    Dvar_Shutdown();
    Cmd_Shutdown();
    SL_Shutdown();
    Sys_LeaveCriticalSection(CRITSECT_COM_ERROR);
    std::exit(0);
}

void __cdecl Sys_OpenURL(const char *url, int doexit)
{
    (void)url;
    if (doexit)
        Sys_Quit();
}

void __cdecl Sys_QuitAndStartProcess(const char *exeName, const char *parameters)
{
    (void)exeName;
    (void)parameters;
    // Relaunch is a later NOW item; a headless server simply stops.
}

void Sys_In_Restart_f()
{
    // No input subsystem on a headless server.
}

#ifdef KISAK_MP
void Sys_Net_Restart_f()
{
    NET_Restart();
}
#endif

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

//=============================================================================
// Init
//=============================================================================

void __cdecl Sys_Init()
{
    Cmd_AddCommandInternal("in_restart", Sys_In_Restart_f, &Sys_In_Restart_f_VAR);
#ifdef KISAK_MP
    Cmd_AddCommandInternal("net_restart", Sys_Net_Restart_f, &Sys_Net_Restart_f_VAR);
    Cmd_AddCommandInternal("net_listen", Sys_Listen_f, &Sys_Listen_f_VAR);
#endif

    Com_Printf(16, "CPU vendor is \"%s\"\n", sys_info.cpuVendor);
    Com_Printf(16, "CPU name is \"%s\"\n", sys_info.cpuName);
    Com_Printf(16, "%i logical CPU%s reported\n", sys_info.logicalCpuCount,
        sys_info.logicalCpuCount == 1 ? "" : "s");
    Com_Printf(16, "%i physical CPU%s detected\n", sys_info.physicalCpuCount,
        sys_info.physicalCpuCount == 1 ? "" : "s");
    Com_Printf(16, "Measured CPU speed is %.2lf GHz\n", static_cast<double>(sys_info.cpuGHz));
    Com_Printf(16, "Total CPU performance is estimated as %.2lf GHz\n",
        static_cast<double>(sys_info.configureGHz));
    Com_Printf(16, "System memory is %i MB (capped at 1 GB)\n", sys_info.sysMB);
    Com_Printf(16, "Video card is \"%s\"\n", sys_info.gpuDescription);
    Com_Printf(16, "Streaming SIMD Extensions (SSE) %ssupported\n", sys_info.SSE ? "" : "not ");
    Com_Printf(16, "\n");
}

/*
==================
main

The headless dedicated entry point: bring up the engine, then run Com_Frame
until Sys_Quit terminates the process. The frame loop yields like the Win32
dedicated path so an idle server does not spin a core.
==================
*/
int main(int argc, char **argv)
{
    Sys_InitializeCriticalSections();
    Sys_InitMainThread();
    DetectCpu();

    // Reassemble the command line the way Com_ParseCommandLine expects it:
    // WinMain's lpCmdLine, i.e. the arguments without the executable name
    // (the name would run as an "Unknown command"), each argument that holds
    // whitespace quoted so a path such as fs_basepath survives tokenizing.
    size_t offset = 0;
    for (int i = 1; i < argc; ++i)
    {
        const size_t length = std::strlen(argv[i]);
        const bool quote = std::strpbrk(argv[i], " \t") != nullptr;
        if (offset + length + (quote ? 2 : 0) + 2 >= sizeof(sys_cmdline))
            break;
        if (offset > 0)
            sys_cmdline[offset++] = ' ';
        if (quote)
            sys_cmdline[offset++] = '"';
        std::memcpy(sys_cmdline + offset, argv[i], length);
        offset += length;
        if (quote)
            sys_cmdline[offset++] = '"';
    }
    sys_cmdline[offset] = 0;

    Sys_ConsoleInitTerminal();
    atexit(Sys_ConsoleShutdownTerminal);

    Com_InitParse();
    Dvar_Init();
    InitTiming();
    Sys_Milliseconds();

    Com_Init(sys_cmdline);
    PrintWorkingDir();

    while (true)
    {
        // A dedicated server has no frame budget to hit, so yield exactly like
        // the Win32 dedicated path and let Com_Frame drive the tick.
        Sys_Sleep(5);
        Com_Frame();
    }
}
