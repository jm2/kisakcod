#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <qcommon/sys_console_internal.h>

#include <cstddef>
#include <limits>

namespace
{
DWORD OutputIdentifier(const SysConsoleOutputStream stream) noexcept
{
    return stream == SysConsoleOutputStream::StandardOutput
        ? STD_OUTPUT_HANDLE
        : STD_ERROR_HANDLE;
}

HANDLE StandardHandle(const DWORD identifier) noexcept
{
    const HANDLE handle = GetStdHandle(identifier);
    return handle && handle != INVALID_HANDLE_VALUE ? handle : nullptr;
}

bool IsEndOfPipeError(const DWORD error) noexcept
{
    return error == ERROR_BROKEN_PIPE
        || error == ERROR_HANDLE_EOF
        || error == ERROR_PIPE_NOT_CONNECTED;
}

SysConsoleIoStatus OutputErrorStatus(const DWORD error) noexcept
{
    return error == ERROR_INVALID_HANDLE
        ? SysConsoleIoStatus::Unavailable
        : SysConsoleIoStatus::IoError;
}

// Translate a Win32 console-input failure into the bytewise boundary's
// vocabulary. The line parser only distinguishes NoData, EndOfFile, and
// IoError; InvalidData / Truncated are line-level classifications that
// do not apply to a single byte. A real console reporting
// ERROR_INVALID_HANDLE (parent console detached, redirected by another
// process) or ERROR_ACCESS_DENIED (handle revoked) collapses to IoError
// so the parser surfaces the failure without misinterpreting the void
// as a NUL byte.
bool MapConsoleInputError(
    const DWORD error,
    SysConsoleRawReadResult &result) noexcept
{
    if (IsEndOfPipeError(error))
    {
        result = {SysConsoleRawReadStatus::EndOfFile, 0};
        return true;
    }
    if (error == ERROR_INVALID_HANDLE || error == ERROR_ACCESS_DENIED)
    {
        result = {SysConsoleRawReadStatus::IoError, 0};
        return true;
    }
    return false;
}

// Translate one pending console input event into a raw console byte or a
// "keep draining" decision. The portable boundary exposes only single-byte
// reads, so KEY_UP, function/arrow keys, mouse, focus, window-buffer-size,
// and menu events are drained without producing output. KEY_DOWN records
// with a zero ASCII char (e.g. arrow keys, function keys in cooked mode,
// or non-ASCII code points) are also drained so the next key can be
// evaluated; the portable boundary is byte-oriented and cannot faithfully
// encode codepoints above 0x7F without breaking the line parser's
// bytewise contract. KEY_DOWN records with a non-zero ASCII char produce
// one Data byte. ENABLE_PROCESSED_INPUT translates Ctrl+C into a
// CTRL_C_EVENT record; the record is drained so the line parser cannot
// misinterpret it as data. The console control handler dispatch path runs
// independently of ReadConsoleInput, so the engine's signal policy is
// unchanged.
SysConsoleRawReadResult TranslateConsoleEvent(
    const INPUT_RECORD &event) noexcept
{
    if (event.EventType != KEY_EVENT)
        return {SysConsoleRawReadStatus::NoData, 0};
    const KEY_EVENT_RECORD &key = event.Event.KeyEvent;
    if (!key.bKeyDown)
        return {SysConsoleRawReadStatus::NoData, 0};
    if (key.uChar.AsciiChar == 0)
        return {SysConsoleRawReadStatus::NoData, 0};
    return {SysConsoleRawReadStatus::Data,
        static_cast<unsigned char>(key.uChar.AsciiChar)};
}

// Drain pending console input events until one yields a Data byte, the
// input queue is empty, or a fatal error is reported. The function is
// single-consumer by contract (the line parser drives it from one
// thread); pending repeat bytes are tracked in file-scope state so
// KEY_EVENT records with wRepeatCount > 1 produce one byte per call
// across consecutive calls instead of collapsing to a single byte.
//
// Returns the first Data byte seen, EndOfFile when the input handle
// reports a pipe-class EOF, IoError on any other console failure, or
// NoData when the queue is empty.
SysConsoleRawReadResult TryReadConsoleByte(const HANDLE input) noexcept
{
    static unsigned char pendingRepeatByte = 0;
    static WORD pendingRepeatRemaining = 0;

    if (pendingRepeatRemaining != 0)
    {
        const unsigned char byte = pendingRepeatByte;
        --pendingRepeatRemaining;
        return {SysConsoleRawReadStatus::Data, byte};
    }

    for (;;)
    {
        DWORD pending = 0;
        if (!GetNumberOfConsoleInputEvents(input, &pending))
        {
            SysConsoleRawReadResult mapped{};
            const DWORD error = GetLastError();
            if (MapConsoleInputError(error, mapped))
                return mapped;
            return {SysConsoleRawReadStatus::IoError, 0};
        }
        if (pending == 0)
            return {SysConsoleRawReadStatus::NoData, 0};

        INPUT_RECORD event{};
        DWORD readCount = 0;
        if (!ReadConsoleInput(input, &event, 1, &readCount)
            || readCount == 0)
        {
            SysConsoleRawReadResult mapped{};
            const DWORD error = GetLastError();
            if (MapConsoleInputError(error, mapped))
                return mapped;
            return {SysConsoleRawReadStatus::IoError, 0};
        }

        const SysConsoleRawReadResult translated =
            TranslateConsoleEvent(event);
        if (translated.status != SysConsoleRawReadStatus::Data)
            continue;
        if (event.Event.KeyEvent.wRepeatCount > 1)
        {
            pendingRepeatByte = translated.byte;
            pendingRepeatRemaining =
                static_cast<WORD>(event.Event.KeyEvent.wRepeatCount - 1);
        }
        return translated;
    }
}
} // namespace

SysConsoleIoStatus Sys_ConsoleBackendWrite(
    const SysConsoleOutputStream stream,
    const char *bytes,
    std::size_t byteCount) noexcept
{
    const HANDLE output = StandardHandle(OutputIdentifier(stream));
    if (!output)
        return SysConsoleIoStatus::Unavailable;

    while (byteCount != 0)
    {
        const DWORD request = byteCount
                > static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())
            ? (std::numeric_limits<DWORD>::max)()
            : static_cast<DWORD>(byteCount);
        DWORD written = 0;
        if (!WriteFile(output, bytes, request, &written, nullptr))
            return OutputErrorStatus(GetLastError());
        if (written == 0)
            return SysConsoleIoStatus::IoError;
        bytes += written;
        byteCount -= written;
    }
    return SysConsoleIoStatus::Complete;
}

SysConsoleIoStatus Sys_ConsoleBackendFlush(
    const SysConsoleOutputStream stream) noexcept
{
    const HANDLE output = StandardHandle(OutputIdentifier(stream));
    if (!output)
        return SysConsoleIoStatus::Unavailable;
    SetLastError(ERROR_SUCCESS);
    const DWORD type = GetFileType(output);
    if (type == FILE_TYPE_CHAR || type == FILE_TYPE_PIPE)
        return SysConsoleIoStatus::Complete;
    if (type == FILE_TYPE_UNKNOWN)
    {
        const DWORD error = GetLastError();
        if (error != ERROR_SUCCESS)
            return OutputErrorStatus(error);
    }
    if (!FlushFileBuffers(output))
        return OutputErrorStatus(GetLastError());
    return SysConsoleIoStatus::Complete;
}

bool Sys_ConsoleBackendIsRedirected(
    const SysConsoleOutputStream stream) noexcept
{
    const HANDLE output = StandardHandle(OutputIdentifier(stream));
    if (!output)
        return false;
    const DWORD type = GetFileType(output);
    return type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE;
}

SysConsoleRawReadResult Sys_ConsoleBackendTryReadByte() noexcept
{
    const HANDLE input = StandardHandle(STD_INPUT_HANDLE);
    if (!input)
        return {SysConsoleRawReadStatus::IoError, 0};

    const DWORD type = GetFileType(input);
    if (type == FILE_TYPE_CHAR)
    {
        // Native character-console input. The windowed GUI console owns
        // interactive line editing through its edit control and never
        // reaches this path; Sys_ConsoleInput returns its edit-control
        // buffer directly under !KISAK_DEDI_HEADLESS. Headless profile
        // owns this stdin handle and drains console events one byte at
        // a time without disturbing the windowed edit-control owner.
        return TryReadConsoleByte(input);
    }
    if (type == FILE_TYPE_PIPE)
    {
        DWORD available = 0;
        if (!PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr))
        {
            return IsEndOfPipeError(GetLastError())
                ? SysConsoleRawReadResult{
                    SysConsoleRawReadStatus::EndOfFile, 0}
                : SysConsoleRawReadResult{
                    SysConsoleRawReadStatus::IoError, 0};
        }
        if (available == 0)
            return {SysConsoleRawReadStatus::NoData, 0};
    }
    else if (type != FILE_TYPE_DISK)
    {
        return {SysConsoleRawReadStatus::IoError, 0};
    }

    unsigned char byte = 0;
    DWORD readCount = 0;
    if (!ReadFile(input, &byte, 1, &readCount, nullptr))
    {
        const DWORD error = GetLastError();
        // Message-mode pipes report ERROR_MORE_DATA even though the requested
        // byte was consumed successfully.  Preserve that byte so the common
        // line parser can drain the rest of the message on later reads.
        if (error == ERROR_MORE_DATA && readCount == 1)
            return {SysConsoleRawReadStatus::Data, byte};
        if (IsEndOfPipeError(error))
            return {SysConsoleRawReadStatus::EndOfFile, 0};
        if (error == ERROR_NO_DATA)
            return {SysConsoleRawReadStatus::NoData, 0};
        return {SysConsoleRawReadStatus::IoError, 0};
    }
    if (readCount == 1)
        return {SysConsoleRawReadStatus::Data, byte};
    return type == FILE_TYPE_PIPE
        ? SysConsoleRawReadResult{SysConsoleRawReadStatus::NoData, 0}
        : SysConsoleRawReadResult{SysConsoleRawReadStatus::EndOfFile, 0};
}