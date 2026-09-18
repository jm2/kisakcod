// SPDX-License-Identifier: GPL-3.0-only
//
// platform_socket_stream_harness.h -- shared scaffolding for the
// platform socket stream test binary: the failing-check recorder, the
// raw loopback listener standing in as the remote endpoint, the bounded
// connect poll, and the bounded non-blocking send/receive pumps. The
// stage functions live in platform_socket_stream_tests.cpp.

#ifndef PLATFORM_SOCKET_STREAM_HARNESS_H
#define PLATFORM_SOCKET_STREAM_HARNESS_H

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <qcommon/sys_socket.h>

#include <chrono>
#include <cstdint>
#include <thread>

// A failing check records its stage both for diagnostics and for the
// process exit code: a broken contract must fail the run, not just
// print. The run-wide state lives in platform_socket_stream_harness.cpp.
bool Check(bool condition, const char *stage);
bool StreamHarnessFailed();
const char *StreamHarnessFailedStage();

// The raw platform socket handle: on Windows SOCKET is an unsigned
// UINT_PTR, so signed-comparison validity checks silently never fire.
// Every raw-handle validity decision in this harness goes through the
// alias and the explicit invalid constant instead.
#ifdef _WIN32
using RawSocket = SOCKET;
constexpr RawSocket kInvalidRawSocket = INVALID_SOCKET;
#else
using RawSocket = int;
constexpr RawSocket kInvalidRawSocket = -1;
#endif

constexpr bool RawSocketIsValid(const RawSocket socket) noexcept
{
    return socket != kInvalidRawSocket;
}

// A raw platform listener on the loopback interface. The test controls
// accept/close itself so the client-side contract can be exercised
// against a real, independent endpoint.
class LoopbackListener
{
public:
    bool Start()
    {
        listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (!RawSocketIsValid(listenSocket))
            return false;
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (bind(listenSocket, reinterpret_cast<sockaddr *>(&address),
                sizeof(address))
            != 0)
            return false;
        socklen_t length = sizeof(address);
        if (getsockname(listenSocket,
                reinterpret_cast<sockaddr *>(&address), &length)
            != 0)
            return false;
        port_ = ntohs(address.sin_port);
        if (listen(listenSocket, 1) != 0)
            return false;
        return true;
    }

    // Bounds a raw blocking wait so a pathological stall fails a check
    // instead of hanging the suite past its CTest timeout. WSAPoll on
    // Windows and poll elsewhere share the same wait semantics: a
    // relative millisecond timeout with no wall-clock year involved.
    static bool WaitReadable(const RawSocket socket,
        const int timeoutMilliseconds)
    {
#ifdef _WIN32
        WSAPOLLFD waiting{};
        waiting.fd = socket;
        waiting.events = POLLRDNORM;
        return ::WSAPoll(&waiting, 1, timeoutMilliseconds) > 0;
#else
        pollfd waiting{};
        waiting.fd = socket;
        waiting.events = POLLIN;
        return ::poll(&waiting, 1, timeoutMilliseconds) > 0;
#endif
    }

    // Blocks until the client connects. Safe: every stage connects before
    // calling this, so the accept cannot wait on anything but this
    // process -- and the wait is bounded so a broken connect surfaces as
    // a failed check rather than a hang.
    bool Accept()
    {
        if (!WaitReadable(listenSocket, 5000))
            return false;
        peerSocket = accept(listenSocket, nullptr, nullptr);
        if (!RawSocketIsValid(peerSocket))
            return false;
        // Accepted sockets do not inherit the timeout options everywhere;
        // bound the raw send/recv waits on the peer as well. The POSIX
        // SO_*TIMEO options require a struct timeval here; the duration
        // semantics (5 s) stay constant regardless of wall-clock year.
#ifdef _WIN32
        const DWORD waitMilliseconds = 5000;
        setsockopt(peerSocket, SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char *>(&waitMilliseconds),
            sizeof(waitMilliseconds));
        setsockopt(peerSocket, SOL_SOCKET, SO_SNDTIMEO,
            reinterpret_cast<const char *>(&waitMilliseconds),
            sizeof(waitMilliseconds));
#else
        timeval waitTimeout{};
        waitTimeout.tv_sec = 5;
        setsockopt(peerSocket, SOL_SOCKET, SO_RCVTIMEO, &waitTimeout,
            sizeof(waitTimeout));
        setsockopt(peerSocket, SOL_SOCKET, SO_SNDTIMEO, &waitTimeout,
            sizeof(waitTimeout));
#endif
        return true;
    }

    bool SendAll(const char *data, std::uint32_t length) const
    {
        while (length != 0)
        {
#ifdef _WIN32
            const int sent = ::send(peerSocket, data,
                static_cast<int>(length), 0);
#else
            const ssize_t sent = ::send(peerSocket, data, length, 0);
#endif
            if (sent <= 0)
                return false;
            data += sent;
            length -= static_cast<std::uint32_t>(sent);
        }
        return true;
    }

    // Receives exactly `length` bytes.
    bool RecvAll(char *buffer, std::uint32_t length) const
    {
        while (length != 0)
        {
#ifdef _WIN32
            const int received = ::recv(peerSocket, buffer,
                static_cast<int>(length), 0);
#else
            const ssize_t received = ::recv(peerSocket, buffer, length, 0);
#endif
            if (received <= 0)
                return false;
            buffer += received;
            length -= static_cast<std::uint32_t>(received);
        }
        return true;
    }

    void ClosePeer()
    {
        if (RawSocketIsValid(peerSocket))
        {
#ifdef _WIN32
            closesocket(peerSocket);
#else
            ::close(peerSocket);
#endif
            peerSocket = kInvalidRawSocket;
        }
    }

    void Stop()
    {
        ClosePeer();
        if (RawSocketIsValid(listenSocket))
        {
#ifdef _WIN32
            closesocket(listenSocket);
#else
            ::close(listenSocket);
#endif
            listenSocket = kInvalidRawSocket;
        }
    }

    std::uint16_t Port() const { return port_; }

private:
    RawSocket listenSocket{kInvalidRawSocket};
    RawSocket peerSocket{kInvalidRawSocket};
    std::uint16_t port_{0};
};

// Pumps Sys_SocketPollConnected until Ready (bounded); a Failed poll with
// a listening peer means the handshake contract itself broke.
inline bool PollUntilReady(const SysSocketHandle socket,
    SysSocketStreamPollStatus *const outFinal)
{
    for (int attempt = 0; attempt < 2000; ++attempt)
    {
        const SysSocketStreamPollStatus status =
            Sys_SocketPollConnected(socket);
        if (status == SysSocketStreamPollStatus::Ready)
        {
            *outFinal = status;
            return true;
        }
        if (status != SysSocketStreamPollStatus::InProgress)
        {
            *outFinal = status;
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    *outFinal = SysSocketStreamPollStatus::InProgress;
    return false;
}

// Bounded non-blocking pumps shared by the exchange stages: every pump
// bounds its WouldBlock retries so a pathological stall fails a check
// instead of hanging the suite past its CTest timeout.

// Sends one request piece through the non-blocking stream; a partial
// first send is pumped to completion within the shared bound. `tag`
// names the piece in failure reports.
inline bool PumpStreamSend(const SysSocketHandle client,
    const char *const data, const std::uint32_t length,
    const char *const tag)
{
    std::uint32_t sent = 0;
    if (!Check(Sys_SocketSendStream(client, data, length, &sent)
                == SysSocketStreamSendStatus::Sent,
            tag))
        return false;
    Check(sent <= length, tag);
    if (sent >= length)
        return true;

    // Drained partially; push the remainder. Bounded like every other
    // pump in this harness.
    std::uint32_t delivered = sent;
    bool pieceDelivered = false;
    for (int attempt = 0; attempt < 2000; ++attempt)
    {
        const SysSocketStreamSendStatus status = Sys_SocketSendStream(
            client, data + delivered, length - delivered, &sent);
        if (status == SysSocketStreamSendStatus::WouldBlock)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (status != SysSocketStreamSendStatus::Sent)
            return Check(false, "stream-send-retry");
        delivered += sent;
        if (delivered >= length)
        {
            pieceDelivered = true;
            break;
        }
    }
    return Check(pieceDelivered, "stream-send-retry-bound");
}

// Bounded wait for the first non-blocking receive window. A missing
// window is a contract failure, not a stall.
inline bool PumpFirstRecvWindow(const SysSocketHandle client,
    char *const buffer, const std::uint32_t capacity,
    std::uint32_t *const outLength)
{
    for (int attempt = 0; attempt < 2000; ++attempt)
    {
        const SysSocketStreamRecvStatus status = Sys_SocketRecvStream(
            client, buffer, capacity, outLength);
        if (status == SysSocketStreamRecvStatus::WouldBlock)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (status != SysSocketStreamRecvStatus::Received)
            return Check(false, "stream-recv-first");
        return true;
    }
    return Check(false, "stream-recv-first-bound");
}

// Drains the stream until the peer's orderly shutdown surfaces as
// Disconnected. Bounded like the first-window pump: after the peer
// closed, the FIN must surface within the bound or the contract failed.
inline bool PumpRecvUntilDisconnect(const SysSocketHandle client,
    bool *const outSawRemainder)
{
    *outSawRemainder = false;
    for (int attempt = 0; attempt < 2000; ++attempt)
    {
        char chunk[64];
        std::uint32_t chunkLength = 0;
        const SysSocketStreamRecvStatus status = Sys_SocketRecvStream(
            client, chunk, sizeof(chunk), &chunkLength);
        if (status == SysSocketStreamRecvStatus::Received)
        {
            *outSawRemainder = true;
            continue;
        }
        if (status == SysSocketStreamRecvStatus::WouldBlock)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (status == SysSocketStreamRecvStatus::Disconnected)
            return true;
        return Check(false, "stream-recv-disconnect");
    }
    return Check(false, "stream-recv-disconnect-bound");
}

#endif // PLATFORM_SOCKET_STREAM_HARNESS_H
