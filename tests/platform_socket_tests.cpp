// SPDX-License-Identifier: GPL-3.0-only
//
// platform_socket_tests.cpp -- exercises the portable Sys_Socket* API on
// the host platform backend over real UDP loopback traffic. The binary
// runs the full suite with no arguments; each stage names its checks and
// a failing check reports the stage that owned it.

#include <qcommon/sys_socket.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

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

// Shared per-run state: two nonblocking sockets, their bound endpoints, and
// scratch buffers for datagram round-trips.
struct SocketFixture
{
    SysSocketHandle first = nullptr;
    SysSocketHandle second = nullptr;
    SysSocketAddress firstAddress{};
    SysSocketAddress secondAddress{};
    SysSocketAddress loopback{};
    SysSocketAddress source{};
    std::uint8_t payload[64] = {};
    std::uint8_t received[64] = {};
    std::uint32_t receivedBytes = 0;
};

using StageFn = bool (*)(SocketFixture &);

void SeedPayload(std::uint8_t *payload, const std::size_t size)
{
    for (std::size_t index = 0; index < size; ++index)
        payload[index] = static_cast<std::uint8_t>(index * 5 + 1);
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

int ReportFailure()
{
    std::fprintf(stderr, "platform-socket: %s failed\n", checkStage);
    return EXIT_FAILURE;
}

// UDP loopback delivery can lag the sender's return, so a nonblocking
// receive may transiently report WouldBlock. Poll until a terminal status
// (anything but WouldBlock) or the bounded deadline expires; a WouldBlock
// return after expiry preserves the caller's failure handling.
SysSocketRecvStatus RecvUntilDeadline(SysSocketHandle handle,
    void *const buffer, const std::uint32_t capacity,
    SysSocketAddress *const source, std::uint32_t *const outByteCount)
{
    for (int attempt = 0; attempt < 500; ++attempt)
    {
        const SysSocketRecvStatus status = Sys_SocketRecvFrom(handle,
            buffer, capacity, source, outByteCount);
        if (status != SysSocketRecvStatus::WouldBlock)
            return status;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return SysSocketRecvStatus::WouldBlock;
}

// Argument validation: null out-pointers and pre-set handles are rejected
// without touching system state.
bool StageArgumentValidation(SocketFixture &)
{
    SysSocketAddress address{};
    if (!Check(Sys_SocketOpenUdp(0, true, nullptr) ==
               SysSocketOpenStatus::InvalidArgument,
            "open null out pointer"))
        return false;
    SysSocketHandle preset = reinterpret_cast<SysSocketHandle>(
        static_cast<std::uintptr_t>(1));
    if (!Check(Sys_SocketOpenUdp(0, true, &preset) ==
               SysSocketOpenStatus::InvalidArgument,
            "open preset handle"))
        return false;
    return Check(Sys_SocketGetLocalAddress(nullptr, &address) == false,
            "local address null handle")
        && Check(Sys_SocketGetLocalAddress(nullptr, nullptr) == false,
            "local address null out");
}

// Endpoint helper contract: loopback and wildcard endpoints agree with the
// bound sockets and compare by exact byte and port equality.
bool StageEndpointContract(SocketFixture &fixture)
{
    SysSocketAddress loopback{};
    SysSocketAddress anyProbe{};
    if (!Check(Sys_SocketMakeLoopbackAddress(
                   fixture.secondAddress.port, &loopback),
            "make loopback endpoint")
        || !Check(fixture.secondAddress.address[0] == 0
                && fixture.secondAddress.address[1] == 0
                && fixture.secondAddress.address[2] == 0
                && fixture.secondAddress.address[3] == 0,
            "wildcard bind exposes the any address")
        || !Check(Sys_SocketMakeAnyAddress(fixture.secondAddress.port,
                      &anyProbe)
                && Sys_SocketAddressIsEqual(&anyProbe,
                    &fixture.secondAddress),
            "any endpoint matches the wildcard bind")
        || !Check(!Sys_SocketAddressIsEqual(&loopback,
                &fixture.secondAddress),
            "loopback differs from the wildcard endpoint")
        || !Check(Sys_SocketAddressIsEqual(&loopback, &loopback),
            "endpoint equality is reflexive"))
    {
        return false;
    }
    fixture.loopback = loopback;
    return true;
}

// Receive contract: an idle nonblocking socket reports WouldBlock and
// invalid receives are rejected before any system call.
bool StageReceiveContract(SocketFixture &fixture)
{
    if (!Check(Sys_SocketRecvFrom(fixture.second, fixture.received,
                   sizeof(fixture.received), nullptr,
                   &fixture.receivedBytes)
                == SysSocketRecvStatus::WouldBlock,
            "idle recv reports WouldBlock"))
        return false;

    return Check(Sys_SocketRecvFrom(nullptr, fixture.received,
                     sizeof(fixture.received), nullptr,
                     &fixture.receivedBytes)
                == SysSocketRecvStatus::InvalidArgument,
        "recv null handle")
        && Check(Sys_SocketRecvFrom(fixture.second, nullptr,
                     sizeof(fixture.received), nullptr,
                     &fixture.receivedBytes)
                == SysSocketRecvStatus::InvalidArgument,
            "recv null buffer")
        && Check(Sys_SocketRecvFrom(fixture.second, fixture.received, 0,
                     nullptr, &fixture.receivedBytes)
                == SysSocketRecvStatus::InvalidArgument,
            "recv zero capacity")
        && Check(Sys_SocketRecvFrom(fixture.second, fixture.received,
                     sizeof(fixture.received), nullptr, nullptr)
                == SysSocketRecvStatus::InvalidArgument,
            "recv null byte count");
}

// Send contract: invalid sends are rejected before any system call and an
// oversize datagram is reported rather than attempted.
bool StageSendContract(SocketFixture &fixture)
{
    SysSocketAddress any{};
    if (!Check(Sys_SocketMakeAnyAddress(1, &any), "make any endpoint"))
        return false;
    return Check(Sys_SocketSendTo(nullptr, fixture.payload, 1,
                     &fixture.loopback)
                == SysSocketSendStatus::InvalidArgument,
        "send null handle")
        && Check(Sys_SocketSendTo(fixture.first, nullptr, 1,
                     &fixture.loopback)
                == SysSocketSendStatus::InvalidArgument,
            "send null buffer")
        && Check(Sys_SocketSendTo(fixture.first, fixture.payload, 0,
                     &fixture.loopback)
                == SysSocketSendStatus::InvalidArgument,
            "send zero length")
        && Check(Sys_SocketSendTo(fixture.first, fixture.payload,
                     SysSocketMaxDatagramBytes + 1, &fixture.loopback)
                == SysSocketSendStatus::MessageTooLarge,
            "send oversize datagram")
        && Check(Sys_SocketSendTo(fixture.first, fixture.payload, 8,
                     nullptr)
                == SysSocketSendStatus::InvalidArgument,
            "send null destination");
}

// first -> second datagram with sender endpoint recovery.
bool StageLoopbackSend(SocketFixture &fixture)
{
    if (!Check(Sys_SocketSendTo(fixture.first, fixture.payload,
                   sizeof(fixture.payload), &fixture.loopback)
                == SysSocketSendStatus::Sent,
            "send first to second"))
        return false;

    std::memset(fixture.received, 0, sizeof(fixture.received));
    return Check(RecvUntilDeadline(fixture.second, fixture.received,
                     sizeof(fixture.received), &fixture.source,
                     &fixture.receivedBytes)
                 == SysSocketRecvStatus::Received,
        "recv on second")
        && Check(fixture.receivedBytes == sizeof(fixture.payload),
            "payload size round-trip")
        && Check(std::memcmp(fixture.payload, fixture.received,
                 sizeof(fixture.payload))
                == 0,
            "payload bytes round-trip")
        && Check(fixture.source.port == fixture.firstAddress.port,
            "source port round-trip");
}

// second -> first reply using the recovered source endpoint verbatim.
bool StageLoopbackReply(SocketFixture &fixture)
{
    for (std::size_t index = 0; index < sizeof(fixture.payload); ++index)
        fixture.payload[index] = static_cast<std::uint8_t>(255 - index);
    if (!Check(Sys_SocketSendTo(fixture.second, fixture.payload,
                   sizeof(fixture.payload), &fixture.source)
                == SysSocketSendStatus::Sent,
            "send second to first"))
        return false;

    std::memset(fixture.received, 0, sizeof(fixture.received));
    return Check(RecvUntilDeadline(fixture.first, fixture.received,
                     sizeof(fixture.received), nullptr,
                     &fixture.receivedBytes)
                 == SysSocketRecvStatus::Received,
        "recv on first")
        && Check(fixture.receivedBytes == sizeof(fixture.payload),
            "reply size round-trip")
        && Check(std::memcmp(fixture.payload, fixture.received,
                 sizeof(fixture.payload))
                == 0,
            "reply bytes round-trip");
}

// Broadcast option applies on both backends and rejects bad handles.
bool StageBroadcastOption(SocketFixture &fixture)
{
    return Check(Sys_SocketEnableBroadcast(fixture.first) ==
               SysSocketOptionStatus::Applied,
        "enable broadcast")
        && Check(Sys_SocketEnableBroadcast(nullptr) ==
               SysSocketOptionStatus::InvalidHandle,
            "broadcast null handle");
}

// Explicit bind: the requested port is honored and recovered.
bool StageExplicitBind(SocketFixture &)
{
    SysSocketHandle bound = nullptr;
    SysSocketAddress boundAddress{};
    // Choose an unlikely high port; failure to bind a specific busy port
    // is reported as SystemFailure, which this suite treats as skippable
    // rather than fatal.
    const std::uint16_t requestedPort = 43191;
    const SysSocketOpenStatus openStatus =
        Sys_SocketOpenUdp(requestedPort, true, &bound);
    if (openStatus == SysSocketOpenStatus::SystemFailure)
    {
        std::fprintf(stderr, "platform-socket: explicit bind port %u busy;"
                             " skipping\n",
            static_cast<unsigned>(requestedPort));
        return true;
    }
    if (!Check(openStatus == SysSocketOpenStatus::Opened,
            "explicit bind opened")
        || !Check(Sys_SocketGetLocalAddress(bound, &boundAddress),
            "explicit bind recovered")
        || !Check(boundAddress.port == requestedPort,
            "explicit bind port honored"))
        return false;
    return Check(Sys_SocketClose(&bound) == SysSocketCloseStatus::Closed,
        "explicit bind closed");
}

// Teardown: close is unconditional, nulls the caller's handle, and a
// second close is a no-op.
bool StageTeardown(SocketFixture &fixture)
{
    SysSocketHandle &first = fixture.first;
    SysSocketHandle &second = fixture.second;
    return Check(Sys_SocketClose(&first) == SysSocketCloseStatus::Closed
            && first == nullptr,
        "close first socket")
        && Check(Sys_SocketClose(&first) == SysSocketCloseStatus::Closed,
            "double close is a no-op")
        && Check(Sys_SocketClose(&second) == SysSocketCloseStatus::Closed
            && second == nullptr,
            "close second socket")
        && Check(Sys_SocketClose(nullptr) ==
               SysSocketCloseStatus::InvalidHandle,
            "close null pointer");
}
} // namespace

int main()
{
    SocketFixture fixture{};
    SeedPayload(fixture.payload, sizeof(fixture.payload));

    if (!OpenEphemeral(true, &fixture.first, &fixture.firstAddress,
            "open first socket")
        || !OpenEphemeral(true, &fixture.second, &fixture.secondAddress,
            "open second socket"))
        return ReportFailure();

    const StageFn stages[] = {&StageArgumentValidation,
        &StageEndpointContract, &StageReceiveContract, &StageSendContract,
        &StageLoopbackSend, &StageLoopbackReply, &StageBroadcastOption,
        &StageExplicitBind, &StageTeardown};

    for (std::size_t index = 0; index < sizeof(stages) / sizeof(stages[0]);
         ++index)
    {
        if (!stages[index](fixture))
            return ReportFailure();
    }

    std::printf("platform-socket: all checks passed\n");
    return EXIT_SUCCESS;
}
