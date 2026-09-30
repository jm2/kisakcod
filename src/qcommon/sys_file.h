#pragma once

// Portable asynchronous file reads for the fast-file loader (docs/design/
// PLATFORM_POSIX.md). Win32 keeps overlapped I/O; POSIX fulfils with pread on
// the calling thread. One staged read at a time per handle; a request covers at
// most one fast-file staging slot (256 KiB). Close requires a drained handle.

#include <cstdint>

#include <universal/platform_compat.h>

struct SysFile;
using SysFileHandle = SysFile *;

enum class SysFileReadStatus : std::uint32_t
{
    Pending = 0,
    Complete = 1,
    Eof = 2,
    Error = 3,
    TimedOut = 4,
    CancelFailed = 5,
    WaitFailed = 6,
    Invalid = 7,
};

struct SysFileReadResult
{
    SysFileReadStatus status;
    std::uint32_t bytes;
    std::uint32_t error;
};

// Opens utf8Path for asynchronous reads. Returns null on failure.
SysFileHandle KISAK_CDECL Sys_FileOpenRead(const char *utf8Path);

// Closes the handle and resets the caller's pointer to null.
void KISAK_CDECL Sys_FileClose(SysFileHandle *file);

// Reports the file length in bytes.
bool KISAK_CDECL Sys_FileGetSize(SysFileHandle file, std::uint64_t *outSize);

// Issues one read of `bytes` at `offset` into `buffer`. Pending on success;
// any other status is a terminal issue-time outcome. `bytes` in [1, 0x40000].
SysFileReadResult KISAK_CDECL Sys_FileReadBegin(
    SysFileHandle file, std::uint64_t offset, void *buffer, std::uint32_t bytes);

// Waits for the outstanding request. `timeoutMs` bounds each wait slice; on
// expiry the request is cancelled and drained (TimedOut / CancelFailed).
SysFileReadResult KISAK_CDECL Sys_FileReadWait(SysFileHandle file, std::uint32_t timeoutMs);
