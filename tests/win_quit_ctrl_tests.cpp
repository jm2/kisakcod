// win_quit_ctrl_tests.cpp: the Win32 headless server's orderly quit on Ctrl+C,
// Ctrl+Break and console close (qcommon/sys_quit.h,
// _platform/win32/sys_console.cpp). A console control event reaches every
// process on the console, so each case runs in a child started on a hidden
// console of its own; the child installs the real handler and raises real
// events on that console. The frame loop's step then feeds the real command
// buffer (qcommon/cmd.cpp), whose Cbuf_Execute runs the "quit" command.
//
// usage: kisakcod-win-quit-ctrl-tests            runs every case in a child
//        kisakcod-win-quit-ctrl-tests <case>     one case; needs a console of its own

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>

#include <database/database.h>
#include <qcommon/cmd.h>
#include <qcommon/qcommon.h>
#include <qcommon/sys_console.h>
#include <qcommon/sys_quit.h>
#include <universal/com_files.h>

extern CmdText cmd_textArray[1]; // cmd.cpp's command buffer

namespace
{
int g_failures = 0;

void Check(const bool ok, const char *const what)
{
    if (!ok)
    {
        ++g_failures;
        std::printf("FAIL %s\n", what);
    }
}

// The handler runs on a thread the system starts; give it a moment.
bool WaitForRequest(const DWORD timeoutMs)
{
    const ULONGLONG end = GetTickCount64() + timeoutMs;
    while (!Sys_QuitRequested() && GetTickCount64() < end)
        Sleep(10);
    return Sys_QuitRequested();
}

bool SourceIs(const char *const name)
{
    const char *const source = Sys_QuitRequestSource();
    return source && std::strcmp(source, name) == 0;
}

int g_quitRuns = 0;
cmd_function_s g_quitCommand;

// Stands in for Com_Quit_f, which would end the test process.
void Quit_f()
{
    ++g_quitRuns;
}

std::string_view Queued()
{
    return {reinterpret_cast<const char *>(cmd_textArray[0].data), static_cast<size_t>(cmd_textArray[0].cmdsize)};
}

// Ctrl+C or Ctrl+Break, before the frame loop's first step as during Com_Init.
void RunEvent(const DWORD event, const char *const name)
{
    Cbuf_Init();
    Cmd_AddCommandInternal("quit", Quit_f, &g_quitCommand);
    Check(!Cbuf_AddRequestedQuit() && Queued().empty(), "no request, nothing queued");
    // A launcher's inherited Ctrl+C ignore is its own case (ignored-ctrl-c).
    (void)SetConsoleCtrlHandler(nullptr, FALSE);
    Check(Sys_ConsoleInstallQuitHandler(), "the handler installs");
    Check(!Sys_QuitRequested(), "no request before an event");
    // Without the handler the event ends this process here.
    Check(GenerateConsoleCtrlEvent(event, 0) != FALSE, "GenerateConsoleCtrlEvent");
    Check(WaitForRequest(5000), "the event requests a quit");
    Check(SourceIs(name), "the request names the event");
    Check(Cbuf_AddRequestedQuit(), "the first frame queues the quit");
    Check(Queued() == "quit\n", "the queued text is a typed quit");
    Check(!Cbuf_AddRequestedQuit() && Queued() == "quit\n", "the quit is queued once");
    Cbuf_Execute(0, 0);
    Check(g_quitRuns == 1 && Queued().empty(), "Cbuf_Execute runs the quit command");
}

// A second event ends the process through the default handler: the child
// reports that it outlived the first, then dies of the second.
void RunSecondEvent(const DWORD first, const DWORD second)
{
    (void)SetConsoleCtrlHandler(nullptr, FALSE);
    Check(Sys_ConsoleInstallQuitHandler(), "the handler installs");
    Check(GenerateConsoleCtrlEvent(first, 0) != FALSE && WaitForRequest(5000), "the first event requests a quit");
    std::printf("survived the first event\n");
    std::fflush(stdout);
    (void)GenerateConsoleCtrlEvent(second, 0);
    Sleep(10000);
    Check(false, "the second event ends the process");
}

// A launcher's inherited Ctrl+C ignore (start /b, a service wrapper) stays in
// force; Ctrl+Break cannot be ignored and still quits.
void RunIgnoredCtrlC()
{
    (void)SetConsoleCtrlHandler(nullptr, TRUE);
    Check(Sys_ConsoleInstallQuitHandler(), "the handler installs");
    Check(GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0) != FALSE, "GenerateConsoleCtrlEvent");
    Check(!WaitForRequest(1000), "an ignored Ctrl+C requests nothing");
    Check(GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, 0) != FALSE && WaitForRequest(5000),
          "Ctrl+Break still requests a quit");
    Check(SourceIs("CTRL_BREAK_EVENT"), "the request names Ctrl+Break");
}

// Closing the console: the handler holds the process while this thread, in the
// server's place, finishes its quit and exits 0. Had the handler returned, the
// system would have ended the process before the exit below.
void RunClose()
{
    Check(Sys_ConsoleInstallQuitHandler(), "the handler installs");
    const HWND console = GetConsoleWindow();
    Check(console != nullptr, "a console window to close");
    Check(PostMessageW(console, WM_CLOSE, 0, 0) != FALSE, "WM_CLOSE posted");
    Check(WaitForRequest(5000), "closing the console requests a quit");
    Check(SourceIs("CTRL_CLOSE_EVENT"), "the request names the close");
    Sleep(1000);
    std::printf("quit finished inside the grace period\n");
    std::fflush(stdout);
}

struct ChildResult
{
    DWORD exitCode = 0;
    std::string output;
    bool finished = false;
};

// Runs this executable with one case on a hidden console of its own; its
// standard output and error come back through a pipe.
ChildResult RunChild(const char *const caseName)
{
    ChildResult result;
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &inherit, 1 << 16))
        return result;
    (void)SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    wchar_t self[MAX_PATH * 4];
    DWORD selfLength = static_cast<DWORD>(std::size(self));
    if (!QueryFullProcessImageNameW(GetCurrentProcess(), 0, self, &selfLength))
    {
        (void)CloseHandle(readEnd);
        (void)CloseHandle(writeEnd);
        return result;
    }
    std::wstring commandLine = L"\"" + std::wstring(self) + L"\" ";
    for (const char *c = caseName; *c; ++c)
        commandLine += static_cast<wchar_t>(*c);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = writeEnd;
    startup.hStdError = writeEnd;
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(self, commandLine.data(), nullptr, nullptr, TRUE,
                                       CREATE_NEW_CONSOLE, nullptr, nullptr, &startup, &process);
    (void)CloseHandle(writeEnd);
    if (started)
    {
        if (WaitForSingleObject(process.hProcess, 30000) == WAIT_OBJECT_0)
            result.finished = GetExitCodeProcess(process.hProcess, &result.exitCode) != FALSE;
        else
            (void)TerminateProcess(process.hProcess, 99);
        (void)CloseHandle(process.hThread);
        (void)CloseHandle(process.hProcess);
        char buffer[4096];
        DWORD read = 0;
        while (ReadFile(readEnd, buffer, sizeof(buffer), &read, nullptr) && read)
            result.output.append(buffer, read);
    }
    (void)CloseHandle(readEnd);
    return result;
}

bool Expect(const char *const caseName, const DWORD exitCode, const char *const marker)
{
    const ChildResult child = RunChild(caseName);
    const bool ok = child.finished && child.exitCode == exitCode
        && (!marker || child.output.find(marker) != std::string::npos);
    std::printf("%s %s (exit %#lx)\n", ok ? "ok  " : "FAIL", caseName, static_cast<unsigned long>(child.exitCode));
    if (!ok)
    {
        std::printf("  expected exit %#lx%s%s; child output:\n%s\n", static_cast<unsigned long>(exitCode),
                    marker ? " and output " : "", marker ? marker : "", child.output.c_str());
        ++g_failures;
    }
    return ok;
}

int RunCase(const char *const caseName)
{
    if (std::strcmp(caseName, "ctrl-c") == 0)
        RunEvent(CTRL_C_EVENT, "CTRL_C_EVENT");
    else if (std::strcmp(caseName, "ctrl-break") == 0)
        RunEvent(CTRL_BREAK_EVENT, "CTRL_BREAK_EVENT");
    else if (std::strcmp(caseName, "second-ctrl-c") == 0)
        RunSecondEvent(CTRL_C_EVENT, CTRL_C_EVENT);
    else if (std::strcmp(caseName, "break-then-ctrl-c") == 0)
        RunSecondEvent(CTRL_BREAK_EVENT, CTRL_C_EVENT);
    else if (std::strcmp(caseName, "ignored-ctrl-c") == 0)
        RunIgnoredCtrlC();
    else if (std::strcmp(caseName, "close") == 0)
        RunClose();
    else
    {
        std::printf("unknown case %s\n", caseName);
        return 2;
    }
    std::printf("win quit %s: %s\n", caseName, g_failures ? "FAILED" : "ok");
    return g_failures ? 1 : 0;
}
} // namespace

// cmd.cpp's engine boundary, as in posix_quit_signal_tests.cpp. The checks never
// reach these; only the linker does.
const dvar_t *com_sv_running;
void Com_Printf(int, const char *, ...) {}
void MyAssertHandler(const char *filename, int line, int, const char *, ...)
{
    std::printf("engine assert at %s:%d\n", filename ? filename : "?", line);
    std::fflush(stdout);
    ExitProcess(3);
}
int Dvar_Command() { return 0; }
int SV_GameCommand() { return 0; }
void SV_WaitServer() {}
void Scr_MonitorCommand(const char *) {}
void Sys_EnterCriticalSection(int) {}
void Sys_LeaveCriticalSection(int) {}
int I_stricmp(const char *s0, const char *s1) { return _stricmp(s0, s1); }
char *va(const char *, ...)
{
    static char text[1];
    return text;
}
// MSVC has no --gc-sections: every function of cmd.obj must link, so the
// exec, list and autocomplete commands' externals are stubbed as well.
void I_strncpyz(char *dest, const char *, int destsize)
{
    if (destsize > 0)
        *dest = '\0';
}
const char *Com_GetFilenameSubString(const char *pathname) { return pathname; }
void Com_DefaultExtension(char *, unsigned int, const char *) {}
char Com_Filter(const char *, char *, int) { return 0; }
void Com_PrintError(int, const char *, ...) {}
const dvar_s *Dvar_FindVar(const char *) { return nullptr; }
void track_static_alloc_internal(void *, int, const char *, int) {}
bool DB_IsMinimumFastFileLoaded() { return false; }
XAssetHeader DB_FindXAssetHeader(XAssetType, const char *) { return {}; }
int FS_ReadFile(const char *, void **) { return -1; }
void FS_FreeFile(char *) {}
const char **FS_ListFiles(const char *, const char *, FsListBehavior_e, int *count)
{
    *count = 0;
    return nullptr;
}
// Referenced by cmd.cpp's Debug-only assertions.
bool Sys_IsMainThread() { return true; }
const dvar_t *useFastFile;
int com_inServerFrame;

int main(int argc, char **argv)
{
    if (argc > 1)
    {
        // The events reach every process on the console: a case run by hand
        // from a terminal would hit the terminal's shell too.
        DWORD attached[2];
        if (GetConsoleProcessList(attached, 2) != 1)
        {
            std::printf("case %s needs a console of its own; run the test without arguments\n", argv[1]);
            return 2;
        }
        return RunCase(argv[1]);
    }

    // A case ended by the default handler exits with STATUS_CONTROL_C_EXIT.
    const DWORD killedByCtrl = static_cast<DWORD>(STATUS_CONTROL_C_EXIT);
    Expect("ctrl-c", 0, "win quit ctrl-c: ok");
    Expect("ctrl-break", 0, "win quit ctrl-break: ok");
    Expect("second-ctrl-c", killedByCtrl, "survived the first event");
    Expect("break-then-ctrl-c", killedByCtrl, "survived the first event");
    Expect("ignored-ctrl-c", 0, "win quit ignored-ctrl-c: ok");
    Expect("close", 0, "win quit close: ok");
    std::printf("win quit ctrl: %s\n", g_failures ? "FAILED" : "ok");
    return g_failures ? 1 : 0;
}
