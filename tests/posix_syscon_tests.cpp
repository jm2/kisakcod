// posix_syscon_tests.cpp: runtime contracts for the POSIX termios console
// (_platform/posix/posix_syscon.cpp), the stdout half of the headless
// dedicated server's console (docs/design/PLATFORM_POSIX.md, NOW row 13).
//
// These execute the real console code: the text cleaner every terminal line
// passes through, and the engine output entry point driven through a pty so
// the live-terminal path runs exactly as it does under a user's shell.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <qcommon/sys_console.h>

// Defined by the termios console translation unit.
uint32_t __cdecl Conbuf_CleanText(const char *source, char *target, int sizeofTarget);
void __cdecl Conbuf_AppendTextInMainThread(const char *msg);

// Engine boundary: the console writes only from the main thread, and a unit
// test runs everything on its main thread.
bool Sys_IsMainThread()
{
    return true;
}

namespace
{
int g_checks = 0;
int g_failures = 0;
const char *g_stage = "startup";

bool Check(const bool condition, const char *const stage)
{
    ++g_checks;
    if (!condition)
    {
        ++g_failures;
        g_stage = stage;
        std::fprintf(stderr, "FAIL %s\n", stage);
        return false;
    }
    return true;
}

void CheckConsoleCleanText()
{
    char target[64];

    // Color codes are stripped; a literal caret survives.
    std::memset(target, 0x5a, sizeof(target));
    Check(Conbuf_CleanText("^1red^2green", target, sizeof(target)) == 8, "clean strips colour codes length");
    Check(std::strcmp(target, "redgreen") == 0, "clean strips colour codes text");

    Check(Conbuf_CleanText("a^^b", target, sizeof(target)) == 4, "clean keeps escaped caret length");
    Check(std::strcmp(target, "a^^b") == 0, "clean keeps escaped caret text");

    // CR/LF pairs and single newlines normalize to a bare newline on a
    // terminal, so the user's scrollback never sees a stray CR.
    Check(Conbuf_CleanText("a\r\nb", target, sizeof(target)) == 3, "clean crlf length");
    Check(std::strcmp(target, "a\nb") == 0, "clean crlf text");
    Check(Conbuf_CleanText("a\nb", target, sizeof(target)) == 3, "clean lf length");
    Check(Conbuf_CleanText("a\rb", target, sizeof(target)) == 3, "clean cr length");

    // Degenerate callers fail closed without writing past the buffer.
    Check(Conbuf_CleanText("abc", nullptr, 16) == 0, "clean null target");
    Check(Conbuf_CleanText(nullptr, target, 16) == 0, "clean null source");
    Check(Conbuf_CleanText("abc", target, 0) == 0, "clean zero capacity");

    // An overlong source is truncated to the window, never overflowed.
    char small[8];
    std::memset(small, 0x5a, sizeof(small));
    // The cleaner reserves the last two bytes of the window for a CRLF pair,
    // so a capacity of 8 accepts at most 6 payload bytes.
    const uint32_t written = Conbuf_CleanText("0123456789abcdef", small, 8);
    Check(written == 6, "clean truncates to window");
    Check(small[written] == 0, "clean NUL-terminates the window");
}

// Drives Conbuf_AppendTextInMainThread with stdout on a pty, so
// Sys_ConsoleIsRedirected reports a live terminal. The engine entry point must
// write the cleaned message exactly once: emitting the raw form as well prints
// every line twice on a terminal (raw bytes followed by cleaned bytes).
void CheckMainThreadOutputSingleWrite()
{
    const int master = posix_openpt(O_RDWR | O_NOCTTY);
    Check(master >= 0, "pty master opens");
    if (master < 0)
        return;
    Check(grantpt(master) == 0, "pty grant");
    Check(unlockpt(master) == 0, "pty unlock");
    const char *const slaveName = ptsname(master);
    Check(slaveName != nullptr, "pty slave name");
    if (!slaveName)
    {
        close(master);
        return;
    }

    const int slave = open(slaveName, O_RDWR | O_NOCTTY);
    Check(slave >= 0, "pty slave opens");
    if (slave < 0)
    {
        close(master);
        return;
    }

    // Raw mode keeps the tty from rewriting the engine's newlines (ONLCR) on
    // the way to the master, so the capture is byte-exact.
    termios raw{};
    Check(tcgetattr(slave, &raw) == 0, "pty termios read");
    cfmakeraw(&raw);
    Check(tcsetattr(slave, TCSANOW, &raw) == 0, "pty termios raw");

    const int savedStdout = dup(STDOUT_FILENO);
    Check(savedStdout >= 0, "stdout saved");
    Check(dup2(slave, STDOUT_FILENO) >= 0, "stdout moved to pty");

    // ^1 is a color code and must be stripped; \r\n must normalize to \n.
    Conbuf_AppendTextInMainThread("X^1Y\r\n");

    Check(dup2(savedStdout, STDOUT_FILENO) >= 0, "stdout restored");
    close(savedStdout);

    char captured[64];
    std::size_t capturedLength = 0;
    while (capturedLength < sizeof(captured))
    {
        pollfd masterPoll{};
        masterPoll.fd = master;
        masterPoll.events = POLLIN;
        const int ready = poll(&masterPoll, 1, capturedLength == 0 ? 2000 : 100);
        if (ready <= 0 || (masterPoll.revents & POLLIN) == 0)
            break;
        const ssize_t chunk = read(
            master, captured + capturedLength, sizeof(captured) - capturedLength);
        if (chunk <= 0)
            break;
        capturedLength += static_cast<std::size_t>(chunk);
    }

    Check(capturedLength == 3, "live terminal single-write length");
    Check(capturedLength == 3 && std::memcmp(captured, "XY\n", 3) == 0,
        "live terminal single-write content");

    close(slave);
    close(master);
}
} // namespace

int main()
{
    CheckConsoleCleanText();
    CheckMainThreadOutputSingleWrite();

    if (g_failures == 0)
        std::printf("posix_syscon: %d checks passed\n", g_checks);
    else
        std::fprintf(stderr, "posix_syscon: %d of %d checks failed at %s\n",
            g_failures, g_checks, g_stage);
    return g_failures == 0 ? 0 : 1;
}
