#pragma once

#include "sys_console.h"

#include <cstdint>

enum class SysConsoleRawReadStatus : std::uint8_t
{
    Data = 0,
    NoData,
    EndOfFile,
    IoError,
};

struct SysConsoleRawReadResult
{
    SysConsoleRawReadStatus status = SysConsoleRawReadStatus::IoError;
    unsigned char byte = 0;
};

[[nodiscard]] SysConsoleIoStatus Sys_ConsoleBackendWrite(
    SysConsoleOutputStream stream,
    const char *bytes,
    std::size_t byteCount) noexcept;
[[nodiscard]] SysConsoleIoStatus Sys_ConsoleBackendFlush(
    SysConsoleOutputStream stream) noexcept;
[[nodiscard]] bool Sys_ConsoleBackendIsRedirected(
    SysConsoleOutputStream stream) noexcept;
[[nodiscard]] SysConsoleRawReadResult Sys_ConsoleBackendTryReadByte() noexcept;
