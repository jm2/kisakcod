#pragma once

#include <cstddef>
#include <cstdint>

#include <universal/platform_compat.h>

enum class SysConsoleOutputStream : std::uint8_t
{
    StandardOutput = 0,
    StandardError = 1,
};

enum class SysConsoleIoStatus : std::uint8_t
{
    Complete = 0,
    InvalidArgument,
    Unavailable,
    IoError,
};

enum class SysConsoleReadStatus : std::uint8_t
{
    NoData = 0,
    LineReady,
    Truncated,
    InvalidData,
    EndOfFile,
    InvalidArgument,
    IoError,
};

struct SysConsoleReadResult
{
    SysConsoleReadStatus status = SysConsoleReadStatus::InvalidArgument;
    std::size_t length = 0;
};

inline constexpr std::size_t SYS_CONSOLE_MAX_LINE_LENGTH = 511;

// Writes exactly byteCount bytes to one inherited process output stream. A
// zero-byte write succeeds without inspecting bytes. No engine locks,
// allocation, reporting, or GUI policy are entered by this boundary.
[[nodiscard]] SysConsoleIoStatus KISAK_CDECL Sys_ConsoleWrite(
    SysConsoleOutputStream stream,
    const char *bytes,
    std::size_t byteCount) noexcept;

// Flushes an inherited output when its backend needs an explicit flush. The
// POSIX backend writes directly to the descriptor and therefore only verifies
// that the selected descriptor still exists.
[[nodiscard]] SysConsoleIoStatus KISAK_CDECL Sys_ConsoleFlush(
    SysConsoleOutputStream stream) noexcept;

// The longest message Sys_ConsoleWriteFatalError writes; Sys_Error formats
// into 4096 bytes.
inline constexpr std::size_t SYS_CONSOLE_MAX_FATAL_MESSAGE = 4095;

// Writes "\nKisakCOD fatal error: <message>\n" to standard error in ONE write,
// so output other threads print at the same moment cannot land inside the
// line. A null message reads "Unknown fatal error"; a longer one is cut to
// SYS_CONSOLE_MAX_FATAL_MESSAGE bytes. Nothing is allocated: the out-of-memory
// path reports through it.
[[nodiscard]] SysConsoleIoStatus KISAK_CDECL Sys_ConsoleWriteFatalError(
    const char *message) noexcept;

// True means the selected output is a valid non-terminal file or pipe. An
// unavailable stream is not reported as redirected.
[[nodiscard]] bool KISAK_CDECL Sys_ConsoleIsRedirected(
    SysConsoleOutputStream stream) noexcept;

// Nonblocking, single-consumer line input from the inherited standard input.
// POSIX terminals and redirected input are supported.  The Win32 backend
// consumes redirected disk/pipe input and the native character-console input
// of the headless profile; the windowed profile owns its edit-control input
// directly and never calls this boundary.
// Lines are returned without CR/LF and are always NUL-terminated. Empty lines
// are valid. Overlong lines and lines containing embedded NUL bytes are fully
// drained before Truncated/InvalidData is returned, so their suffix cannot be
// interpreted as a later command. Work per call is bounded; NoData can also
// mean that a partial or rejected line still needs draining. All non-LineReady
// results leave output empty when outputCapacity is nonzero.
[[nodiscard]] SysConsoleReadResult KISAK_CDECL Sys_ConsoleTryReadLine(
    char *output,
    std::size_t outputCapacity) noexcept;

#if defined(_WIN32) && defined(KISAK_DEDI_HEADLESS)
// Interactive console input for the Win32 headless server. When standard input
// is a console, puts it in the console host's line-editing mode (echo,
// backspace, history) and reads it on a thread that forwards each finished
// line through a pipe published as standard input, so Sys_ConsoleTryReadLine
// returns the lines the user typed and saw. Returns false when standard input
// is not a console (a pipe, a file, NUL, none), when the reader already runs, or
// when it cannot start; standard input is then left as it was.
[[nodiscard]] bool KISAK_CDECL Sys_ConsoleStartLineEditing() noexcept;

// Ctrl+C, Ctrl+Break and closing the console ask the Win32 headless server for
// the orderly quit a typed quit runs (qcommon/sys_quit.h), as SIGINT and
// SIGTERM do on POSIX. The first Ctrl+C or Ctrl+Break only records the request,
// which the frame loop turns into "quit"; a second one ends the process as the
// default handler would. On a console close (or logoff, shutdown) the handler
// holds the system's grace period, about 5 s, for the quit to finish. Returns
// false when the handler cannot be installed.
[[nodiscard]] bool KISAK_CDECL Sys_ConsoleInstallQuitHandler() noexcept;
#endif
