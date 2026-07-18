#include <qcommon/sys_console_internal.h>

#include <cerrno>
#include <cstddef>
#include <limits>

#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
int OutputDescriptor(const SysConsoleOutputStream stream) noexcept
{
    return stream == SysConsoleOutputStream::StandardOutput
        ? STDOUT_FILENO
        : STDERR_FILENO;
}
} // namespace

SysConsoleIoStatus Sys_ConsoleBackendWrite(
    const SysConsoleOutputStream stream,
    const char *bytes,
    std::size_t byteCount) noexcept
{
    const int descriptor = OutputDescriptor(stream);
    while (byteCount != 0)
    {
        const std::size_t request = byteCount
                > static_cast<std::size_t>(
                    (std::numeric_limits<ssize_t>::max)())
            ? static_cast<std::size_t>(
                (std::numeric_limits<ssize_t>::max)())
            : byteCount;
        const ssize_t written = write(descriptor, bytes, request);
        if (written > 0)
        {
            bytes += written;
            byteCount -= static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR)
            continue;
        if (written == 0)
            return SysConsoleIoStatus::IoError;
        return errno == EBADF
            ? SysConsoleIoStatus::Unavailable
            : SysConsoleIoStatus::IoError;
    }
    return SysConsoleIoStatus::Complete;
}

SysConsoleIoStatus Sys_ConsoleBackendFlush(
    const SysConsoleOutputStream stream) noexcept
{
    struct stat status{};
    return fstat(OutputDescriptor(stream), &status) == 0
        ? SysConsoleIoStatus::Complete
        : (errno == EBADF
            ? SysConsoleIoStatus::Unavailable
            : SysConsoleIoStatus::IoError);
}

bool Sys_ConsoleBackendIsRedirected(
    const SysConsoleOutputStream stream) noexcept
{
    const int descriptor = OutputDescriptor(stream);
    struct stat status{};
    return fstat(descriptor, &status) == 0 && isatty(descriptor) == 0;
}

SysConsoleRawReadResult Sys_ConsoleBackendTryReadByte() noexcept
{
    pollfd input{};
    input.fd = STDIN_FILENO;
    input.events = POLLIN | POLLHUP;

    int pollResult = 0;
    do
    {
        pollResult = poll(&input, 1, 0);
    } while (pollResult < 0 && errno == EINTR);

    if (pollResult == 0)
        return {SysConsoleRawReadStatus::NoData, 0};
    if (pollResult < 0 || (input.revents & POLLNVAL) != 0)
        return {SysConsoleRawReadStatus::IoError, 0};
    if ((input.revents & (POLLIN | POLLHUP)) == 0)
        return {SysConsoleRawReadStatus::IoError, 0};

    unsigned char byte = 0;
    ssize_t readCount = 0;
    do
    {
        readCount = read(STDIN_FILENO, &byte, 1);
    } while (readCount < 0 && errno == EINTR);

    if (readCount == 1)
        return {SysConsoleRawReadStatus::Data, byte};
    if (readCount == 0)
        return {SysConsoleRawReadStatus::EndOfFile, 0};
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return {SysConsoleRawReadStatus::NoData, 0};
    return {SysConsoleRawReadStatus::IoError, 0};
}
