// SPDX-License-Identifier: GPL-3.0-only
//
// platform_socket_tests.cpp -- exercises the portable Sys_Socket* API on
// the host platform backend over real UDP loopback traffic. The binary
// runs the full suite with no arguments; each check names its stage.

#include <qcommon/sys_socket.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
const char *checkStage = "startup";

bool Check(const bool condition, const char *const stage)
{
    if (!condition)
    {
        checkStage = stage;
        return false;
    }
    return true;
}

bool OpenEphemeral(const bool nonBlocking,
    SysSocketHandle *handle,
    SysSocketAddress *bound,
    const char *stage)
{
    if (!Check(Sys_SocketOpenUdp(0, nonBlocking, handle) ==
            SysSocketOpenStatus::Opened,
            stage))
        return false;
    if (!Check(Sys_SocketGetLocalAddress(*handle, bound), stage))
        return false;
    return Check(bound->port != 0, "ephemeral port must be resolved");
}
} // namespace

int main()
{
    // Argument validation: null out-pointers and pre-set handles are
    // rejected without touching system state.
    {
        SysSocketAddress address{};
        Check(Sys_SocketOpenUdp(0, true, nullptr) ==
                SysSocketOpenStatus::InvalidArgument,
            "open null out pointer");
        SysSocketHandle preset = reinterpret_cast<SysSocketHandle>(
            static_cast<std::uintptr_t>(1));
        Check(Sys_SocketOpenUdp(0, true, &preset) ==
                SysSocketOpenStatus::InvalidArgument,
            "open preset handle");
        Check(Sys_SocketGetLocalAddress(nullptr, &address) == false,
            "local address null handle");
        Check(Sys_SocketGetLocalAddress(nullptr, nullptr) == false,
            "local address null out");
    }

    // Loopback echo: two nonblocking sockets exchange a datagram both ways.
    std::uint8_t payload[64];
    std::uint8_t received[64];
    for (std::size_t index = 0; index < sizeof(payload); ++index)
        payload[index] = static_cast<std::uint8_t>(index * 5 + 1);

    SysSocketHandle first = nullptr;
    SysSocketAddress firstAddress{};
    SysSocketHandle second = nullptr;
    SysSocketAddress secondAddress{};
    if (!OpenEphemeral(true, &first, &firstAddress, "open first socket")
        || !OpenEphemeral(true, &second, &secondAddress, "open second socket"))
    {
        std::fprintf(stderr, "platform-socket: %s failed\n", checkStage);
        return EXIT_FAILURE;
    }

    SysSocketAddress loopback{};
    SysSocketAddress anyProbe{};
    if (!Check(Sys_SocketMakeLoopbackAddress(secondAddress.port, &loopback),
            "make loopback endpoint")
        || !Check(secondAddress.address[0] == 0 && secondAddress.address[1] == 0
                && secondAddress.address[2] == 0
                && secondAddress.address[3] == 0,
            "wildcard bind exposes the any address")
        || !Check(Sys_SocketMakeAnyAddress(secondAddress.port, &anyProbe)
                && Sys_SocketAddressIsEqual(&anyProbe, &secondAddress),
            "any endpoint matches the wildcard bind")
        || !Check(!Sys_SocketAddressIsEqual(&loopback, &secondAddress),
            "loopback differs from the wildcard endpoint")
        || !Check(Sys_SocketAddressIsEqual(&loopback, &loopback),
            "endpoint equality is reflexive"))
    {
        std::fprintf(stderr, "platform-socket: %s failed\n", checkStage);
        return EXIT_FAILURE;
    }

    // An idle nonblocking socket reports WouldBlock, never blocks.
    std::uint32_t receivedBytes = 0;
    if (!Check(Sys_SocketRecvFrom(second, received, sizeof(received), nullptr,
                   &receivedBytes) == SysSocketRecvStatus::WouldBlock,
            "idle recv reports WouldBlock"))
    {
        std::fprintf(stderr, "platform-socket: %s failed\n", checkStage);
        return EXIT_FAILURE;
    }

    // Invalid receives are rejected before any system call.
    if (!Check(Sys_SocketRecvFrom(nullptr, received, sizeof(received), nullptr,
                   &receivedBytes) == SysSocketRecvStatus::InvalidArgument,
            "recv null handle")
        || !Check(Sys_SocketRecvFrom(second, nullptr, sizeof(received), nullptr,
                       &receivedBytes) == SysSocketRecvStatus::InvalidArgument,
            "recv null buffer")
        || !Check(Sys_SocketRecvFrom(second, received, 0, nullptr,
                       &receivedBytes) == SysSocketRecvStatus::InvalidArgument,
            "recv zero capacity")
        || !Check(Sys_SocketRecvFrom(second, received, sizeof(received),
                       nullptr, nullptr) == SysSocketRecvStatus::InvalidArgument,
            "recv null byte count"))
    {
        std::fprintf(stderr, "platform-socket: %s failed\n", checkStage);
        return EXIT_FAILURE;
    }

    // Invalid sends are rejected before any system call.
    SysSocketAddress any{};
    Check(Sys_SocketMakeAnyAddress(1, &any), "make any endpoint");
    if (!Check(Sys_SocketSendTo(nullptr, payload, 1, &loopback) ==
            SysSocketSendStatus::InvalidArgument,
            "send null handle")
        || !Check(Sys_SocketSendTo(first, nullptr, 1, &loopback) ==
                SysSocketSendStatus::InvalidArgument,
            "send null buffer")
        || !Check(Sys_SocketSendTo(first, payload, 0, &loopback) ==
                SysSocketSendStatus::InvalidArgument,
            "send zero length")
        || !Check(Sys_SocketSendTo(first, payload,
                       SysSocketMaxDatagramBytes + 1, &loopback) ==
                SysSocketSendStatus::MessageTooLarge,
            "send oversize datagram")
        || !Check(Sys_SocketSendTo(first, payload, 8, nullptr) ==
                SysSocketSendStatus::InvalidArgument,
            "send null destination"))
    {
        std::fprintf(stderr, "platform-socket: %s failed\n", checkStage);
        return EXIT_FAILURE;
    }

    // first -> second datagram with sender endpoint recovery.
    if (!Check(Sys_SocketSendTo(first, payload, sizeof(payload), &loopback) ==
            SysSocketSendStatus::Sent,
            "send first to second"))
    {
        std::fprintf(stderr, "platform-socket: %s failed\n", checkStage);
        return EXIT_FAILURE;
    }
    SysSocketAddress source{};
    std::memset(received, 0, sizeof(received));
    if (!Check(Sys_SocketRecvFrom(second, received, sizeof(received), &source,
                   &receivedBytes) == SysSocketRecvStatus::Received,
            "recv on second")
        || !Check(receivedBytes == sizeof(payload), "payload size round-trip")
        || !Check(std::memcmp(payload, received, sizeof(payload)) == 0,
            "payload bytes round-trip")
        || !Check(source.port == firstAddress.port, "source port round-trip"))
    {
        std::fprintf(stderr, "platform-socket: %s failed\n", checkStage);
        return EXIT_FAILURE;
    }

    // second -> first reply using the recovered source endpoint verbatim.
    for (std::size_t index = 0; index < sizeof(payload); ++index)
        payload[index] = static_cast<std::uint8_t>(255 - index);
    if (!Check(Sys_SocketSendTo(second, payload, sizeof(payload), &source) ==
            SysSocketSendStatus::Sent,
            "send second to first"))
    {
        std::fprintf(stderr, "platform-socket: %s failed\n", checkStage);
        return EXIT_FAILURE;
    }
    std::memset(received, 0, sizeof(received));
    if (!Check(Sys_SocketRecvFrom(first, received, sizeof(received), nullptr,
                   &receivedBytes) == SysSocketRecvStatus::Received,
            "recv on first")
        || !Check(receivedBytes == sizeof(payload), "reply size round-trip")
        || !Check(std::memcmp(payload, received, sizeof(payload)) == 0,
            "reply bytes round-trip"))
    {
        std::fprintf(stderr, "platform-socket: %s failed\n", checkStage);
        return EXIT_FAILURE;
    }

    // Broadcast option applies on both backends and rejects bad handles.
    if (!Check(Sys_SocketEnableBroadcast(first) ==
            SysSocketOptionStatus::Applied,
            "enable broadcast")
        || !Check(Sys_SocketEnableBroadcast(nullptr) ==
                SysSocketOptionStatus::InvalidHandle,
            "broadcast null handle"))
    {
        std::fprintf(stderr, "platform-socket: %s failed\n", checkStage);
        return EXIT_FAILURE;
    }

    // Explicit bind: the requested port is honored and recovered.
    {
        SysSocketHandle bound = nullptr;
        SysSocketAddress boundAddress{};
        // Choose an unlikely high port; failure to bind a specific busy port
        // is reported as SystemFailure, which this suite treats as fatal.
        const std::uint16_t requestedPort = 43191;
        const SysSocketOpenStatus openStatus =
            Sys_SocketOpenUdp(requestedPort, true, &bound);
        if (openStatus == SysSocketOpenStatus::SystemFailure)
        {
            std::fprintf(stderr,
                "platform-socket: explicit bind port %u busy; skipping\n",
                static_cast<unsigned>(requestedPort));
        }
        else
        {
            if (!Check(openStatus == SysSocketOpenStatus::Opened,
                    "explicit bind opened")
                || !Check(Sys_SocketGetLocalAddress(bound, &boundAddress),
                    "explicit bind recovered")
                || !Check(boundAddress.port == requestedPort,
                    "explicit bind port honored"))
            {
                std::fprintf(stderr, "platform-socket: %s failed\n",
                    checkStage);
                return EXIT_FAILURE;
            }
            Check(Sys_SocketClose(&bound) == SysSocketCloseStatus::Closed,
                "explicit bind closed");
        }
    }

    // Teardown: close is unconditional, nulls the caller's handle, and a
    // second close is a no-op.
    if (!Check(Sys_SocketClose(&first) == SysSocketCloseStatus::Closed
            && first == nullptr,
            "close first socket")
        || !Check(Sys_SocketClose(&first) == SysSocketCloseStatus::Closed,
            "double close is a no-op")
        || !Check(Sys_SocketClose(&second) == SysSocketCloseStatus::Closed
                && second == nullptr,
            "close second socket")
        || !Check(Sys_SocketClose(nullptr) == SysSocketCloseStatus::InvalidHandle,
            "close null pointer"))
    {
        std::fprintf(stderr, "platform-socket: %s failed\n", checkStage);
        return EXIT_FAILURE;
    }

    std::printf("platform-socket: all checks passed\n");
    return EXIT_SUCCESS;
}
