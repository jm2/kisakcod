// net_local.h: portable datagram network surface for the engine's NET_*/Sys_*
// packet layer. Split out of win32/win_net.h so shared and headless translation
// units no longer pull <winsock.h> (and the whole Win32 socket surface) into
// every composition; see docs/design/PLATFORM_POSIX.md. The implementation
// (net_local.cpp) sits on the qcommon/sys_socket.h UDP service and is common
// to every platform, so the headless dedicated build links one net file on
// Linux, macOS and Windows alike.
//
// Win32-only networking that this header does not cover (the SOCKS relay, the
// IPX-era paths and NET_TCPIPSocket's SOCKET return) stays in win32/win_net.cpp
// for the retail Windows client and non-headless server builds.
#pragma once

#include <universal/q_shared.h>
// Shared TUs (common.cpp, db_registry.cpp, scr_debugger.cpp) include this in
// the SP build too; pick the netchan header the way win32/win_net.h does.
#ifdef KISAK_MP
#include <qcommon/net_chan_mp.h>
#else
#include <qcommon/net_chan.h>
#endif

struct msg_t;

// Opens the datagram layer: registers the net_* latches and binds the UDP
// socket. Safe to call more than once; a second call is a no-op.
void NET_Init(void);

// Closes the datagram layer and releases the UDP socket. Safe to call before
// NET_Init and more than once.
void NET_Shutdown(void);

// Re-reads the net_* latches and rebinds the UDP socket to match.
void NET_Restart(void);

// Enables or disables datagram traffic. Disabling closes the socket; enabling
// rebinds it. The current latch set always wins over the argument when the
// caller's request conflicts with net_noudp.
void NET_Config(bool enableNetworking);

// A portable description of the most recent datagram-layer failure. The
// returned pointer is a static string and stays valid until the next call.
const char *NET_ErrorString(void);

// Sleeps for msec. Kept as the NET_ spelling because callers use it as the
// engine's "yield until the network might have work" primitive, not as a
// general-purpose delay.
void NET_Sleep(int msec);

// Sends one datagram of `length` bytes from `data` to `to`. Returns nonzero on
// acceptance by the transport, zero when the datagram was not queued. A bad
// address type is a programming error and is reported as a fatal engine error,
// matching the retail layer.
char Sys_SendPacket(int length, unsigned __int8 *data, netadr_t to);

// Receives one datagram into `net_message`, filling `net_from` with the sender.
// Returns nonzero when a whole datagram was delivered. Oversize datagrams are
// reported and dropped rather than truncated into a message the parser would
// misread.
qboolean Sys_GetPacket(netadr_t *net_from, msg_t *net_message);

// Receives one broadcast datagram into `net_message`. Returns nonzero when a
// datagram was delivered.
qboolean Sys_GetBroadcastPacket(msg_t *net_message);

// Resolves `s` (a dotted-quad literal or a host name) into `a`. Returns qtrue
// on success and leaves `a` untouched on failure.
qboolean Sys_StringToAdr(const char *s, netadr_t *a);

// LAN classification used for rate limiting and authorization policy. The
// subnet-based variant ignores the local-interface match.
bool Sys_IsLANAddress(netadr_t adr);
bool Sys_IsLANAddress_IgnoreSubnet(netadr_t adr);

// Prints the bound local addresses of the datagram layer.
void Sys_ShowIP(void);
