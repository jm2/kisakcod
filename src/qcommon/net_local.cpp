// net_local.cpp: the portable datagram layer for the engine's NET_*/Sys_*
// packet surface. One file serves every platform's headless dedicated build:
// it talks to the qcommon/sys_socket.h UDP service, whose backends are
// Winsock2 on Windows and BSD sockets on POSIX hosts, so no platform socket
// type, header or constant appears here. See docs/design/PLATFORM_POSIX.md.
//
// What is deliberately not here: the SOCKS relay, the IPX-era address types and
// the TCP helper that returns a raw SOCKET. Those are retail Win32 networking
// concerns and stay in win32/win_net.cpp for the Windows client and non-headless
// server builds.

#include <qcommon/net_local.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <qcommon/msg_mp.h>
#include <qcommon/qcommon.h>
#include <qcommon/sys_console.h>
#include <qcommon/sys_socket.h>
#include <qcommon/sys_time.h>

namespace
{
// The bound game socket and the optional broadcast socket. A null handle means
// the layer is closed; Sys_SocketClose treats a null pointer as a no-op, so
// teardown can call it unconditionally.
SysSocketHandle gameSocket = nullptr;
SysSocketHandle broadcastSocket = nullptr;

bool networkingEnabled = false;
bool initialized = false;
int localPort = 0;

const dvar_t *net_noudp = nullptr;
const dvar_t *net_ip = nullptr;
const dvar_t *net_port = nullptr;

// The single static error buffer NET_ErrorString hands out. Kept as a plain
// literal table so a failure never allocates on the error path.
const char *lastError = "no error";

void SetError(const char *message)
{
    lastError = message ? message : "unknown error";
}

void NetadrFromSocketAddress(const SysSocketAddress &source, netadr_t *out)
{
    std::memset(out, 0, sizeof(*out));
    out->type = NA_IP;
    std::memcpy(out->ip, source.address, sizeof(out->ip));
    // netadr_t::port is stored in network byte order (it is a raw sin_port);
    // SysSocketAddress::port is host order.
    out->port = static_cast<unsigned short>(BigShort(static_cast<short>(source.port)));
}

bool SocketAddressFromNetadr(const netadr_t &in, SysSocketAddress *out)
{
    std::memset(out, 0, sizeof(*out));
    switch (in.type)
    {
    case NA_IP:
        std::memcpy(out->address, in.ip, sizeof(out->address));
        break;
    case NA_BROADCAST:
        out->address[0] = 255;
        out->address[1] = 255;
        out->address[2] = 255;
        out->address[3] = 255;
        break;
    default:
        return false;
    }
    out->port = static_cast<std::uint16_t>(BigShort(static_cast<short>(in.port)));
    return true;
}

void CloseSockets()
{
    (void)Sys_SocketClose(&gameSocket);
    (void)Sys_SocketClose(&broadcastSocket);
}

// Retail NET_IPSocket binds every interface when net_ip is empty or
// "localhost" (any case), and only the named interface otherwise.
bool IsWildcardInterface(const char *ip)
{
    if (!ip || !*ip)
        return true;
    static const char localhost[] = "localhost";
    for (std::size_t i = 0; i < sizeof(localhost); ++i)
    {
        char c = ip[i];
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
        if (c != localhost[i])
            return false;
    }
    return true;
}

// Binds the game socket to the latched net_ip/net_port, and a broadcast socket when
// the address is a wildcard bind so LAN discovery can hear replies. A failure
// leaves both handles null rather than half-configured.
bool OpenSockets()
{
    CloseSockets();

    const int port = net_port ? net_port->current.integer : 0;
    if (port < 0 || port > 65535)
    {
        SetError("net_port is out of range");
        return false;
    }

    const char *const ip = net_ip ? net_ip->current.string : nullptr;
    SysSocketOpenStatus openStatus;
    if (IsWildcardInterface(ip))
    {
        Com_Printf(16, "Opening IP socket: localhost:%i\n", port);
        openStatus = Sys_SocketOpenUdp(static_cast<std::uint16_t>(port), true, &gameSocket);
    }
    else
    {
        SysSocketAddress local{};
        if (Sys_SocketResolveHost(ip, static_cast<std::uint16_t>(port), &local) != SysSocketResolveStatus::Resolved)
        {
            SetError("could not resolve net_ip");
            return false;
        }
        Com_Printf(16, "Opening IP socket: %s:%i\n", ip, port);
        openStatus = Sys_SocketOpenUdpAt(&local, true, &gameSocket);
    }
    if (openStatus != SysSocketOpenStatus::Opened)
    {
        gameSocket = nullptr;
        SetError("could not bind the game socket");
        return false;
    }

    // The broadcast socket is best effort: LAN discovery is optional and a
    // host that refuses a second bind still runs the server.
    openStatus = Sys_SocketOpenUdp(0, true, &broadcastSocket);
    if (openStatus != SysSocketOpenStatus::Opened)
    {
        broadcastSocket = nullptr;
    }
    else if (Sys_SocketEnableBroadcast(broadcastSocket) != SysSocketOptionStatus::Applied)
    {
        (void)Sys_SocketClose(&broadcastSocket);
        broadcastSocket = nullptr;
    }

    SysSocketAddress bound{};
    localPort = 0;
    if (Sys_SocketGetLocalAddress(gameSocket, &bound))
        localPort = bound.port;

    return true;
}

bool RegisterLatches()
{
    net_noudp = Dvar_RegisterBool("net_noudp", false, DVAR_ARCHIVE | DVAR_LATCH, "Disable UDP");
    net_ip = Dvar_RegisterString("net_ip", "localhost", DVAR_LATCH, "Network IP Address");
    net_port = Dvar_RegisterInt("net_port", 28960, DvarLimits(0, 65535), DVAR_LATCH, "Network port");
    return net_noudp && net_ip && net_port;
}

// Copies one datagram into `message`, reporting oversize datagrams instead of
// truncating them into a payload the packet parser would misread. Returns the
// receive status so the caller can distinguish "nothing yet" from "dropped".
bool ReceiveInto(SysSocketHandle handle, netadr_t *from, msg_t *message)
{
    if (!handle || !message || !message->data || message->maxsize <= 0)
        return false;

    std::uint32_t byteCount = 0;
    SysSocketAddress source{};
    const SysSocketRecvStatus status = Sys_SocketRecvFrom(
        handle,
        message->data,
        static_cast<std::uint32_t>(message->maxsize),
        &source,
        &byteCount);

    switch (status)
    {
    case SysSocketRecvStatus::Received:
        break;
    case SysSocketRecvStatus::Truncated:
        // The datagram was consumed but does not fit a message the engine can
        // parse; drop it rather than hand up a partial packet.
        Com_PrintWarning(16, "Oversize packet dropped\n");
        return false;
    case SysSocketRecvStatus::WouldBlock:
        return false;
    default:
        Com_PrintError(16, "Sys_GetPacket: %s\n", NET_ErrorString());
        return false;
    }

    if (from)
        NetadrFromSocketAddress(source, from);
    message->cursize = static_cast<int>(byteCount);
    message->readcount = 0;
    return true;
}
} // namespace

const char *NET_ErrorString(void)
{
    return lastError;
}

void NET_Sleep(int msec)
{
    if (msec > 0)
        Sys_Sleep(static_cast<std::uint32_t>(msec));
}

bool Sys_IsLANAddress_IgnoreSubnet(netadr_t adr)
{
    switch (adr.type)
    {
    case NA_LOOPBACK:
    case NA_BOT:
        return true;
    default:
        break;
    }
    if (adr.type != NA_IP)
        return false;
    if (adr.ip[0] == 10)
        return true;
    if (adr.ip[0] == 127)
        return true;
    if (adr.ip[0] == 169 && adr.ip[1] == 254)
        return true;
    if (adr.ip[0] == 172 && (adr.ip[1] & 0xF0) == 0x10)
        return true;
    return adr.ip[0] == 192 && adr.ip[1] == 168;
}

bool Sys_IsLANAddress(netadr_t adr)
{
    // The local-interface match the retail layer applies is a Winsock
    // enumeration; on a portable layer the RFC1918/link-local classification
    // already covers every address a LAN client can present, so the subnet
    // rule is the whole answer here.
    return Sys_IsLANAddress_IgnoreSubnet(adr);
}

void Sys_ShowIP(void)
{
    SysSocketAddress bound{};
    if (gameSocket && Sys_SocketGetLocalAddress(gameSocket, &bound))
    {
        Com_Printf(
            16,
            "IP: %u.%u.%u.%u:%u\n",
            bound.address[0],
            bound.address[1],
            bound.address[2],
            bound.address[3],
            bound.port);
    }
    else
    {
        Com_Printf(16, "IP: no bound interface\n");
    }
}

qboolean Sys_StringToAdr(const char *s, netadr_t *a)
{
    if (!s || !*s || !a)
        return qfalse;

    SysSocketAddress resolved{};
    const SysSocketResolveStatus status = Sys_SocketResolveHost(s, 0, &resolved);
    if (status != SysSocketResolveStatus::Resolved)
        return qfalse;

    NetadrFromSocketAddress(resolved, a);
    return qtrue;
}

char Sys_SendPacket(int length, unsigned __int8 *data, netadr_t to)
{
    if (to.type != NA_IP && to.type != NA_BROADCAST)
    {
        Com_Error(ERR_FATAL, "Sys_SendPacket: bad address type");
        return 0;
    }
    if (!gameSocket)
        return 0;
    if (length <= 0 || !data)
        return 0;

    SysSocketAddress destination{};
    if (!SocketAddressFromNetadr(to, &destination))
        return 0;

    const SysSocketSendStatus status = Sys_SocketSendTo(
        gameSocket, data, static_cast<std::uint32_t>(length), &destination);

    switch (status)
    {
    case SysSocketSendStatus::Sent:
        return 1;
    case SysSocketSendStatus::WouldBlock:
        // A full send queue is silent, matching the retail layer: the caller
        // treats a zero return as "not queued" and retries on the next frame.
        return 0;
    case SysSocketSendStatus::MessageTooLarge:
        Com_PrintWarning(16, "Sys_SendPacket: datagram of %d bytes is too large\n", length);
        return 0;
    default:
        Com_PrintWarning(16, "Sys_SendPacket: %s\n", NET_ErrorString());
        return 0;
    }
}

qboolean Sys_GetPacket(netadr_t *net_from, msg_t *net_message)
{
    return ReceiveInto(gameSocket, net_from, net_message) ? qtrue : qfalse;
}

qboolean Sys_GetBroadcastPacket(msg_t *net_message)
{
    return ReceiveInto(broadcastSocket, nullptr, net_message) ? qtrue : qfalse;
}

void NET_Config(bool enableNetworking)
{
    if (!initialized)
        return;

    const bool blocked = net_noudp && net_noudp->current.enabled;
    const bool wantNetworking = enableNetworking && !blocked;

    if (wantNetworking == networkingEnabled && (wantNetworking ? gameSocket != nullptr : gameSocket == nullptr))
        return;

    networkingEnabled = wantNetworking;
    if (!wantNetworking)
    {
        CloseSockets();
        localPort = 0;
        return;
    }

    if (!OpenSockets())
    {
        networkingEnabled = false;
        Com_PrintWarning(16, "WARNING: NET_Init: %s\n", lastError);
    }
}

void NET_Restart(void)
{
    // A restart always rebinds, even when the enable state is unchanged: the
    // point of the command is to pick up a changed net_ip/net_port latch.
    if (!initialized)
        return;
    // Re-registering a latched dvar makes its pending value current, which is
    // how retail's NET_Config (NET_GetDvars) applies net_ip/net_port/net_noudp.
    (void)RegisterLatches();

    const bool blocked = net_noudp && net_noudp->current.enabled;
    networkingEnabled = !blocked;
    if (!networkingEnabled)
    {
        CloseSockets();
        localPort = 0;
        return;
    }

    if (!OpenSockets())
    {
        networkingEnabled = false;
        Com_PrintWarning(16, "WARNING: NET_Restart: %s\n", lastError);
        return;
    }

    Com_Printf(16, "NET_Restart: bound UDP port %d\n", localPort);
}

void NET_Init(void)
{
    if (initialized)
        return;

    if (!RegisterLatches())
    {
        SetError("could not register the net_* latches");
        Com_PrintWarning(16, "WARNING: NET_Init: %s\n", lastError);
        return;
    }

    initialized = true;
    Com_Printf(16, "Networking Initialized\n");
    NET_Restart();
}

void NET_Shutdown(void)
{
    if (!initialized)
        return;

    CloseSockets();
    localPort = 0;
    networkingEnabled = false;
    initialized = false;
}
