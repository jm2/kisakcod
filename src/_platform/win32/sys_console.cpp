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
        || error == ERROR_HANDLE_EOF;
}

SysConsoleIoStatus OutputErrorStatus(const DWORD error) noexcept
{
    return error == ERROR_INVALID_HANDLE
        ? SysConsoleIoStatus::Unavailable
        : SysConsoleIoStatus::IoError;
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
    if (type == FILE_TYPE_CHAR)
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
        // The established Win32 GUI console owns interactive line editing.
        // This portable boundary consumes redirected standard input only.
        return {SysConsoleRawReadStatus::NoData, 0};
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
        if (IsEndOfPipeError(error))
            return {SysConsoleRawReadStatus::EndOfFile, 0};
        if (error == ERROR_NO_DATA)
            return {SysConsoleRawReadStatus::NoData, 0};
        return {SysConsoleRawReadStatus::IoError, 0};
    }
    return readCount == 1
        ? SysConsoleRawReadResult{SysConsoleRawReadStatus::Data, byte}
        : SysConsoleRawReadResult{SysConsoleRawReadStatus::EndOfFile, 0};
}
