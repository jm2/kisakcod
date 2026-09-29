// POSIX backend for the fast-file async-read service. The request is fulfilled
// with pread on the calling database thread (docs/design/PLATFORM_POSIX.md):
// Begin performs the transfer and records the outcome, Wait returns it. A
// helper thread is only justified if profiling ever asks.

#include <qcommon/sys_file.h>

#include <database/db_load_atomic.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <new>

struct SysFile
{
    int fd;
    SysFileReadResult result;
    std::uint32_t requested;
    bool outstanding;
};

namespace
{
SysFileReadResult Sys_FileMakeResult(
    const std::uint32_t requested,
    const int errorWord,
    const std::uint32_t bytes)
{
    if (errorWord != 0)
        return {SysFileReadStatus::Error, bytes, static_cast<std::uint32_t>(errorWord)};
    return {
        bytes == requested ? SysFileReadStatus::Complete : SysFileReadStatus::Eof,
        bytes,
        0};
}
}

SysFileHandle KISAK_CDECL Sys_FileOpenRead(const char *utf8Path)
{
    if (!utf8Path || !*utf8Path)
        return nullptr;

    const int fd = open(utf8Path, O_RDONLY);
    if (fd < 0)
        return nullptr;

    SysFile *const file = new (std::nothrow) SysFile{};
    if (!file)
    {
        close(fd);
        return nullptr;
    }
    file->fd = fd;
    return file;
}

void KISAK_CDECL Sys_FileClose(SysFileHandle *file)
{
    if (!file || !*file)
        return;
    SysFile *const handle = *file;
    if (handle->fd >= 0)
    {
        close(handle->fd);
        handle->fd = -1;
    }
    delete handle;
    *file = nullptr;
}

bool KISAK_CDECL Sys_FileGetSize(SysFileHandle file, std::uint64_t *outSize)
{
    if (!file || !outSize)
        return false;
    struct stat fileStatus {};
    if (fstat(file->fd, &fileStatus) != 0)
        return false;
    *outSize = static_cast<std::uint64_t>(fileStatus.st_size);
    return true;
}

SysFileReadResult KISAK_CDECL Sys_FileReadBegin(
    SysFileHandle file,
    std::uint64_t offset,
    void *buffer,
    std::uint32_t bytes)
{
    if (!file || !buffer || bytes == 0 || bytes > db::load_atomic::kFileReadBytes)
        return {SysFileReadStatus::Invalid, 0, static_cast<std::uint32_t>(EINVAL)};
    if (file->outstanding)
        return {SysFileReadStatus::Invalid, 0, static_cast<std::uint32_t>(EBUSY)};

    file->requested = bytes;
    file->result = SysFileReadResult{SysFileReadStatus::Invalid, 0, 0};

    std::uint32_t total = 0;
    int errorWord = 0;
    while (total < bytes)
    {
        const ssize_t transferred = pread(
            file->fd,
            static_cast<char *>(buffer) + total,
            bytes - total,
            static_cast<off_t>(offset + total));
        if (transferred < 0)
        {
            if (errno == EINTR)
                continue;
            errorWord = errno;
            break;
        }
        if (transferred == 0)
            break;
        total += static_cast<std::uint32_t>(transferred);
    }

    // A read that starts at or past end of file never becomes a request: the
    // waiter would only observe a zero-byte completion. Issue-time outcomes
    // are terminal here, mirroring the Win32 answers for ReadFileEx refusals
    // so the loader sees one behaviour on both platforms.
    if (total == 0)
    {
        if (errorWord == 0)
            return {SysFileReadStatus::Eof, 0, 0};
        return {SysFileReadStatus::Error, 0, static_cast<std::uint32_t>(errorWord)};
    }

    file->result = Sys_FileMakeResult(bytes, errorWord, total);
    file->outstanding = true;
    return {SysFileReadStatus::Pending, 0, 0};
}

SysFileReadResult KISAK_CDECL Sys_FileReadWait(SysFileHandle file, std::uint32_t timeoutMs)
{
    (void)timeoutMs;
    if (!file || !file->outstanding)
        return {SysFileReadStatus::Invalid, 0, static_cast<std::uint32_t>(EINVAL)};
    file->outstanding = false;
    return file->result;
}
