// posix_quit_signal_tests.cpp: the headless server's orderly quit on SIGINT
// and SIGTERM (qcommon/sys_quit.h, _platform/posix/posix_syscon.cpp). The
// real handlers are installed and the signals raised in-process. Where the
// engine compiles (Linux clang, POSIX_QUIT_ENGINE_CBUF), the frame loop's step
// also feeds the real command buffer (qcommon/cmd.cpp), whose Cbuf_Execute
// then runs the "quit" command.
//
// usage: kisakcod-posix-quit-signal-tests sigterm|sigint|second-signal|ignored-sigint

#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <strings.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <qcommon/sys_quit.h>

#if defined(POSIX_QUIT_ENGINE_CBUF)
#include <string>
#include <string_view>

#include <qcommon/cmd.h>
#include <qcommon/qcommon.h>
#include <script/scr_debugger.h>
#include <server/sv_game.h>
#include <universal/assertive.h>

extern CmdText cmd_textArray[1]; // cmd.cpp's command buffer
void SV_WaitServer();
#endif

void Sys_InstallQuitSignalHandlers(); // posix_syscon.cpp

// posix_syscon.cpp's console output writes only from the main thread.
bool Sys_IsMainThread()
{
    return true;
}

namespace
{
int g_failures = 0;

void Check(const bool ok, const char *const what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

using Handler = void (*)(int);

Handler Disposition(const int signalNumber, int *flags = nullptr)
{
    struct sigaction current{};
    (void)sigaction(signalNumber, nullptr, &current);
    if (flags)
        *flags = current.sa_flags;
    return current.sa_handler;
}

// A handler of ours, without SA_RESTART so a blocking read or sleep wakes.
bool Handled(const int signalNumber)
{
    int flags = 0;
    const Handler handler = Disposition(signalNumber, &flags);
    return handler != SIG_DFL && handler != SIG_IGN && (flags & SA_RESTART) == 0;
}

#if defined(POSIX_QUIT_ENGINE_CBUF)
int g_quitRuns = 0;
const char *g_printedSource = "";
char g_vaText[1];
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
#endif

void RunSignal(const int signalNumber, const char *const name)
{
#if defined(POSIX_QUIT_ENGINE_CBUF)
    Cbuf_Init();
    Cmd_AddCommandInternal("quit", Quit_f, &g_quitCommand);
    Check(!Cbuf_AddRequestedQuit() && Queued().empty(), "no request, nothing queued");
#endif
    const Handler pipeBefore = Disposition(SIGPIPE);
    Sys_InstallQuitSignalHandlers();
    Check(Handled(SIGINT) && Handled(SIGTERM), "SIGINT and SIGTERM handled, without SA_RESTART");
    Check(Disposition(SIGPIPE) == pipeBefore, "SIGPIPE left as it was");
    Check(!Sys_QuitRequested(), "no request before a signal");

    // Before the frame loop's first step, as during Com_Init. Without the
    // handler the signal ends this process here.
    Check(raise(signalNumber) == 0, "raise");
    Check(Sys_QuitRequested(), "the signal requests a quit");
    const char *const source = Sys_QuitRequestSource();
    Check(source && std::strcmp(source, name) == 0, "the request names the signal");

#if defined(POSIX_QUIT_ENGINE_CBUF)
    // A buffer without room for the quit leaves the request pending; the frame
    // after Cbuf_Execute drains it queues the quit.
    const std::string filler(static_cast<size_t>(cmd_textArray[0].maxsize - 3), 'x');
    Cbuf_AddText(0, filler.c_str());
    Check(!Cbuf_AddRequestedQuit() && Queued() == filler, "a full buffer leaves the request pending");
    Check(*g_printedSource == '\0', "nothing is logged until the quit is queued");
    Cbuf_Execute(0, 0);
    Check(g_quitRuns == 0 && Queued().empty(), "Cbuf_Execute drains the buffer");

    // The first frame with room queues exactly what a typed quit queues, once.
    Check(Cbuf_AddRequestedQuit(), "the first frame queues the quit");
    Check(Queued() == "quit\n", "the queued text is a typed quit");
    Check(std::strcmp(g_printedSource, name) == 0, "the log names the signal");
    Check(!Cbuf_AddRequestedQuit() && Queued() == "quit\n", "the quit is queued once");
    Cbuf_Execute(0, 0);
    Check(g_quitRuns == 1 && Queued().empty(), "Cbuf_Execute runs the quit command");
#endif
}

// A second signal, of either kind, kills: the child survives the first
// (it records that in a shared page) and dies of the second.
void RunSecondSignal()
{
    void *const page = mmap(nullptr, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED)
    {
        Check(false, "mmap");
        return;
    }
    volatile int *const survived = static_cast<volatile int *>(page);
    const int pairs[][2] = {{SIGTERM, SIGTERM}, {SIGINT, SIGINT}, {SIGTERM, SIGINT}};
    for (const auto &pair : pairs)
    {
        *survived = 0;
        const pid_t child = fork();
        if (child == 0)
        {
            Sys_InstallQuitSignalHandlers();
            if (raise(pair[0]) != 0 || !Sys_QuitRequested())
                _exit(1);
            *survived = 1;
            _exit(raise(pair[1]) == 0 ? 0 : 1);
        }
        int status = 0;
        Check(child > 0 && waitpid(child, &status, 0) == child, "fork and wait");
        Check(*survived == 1, "the first signal only requests a quit");
        Check(WIFSIGNALED(status) && WTERMSIG(status) == pair[1], "the second signal kills with that signal");
    }
    (void)munmap(page, sizeof(int));
}

// A launcher's SIG_IGN (a non-interactive shell's background job) survives.
void RunIgnoredSigint()
{
    (void)std::signal(SIGINT, SIG_IGN);
    Sys_InstallQuitSignalHandlers();
    Check(Disposition(SIGINT) == SIG_IGN, "an ignored SIGINT stays ignored");
    Check(Handled(SIGTERM), "SIGTERM is still handled");
    Check(raise(SIGINT) == 0 && !Sys_QuitRequested(), "an ignored SIGINT requests nothing");
}
} // namespace

#if defined(POSIX_QUIT_ENGINE_CBUF)
// cmd.cpp's engine boundary, weak as in the other engine-TU tests;
// --gc-sections drops the engine code these checks never reach.
#define WEAK __attribute__((weak))
WEAK const dvar_t *com_sv_running;
// Keeps the leading %s argument, which Cbuf_AddRequestedQuit fills with the source.
WEAK void Com_Printf(int, const char *fmt, ...)
{
    if (std::strncmp(fmt, "%s", 2) != 0)
        return;
    va_list args;
    va_start(args, fmt);
    g_printedSource = va_arg(args, const char *);
    va_end(args);
}
WEAK void MyAssertHandler(const char *filename, int line, int, const char *, ...)
{
    std::fprintf(stderr, "engine assert at %s:%d\n", filename ? filename : "?", line);
    std::exit(3);
}
WEAK int Dvar_Command() { return 0; }
WEAK int SV_GameCommand() { return 0; }
WEAK void SV_WaitServer() {}
WEAK void Scr_MonitorCommand(const char *) {}
WEAK void Sys_EnterCriticalSection(int) {}
WEAK void Sys_LeaveCriticalSection(int) {}
WEAK int I_stricmp(const char *s0, const char *s1) { return strcasecmp(s0, s1); }
WEAK char *va(const char *, ...) { return g_vaText; }
#endif

int main(int argc, char **argv)
{
    const char *const mode = argc > 1 ? argv[1] : "";
    if (std::strcmp(mode, "sigterm") == 0)
        RunSignal(SIGTERM, "SIGTERM");
    else if (std::strcmp(mode, "sigint") == 0)
        RunSignal(SIGINT, "SIGINT");
    else if (std::strcmp(mode, "second-signal") == 0)
        RunSecondSignal();
    else if (std::strcmp(mode, "ignored-sigint") == 0)
        RunIgnoredSigint();
    else
    {
        std::fprintf(stderr, "usage: %s sigterm|sigint|second-signal|ignored-sigint\n", argv[0]);
        return 2;
    }
    std::printf("posix quit %s: %s\n", mode, g_failures ? "FAILED" : "ok");
    return g_failures ? 1 : 0;
}
