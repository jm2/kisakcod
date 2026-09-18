// SPDX-License-Identifier: GPL-3.0-only
//
// platform_socket_stream_tests.cpp -- exercises the TCP stream extension
// of the portable Sys_Socket* API over real loopback connections, against
// a raw platform listener standing in as the remote endpoint. Covers the
// connect/poll handshake, partial stream sends, read boundaries, orderly
// shutdown, and the fail-closed argument contract. The binary runs the
// full suite with no arguments; each stage names its checks and a failing
// check reports the stage that owned it.

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
#include <cstdio>
#include <cstring>
#include <thread>

namespace
{
const char *checkStage = "startup";
// A failing check records its stage both for diagnostics and for the
// process exit code: a broken contract must fail the run, not just print.
bool checkFailed = false;

bool Check(const bool condition, const char *const stage)
{
    if (!condition)
    {
        checkStage = stage;
        checkFailed = true;
        return false;
    }
    return true;
}

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
    // instead of hanging the suite past its CTest timeout. Windows
    // ignores the first select() argument, so pass 0 there instead of
    // narrowing the unsigned handle for a value the platform discards.
    static bool WaitReadable(const RawSocket socket, const int timeoutMilliseconds)
    {
#ifdef _WIN32
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(socket, &readable);
        timeval waitTimeout{};
        waitTimeout.tv_sec = timeoutMilliseconds / 1000;
        waitTimeout.tv_usec
            = (timeoutMilliseconds % 1000) * 1000;
        return ::select(0, &readable, nullptr, nullptr,
                   &waitTimeout)
            > 0;
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
        // bound the raw send/recv waits on the peer as well.
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
bool PollUntilReady(const SysSocketHandle socket,
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

// Drives one full non-blocking exchange: open, connect, poll, partial
// send, request delivery, response receive until WouldBlock, orderly
// shutdown. Returns false at the first broken contract.
bool RunNonBlockingExchange(LoopbackListener &listener)
{
    SysSocketHandle client = nullptr;
    if (!Check(Sys_SocketOpenStream(true, &client)
                == SysSocketStreamOpenStatus::Opened,
            "stream-open"))
        return false;

    SysSocketAddress endpoint{};
    Sys_SocketMakeLoopbackAddress(listener.Port(), &endpoint);
    const SysSocketStreamConnectStatus connected =
        Sys_SocketConnectStream(client, &endpoint);
    if (!Check(connected == SysSocketStreamConnectStatus::InProgress
                || connected == SysSocketStreamConnectStatus::Connected,
            "stream-connect"))
        return false;
    if (!listener.Accept())
        return Check(false, "stream-accept");

    SysSocketStreamPollStatus pollStatus{};
    if (!PollUntilReady(client, &pollStatus)
        || pollStatus != SysSocketStreamPollStatus::Ready)
        return Check(false, "stream-poll-ready");

    // Partial sends: queue the request in two pieces and let the
    // transport report how much moved each time.
    static const char requestPart1[] = "GET /file.map HTTP/1.1\r\n";
    static const char requestPart2[] = "Host: loopback\r\n\r\n";
    std::uint32_t sent = 0;
    if (!Check(Sys_SocketSendStream(client, requestPart1,
                   static_cast<std::uint32_t>(sizeof(requestPart1) - 1),
                   &sent)
            == SysSocketStreamSendStatus::Sent,
        "stream-send-part1"))
        return false;
    Check(sent <= sizeof(requestPart1) - 1, "stream-send-part1");
    std::uint32_t sentPart1 = sent;
    if (sentPart1 < sizeof(requestPart1) - 1)
    {
        // Drained partially; push the remainder. Bounded like every
        // other pump in this harness.
        bool part1Delivered = false;
        for (int attempt = 0; attempt < 2000; ++attempt)
        {
            const SysSocketStreamSendStatus status = Sys_SocketSendStream(
                client, requestPart1 + sentPart1,
                static_cast<std::uint32_t>(sizeof(requestPart1) - 1
                    - sentPart1), &sent);
            if (status == SysSocketStreamSendStatus::WouldBlock)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (status != SysSocketStreamSendStatus::Sent)
                return Check(false, "stream-send-retry");
            sentPart1 += sent;
            if (sentPart1 >= sizeof(requestPart1) - 1)
            {
                part1Delivered = true;
                break;
            }
        }
        if (!Check(part1Delivered, "stream-send-retry-bound"))
            return false;
    }
    if (!Check(Sys_SocketSendStream(client, requestPart2,
                   static_cast<std::uint32_t>(sizeof(requestPart2) - 1),
                   &sent)
            == SysSocketStreamSendStatus::Sent,
        "stream-send-part2"))
        return false;
    if (sent < sizeof(requestPart2) - 1)
    {
        // Bounded like the part-1 pump.
        std::uint32_t sentPart2 = sent;
        bool part2Delivered = false;
        for (int attempt = 0; attempt < 2000; ++attempt)
        {
            const SysSocketStreamSendStatus status = Sys_SocketSendStream(
                client, requestPart2 + sentPart2,
                static_cast<std::uint32_t>(sizeof(requestPart2) - 1
                    - sentPart2), &sent);
            if (status == SysSocketStreamSendStatus::WouldBlock)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (status != SysSocketStreamSendStatus::Sent)
                return Check(false, "stream-send-retry");
            sentPart2 += sent;
            if (sentPart2 >= sizeof(requestPart2) - 1)
            {
                part2Delivered = true;
                break;
            }
        }
        if (!Check(part2Delivered, "stream-send-retry-bound"))
            return false;
    }

    // The listener received the exact bytes the client queued.
    char receivedRequest[64]{};
    if (!listener.RecvAll(receivedRequest,
            static_cast<std::uint32_t>(sizeof(requestPart1)
                + sizeof(requestPart2) - 2)))
        return Check(false, "stream-server-recv");
    static const char expectedRequest[] =
        "GET /file.map HTTP/1.1\r\nHost: loopback\r\n\r\n";
    if (!Check(std::memcmp(receivedRequest, expectedRequest,
                   sizeof(expectedRequest) - 1)
            == 0,
        "stream-request-bytes"))
        return false;

    // While the response has not been sent yet, a read reports WouldBlock
    // rather than stalling or inventing data.
    char probe[16];
    std::uint32_t probeLength = 0;
    if (!Check(Sys_SocketRecvStream(client, probe, sizeof(probe),
                   &probeLength)
            == SysSocketStreamRecvStatus::WouldBlock,
        "stream-recv-wouldblock"))
        return false;

    static const char response[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 8\r\n\r\nPAYLOAD!";
    if (!listener.SendAll(response, static_cast<std::uint32_t>(sizeof(response) - 1)))
        return Check(false, "stream-server-send");
    // Orderly shutdown: the response is fully queued, so closing the peer
    // delivers FIN after the data and the drain below must observe
    // Disconnected once the buffer is exhausted.
    listener.ClosePeer();

    // Read boundary: the first window must carry data exactly as sent,
    // never more. The wait is bounded like every other pump in this
    // harness: a missing window is a contract failure, not a stall.
    char first[8]{};
    std::uint32_t firstLength = 0;
    bool firstReceived = false;
    for (int attempt = 0; attempt < 2000; ++attempt)
    {
        const SysSocketStreamRecvStatus status = Sys_SocketRecvStream(client,
            first, sizeof(first), &firstLength);
        if (status == SysSocketStreamRecvStatus::WouldBlock)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (status != SysSocketStreamRecvStatus::Received)
            return Check(false, "stream-recv-first");
        firstReceived = true;
        break;
    }
    if (!Check(firstReceived, "stream-recv-first-bound"))
        return false;
    Check(firstLength >= 1 && firstLength <= sizeof(first),
        "stream-recv-first");
    Check(std::memcmp(first, response, firstLength) == 0,
        "stream-recv-first");

    // Drain the rest until the peer's orderly shutdown surfaces as
    // Disconnected. Bounded like the first-window pump: after the peer
    // closed, the FIN must surface within the bound or the contract
    // failed.
    bool sawRemainder = false;
    bool sawDisconnect = false;
    for (int attempt = 0; attempt < 2000; ++attempt)
    {
        char chunk[64];
        std::uint32_t chunkLength = 0;
        const SysSocketStreamRecvStatus status = Sys_SocketRecvStream(client,
            chunk, sizeof(chunk), &chunkLength);
        if (status == SysSocketStreamRecvStatus::Received)
        {
            sawRemainder = true;
            continue;
        }
        if (status == SysSocketStreamRecvStatus::WouldBlock)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (status == SysSocketStreamRecvStatus::Disconnected)
        {
            sawDisconnect = true;
            break;
        }
        return Check(false, "stream-recv-disconnect");
    }
    if (!Check(sawDisconnect, "stream-recv-disconnect-bound"))
        return false;
    Check(sawRemainder, "stream-recv-disconnect");

    // The stream is finished; every further read reports Disconnected and
    // the handle stays valid for close.
    char after[8];
    std::uint32_t afterLength = 0;
    Check(Sys_SocketRecvStream(client, after, sizeof(after), &afterLength)
            == SysSocketStreamRecvStatus::Disconnected,
        "stream-recv-after-eof");
    Check(Sys_SocketClose(&client) == SysSocketCloseStatus::Closed,
        "stream-close");
    Check(client == nullptr, "stream-close");
    listener.ClosePeer();
    return true;
}

void StageArgumentContract()
{
    SysSocketHandle socket = nullptr;

    // Open rejects null and non-null-slot out pointers.
    Check(Sys_SocketOpenStream(true, nullptr)
            == SysSocketStreamOpenStatus::InvalidArgument,
        "open-null-out");
    Check(Sys_SocketOpenStream(false, &socket)
            == SysSocketStreamOpenStatus::Opened,
        "open-blocking");
    Check(Sys_SocketOpenStream(true, &socket)
            == SysSocketStreamOpenStatus::InvalidArgument,
        "open-dirty-slot");

    // Connect validates handle and endpoint before any system call.
    Check(Sys_SocketConnectStream(nullptr, nullptr)
            == SysSocketStreamConnectStatus::InvalidArgument,
        "connect-null");
    SysSocketAddress endpoint{};
    Sys_SocketMakeLoopbackAddress(1, &endpoint);
    Check(Sys_SocketConnectStream(socket, nullptr)
            == SysSocketStreamConnectStatus::InvalidArgument,
        "connect-null-endpoint");

    // Poll validates the handle.
    Check(Sys_SocketPollConnected(nullptr)
            == SysSocketStreamPollStatus::InvalidArgument,
        "poll-null");

    // Stream I/O validates every argument: null handle, null buffer,
    // zero length, null out pointers.
    char buffer[8];
    std::uint32_t count = 0;
    Check(Sys_SocketSendStream(nullptr, buffer, sizeof(buffer), &count)
            == SysSocketStreamSendStatus::InvalidArgument,
        "send-null-handle");
    Check(Sys_SocketSendStream(socket, nullptr, sizeof(buffer), &count)
            == SysSocketStreamSendStatus::InvalidArgument,
        "send-null-buffer");
    Check(Sys_SocketSendStream(socket, buffer, 0, &count)
            == SysSocketStreamSendStatus::InvalidArgument,
        "send-zero-length");
    Check(Sys_SocketSendStream(socket, buffer, sizeof(buffer), nullptr)
            == SysSocketStreamSendStatus::InvalidArgument,
        "send-null-out");
    Check(Sys_SocketRecvStream(nullptr, buffer, sizeof(buffer), &count)
            == SysSocketStreamRecvStatus::InvalidArgument,
        "recv-null-handle");
    Check(Sys_SocketRecvStream(socket, nullptr, sizeof(buffer), &count)
            == SysSocketStreamRecvStatus::InvalidArgument,
        "recv-null-buffer");
    Check(Sys_SocketRecvStream(socket, buffer, 0, &count)
            == SysSocketStreamRecvStatus::InvalidArgument,
        "recv-zero-length");
    Check(Sys_SocketRecvStream(socket, buffer, sizeof(buffer), nullptr)
            == SysSocketStreamRecvStatus::InvalidArgument,
        "recv-null-out");
    // A rejected call never publishes partial state.
    Check(count == 0, "send-recv-no-partial");

    Check(Sys_SocketClose(&socket) == SysSocketCloseStatus::Closed,
        "contract-close");
    Check(socket == nullptr, "contract-close");
    // Close is unconditional and idempotent.
    Check(Sys_SocketClose(nullptr) == SysSocketCloseStatus::Closed,
        "contract-close-null");
}

void StageNonBlockingExchange()
{
    LoopbackListener listener;
    if (!Check(listener.Start(), "listener-start"))
        return;
    // Stop is unconditional: a failed exchange must not leak the raw
    // listener descriptors into later stages. Check() already recorded
    // the failing stage tag inside RunNonBlockingExchange.
    RunNonBlockingExchange(listener);
    listener.Stop();
}

void StageBlockingConnect()
{
    LoopbackListener listener;
    if (!Check(listener.Start(), "listener-start-blocking"))
        return;

    SysSocketHandle client = nullptr;
    Check(Sys_SocketOpenStream(false, &client)
            == SysSocketStreamOpenStatus::Opened,
        "open-blocking");
    SysSocketAddress endpoint{};
    Sys_SocketMakeLoopbackAddress(listener.Port(), &endpoint);
    // A blocking connect completes inline or fails; against a listening
    // peer it completes.
    Check(Sys_SocketConnectStream(client, &endpoint)
            == SysSocketStreamConnectStatus::Connected,
        "blocking-connect");
    Check(listener.Accept(), "blocking-accept");

    static const char payload[] = "block";
    std::uint32_t sent = 0;
    Check(Sys_SocketSendStream(client, payload,
              static_cast<std::uint32_t>(sizeof(payload) - 1), &sent)
            == SysSocketStreamSendStatus::Sent,
        "blocking-send");
    Check(sent == sizeof(payload) - 1, "blocking-send");

    char received[8]{};
    Check(listener.RecvAll(received, sizeof(payload) - 1),
        "blocking-server-recv");
    Check(std::memcmp(received, payload, sizeof(payload) - 1) == 0,
        "blocking-payload");

    listener.ClosePeer();
    // Blocking read after orderly shutdown reports Disconnected.
    char after[8];
    std::uint32_t afterLength = 0;
    Check(Sys_SocketRecvStream(client, after, sizeof(after), &afterLength)
            == SysSocketStreamRecvStatus::Disconnected,
        "blocking-recv-disconnect");

    Sys_SocketClose(&client);
    listener.Stop();
}

void StageConnectRefused()
{
    // Bind a listener, learn its ephemeral port, close it, then connect:
    // the poll must surface the refusal as Failed rather than hang.
    std::uint16_t deadPort = 0;
    {
        LoopbackListener listener;
        if (!Check(listener.Start(), "refused-listener"))
            return;
        deadPort = listener.Port();
        listener.Stop();
    }

    SysSocketHandle client = nullptr;
    Check(Sys_SocketOpenStream(true, &client)
            == SysSocketStreamOpenStatus::Opened,
        "refused-open");
    SysSocketAddress endpoint{};
    Sys_SocketMakeLoopbackAddress(deadPort, &endpoint);
    const SysSocketStreamConnectStatus connected =
        Sys_SocketConnectStream(client, &endpoint);
    Check(connected == SysSocketStreamConnectStatus::InProgress
            || connected == SysSocketStreamConnectStatus::SystemFailure,
        "refused-connect");

    if (connected == SysSocketStreamConnectStatus::InProgress)
    {
        // Pump the poll; the refusal lands as Failed within the bound.
        bool refused = false;
        for (int attempt = 0; attempt < 2000; ++attempt)
        {
            const SysSocketStreamPollStatus status =
                Sys_SocketPollConnected(client);
            if (status == SysSocketStreamPollStatus::Failed)
            {
                refused = true;
                break;
            }
            if (status != SysSocketStreamPollStatus::InProgress)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Check(refused, "refused-poll");
    }

    Sys_SocketClose(&client);
}

} // namespace

int main()
{
    StageArgumentContract();
    StageNonBlockingExchange();
    StageBlockingConnect();
    StageConnectRefused();

    if (checkFailed)
    {
        std::printf("platform_socket_stream: stage '%s' failed\n",
            checkStage);
        return 1;
    }
    std::printf("platform_socket_stream: all stages passed\n");
    return 0;
}
