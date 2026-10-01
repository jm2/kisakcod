// posix_syscon.cpp: the termios console for the POSIX headless dedicated
// server. This is the POSIX counterpart of win32/win_syscon.cpp -- the
// Conbuf_* engine console surface plus terminal mode handling -- built on the
// portable qcommon/sys_console.h service (docs/design/PLATFORM_POSIX.md,
// NOW row 13).
//
// Terminal handling is the termios half of the job: while the server owns the
// terminal it puts it in non-canonical mode so the engine sees each keystroke
// rather than having the tty driver line-buffer for it, and it restores the
// caller's original mode on every exit path (including Sys_Quit and a fatal
// error). Without that restore an aborted server leaves the user's shell in
// raw mode.
//
// Ctrl+C at the terminal (SIGINT) and kill or systemctl stop (SIGTERM) ask for
// the same orderly quit a typed quit runs (qcommon/sys_quit.h); a second one
// kills the server as the default action would.

#include <qcommon/sys_local.h>

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstring>

#include <termios.h>
#include <unistd.h>

#include <qcommon/qcommon.h>
#include <qcommon/sys_console.h>
#include <qcommon/sys_quit.h>
#include <qcommon/threads.h>

namespace
{
// Color-code stripping and newline normalization work buffer, sized like the
// Win32 console buffer so a long engine line cannot overflow the clean step.
constexpr std::size_t kCleanTextCapacity = 0x8000;
// Engine messages are NUL-terminated; lengths are still bounded so a message
// without a terminator cannot run the scan past this many bytes.
constexpr std::size_t kMaxMessageBytes = 0x10000;

// Original terminal state, captured once at startup and put back at teardown.
// A null termios pointer means stdin was never a terminal and there is nothing
// to restore.
termios savedTerminal{};
bool terminalStateSaved = false;
bool terminalModeActive = false;

void RestoreTerminalMode()
{
    if (!terminalStateSaved || !terminalModeActive)
        return;
    if (isatty(STDIN_FILENO) == 1)
        (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &savedTerminal);
    terminalModeActive = false;
}

// Puts the controlling terminal into non-canonical mode with echo left to the
// tty: the engine already redraws the prompt, so suppressing echo would make
// typed commands invisible. VMIN=0/VTIME=0 makes the read in the console
// backend non-blocking on a tty, matching the redirected-input path.
void EnterTerminalMode()
{
    if (!terminalStateSaved || terminalModeActive)
        return;

    termios raw = savedTerminal;
    raw.c_lflag &= static_cast<tcflag_t>(~ICANON);
    raw.c_iflag &= static_cast<tcflag_t>(~(IXON | ICRNL));
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0)
        terminalModeActive = true;
}

// Async-signal-safe: an atomic request, then sigaction and raise. The first
// signal only records the request, which the frame loop turns into quit. A
// second one, of either kind, restores the default action and re-raises; the
// signal is blocked while this handler runs, so it lands on return and the
// process dies of it.
void OnQuitSignal(int signalNumber)
{
    const int savedErrno = errno;
    if (Sys_RequestQuit(signalNumber == SIGINT ? "SIGINT" : "SIGTERM") > 1)
    {
        struct sigaction defaultAction{};
        defaultAction.sa_handler = SIG_DFL;
        sigemptyset(&defaultAction.sa_mask);
        (void)sigaction(signalNumber, &defaultAction, nullptr);
        (void)raise(signalNumber);
    }
    errno = savedErrno;
}
} // namespace

/*
==================
Conbuf_CleanText

Strips engine color codes and normalizes newlines. Semantics are identical to
the Win32 console buffer cleaner so a message reads the same on both targets.
Returns the number of bytes written to `target`.
==================
*/
uint32_t __cdecl Conbuf_CleanText(const char *source, char *target, int sizeofTarget)
{
    if (!target || sizeofTarget <= 0)
        return 0;

    target[0] = 0;
    if (!source || sizeofTarget < 3)
        return 0;

    const char *start = target;
    const char *last = &target[sizeofTarget - 3];

    while (*source && target <= last)
    {
        if ((source[0] == '\n' && source[1] == '\r')
            || (source[0] == '\r' && source[1] == '\n'))
        {
            target[0] = '\n';
            target[1] = 0;
            ++target;
            source += 2;
        }
        else if (source[0] == '\r' || source[0] == '\n')
        {
            // A terminal wants a bare newline; the Win32 console buffer takes
            // CR-LF, which is why the two cleaners diverge here.
            *target++ = '\n';
            ++source;
        }
        else if (source[0] == '^' && source[1] && source[1] != '^'
            && source[1] >= 48 && source[1] <= 57)
        {
            source += 2;
        }
        else
        {
            *target++ = *source++;
        }
    }

    *target = 0;
    return static_cast<uint32_t>(target - start);
}

/*
==================
Conbuf_AppendText

Appends to the console output. On a headless server there is no console window
to append to, so this is the standard-output write plus the prompt redraw.
==================
*/
void __cdecl Conbuf_AppendText(const char *pMsg)
{
    if (!pMsg)
        return;

    char cleaned[kCleanTextCapacity];
    const size_t messageLength = strnlen(pMsg, kMaxMessageBytes);
    const char *source = messageLength <= 0x3FFF ? pMsg : &pMsg[messageLength - 0x3FFF];
    const uint32_t cleanedLength = Conbuf_CleanText(source, cleaned, static_cast<int>(sizeof(cleaned)));

    (void)Sys_ConsoleWrite(
        SysConsoleOutputStream::StandardOutput, cleaned, cleanedLength);
    (void)Sys_ConsoleFlush(SysConsoleOutputStream::StandardOutput);
}

/*
==================
Conbuf_AppendTextInMainThread

The engine's console output entry point. Kept separate from Conbuf_AppendText
so a worker thread can never write concurrently with the frame's own output.
==================
*/
void __cdecl Conbuf_AppendTextInMainThread(const char *msg)
{
    if (!msg || !Sys_IsMainThread())
        return;

    // A redirected stdout is a log sink: it keeps the exact bytes the engine
    // produced, and there is no terminal to protect from color codes or CR/LF
    // pairs. This matches the Win32 path, which writes to stdout only when
    // the stream is redirected.
    if (Sys_ConsoleIsRedirected(SysConsoleOutputStream::StandardOutput))
    {
        if (Sys_ConsoleWrite(
                SysConsoleOutputStream::StandardOutput,
                msg,
                strnlen(msg, kMaxMessageBytes)) != SysConsoleIoStatus::Complete)
        {
            // The write failed and there is no debugger to fall back on; the
            // message is dropped rather than retried on a dead descriptor.
        }
        return;
    }

    // A live terminal gets only the engine's normalized form (Conbuf_AppendText
    // strips color codes and normalizes CR/LF), so a color code or a CR/LF pair
    // never lands in the user's scrollback and no line is printed twice.
    Conbuf_AppendText(msg);
}

/*
==================
Sys_ConsoleInput

Hands out one completed console line. The line arrives from the portable
Sys_ConsoleTryReadLine boundary, which the frame pump feeds through the
non-canonical terminal set up at startup.
==================
*/
char *Sys_ConsoleInput(void)
{
    static char consoleLine[SYS_CONSOLE_MAX_LINE_LENGTH + 1];

    const SysConsoleReadResult read = Sys_ConsoleTryReadLine(
        consoleLine, sizeof(consoleLine));
    if (read.status != SysConsoleReadStatus::LineReady)
        return nullptr;
    return consoleLine;
}

void Sys_DestroyConsole()
{
    RestoreTerminalMode();
}

void __cdecl Sys_ShowConsole()
{
    // A headless server has no console window to reveal.
}

void Sys_ConsoleInitTerminal()
{
    if (isatty(STDIN_FILENO) != 1)
        return;

    if (tcgetattr(STDIN_FILENO, &savedTerminal) == 0)
    {
        terminalStateSaved = true;
        EnterTerminalMode();
    }
}

void Sys_ConsoleShutdownTerminal()
{
    RestoreTerminalMode();
}

// Installs OnQuitSignal for SIGINT and SIGTERM. SA_RESTART stays off, so a
// blocking read or sleep wakes with EINTR. A signal the launcher ignores (a
// non-interactive shell's background job ignores SIGINT) stays ignored.
// SIGPIPE is the console backend's (qcommon/sys_console.h) and is untouched.
void Sys_InstallQuitSignalHandlers()
{
    struct sigaction action{};
    action.sa_handler = OnQuitSignal;
    sigemptyset(&action.sa_mask);
    sigaddset(&action.sa_mask, SIGINT);
    sigaddset(&action.sa_mask, SIGTERM);
    const int quitSignals[] = {SIGINT, SIGTERM};
    for (const int signalNumber : quitSignals)
    {
        struct sigaction inherited{};
        if (sigaction(signalNumber, nullptr, &inherited) == 0 && inherited.sa_handler == SIG_IGN)
            continue;
        (void)sigaction(signalNumber, &action, nullptr);
    }
}
