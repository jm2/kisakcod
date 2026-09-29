#pragma once

// Portable asynchronous file reads for the fast-file loader. The engine issues
// one staged read at a time against a ring buffer; this service owns the
// platform handle and the completion path so engine code never sees HANDLE or
// completion routines (docs/design/PLATFORM_POSIX.md).
//
// Win32 keeps overlapped I/O (ReadFileEx plus an alertable wait); POSIX fulfils
// the request with pread on the calling database thread. Both backends publish
// through db::load_atomic::FileReadState, which is the shared protocol between
// the fast-file platform adapter and its waiter.
//
// A request covers at most one fast-file staging slot (256 KiB,
// db::load_atomic::kFileReadBytes). At most one request is outstanding per
// handle; close requires the caller to have drained it.

#include <cstdint>

#include <universal/platform_compat.h>

struct SysFile;
using SysFileHandle = SysFile *;

enum class SysFileReadStatus : std::uint32_t
{
    Pending = 0,     // Begin only: the request was issued and must be waited
    Complete = 1,    // the request transferred its full byte count
    Eof = 2,         // the file ended during the request (short or empty read)
    Error = 3,       // the platform reported an I/O failure (see error)
    TimedOut = 4,    // the wait budget expired while cancellation was pending
    CancelFailed = 5, // the outstanding request could not be cancelled
    WaitFailed = 6,  // the platform wait itself failed
    Invalid = 7,     // bad handle, buffer, size, or no request outstanding
};

struct SysFileReadResult
{
    SysFileReadStatus status;
    std::uint32_t bytes; // bytes transferred into the caller's buffer
    std::uint32_t error; // platform error word for diagnostics; 0 when none
};

// Opens utf8Path for asynchronous reads. Returns null when the file cannot be
// opened. The handle owns the native handle and the outstanding-request state.
SysFileHandle KISAK_CDECL Sys_FileOpenRead(const char *utf8Path);

// Closes the handle and releases the native handle. No request may be
// outstanding; a non-null pointer is required and the caller's handle is reset
// to null.
void KISAK_CDECL Sys_FileClose(SysFileHandle *file);

// Reports the file length in bytes. Returns false when it cannot be measured.
bool KISAK_CDECL Sys_FileGetSize(SysFileHandle file, std::uint64_t *outSize);

// Issues one asynchronous read of `bytes` at `offset` into `buffer`. The
// request is pending when the result status is Pending; anything else is a
// terminal outcome for a request that never started. `bytes` must be in
// [1, 0x40000].
SysFileReadResult KISAK_CDECL Sys_FileReadBegin(
    SysFileHandle file,
    std::uint64_t offset,
    void *buffer,
    std::uint32_t bytes);

// Waits for the outstanding request and reports its outcome. `timeoutMs` bounds
// each wait slice; on expiry the service cancels the request and waits for the
// cancellation to be published, reporting TimedOut when that drain also expires.
SysFileReadResult KISAK_CDECL Sys_FileReadWait(SysFileHandle file, std::uint32_t timeoutMs);
