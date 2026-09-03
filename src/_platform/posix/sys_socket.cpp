// SPDX-License-Identifier: GPL-3.0-only
//
// Socket service implementation for POSIX hosts (Linux, macOS). Wraps BSD
// UDP datagram sockets in the portable Sys_Socket* API. Nonblocking mode is
// enforced with O_NONBLOCK; transient EINTR restarts the system call.

#include <qcommon/sys_socket.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <new>
#include <sys/socket.h>
#include <unistd.h>

// The opaque handle's concrete shape; defined here so the anonymous-namespace
// helpers below can validate and dereference handles.
struct SysSocket
{
    int handle{-1};
};

namespace
{
bool ToSocketAddress(const sockaddr_in &source, SysSocketAddress *out) noexcept
{
    if (source.sin_family != AF_INET)
        return false;
    // Extract the four address bytes from the network-order value without
    // copying through object representations; each byte is derived from the
    // host-order value so the portable byte order (index 0 = most
    // significant) is preserved on any endianness.
    const std::uint32_t host = ntohl(source.sin_addr.s_addr);
    out->address[0] = static_cast<std::uint8_t>((host >> 24) & 0xFFU);
    out->address[1] = static_cast<std::uint8_t>((host >> 16) & 0xFFU);
    out->address[2] = static_cast<std::uint8_t>((host >> 8) & 0xFFU);
    out->address[3] = static_cast<std::uint8_t>(host & 0xFFU);
    out->port = ntohs(source.sin_port);
    return true;
}

sockaddr_in ToSockaddrIn(const SysSocketAddress &source) noexcept
{
    sockaddr_in result{};
    result.sin_family = AF_INET;
    result.sin_port = htons(source.port);
    result.sin_addr.s_addr = htonl(
        (static_cast<std::uint32_t>(source.address[0]) << 24)
        | (static_cast<std::uint32_t>(source.address[1]) << 16)
        | (static_cast<std::uint32_t>(source.address[2]) << 8)
        | static_cast<std::uint32_t>(source.address[3]));
    return result;
}

bool SendArgumentsValid(SysSocketHandle const handle,
    const void *const data,
    const SysSocketAddress *const destination,
    const std::uint32_t byteCount) noexcept
{
    return handle && handle->handle >= 0 && data && destination
        && byteCount != 0;
}

bool RecvArgumentsValid(SysSocketHandle const handle,
    const void *const buffer,
    const std::uint32_t bufferCapacity,
    const std::uint32_t *const outByteCount) noexcept
{
    return handle && handle->handle >= 0 && buffer && bufferCapacity != 0
        && outByteCount;
}

// errno is read inside the helpers so the WouldBlock mapping stays in one
// place; callers invoke them only after a failed system call.
SysSocketSendStatus ClassifySendError() noexcept
{
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return SysSocketSendStatus::WouldBlock;
    return SysSocketSendStatus::SystemFailure;
}

SysSocketRecvStatus ClassifyRecvError() noexcept
{
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return SysSocketRecvStatus::WouldBlock;
    return SysSocketRecvStatus::InvalidHandle;
}
} // namespace

SysSocketOpenStatus KISAK_CDECL Sys_SocketOpenUdp(
    const std::uint16_t port,
    const bool nonBlocking,
    SysSocketHandle *const outHandle)
{
    if (!outHandle || *outHandle)
        return SysSocketOpenStatus::InvalidArgument;

    const int raw = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (raw < 0)
        return SysSocketOpenStatus::SystemFailure;

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(port);
    local.sin_addr.s_addr = htonl(INADDR_ANY);

    // SO_REUSEADDR must be applied before bind to affect that bind attempt;
    // it permits rebinding endpoints in TIME_WAIT, not live port sharing.
    const int reuse = 1;
    if (setsockopt(raw, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0
        || bind(raw, reinterpret_cast<const sockaddr *>(&local),
            sizeof(local))
            != 0)
    {
        close(raw);
        return SysSocketOpenStatus::SystemFailure;
    }

    if (nonBlocking)
    {
        const int flags = fcntl(raw, F_GETFL, 0);
        if (flags < 0
            || fcntl(raw, F_SETFL, flags | O_NONBLOCK) < 0)
        {
            close(raw);
            return SysSocketOpenStatus::SystemFailure;
        }
    }

    // Allocation is non-throwing so a failure cannot bypass the status
    // contract and leak the already-open descriptor.
    SysSocket *socket = new (std::nothrow) SysSocket();
    if (!socket)
    {
        close(raw);
        return SysSocketOpenStatus::SystemFailure;
    }
    socket->handle = raw;
    *outHandle = socket;
    return SysSocketOpenStatus::Opened;
}

SysSocketCloseStatus KISAK_CDECL Sys_SocketClose(SysSocketHandle *const handle)
{
    if (!handle)
        return SysSocketCloseStatus::InvalidHandle;
    SysSocket *const socket = *handle;
    if (!socket)
        return SysSocketCloseStatus::Closed;
    *handle = nullptr;
    close(socket->handle);
    delete socket;
    return SysSocketCloseStatus::Closed;
}

SysSocketSendStatus KISAK_CDECL Sys_SocketSendTo(
    SysSocketHandle const handle,
    const void *const data,
    const std::uint32_t byteCount,
    const SysSocketAddress *const destination)
{
    if (!SendArgumentsValid(handle, data, destination, byteCount))
        return SysSocketSendStatus::InvalidArgument;
    if (byteCount > SysSocketMaxDatagramBytes)
        return SysSocketSendStatus::MessageTooLarge;

    const sockaddr_in to = ToSockaddrIn(*destination);
    ssize_t sent = 0;
    do
    {
        sent = sendto(handle->handle,
            data,
            static_cast<size_t>(byteCount),
            0,
            reinterpret_cast<const sockaddr *>(&to),
            sizeof(to));
    } while (sent < 0 && errno == EINTR);
    if (sent < 0)
        return ClassifySendError();
    if (sent != static_cast<ssize_t>(byteCount))
        return SysSocketSendStatus::SystemFailure;
    return SysSocketSendStatus::Sent;
}

SysSocketRecvStatus KISAK_CDECL Sys_SocketRecvFrom(
    SysSocketHandle const handle,
    void *const buffer,
    const std::uint32_t bufferCapacity,
    SysSocketAddress *const outSource,
    std::uint32_t *const outByteCount)
{
    if (outByteCount)
        *outByteCount = 0;
    if (!RecvArgumentsValid(handle, buffer, bufferCapacity, outByteCount))
        return SysSocketRecvStatus::InvalidArgument;

    sockaddr_in from{};
    socklen_t fromLength = sizeof(from);
    ssize_t received = 0;
    do
    {
        received = recvfrom(handle->handle,
            buffer,
            static_cast<size_t>(bufferCapacity),
            0,
            reinterpret_cast<sockaddr *>(&from),
            &fromLength);
    } while (received < 0 && errno == EINTR);
    if (received < 0)
        return ClassifyRecvError();

    if (outSource && !ToSocketAddress(from, outSource))
        return SysSocketRecvStatus::InvalidHandle;
    *outByteCount = static_cast<std::uint32_t>(received);
    return SysSocketRecvStatus::Received;
}

SysSocketOptionStatus KISAK_CDECL Sys_SocketEnableBroadcast(
    SysSocketHandle const handle)
{
    if (!handle || handle->handle < 0)
        return SysSocketOptionStatus::InvalidHandle;
    const int enable = 1;
    if (setsockopt(handle->handle, SOL_SOCKET, SO_BROADCAST, &enable,
            sizeof(enable)) != 0)
        return SysSocketOptionStatus::SystemFailure;
    return SysSocketOptionStatus::Applied;
}

bool KISAK_CDECL Sys_SocketGetLocalAddress(
    SysSocketHandle const handle,
    SysSocketAddress *const outAddress)
{
    if (!handle || handle->handle < 0 || !outAddress)
        return false;
    sockaddr_in local{};
    socklen_t length = sizeof(local);
    if (getsockname(handle->handle,
            reinterpret_cast<sockaddr *>(&local), &length) != 0)
        return false;
    return ToSocketAddress(local, outAddress);
}

bool KISAK_CDECL Sys_SocketMakeLoopbackAddress(
    const std::uint16_t port,
    SysSocketAddress *const outAddress)
{
    if (!outAddress)
        return false;
    outAddress->address[0] = 127;
    outAddress->address[1] = 0;
    outAddress->address[2] = 0;
    outAddress->address[3] = 1;
    outAddress->port = port;
    return true;
}

bool KISAK_CDECL Sys_SocketMakeAnyAddress(
    const std::uint16_t port,
    SysSocketAddress *const outAddress)
{
    if (!outAddress)
        return false;
    outAddress->address[0] = 0;
    outAddress->address[1] = 0;
    outAddress->address[2] = 0;
    outAddress->address[3] = 0;
    outAddress->port = port;
    return true;
}

bool KISAK_CDECL Sys_SocketAddressIsEqual(
    const SysSocketAddress *const first,
    const SysSocketAddress *const second)
{
    if (!first || !second)
        return false;
    return std::memcmp(first->address, second->address, 4) == 0
        && first->port == second->port;
}
