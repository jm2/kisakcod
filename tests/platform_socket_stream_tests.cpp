// SPDX-License-Identifier: GPL-3.0-only
//
// platform_socket_stream_tests.cpp -- the stream contract stages proper,
// composed on top of the shared loopback harness
// (platform_socket_stream_harness.cpp). Covers the connect/poll
// handshake, partial stream sends, read boundaries, orderly shutdown,
// and the fail-closed argument contract. The binary runs the full suite
// with no arguments; each stage names its checks and a failing check
// reports the stage that owned it.

#include "platform_socket_stream_harness.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

namespace
{
// The canned response the listener sends back; the first receive window
// must carry these bytes exactly as sent.
const char kResponse[] =
    "HTTP/1.1 200 OK\r\nContent-Length: 8\r\n\r\nPAYLOAD!";

// Opens the client stream, connects to the listener's loopback
// endpoint, accepts on the raw peer, and pumps the poll until the
// handshake reports Ready.
bool ConnectAndAccept(LoopbackListener &listener,
    SysSocketHandle *const client)
{
    if (!Check(Sys_SocketOpenStream(true, client)
                == SysSocketStreamOpenStatus::Opened,
            "stream-open"))
        return false;

    SysSocketAddress endpoint{};
    Sys_SocketMakeLoopbackAddress(listener.Port(), &endpoint);
    const SysSocketStreamConnectStatus connected =
        Sys_SocketConnectStream(*client, &endpoint);
    if (!Check(connected == SysSocketStreamConnectStatus::InProgress
                || connected == SysSocketStreamConnectStatus::Connected,
            "stream-connect"))
        return false;
    if (!listener.Accept())
        return Check(false, "stream-accept");

    SysSocketStreamPollStatus pollStatus{};
    if (!PollUntilReady(*client, &pollStatus)
        || pollStatus != SysSocketStreamPollStatus::Ready)
        return Check(false, "stream-poll-ready");
    return true;
}

// Partial sends: queue the request in two pieces and let the transport
// report how much moved each time; the listener must receive the exact
// bytes the client queued.
bool ExchangeRequest(LoopbackListener &listener, const SysSocketHandle client)
{
    static const char requestPart1[] = "GET /file.map HTTP/1.1\r\n";
    static const char requestPart2[] = "Host: loopback\r\n\r\n";
    if (!PumpStreamSend(client, requestPart1,
            static_cast<std::uint32_t>(sizeof(requestPart1) - 1),
            "stream-send-part1"))
        return false;
    if (!PumpStreamSend(client, requestPart2,
            static_cast<std::uint32_t>(sizeof(requestPart2) - 1),
            "stream-send-part2"))
        return false;

    char receivedRequest[64]{};
    if (!listener.RecvAll(receivedRequest,
            static_cast<std::uint32_t>(sizeof(requestPart1)
                + sizeof(requestPart2) - 2)))
        return Check(false, "stream-server-recv");
    static const char expectedRequest[] =
        "GET /file.map HTTP/1.1\r\nHost: loopback\r\n\r\n";
    return Check(std::memcmp(receivedRequest, expectedRequest,
                     sizeof(expectedRequest) - 1)
                     == 0,
        "stream-request-bytes");
}

// While the response has not been sent yet, a read reports WouldBlock
// rather than stalling or inventing data. Then the full response is
// queued and the peer performs an orderly shutdown: closing the peer
// delivers FIN after the data.
bool SendResponseAndClosePeer(LoopbackListener &listener,
    const SysSocketHandle client)
{
    char probe[16];
    std::uint32_t probeLength = 0;
    if (!Check(Sys_SocketRecvStream(client, probe, sizeof(probe),
                    &probeLength)
                == SysSocketStreamRecvStatus::WouldBlock,
            "stream-recv-wouldblock"))
        return false;

    if (!listener.SendAll(kResponse,
            static_cast<std::uint32_t>(sizeof(kResponse) - 1)))
        return Check(false, "stream-server-send");
    listener.ClosePeer();
    return true;
}

// Read boundary: the first window must carry data exactly as sent,
// never more.
bool ReceiveFirstWindow(const SysSocketHandle client)
{
    char first[8]{};
    std::uint32_t firstLength = 0;
    if (!PumpFirstRecvWindow(client, first, sizeof(first), &firstLength))
        return false;
    Check(firstLength >= 1 && firstLength <= sizeof(first),
        "stream-recv-first");
    return Check(std::memcmp(first, kResponse, firstLength) == 0,
        "stream-recv-first");
}

// Drain the rest until the peer's orderly shutdown surfaces as
// Disconnected; afterwards every read reports Disconnected and the
// handle stays valid for close.
bool DrainAndFinish(LoopbackListener &listener,
    SysSocketHandle *const client)
{
    bool sawRemainder = false;
    if (!PumpRecvUntilDisconnect(*client, &sawRemainder))
        return false;
    Check(sawRemainder, "stream-recv-disconnect");

    char after[8];
    std::uint32_t afterLength = 0;
    Check(Sys_SocketRecvStream(*client, after, sizeof(after), &afterLength)
            == SysSocketStreamRecvStatus::Disconnected,
        "stream-recv-after-eof");
    Check(Sys_SocketClose(client) == SysSocketCloseStatus::Closed,
        "stream-close");
    Check(*client == nullptr, "stream-close");
    listener.ClosePeer();
    return true;
}

// Drives one full non-blocking exchange: open, connect, poll, partial
// send, request delivery, response receive until WouldBlock, orderly
// shutdown. Returns false at the first broken contract.
bool RunNonBlockingExchange(LoopbackListener &listener)
{
    SysSocketHandle client = nullptr;
    if (!ConnectAndAccept(listener, &client))
        return false;
    if (!ExchangeRequest(listener, client))
        return false;
    if (!SendResponseAndClosePeer(listener, client))
        return false;
    if (!ReceiveFirstWindow(client))
        return false;
    return DrainAndFinish(listener, &client);
}

// Open rejects null and non-null-slot out pointers.
void StageOpenContract(SysSocketHandle *const socket)
{
    Check(Sys_SocketOpenStream(true, nullptr)
            == SysSocketStreamOpenStatus::InvalidArgument,
        "open-null-out");
    Check(Sys_SocketOpenStream(false, socket)
            == SysSocketStreamOpenStatus::Opened,
        "open-blocking");
    Check(Sys_SocketOpenStream(true, socket)
            == SysSocketStreamOpenStatus::InvalidArgument,
        "open-dirty-slot");
}

// Connect validates handle and endpoint before any system call; poll
// validates the handle.
void StageConnectPollContract(const SysSocketHandle socket)
{
    Check(Sys_SocketConnectStream(nullptr, nullptr)
            == SysSocketStreamConnectStatus::InvalidArgument,
        "connect-null");
    SysSocketAddress endpoint{};
    Sys_SocketMakeLoopbackAddress(1, &endpoint);
    Check(Sys_SocketConnectStream(socket, nullptr)
            == SysSocketStreamConnectStatus::InvalidArgument,
        "connect-null-endpoint");

    Check(Sys_SocketPollConnected(nullptr)
            == SysSocketStreamPollStatus::InvalidArgument,
        "poll-null");
}

// Stream I/O validates every argument: null handle, null buffer, zero
// length, null out pointers.
void StageStreamIoContract(const SysSocketHandle socket)
{
    // Zero-initialized: every call below hands the buffer to a
    // contract-rejection path that never reads it, but GCC's
    // -Wmaybe-uninitialized at -O0 cannot see through the call boundary
    // and flags the un-initialized pass-by-pointer as a read.
    char buffer[8]{};
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
}

// Close is unconditional and idempotent.
void StageCloseContract(SysSocketHandle *const socket)
{
    Check(Sys_SocketClose(socket) == SysSocketCloseStatus::Closed,
        "contract-close");
    Check(*socket == nullptr, "contract-close");
    Check(Sys_SocketClose(nullptr) == SysSocketCloseStatus::Closed,
        "contract-close-null");
}

void StageArgumentContract()
{
    SysSocketHandle socket = nullptr;
    StageOpenContract(&socket);
    StageConnectPollContract(socket);
    StageStreamIoContract(socket);
    StageCloseContract(&socket);
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

    if (StreamHarnessFailed())
    {
        std::printf("platform_socket_stream: stage '%s' failed\n",
            StreamHarnessFailedStage());
        return 1;
    }
    std::printf("platform_socket_stream: all stages passed\n");
    return 0;
}
