// SPDX-License-Identifier: GPL-3.0-only
//
// Socket service implementation for Win32. Wraps Winsock2 UDP datagram
// sockets in the portable Sys_Socket* API. Winsock is initialized once per
// process on the first open and stays initialized for the process lifetime,
// matching the production network layer's lifetime.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>

#include <qcommon/sys_socket.h>

#include <atomic>
#include <cstring>

namespace
{
std::atomic<int> &WinsockUsers() noexcept
{
    static std::atomic<int> users{0};
    return users;
}

bool EnsureWinsockStarted() noexcept
{
    int expected = WinsockUsers().load(std::memory_order_relaxed);
    while (expected == 0)
    {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            return false;
        if (WinsockUsers().compare_exchange_strong(
                expected, 1, std::memory_order_relaxed))
            return true;
        // Another thread won the startup race; release ours and retry.
        WSACleanup();
    }
    // Increment the user count for an already-initialized Winsock.
    WinsockUsers().fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool ToSocketAddress(const sockaddr_in &source, SysSocketAddress *out) noexcept
{
    if (source.sin_family != AF_INET)
        return false;
    out->address[0] = reinterpret_cast<const std::uint8_t *>(
        &source.sin_addr)[0];
    out->address[1] = reinterpret_cast<const std::uint8_t *>(
        &source.sin_addr)[1];
    out->address[2] = reinterpret_cast<const std::uint8_t *>(
        &source.sin_addr)[2];
    out->address[3] = reinterpret_cast<const std::uint8_t *>(
        &source.sin_addr)[3];
    out->port = ntohs(source.sin_port);
    return true;
}

sockaddr_in ToSockaddrIn(const SysSocketAddress &source) noexcept
{
    sockaddr_in result{};
    result.sin_family = AF_INET;
    result.sin_port = htons(source.port);
    std::memcpy(&result.sin_addr, source.address, 4);
    return result;
}
} // namespace

struct SysSocket
{
    SOCKET handle{INVALID_SOCKET};
};

SysSocketOpenStatus KISAK_CDECL Sys_SocketOpenUdp(
    const std::uint16_t port,
    const bool nonBlocking,
    SysSocketHandle *const outHandle)
{
    if (!outHandle || *outHandle)
        return SysSocketOpenStatus::InvalidArgument;
    if (!EnsureWinsockStarted())
        return SysSocketOpenStatus::SystemFailure;

    const SOCKET raw = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (raw == INVALID_SOCKET)
        return SysSocketOpenStatus::SystemFailure;

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(port);
    local.sin_addr.s_addr = INADDR_ANY;

    const BOOL reuse = TRUE;
    if (bind(raw, reinterpret_cast<const sockaddr *>(&local), sizeof(local)) != 0
        || setsockopt(raw, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char *>(&reuse), sizeof(reuse)) != 0)
    {
        closesocket(raw);
        return SysSocketOpenStatus::SystemFailure;
    }

    if (nonBlocking)
    {
        u_long mode = 1;
        if (ioctlsocket(raw, FIONBIO, &mode) != 0)
        {
            closesocket(raw);
            return SysSocketOpenStatus::SystemFailure;
        }
    }

    SysSocket *socket = new SysSocket();
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
    closesocket(socket->handle);
    delete socket;
    return SysSocketCloseStatus::Closed;
}

SysSocketSendStatus KISAK_CDECL Sys_SocketSendTo(
    SysSocketHandle const handle,
    const void *const data,
    const std::uint32_t byteCount,
    const SysSocketAddress *const destination)
{
    if (!handle || handle->handle == INVALID_SOCKET || !data || !destination
        || byteCount == 0)
        return SysSocketSendStatus::InvalidArgument;
    if (byteCount > SysSocketMaxDatagramBytes)
        return SysSocketSendStatus::MessageTooLarge;

    const sockaddr_in to = ToSockaddrIn(*destination);
    const int sent = sendto(handle->handle,
        static_cast<const char *>(data),
        static_cast<int>(byteCount),
        0,
        reinterpret_cast<const sockaddr *>(&to),
        sizeof(to));
    if (sent == SOCKET_ERROR)
    {
        const int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS)
            return SysSocketSendStatus::WouldBlock;
        return SysSocketSendStatus::SystemFailure;
    }
    if (sent != static_cast<int>(byteCount))
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
    if (!handle || handle->handle == INVALID_SOCKET || !buffer
        || bufferCapacity == 0 || !outByteCount)
        return SysSocketRecvStatus::InvalidArgument;

    sockaddr_in from{};
    int fromLength = sizeof(from);
    const int received = recvfrom(handle->handle,
        static_cast<char *>(buffer),
        static_cast<int>(bufferCapacity),
        0,
        reinterpret_cast<sockaddr *>(&from),
        &fromLength);
    if (received == SOCKET_ERROR)
    {
        const int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK)
            return SysSocketRecvStatus::WouldBlock;
        return SysSocketRecvStatus::InvalidHandle;
    }
    if (received < 0)
        return SysSocketRecvStatus::InvalidHandle;

    if (outSource && !ToSocketAddress(from, outSource))
        return SysSocketRecvStatus::InvalidHandle;
    *outByteCount = static_cast<std::uint32_t>(received);
    return SysSocketRecvStatus::Received;
}

SysSocketOptionStatus KISAK_CDECL Sys_SocketEnableBroadcast(
    SysSocketHandle const handle)
{
    if (!handle || handle->handle == INVALID_SOCKET)
        return SysSocketOptionStatus::InvalidHandle;
    const BOOL enable = TRUE;
    if (setsockopt(handle->handle, SOL_SOCKET, SO_BROADCAST,
            reinterpret_cast<const char *>(&enable), sizeof(enable)) != 0)
        return SysSocketOptionStatus::SystemFailure;
    return SysSocketOptionStatus::Applied;
}

bool KISAK_CDECL Sys_SocketGetLocalAddress(
    SysSocketHandle const handle,
    SysSocketAddress *const outAddress)
{
    if (!handle || handle->handle == INVALID_SOCKET || !outAddress)
        return false;
    sockaddr_in local{};
    int length = sizeof(local);
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
