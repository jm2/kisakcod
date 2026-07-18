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

// True means the selected output is a valid non-terminal file or pipe. An
// unavailable stream is not reported as redirected.
[[nodiscard]] bool KISAK_CDECL Sys_ConsoleIsRedirected(
    SysConsoleOutputStream stream) noexcept;

// Nonblocking, single-consumer line input from the inherited standard input.
// POSIX terminals and redirected input are supported.  The Win32 backend
// consumes redirected disk/pipe input; native character-console input remains
// unsupported in the headless profile and is owned by the GUI console in
// windowed profiles.
// Lines are returned without CR/LF and are always NUL-terminated. Empty lines
// are valid. Overlong lines and lines containing embedded NUL bytes are fully
// drained before Truncated/InvalidData is returned, so their suffix cannot be
// interpreted as a later command. Work per call is bounded; NoData can also
// mean that a partial or rejected line still needs draining. All non-LineReady
// results leave output empty when outputCapacity is nonzero.
[[nodiscard]] SysConsoleReadResult KISAK_CDECL Sys_ConsoleTryReadLine(
    char *output,
    std::size_t outputCapacity) noexcept;
