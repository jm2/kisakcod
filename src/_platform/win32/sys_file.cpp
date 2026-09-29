// Win32 backend for the fast-file async-read service. Overlapped I/O is
// preserved: the request is issued with ReadFileEx and its completion routine
// publishes through the shared FileReadState protocol. Wait pumps delivery with
// an alertable SleepEx, and on expiry cancels the request and waits for the
// cancelled completion to be published (the race the loader historically
// handled inline).

#include <qcommon/sys_file.h>

#include <database/db_load_atomic.h>

#include <Windows.h>

#include <new>

static_assert(
    db::load_atomic::kFileReadBytes == 0x40000u,
    "Sys_FileReadBegin documents the fast-file staging slot size");

// overlapped is the first member so the completion routine can recover the
// request without a container-of calculation.
struct SysFile
{
    OVERLAPPED overlapped;
    HANDLE handle;
    db::load_atomic::FileReadState state;
    std::uint32_t requested;
    bool outstanding;
};

namespace
{
SysFileReadResult Sys_FileMakeResult(
    const std::uint32_t requested,
    const std::uint32_t error,
    const std::uint32_t bytes)
{
    if (error == ERROR_HANDLE_EOF)
        return {SysFileReadStatus::Eof, bytes, error};
    if (error != ERROR_SUCCESS)
        return {SysFileReadStatus::Error, bytes, error};
    return {
        bytes == requested ? SysFileReadStatus::Complete : SysFileReadStatus::Eof,
        bytes,
        0};
}

VOID CALLBACK Sys_FileReadCompletion(
    DWORD dwErrorCode,
    DWORD dwNumberOfBytesTransfered,
    LPOVERLAPPED lpOverlapped)
{
    // ReadFileEx delivers exactly the OVERLAPPED that was issued; a null or
    // foreign completion cannot name a slot and is dropped.
    if (!lpOverlapped)
        return;
    SysFile *const file = reinterpret_cast<SysFile *>(lpOverlapped);
    (void)db::load_atomic::PublishFileRead(
        &file->state,
        static_cast<std::uint32_t>(dwErrorCode),
        static_cast<std::uint32_t>(dwNumberOfBytesTransfered),
        static_cast<std::uint32_t>(ERROR_INVALID_DATA));
}
}

SysFileHandle KISAK_CDECL Sys_FileOpenRead(const char *utf8Path)
{
    if (!utf8Path || !*utf8Path)
        return nullptr;

    // Buffered overlapped reads avoid the sector-alignment contract imposed by
    // unbuffered I/O. The fast-file ring is only naturally word-aligned;
    // sequential-scan caching is the safe equivalent until the file adapter
    // owns an explicitly aligned allocation.
    HANDLE const handle = CreateFileA(
        utf8Path,
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return nullptr;

    SysFile *const file = new (std::nothrow) SysFile{};
    if (!file)
    {
        CloseHandle(handle);
        return nullptr;
    }
    file->handle = handle;
    return file;
}

void KISAK_CDECL Sys_FileClose(SysFileHandle *file)
{
    if (!file || !*file)
        return;
    SysFile *const handle = *file;
    if (handle->outstanding)
    {
        // The contract requires the caller to drain first; cancel and drain
        // anyway so no completion routine touches a freed slot.
        (void)Sys_FileReadWait(handle, 1000u);
    }
    CloseHandle(handle->handle);
    delete handle;
    *file = nullptr;
}

bool KISAK_CDECL Sys_FileGetSize(SysFileHandle file, std::uint64_t *outSize)
{
    if (!file || !outSize)
        return false;
    LARGE_INTEGER nativeFileSize{};
    if (!GetFileSizeEx(file->handle, &nativeFileSize)
        || nativeFileSize.QuadPart < 0)
    {
        return false;
    }
    *outSize = static_cast<std::uint64_t>(nativeFileSize.QuadPart);
    return true;
}

SysFileReadResult KISAK_CDECL Sys_FileReadBegin(
    SysFileHandle file,
    std::uint64_t offset,
    void *buffer,
    std::uint32_t bytes)
{
    if (!file || !buffer || bytes == 0 || bytes > db::load_atomic::kFileReadBytes)
        return {SysFileReadStatus::Invalid, 0, ERROR_INVALID_PARAMETER};
    if (file->outstanding)
        return {SysFileReadStatus::Invalid, 0, ERROR_BUSY};

    file->requested = bytes;
    file->overlapped = OVERLAPPED{};
    file->overlapped.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFull);
    file->overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
    db::load_atomic::ResetFileRead(&file->state);

    if (!ReadFileEx(
            file->handle,
            buffer,
            bytes,
            &file->overlapped,
            Sys_FileReadCompletion))
    {
        return Sys_FileMakeResult(bytes, GetLastError(), 0);
    }
    file->outstanding = true;
    return {SysFileReadStatus::Pending, 0, 0};
}

SysFileReadResult KISAK_CDECL Sys_FileReadWait(SysFileHandle file, std::uint32_t timeoutMs)
{
    if (!file || !file->outstanding)
        return {SysFileReadStatus::Invalid, 0, ERROR_INVALID_HANDLE};

    bool cancelRequested = false;
    while (!db::load_atomic::FileReadComplete(&file->state))
    {
        const DWORD waitResult = SleepEx(timeoutMs, TRUE);
        if (db::load_atomic::FileReadComplete(&file->state))
            break;

        if (waitResult == 0)
        {
            if (cancelRequested)
                return {SysFileReadStatus::TimedOut, 0, ERROR_TIMEOUT};
            if (!CancelIo(file->handle))
            {
                const DWORD cancelError = GetLastError();
                if (cancelError == ERROR_NOT_FOUND)
                {
                    // The request can finish after the completion check but
                    // before cancellation. Enter another alertable wait so its
                    // already-queued completion APC can publish the slot.
                    cancelRequested = true;
                    continue;
                }
                return {SysFileReadStatus::CancelFailed, 0, cancelError};
            }
            cancelRequested = true;
        }
        else if (waitResult == WAIT_FAILED)
        {
            return {SysFileReadStatus::WaitFailed, 0, GetLastError()};
        }
    }

    file->outstanding = false;
    const db::load_atomic::FileReadSnapshot snapshot =
        db::load_atomic::SnapshotFileRead(&file->state);
    return Sys_FileMakeResult(file->requested, snapshot.error, snapshot.bytes);
}
