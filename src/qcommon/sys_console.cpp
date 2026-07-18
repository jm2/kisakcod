#include "sys_console.h"

#include "sys_console_internal.h"

#include <array>
#include <cstring>

namespace
{
constexpr std::size_t CONSOLE_INPUT_READ_BUDGET = 4096;

struct ConsoleInputState
{
    std::array<char, SYS_CONSOLE_MAX_LINE_LENGTH> line{};
    std::size_t length = 0;
    bool truncated = false;
    bool invalid = false;
    bool skipLineFeed = false;
    bool endOfFile = false;
};

ConsoleInputState inputState;

bool IsValidOutputStream(const SysConsoleOutputStream stream) noexcept
{
    return stream == SysConsoleOutputStream::StandardOutput
        || stream == SysConsoleOutputStream::StandardError;
}

void ResetOutput(char *const output, const std::size_t outputCapacity) noexcept
{
    if (output && outputCapacity != 0)
        output[0] = '\0';
}

SysConsoleReadResult FinishLine(
    char *const output,
    const std::size_t outputCapacity,
    const bool skipLineFeed) noexcept
{
    SysConsoleReadResult result{};
    if (inputState.invalid)
    {
        result.status = SysConsoleReadStatus::InvalidData;
    }
    else if (inputState.truncated
        || inputState.length >= outputCapacity)
    {
        result.status = SysConsoleReadStatus::Truncated;
    }
    else
    {
        if (inputState.length != 0)
        {
            std::memcpy(
                output,
                inputState.line.data(),
                inputState.length);
        }
        output[inputState.length] = '\0';
        result.status = SysConsoleReadStatus::LineReady;
        result.length = inputState.length;
    }

    inputState.length = 0;
    inputState.truncated = false;
    inputState.invalid = false;
    inputState.skipLineFeed = skipLineFeed;
    return result;
}

void ConsumeLineByte(const unsigned char byte) noexcept
{
    if (byte == 0)
    {
        inputState.invalid = true;
        return;
    }
    if (inputState.invalid || inputState.truncated)
        return;
    if (inputState.length == inputState.line.size())
    {
        inputState.truncated = true;
        return;
    }
    inputState.line[inputState.length++] = static_cast<char>(byte);
}
} // namespace

SysConsoleIoStatus KISAK_CDECL Sys_ConsoleWrite(
    const SysConsoleOutputStream stream,
    const char *const bytes,
    const std::size_t byteCount) noexcept
{
    if (!IsValidOutputStream(stream)
        || (byteCount != 0 && !bytes))
    {
        return SysConsoleIoStatus::InvalidArgument;
    }
    if (byteCount == 0)
        return SysConsoleIoStatus::Complete;
    return Sys_ConsoleBackendWrite(stream, bytes, byteCount);
}

SysConsoleIoStatus KISAK_CDECL Sys_ConsoleFlush(
    const SysConsoleOutputStream stream) noexcept
{
    if (!IsValidOutputStream(stream))
        return SysConsoleIoStatus::InvalidArgument;
    return Sys_ConsoleBackendFlush(stream);
}

bool KISAK_CDECL Sys_ConsoleIsRedirected(
    const SysConsoleOutputStream stream) noexcept
{
    return IsValidOutputStream(stream)
        && Sys_ConsoleBackendIsRedirected(stream);
}

SysConsoleReadResult KISAK_CDECL Sys_ConsoleTryReadLine(
    char *const output,
    const std::size_t outputCapacity) noexcept
{
    ResetOutput(output, outputCapacity);
    if (!output || outputCapacity == 0)
    {
        return {SysConsoleReadStatus::InvalidArgument, 0};
    }

    std::size_t bytesConsumed = 0;
    for (;;)
    {
        if (inputState.endOfFile)
        {
            if (inputState.length != 0
                || inputState.truncated
                || inputState.invalid)
            {
                return FinishLine(output, outputCapacity, false);
            }
            inputState.skipLineFeed = false;
            return {SysConsoleReadStatus::EndOfFile, 0};
        }

        if (bytesConsumed == CONSOLE_INPUT_READ_BUDGET)
            return {SysConsoleReadStatus::NoData, 0};

        const SysConsoleRawReadResult read =
            Sys_ConsoleBackendTryReadByte();
        if (read.status == SysConsoleRawReadStatus::NoData)
            return {SysConsoleReadStatus::NoData, 0};
        if (read.status == SysConsoleRawReadStatus::IoError)
            return {SysConsoleReadStatus::IoError, 0};
        if (read.status == SysConsoleRawReadStatus::EndOfFile)
        {
            inputState.endOfFile = true;
            continue;
        }
        ++bytesConsumed;

        if (inputState.skipLineFeed)
        {
            inputState.skipLineFeed = false;
            if (read.byte == static_cast<unsigned char>('\n'))
                continue;
        }
        if (read.byte == static_cast<unsigned char>('\r'))
            return FinishLine(output, outputCapacity, true);
        if (read.byte == static_cast<unsigned char>('\n'))
            return FinishLine(output, outputCapacity, false);
        ConsumeLineByte(read.byte);
    }
}
