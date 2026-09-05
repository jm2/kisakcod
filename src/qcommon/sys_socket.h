#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <universal/platform_compat.h>

// Socket service -- portable UDP/IPv4 datagram API. The contract mirrors the
// operations the production network layer needs (open a bound UDP socket,
// send and receive datagrams, enable LAN broadcast, close) without exposing
// any platform-specific type, header, or constant from this header. Each
// backend selects the native primitive (Winsock2 on Win32, BSD sockets on
// POSIX hosts) that implements the documented semantics.
//
// Every function is fail-closed: a null handle or a null out-pointer is
// rejected with the status documented below, and no partial state is
// published on a failure return. Datagrams are delivered whole or not at
// all: the service never reports a partial datagram as Received, and a
// receive buffer smaller than the arriving datagram is reported
// explicitly as Truncated instead of truncating silently.

struct SysSocket;
using SysSocketHandle = SysSocket *;

// A portable IPv4 endpoint. `address` is stored in network byte order
// (address[0] is the most significant byte of the 32-bit address) so the
// value can be compared with memcmp and logged without conversion; `port`
// is stored in host byte order. Callers construct endpoints through the
// Sys_SocketMake* helpers rather than by hand-packing bytes. The header
// deliberately stays data-only: every endpoint comparison goes through
// Sys_SocketAddressIsEqual, whose byte-exact rule lives with the platform
// backends instead of in an inline member here.
//
// Static-analysis note: Codacy's Cppcheck engine reads this header in
// isolation and as C, so it reports the two members below as unused even
// though both platform backends use them throughout; that same isolated C
// parse is what turned every inline comparison shape once hosted here into
// a false MISRA finding. The repo's .codacy.yml excludes this file from
// that engine view for exactly the reason documented here.
struct SysSocketAddress
{
    uint8_t address[4];
    uint16_t port;
};

// Upper bound for one datagram payload. The UDP protocol allows at most
// 65507 user bytes in an IPv4 datagram; the service rejects larger sends
// up front instead of relying on platform-specific truncation.
inline constexpr std::uint32_t SysSocketMaxDatagramBytes = UINT32_C(65507);

enum class SysSocketOpenStatus : std::uint8_t
{
    Opened,
    InvalidArgument,
    SystemFailure,
};

// Close is unconditional and idempotent, so the operation has a single
// outcome; every Sys_SocketClose call reports Closed.
enum class SysSocketCloseStatus : std::uint8_t
{
    Closed,
};

enum class SysSocketSendStatus : std::uint8_t
{
    Sent,
    WouldBlock,
    InvalidArgument,
    MessageTooLarge,
    InvalidHandle,
    SystemFailure,
};

enum class SysSocketRecvStatus : std::uint8_t
{
    Received,

    // The arriving datagram exceeded the caller's buffer: the first
    // bufferCapacity bytes were copied and the excess was discarded. The
    // whole datagram is consumed either way; callers must treat a
    // truncated payload as malformed rather than process it.
    Truncated,
    WouldBlock,
    InvalidArgument,
    InvalidHandle,
    SystemFailure,
};

enum class SysSocketOptionStatus : std::uint8_t
{
    Applied,
    InvalidHandle,
    SystemFailure,
};

// Opens a UDP/IPv4 socket bound to `port` on all local interfaces
// (port 0 selects an ephemeral port). The bind owns a nonzero port
// exclusively: while that socket stays open, another open of the same
// port reports SystemFailure instead of silently sharing the endpoint,
// so datagrams cannot be diverted to a competing socket. When
// `nonBlocking` is true the socket never blocks: Sys_SocketRecvFrom and
// Sys_SocketSendTo report WouldBlock instead of stalling. On success
// *outHandle (which the caller must pass in null) receives a non-null
// handle. The bound endpoint can be recovered with
// Sys_SocketGetLocalAddress.
SysSocketOpenStatus KISAK_CDECL Sys_SocketOpenUdp(
    std::uint16_t port,
    bool nonBlocking,
    SysSocketHandle *outHandle);

// Closes *handle and resets the caller's pointer to null. Close is
// unconditional: a null pointer, a null handle, and an already closed
// handle are all no-ops that report Closed, so callers may close
// unconditionally during teardown.
SysSocketCloseStatus KISAK_CDECL Sys_SocketClose(SysSocketHandle *handle);

// Sends one datagram of `byteCount` bytes from `data` to `destination`.
// A null buffer with a nonzero count, a null destination, or a count above
// SysSocketMaxDatagramBytes is rejected before any system call. WouldBlock
// means the datagram was not queued; callers may retry the same payload.
SysSocketSendStatus KISAK_CDECL Sys_SocketSendTo(
    SysSocketHandle handle,
    const void *data,
    std::uint32_t byteCount,
    const SysSocketAddress *destination);

// Receives one datagram into `buffer`. On Received, *outByteCount holds the
// payload size (never above `bufferCapacity`) and *outSource holds the
// sender's endpoint when the pointer is non-null. Truncated reports a
// datagram larger than `bufferCapacity`: exactly bufferCapacity bytes were
// copied into the buffer, the excess bytes were discarded, and *outByteCount
// equals bufferCapacity; the datagram is consumed on both platforms, so
// callers must treat a truncated payload as malformed and drop it rather
// than process it. WouldBlock means no complete datagram was available.
// SystemFailure reports a non-would-block system error (for example
// receiver buffer exhaustion); the handle stays valid and the caller may
// retry or close.
SysSocketRecvStatus KISAK_CDECL Sys_SocketRecvFrom(
    SysSocketHandle handle,
    void *buffer,
    std::uint32_t bufferCapacity,
    SysSocketAddress *outSource,
    std::uint32_t *outByteCount);

// Enables sending datagrams to broadcast addresses (LAN discovery). The
// option is applied before the first send; it cannot revoke a bind.
SysSocketOptionStatus KISAK_CDECL Sys_SocketEnableBroadcast(
    SysSocketHandle handle);

// Recovers the bound endpoint of `handle` (address and, for an ephemeral
// bind, the actual chosen port). Returns false without touching *outAddress
// unless the endpoint is available.
bool KISAK_CDECL Sys_SocketGetLocalAddress(
    SysSocketHandle handle,
    SysSocketAddress *outAddress);

// Endpoint helpers. Sys_SocketMakeLoopbackAddress builds the IPv4 loopback
// endpoint for `port`; Sys_SocketMakeAnyAddress builds the wildcard
// (0.0.0.0) endpoint. Sys_SocketAddressIsEqual compares two endpoints for
// exact byte and port equality. All three accept null only for the out
// pointer of the makers, which is rejected with false.
bool KISAK_CDECL Sys_SocketMakeLoopbackAddress(
    std::uint16_t port,
    SysSocketAddress *outAddress);

bool KISAK_CDECL Sys_SocketMakeAnyAddress(
    std::uint16_t port,
    SysSocketAddress *outAddress);

bool KISAK_CDECL Sys_SocketAddressIsEqual(
    const SysSocketAddress *first,
    const SysSocketAddress *second);
