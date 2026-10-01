// console_fatal_error_tests.cpp: the fatal-error line both terminate paths
// write (Sys_ConsoleWriteFatalError, qcommon/sys_console.cpp; called by
// posix_main.cpp and win_main.cpp). The real console boundary runs over a
// backend that records each write: one fatal message must be exactly one
// write holding prefix, message and newline, so another thread's output
// cannot land between them.

#include <qcommon/sys_console_internal.h>

#include <cstdio>
#include <string>
#include <vector>

namespace
{
struct RecordedWrite
{
    SysConsoleOutputStream stream;
    std::string bytes;
};

std::vector<RecordedWrite> g_writes;
SysConsoleIoStatus g_writeStatus = SysConsoleIoStatus::Complete;
int g_failures = 0;

void Check(const bool ok, const char *const what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

const std::string kPrefix = "\nKisakCOD fatal error: ";

// One fatal write of message must record exactly one stderr write of `line`.
void CheckSingleWrite(const char *const message, const std::string &line, const char *const what)
{
    g_writes.clear();
    Check(Sys_ConsoleWriteFatalError(message) == SysConsoleIoStatus::Complete, what);
    Check(g_writes.size() == 1, what);
    if (g_writes.size() == 1)
    {
        Check(g_writes[0].stream == SysConsoleOutputStream::StandardError, what);
        Check(g_writes[0].bytes == line, what);
    }
}
} // namespace

// The console backend, recording instead of writing.
SysConsoleIoStatus Sys_ConsoleBackendWrite(
    const SysConsoleOutputStream stream,
    const char *const bytes,
    const std::size_t byteCount) noexcept
{
    g_writes.push_back({stream, std::string(bytes, byteCount)});
    return g_writeStatus;
}

SysConsoleIoStatus Sys_ConsoleBackendFlush(SysConsoleOutputStream) noexcept
{
    return SysConsoleIoStatus::Complete;
}

bool Sys_ConsoleBackendIsRedirected(SysConsoleOutputStream) noexcept
{
    return false;
}

SysConsoleRawReadResult Sys_ConsoleBackendTryReadByte() noexcept
{
    return {SysConsoleRawReadStatus::NoData, 0};
}

int main()
{
    g_writes.reserve(4);

    CheckSingleWrite("ERROR: Could not find zone 'code_post_gfx_mp.ff'",
        kPrefix + "ERROR: Could not find zone 'code_post_gfx_mp.ff'\n", "message");
    CheckSingleWrite(nullptr, kPrefix + "Unknown fatal error\n", "null message");
    CheckSingleWrite("", kPrefix + "\n", "empty message");

    // A message at the limit is written whole; a longer one is cut to the
    // limit, still in one write.
    const std::string atLimit(SYS_CONSOLE_MAX_FATAL_MESSAGE, 'a');
    CheckSingleWrite(atLimit.c_str(), kPrefix + atLimit + "\n", "message at the limit");
    const std::string overLimit(SYS_CONSOLE_MAX_FATAL_MESSAGE + 4000, 'b');
    CheckSingleWrite(overLimit.c_str(),
        kPrefix + std::string(SYS_CONSOLE_MAX_FATAL_MESSAGE, 'b') + "\n", "message over the limit");

    // The backend's status reaches the caller.
    g_writes.clear();
    g_writeStatus = SysConsoleIoStatus::IoError;
    Check(Sys_ConsoleWriteFatalError("x") == SysConsoleIoStatus::IoError, "backend failure");
    Check(g_writes.size() == 1, "backend failure: one write");

    if (g_failures == 0)
        std::puts("console fatal-error contracts passed");
    return g_failures == 0 ? 0 : 1;
}
