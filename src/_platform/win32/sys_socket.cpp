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
#include <new>

// The opaque handle's concrete shape; defined here so the anonymous-namespace
// helpers below can validate and dereference handles.
struct SysSocket
{
    SOCKET handle{INVALID_SOCKET};
};

// Test seam (test builds only). When KISAK_SOCKET_TEST_HOOKS is defined the
// suite can install a query that replaces the native getaddrinfo call, so the
// failed-resolution contract is exercised deterministically instead of
// depending on the host's resolver configuration. Production builds do not
// define the macro, so neither the hook nor the setter exists in the shipped
// service.
#if defined(KISAK_SOCKET_TEST_HOOKS)
using SocketResolveQuery = int (*)(const char *node,
    const char *service,
    const addrinfo *hints,
    addrinfo **results);

namespace
{
thread_local SocketResolveQuery resolveHostTestHook = nullptr;
} // namespace

void KISAK_CDECL Kisak_SocketSetResolveTestHook(SocketResolveQuery hook)
{
    resolveHostTestHook = hook;
}
#endif

namespace
{
// Winsock is initialized once per process on the first open and stays
// initialized for the process lifetime; this count tracks startup ownership
// across the startup race. Held at namespace scope so no function-local
// static initialization ordering applies.
std::atomic<int> winsockUsers{0};

bool EnsureWinsockStarted() noexcept
{
    int expected = winsockUsers.load(std::memory_order_relaxed);
    while (expected == 0)
    {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            return false;
        if (winsockUsers.compare_exchange_strong(
                expected, 1, std::memory_order_relaxed))
            return true;
        // Another thread won the startup race; release ours and retry.
        WSACleanup();
    }
    // Increment the user count for an already-initialized Winsock.
    winsockUsers.fetch_add(1, std::memory_order_relaxed);
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
    result.sin_addr.s_addr = htonl(
        (static_cast<unsigned long>(source.address[0]) << 24)
        | (static_cast<unsigned long>(source.address[1]) << 16)
        | (static_cast<unsigned long>(source.address[2]) << 8)
        | static_cast<unsigned long>(source.address[3]));
    return result;
}

bool SendArgumentsValid(SysSocketHandle const handle,
    const void *const data,
    const SysSocketAddress *const destination,
    const std::uint32_t byteCount) noexcept
{
    return handle && handle->handle != INVALID_SOCKET && data && destination
        && byteCount != 0;
}

bool RecvArgumentsValid(SysSocketHandle const handle,
    const void *const buffer,
    const std::uint32_t bufferCapacity,
    const std::uint32_t *const outByteCount) noexcept
{
    return handle && handle->handle != INVALID_SOCKET && buffer
        && bufferCapacity != 0 && outByteCount;
}

// WSAGetLastError is read by the caller and passed in so the WouldBlock
// mapping stays in one place; helpers run only after a failed system call.
SysSocketSendStatus ClassifySendError(const int error) noexcept
{
    if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS)
        return SysSocketSendStatus::WouldBlock;
    return SysSocketSendStatus::SystemFailure;
}

SysSocketRecvStatus ClassifyRecvError(const int error) noexcept
{
    if (error == WSAEWOULDBLOCK)
        return SysSocketRecvStatus::WouldBlock;
    return SysSocketRecvStatus::SystemFailure;
}
} // namespace

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

    // A default wildcard bind still permits a competing interface-specific
    // bind under the same Windows user. Reserve the entire port before
    // binding so the portable exclusive-ownership contract also holds here.
    const BOOL exclusive = TRUE;
    if (setsockopt(raw, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
            reinterpret_cast<const char *>(&exclusive), sizeof(exclusive))
        != 0)
    {
        closesocket(raw);
        return SysSocketOpenStatus::SystemFailure;
    }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(port);
    local.sin_addr.s_addr = INADDR_ANY;

    // SO_EXCLUSIVEADDRUSE covers wildcard and specific-interface competitors.
    if (bind(raw, reinterpret_cast<const sockaddr *>(&local),
            sizeof(local))
            != 0)
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

    // Allocation is non-throwing so a failure cannot bypass the status
    // contract and leak the already-open socket.
    SysSocket *socket = new (std::nothrow) SysSocket();
    if (!socket)
    {
        closesocket(raw);
        return SysSocketOpenStatus::SystemFailure;
    }
    socket->handle = raw;
    *outHandle = socket;
    return SysSocketOpenStatus::Opened;
}

SysSocketCloseStatus KISAK_CDECL Sys_SocketClose(SysSocketHandle *const handle)
{
    // Close is unconditional: a null pointer is as closed as a closed
    // handle, so callers may tear down without inspecting state.
    if (!handle)
        return SysSocketCloseStatus::Closed;
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
    if (!SendArgumentsValid(handle, data, destination, byteCount))
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
        return ClassifySendError(WSAGetLastError());
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
    if (!RecvArgumentsValid(handle, buffer, bufferCapacity, outByteCount))
        return SysSocketRecvStatus::InvalidArgument;

    sockaddr_in from{};
    int fromLength = sizeof(from);
    // The public capacity is the caller's uint32 receive window, but
    // Winsock's recvfrom takes a signed int length. A direct conversion
    // wraps a capacity of 2^31 or more to a negative length, turning a
    // valid (reserved) receive window into an invalid request. Clamp to
    // the IPv4 datagram bound BEFORE the signed conversion: no single
    // datagram can exceed that bound, so real traffic never observes the
    // clamp while every capacity stays representable.
    std::uint32_t recvLength = bufferCapacity;
    if (recvLength > SysSocketMaxDatagramBytes)
        recvLength = SysSocketMaxDatagramBytes;
    const int received = recvfrom(handle->handle,
        static_cast<char *>(buffer),
        static_cast<int>(recvLength),
        0,
        reinterpret_cast<sockaddr *>(&from),
        &fromLength);
    if (received == SOCKET_ERROR)
    {
        const int error = WSAGetLastError();
        if (error == WSAEMSGSIZE)
        {
            // An oversized datagram fills the window with its leading
            // bytes and Winsock reports the discarded excess as
            // WSAEMSGSIZE; the whole-datagram contract maps that to
            // Truncated instead of a system failure. The filled count is
            // the clamped window the call was given — never the raw
            // caller capacity, which the clamp may have reduced.
            *outByteCount = recvLength;
            return SysSocketRecvStatus::Truncated;
        }
        return ClassifyRecvError(error);
    }

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
    // Byte-exact equality: the address compares as network-order bytes
    // (memcmp-safe) and the port as a host-order value. The rule lives here
    // rather than in an inline header member, whose C++ body the repo's
    // C-based MISRA analyzer cannot scope.
    bool equal = false;
    const bool sameAddress =
        (std::memcmp(first->address, second->address,
                     sizeof(first->address)) == 0);
    if (sameAddress)
    {
        equal = (first->port == second->port);
    }
    return equal;
}

namespace
{
// Resolves the literal host forms that must never touch the OS resolver:
// the exact name "localhost" and numeric dotted-quad addresses. Returns
// true and fills `outAddress` on a match; false leaves it untouched so the
// caller can fall through to getaddrinfo(AF_INET).
bool TryResolveLiteralHost(
    const char *const hostname,
    SysSocketAddress *const outAddress) noexcept
{
    if (std::strcmp(hostname, "localhost") == 0)
    {
        outAddress->address[0] = 127;
        outAddress->address[1] = 0;
        outAddress->address[2] = 0;
        outAddress->address[3] = 1;
        outAddress->port = 0;
        return true;
    }

    in_addr literal{};
    if (inet_pton(AF_INET, hostname, &literal) == 1)
    {
        const unsigned long host = ntohl(literal.s_addr);
        outAddress->address[0] =
            static_cast<std::uint8_t>((host >> 24) & 0xFFUL);
        outAddress->address[1] =
            static_cast<std::uint8_t>((host >> 16) & 0xFFUL);
        outAddress->address[2] =
            static_cast<std::uint8_t>((host >> 8) & 0xFFUL);
        outAddress->address[3] = static_cast<std::uint8_t>(host & 0xFFUL);
        outAddress->port = 0;
        return true;
    }

    return false;
}

// Maps `hostname` to an IPv4 endpoint with a zero port. Literal forms are
// resolved by TryResolveLiteralHost without host resolver configuration or
// an available network; every other value is delegated to
// getaddrinfo(AF_INET), which requires Winsock to be initialized first. The
// endpoint is written only on Resolved: a caller-visible failure never
// carries a half-populated address.
SysSocketResolveStatus ResolveHostAddress(
    const char *const hostname,
    SysSocketAddress *const outAddress) noexcept
{
    if (TryResolveLiteralHost(hostname, outAddress))
        return SysSocketResolveStatus::Resolved;

    if (!EnsureWinsockStarted())
        return SysSocketResolveStatus::SystemFailure;

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    addrinfo *results = nullptr;
#if defined(KISAK_SOCKET_TEST_HOOKS)
    const SocketResolveQuery query = resolveHostTestHook;
    const int failure = query
        ? query(hostname, nullptr, &hints, &results)
        : getaddrinfo(hostname, nullptr, &hints, &results);
#else
    const int failure = getaddrinfo(hostname, nullptr, &hints, &results);
#endif
    if (failure != 0 || !results)
        return Sys_SocketResolveErrorStatus(failure);

    SysSocketAddress resolved{};
    const bool mapped = ToSocketAddress(
        *reinterpret_cast<const sockaddr_in *>(results->ai_addr), &resolved);
    freeaddrinfo(results);
    if (!mapped)
        return SysSocketResolveStatus::SystemFailure;
    resolved.port = 0;
    *outAddress = resolved;
    return SysSocketResolveStatus::Resolved;
}
} // namespace

SysSocketResolveStatus KISAK_CDECL Sys_SocketResolveErrorStatus(
    const int resolverError)
{
    // EAI_NODATA is the Winsock no-address code (WSANO_DATA) and differs from
    // EAI_NONAME (WSAHOST_NOT_FOUND); an existing name with no IPv4 address
    // is reported as addressless there, and the public contract folds both
    // into NotFound. EAI_ADDRFAMILY, where Winsock defines it, is the same
    // "no address in the requested family" outcome for an AF_INET request and
    // folds in too. The guards keep genuine resolver errors -- temporary,
    // unrecoverable, resource -- as SystemFailure and tolerate platforms that
    // omit the code or alias it to one already classified.
#if defined(EAI_NODATA) && (EAI_NODATA != EAI_NONAME)
    if (resolverError == EAI_NODATA)
        return SysSocketResolveStatus::NotFound;
#endif
#if defined(EAI_ADDRFAMILY) && (EAI_ADDRFAMILY != EAI_NONAME) \
    && (EAI_ADDRFAMILY != EAI_NODATA)
    if (resolverError == EAI_ADDRFAMILY)
        return SysSocketResolveStatus::NotFound;
#endif
    if (resolverError == EAI_NONAME)
        return SysSocketResolveStatus::NotFound;
    return SysSocketResolveStatus::SystemFailure;
}

SysSocketResolveStatus KISAK_CDECL Sys_SocketResolveHost(
    const char *const hostname,
    const std::uint16_t port,
    SysSocketAddress *const outAddress)
{
    if (!hostname || hostname[0] == '\0' || !outAddress)
        return SysSocketResolveStatus::InvalidArgument;

    SysSocketAddress resolved{};
    const SysSocketResolveStatus status =
        ResolveHostAddress(hostname, &resolved);
    if (status != SysSocketResolveStatus::Resolved)
        return status;
    resolved.port = port;
    *outAddress = resolved;
    return SysSocketResolveStatus::Resolved;
}

// ---- TCP stream client extension (Win32) ------------------------------------

namespace
{
bool StreamArgumentsValid(SysSocketHandle const handle,
    const void *const buffer,
    const std::uint32_t byteCount) noexcept
{
    return handle && handle->handle != INVALID_SOCKET && buffer
        && byteCount != 0;
}

// One nonblocking stream send; split out so the status-mapping contract
// stays at the same complexity as the receive path. The public length is
// the caller's uint32 request, but Winsock's send takes a signed int
// length: clamp to the fixed socket bound BEFORE the signed conversion
// so a length of 2^31 or more cannot wrap negative; a stream send simply
// continues from the reported partial progress when the request was
// clamped.
int StreamSend(SOCKET const descriptor,
    const void *const data,
    const std::uint32_t byteCount) noexcept
{
    std::uint32_t sendLength = byteCount;
    if (sendLength > SysSocketMaxDatagramBytes)
        sendLength = SysSocketMaxDatagramBytes;
    return send(descriptor,
        static_cast<const char *>(data),
        static_cast<int>(sendLength),
        0);
}

// One nonblocking stream receive with the same clamp-before-convert
// discipline for the caller's uint32 receive window.
int StreamRecv(SOCKET const descriptor,
    void *const buffer,
    const std::uint32_t bufferCapacity) noexcept
{
    std::uint32_t recvLength = bufferCapacity;
    if (recvLength > SysSocketMaxDatagramBytes)
        recvLength = SysSocketMaxDatagramBytes;
    return recv(descriptor,
        static_cast<char *>(buffer),
        static_cast<int>(recvLength),
        0);
}

// Maps a failed send's WSA error to the portable stream status; keeping
// the branch table separate holds Sys_SocketSendStream to the same
// complexity as the receive path.
SysSocketStreamSendStatus SendStreamErrorStatus() noexcept
{
    const int error = WSAGetLastError();
    if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS)
        return SysSocketStreamSendStatus::WouldBlock;
    if (error == WSAECONNRESET || error == WSAECONNABORTED
        || error == WSAESHUTDOWN)
        return SysSocketStreamSendStatus::Disconnected;
    return SysSocketStreamSendStatus::SystemFailure;
}
} // namespace

SysSocketStreamOpenStatus KISAK_CDECL Sys_SocketOpenStream(
    const bool nonBlocking,
    SysSocketHandle *const outHandle)
{
    if (!outHandle || *outHandle)
        return SysSocketStreamOpenStatus::InvalidArgument;
    if (!EnsureWinsockStarted())
        return SysSocketStreamOpenStatus::SystemFailure;

    const SOCKET raw = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (raw == INVALID_SOCKET)
        return SysSocketStreamOpenStatus::SystemFailure;

    if (nonBlocking)
    {
        u_long mode = 1;
        if (ioctlsocket(raw, FIONBIO, &mode) != 0)
        {
            closesocket(raw);
            return SysSocketStreamOpenStatus::SystemFailure;
        }
    }

    // Allocation is non-throwing so a failure cannot bypass the status
    // contract and leak the already-open socket.
    SysSocket *socket = new (std::nothrow) SysSocket();
    if (!socket)
    {
        closesocket(raw);
        return SysSocketStreamOpenStatus::SystemFailure;
    }
    socket->handle = raw;
    *outHandle = socket;
    return SysSocketStreamOpenStatus::Opened;
}

SysSocketStreamConnectStatus KISAK_CDECL Sys_SocketConnectStream(
    SysSocketHandle const handle,
    const SysSocketAddress *const destination)
{
    if (!handle || handle->handle == INVALID_SOCKET || !destination)
        return SysSocketStreamConnectStatus::InvalidArgument;

    const sockaddr_in to = ToSockaddrIn(*destination);
    const int failure = connect(handle->handle,
        reinterpret_cast<const sockaddr *>(&to), sizeof(to));
    if (failure == 0)
        return SysSocketStreamConnectStatus::Connected;
    // WSAEWOULDBLOCK marks a non-blocking handshake in flight (and an
    // interrupted one stays pending rather than restarting); WSAEALREADY
    // and WSAEISCONN fold into their caller-observable outcomes. All of
    // the in-flight forms report InProgress, which the caller resolves
    // through Sys_SocketPollConnected.
    const int error = WSAGetLastError();
    if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS
        || error == WSAEALREADY)
        return SysSocketStreamConnectStatus::InProgress;
    if (error == WSAEISCONN)
        return SysSocketStreamConnectStatus::Connected;
    return SysSocketStreamConnectStatus::SystemFailure;
}

SysSocketStreamPollStatus KISAK_CDECL Sys_SocketPollConnected(
    SysSocketHandle const handle)
{
    if (!handle || handle->handle == INVALID_SOCKET)
        return SysSocketStreamPollStatus::InvalidArgument;

    // select() with a zero timeout observes connect completion on Windows
    // the same way poll(POLLOUT) does on POSIX. The first select argument
    // is ignored on Windows and meaningful on POSIX hosts only.
    fd_set writeSet{};
    FD_ZERO(&writeSet);
    FD_SET(handle->handle, &writeSet);
    fd_set errorSet{};
    FD_ZERO(&errorSet);
    FD_SET(handle->handle, &errorSet);
    timeval instant{};
    const int ready = select(0, nullptr, &writeSet, &errorSet, &instant);
    if (ready == SOCKET_ERROR)
        return SysSocketStreamPollStatus::SystemFailure;
    if (ready == 0)
        return SysSocketStreamPollStatus::InProgress;

    // Writability (or the except set) ends the handshake; SO_ERROR names
    // the outcome. Zero is established, anything else is the real
    // refusal -- select artifacts are never reported as Failed.
    int socketError = 0;
    int errorLength = sizeof(socketError);
    if (getsockopt(handle->handle, SOL_SOCKET, SO_ERROR,
            reinterpret_cast<char *>(&socketError), &errorLength)
        != 0)
        return SysSocketStreamPollStatus::SystemFailure;
    if (socketError == 0)
        return SysSocketStreamPollStatus::Ready;
    return SysSocketStreamPollStatus::Failed;
}

SysSocketStreamSendStatus KISAK_CDECL Sys_SocketSendStream(
    SysSocketHandle const handle,
    const void *const data,
    const std::uint32_t byteCount,
    std::uint32_t *const outSentBytes)
{
    if (outSentBytes)
        *outSentBytes = 0;
    if (!StreamArgumentsValid(handle, data, byteCount) || !outSentBytes)
        return SysSocketStreamSendStatus::InvalidArgument;

    const int sent = StreamSend(handle->handle, data, byteCount);
    if (sent == SOCKET_ERROR)
        return SendStreamErrorStatus();
    // A stream send of zero cannot occur for a nonzero length, but the
    // guard keeps the contract honest on exotic platforms.
    if (sent == 0)
        return SysSocketStreamSendStatus::WouldBlock;
    *outSentBytes = static_cast<std::uint32_t>(sent);
    return SysSocketStreamSendStatus::Sent;
}

SysSocketStreamRecvStatus KISAK_CDECL Sys_SocketRecvStream(
    SysSocketHandle const handle,
    void *const buffer,
    const std::uint32_t bufferCapacity,
    std::uint32_t *const outByteCount)
{
    if (outByteCount)
        *outByteCount = 0;
    if (!StreamArgumentsValid(handle, buffer, bufferCapacity) || !outByteCount)
        return SysSocketStreamRecvStatus::InvalidArgument;

    const int received = StreamRecv(handle->handle, buffer, bufferCapacity);
    if (received == SOCKET_ERROR)
    {
        const int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK)
            return SysSocketStreamRecvStatus::WouldBlock;
        if (error == WSAECONNRESET || error == WSAECONNABORTED
            || error == WSAESHUTDOWN)
            return SysSocketStreamRecvStatus::Disconnected;
        return SysSocketStreamRecvStatus::SystemFailure;
    }
    // Zero bytes on a stream is the peer's orderly shutdown: the stream
    // is finished and will never yield more data.
    if (received == 0)
        return SysSocketStreamRecvStatus::Disconnected;
    *outByteCount = static_cast<std::uint32_t>(received);
    return SysSocketStreamRecvStatus::Received;
}
