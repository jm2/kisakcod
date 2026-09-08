// SPDX-License-Identifier: GPL-3.0-only
//
// platform_socket_tests.cpp -- exercises the portable Sys_Socket* API on
// the host platform backend over real UDP loopback traffic. The binary
// runs the full suite with no arguments; each stage names its checks and
// a failing check reports the stage that owned it.

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <Windows.h>
#else
#include <sys/mman.h>
#endif

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
        && Check(fixture.source.address[0] == 127
                && fixture.source.address[1] == 0
                && fixture.source.address[2] == 0
                && fixture.source.address[3] == 1,
            "source address is loopback")
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

// Truncation contract: a datagram larger than the receive buffer reports
// Truncated (never Received), fills the buffer with the datagram's leading
// bytes, and consumes the whole datagram on both platforms so no partial
// state leaks into the next receive.
bool StageTruncationContract(SocketFixture &fixture)
{
    std::uint8_t oversized[96] = {};
    SeedPayload(oversized, sizeof(oversized));
    if (!Check(Sys_SocketSendTo(fixture.first, oversized,
                    sizeof(oversized), &fixture.loopback)
                == SysSocketSendStatus::Sent,
            "send oversized datagram"))
        return false;

    std::memset(fixture.received, 0, sizeof(fixture.received));
    if (!Check(RecvUntilDeadline(fixture.second, fixture.received,
                   sizeof(fixture.received), &fixture.source,
                   &fixture.receivedBytes)
               == SysSocketRecvStatus::Truncated,
            "oversized datagram reports Truncated"))
        return false;

    if (!Check(fixture.receivedBytes == sizeof(fixture.received),
            "truncated receive fills the buffer")
        || !Check(std::memcmp(oversized, fixture.received,
                  sizeof(fixture.received))
                == 0,
            "truncated receive keeps the leading bytes"))
        return false;

    // The excess bytes were discarded with the datagram, so the receiver
    // is idle again and reports WouldBlock.
    return Check(Sys_SocketRecvFrom(fixture.second, fixture.received,
                     sizeof(fixture.received), nullptr,
                     &fixture.receivedBytes)
                 == SysSocketRecvStatus::WouldBlock,
        "truncated datagram fully consumed");
}

// Reserves `regionBytes` of address space without committing it, so the
// boundary stage can pass a real 2-GiB window to the receive call without
// reserving real memory. The platform receive writes only the arriving
// datagram's bytes, which land in the leading page; Windows therefore gets
// an explicit commit for that page while POSIX backs pages lazily on
// first touch. Returns null when the platform refuses the reservation.
void *ReserveReceiveWindow(const std::uint32_t regionBytes)
{
#if defined(_WIN32)
    void *region = VirtualAlloc(nullptr, regionBytes, MEM_RESERVE,
        PAGE_READWRITE);
    if (!region)
        return nullptr;
    if (!VirtualAlloc(region, 65536, MEM_COMMIT, PAGE_READWRITE))
    {
        VirtualFree(region, 0, MEM_RELEASE);
        return nullptr;
    }
    return region;
#else
    void *region = mmap(nullptr, static_cast<std::size_t>(regionBytes),
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (region == MAP_FAILED)
        return nullptr;
    return region;
#endif
}

void ReleaseReceiveWindow(void *const region,
    const std::uint32_t regionBytes)
{
#if defined(_WIN32)
    (void)regionBytes;
    VirtualFree(region, 0, MEM_RELEASE);
#else
    munmap(region, static_cast<std::size_t>(regionBytes));
#endif
}

// Receive-capacity boundary regression: the portable API takes a uint32
// capacity while the native receive primitives take a signed (Winsock) or
// size_t (POSIX) length. A direct conversion of a capacity at or above
// 2^31 once wrapped negative on Winsock and turned a valid reserved
// receive window into a failed call. The backend clamps the capacity to
// the datagram bound before the signed conversion; this stage drives a
// real loopback datagram through a reserved 2-GiB window on the native
// platform, so the hosted Windows runners validate the Winsock boundary
// natively and the Linux run guards the portable contract.
bool StageOversizeCapacityBoundary(SocketFixture &fixture)
{
    constexpr std::uint32_t boundaryCapacity = UINT32_C(0x80000000);
    void *window = ReserveReceiveWindow(boundaryCapacity);
    if (!Check(window != nullptr, "reserve the 2 GiB receive window"))
        return false;

    std::uint8_t probe[40] = {};
    SeedPayload(probe, sizeof(probe));
    const bool sent = Check(Sys_SocketSendTo(fixture.first, probe,
            sizeof(probe), &fixture.loopback)
                == SysSocketSendStatus::Sent,
        "send into the receive window");
    std::uint32_t receivedBytes = 0;
    const SysSocketRecvStatus status = RecvUntilDeadline(fixture.second,
        window, boundaryCapacity, nullptr, &receivedBytes);
    const bool received =
        Check(status == SysSocketRecvStatus::Received,
            "oversize capacity receives the datagram")
        && Check(receivedBytes == sizeof(probe),
            "oversize capacity receive size intact")
        && Check(std::memcmp(probe, window, sizeof(probe)) == 0,
            "oversize capacity receive bytes intact");
    ReleaseReceiveWindow(window, boundaryCapacity);
    return sent && received;
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

// Explicit bind: the requested port is honored and recovered. Widely
// separated high ports are tried so one busy endpoint cannot hide a
// regression; exhausting every candidate fails the stage instead of
// skipping it, preserving detection of explicit-bind breakage.
bool StageExplicitBind(SocketFixture &)
{
    const std::uint16_t candidatePorts[] = {43191, 47441, 51691, 55941};
    for (std::size_t index = 0;
         index < sizeof(candidatePorts) / sizeof(candidatePorts[0]);
         ++index)
    {
        const std::uint16_t requestedPort = candidatePorts[index];
        SysSocketHandle bound = nullptr;
        SysSocketAddress boundAddress{};
        const SysSocketOpenStatus openStatus =
            Sys_SocketOpenUdp(requestedPort, true, &bound);
        if (openStatus == SysSocketOpenStatus::SystemFailure)
            continue; // candidate port busy; try the next one
        if (!Check(openStatus == SysSocketOpenStatus::Opened,
                "explicit bind opened")
            || !Check(Sys_SocketGetLocalAddress(bound, &boundAddress),
                "explicit bind recovered")
            || !Check(boundAddress.port == requestedPort,
                "explicit bind port honored"))
            return false;
        return Check(Sys_SocketClose(&bound) ==
                     SysSocketCloseStatus::Closed,
            "explicit bind closed");
    }
    return Check(false,
        "explicit bind opened (no candidate high port available)");
}

// Exclusive bind ownership: while a nonzero port is held open, a second
// open of the same endpoint reports SystemFailure and publishes no
// handle, so datagrams for the held port cannot be diverted to a
// competing socket. Widely separated candidate ports keep the stage
// stable when an unrelated service already owns one of them.
bool StageExclusiveBind(SocketFixture &)
{
    const std::uint16_t candidatePorts[] = {43213, 47461, 51713, 55963};
    for (std::size_t index = 0;
         index < sizeof(candidatePorts) / sizeof(candidatePorts[0]);
         ++index)
    {
        const std::uint16_t requestedPort = candidatePorts[index];
        SysSocketHandle held = nullptr;
        const SysSocketOpenStatus heldStatus =
            Sys_SocketOpenUdp(requestedPort, true, &held);
        if (heldStatus == SysSocketOpenStatus::SystemFailure)
            continue; // candidate port owned elsewhere; try the next one
        if (!Check(heldStatus == SysSocketOpenStatus::Opened,
                "exclusive bind opened")
            || !Check(held != nullptr, "exclusive bind handle published"))
            return false;
        SysSocketHandle second = nullptr;
        const SysSocketOpenStatus secondStatus =
            Sys_SocketOpenUdp(requestedPort, true, &second);
        const bool closed = Sys_SocketClose(&held) ==
                SysSocketCloseStatus::Closed
            && held == nullptr;
        if (!Check(secondStatus == SysSocketOpenStatus::SystemFailure,
                "second open of a held port reports SystemFailure")
            || !Check(second == nullptr,
                "failed second open publishes no handle")
            || !Check(closed, "exclusive bind closed"))
            return false;
        return true;
    }
    return Check(false,
        "exclusive bind (no candidate high port available)");
}

#if defined(_WIN32)
// Two wrapper calls only compare wildcard binds. A native competitor bound
// specifically to loopback must also be rejected, with or without reuse.
bool RejectSpecificCompetitor(const std::uint16_t port, const bool reuse)
{
    const SOCKET competing = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (!Check(competing != INVALID_SOCKET, "native competitor opened"))
        return false;
    const BOOL enable = TRUE;
    if (reuse
        && setsockopt(competing, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char *>(&enable), sizeof(enable)) != 0)
    {
        closesocket(competing);
        return Check(false, "native competitor reuse option");
    }

    sockaddr_in specific{};
    specific.sin_family = AF_INET;
    specific.sin_port = htons(port);
    specific.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int result = bind(competing,
        reinterpret_cast<const sockaddr *>(&specific), sizeof(specific));
    const int error = result == SOCKET_ERROR ? WSAGetLastError() : 0;
    const bool closed = closesocket(competing) == 0;
    return Check(result == SOCKET_ERROR
                     && (error == WSAEACCES || error == WSAEADDRINUSE),
               "exclusive bind rejects specific competitor")
        && Check(closed, "native competitor closed");
}

bool StageExclusiveInterfaceBind(SocketFixture &fixture)
{
    return RejectSpecificCompetitor(fixture.firstAddress.port, false)
        && RejectSpecificCompetitor(fixture.firstAddress.port, true);
}
#endif

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
               SysSocketCloseStatus::Closed,
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
        &StageLoopbackSend, &StageLoopbackReply, &StageTruncationContract,
        &StageOversizeCapacityBoundary, &StageBroadcastOption,
        &StageExplicitBind, &StageExclusiveBind,
#if defined(_WIN32)
        &StageExclusiveInterfaceBind,
#endif
        &StageTeardown};

    for (std::size_t index = 0; index < sizeof(stages) / sizeof(stages[0]);
         ++index)
    {
        if (!stages[index](fixture))
            return ReportFailure();
    }

    std::printf("platform-socket: all checks passed\n");
    return EXIT_SUCCESS;
}
