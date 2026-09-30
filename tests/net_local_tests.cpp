// net_local_tests.cpp: runtime contracts for the portable datagram layer
// (qcommon/net_local.cpp).
//
// These execute real engine code rather than scanning source text: the LAN
// classifier, the address resolver and the closed-layer lifecycle are the
// behaviours the headless dedicated build depends on (docs/design/
// PLATFORM_POSIX.md, NOW row 13), and each check here fails if the
// corresponding behaviour is broken.

#include <chrono>
#include <thread>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <qcommon/com_error.h>
#include <qcommon/net_local.h>
#include <qcommon/qcommon.h>
#include <qcommon/sys_socket.h>


// ---------------------------------------------------------------------------
// Engine boundary stubs. net_local.cpp reports through the Com_* printer and
// registers the net_* latches through the dvar system; a unit test has no
// engine, so it records what the layer asked for instead.
// ---------------------------------------------------------------------------
namespace
{
int g_checks = 0;
int g_failures = 0;
const char *g_stage = "startup";

std::vector<std::string> g_log;

bool Check(const bool condition, const char *const stage)
{
    ++g_checks;
    if (!condition)
    {
        ++g_failures;
        g_stage = stage;
        std::fprintf(stderr, "FAIL %s\n", stage);
        return false;
    }
    return true;
}

void Record(const char *text)
{
    g_log.emplace_back(text ? text : "");
}

// The latch objects the net layer reads back. A test drives them directly so
// NET_Config/NET_Restart see the state the check is about.
dvar_s g_net_noudp_latch;
dvar_s g_net_ip_latch;
dvar_s g_net_port_latch;
} // namespace

void Com_Printf(int, const char *fmt, ...)
{
    Record(fmt);
}

void Com_PrintWarning(int, const char *fmt, ...)
{
    Record(fmt);
}

void Com_PrintError(int, const char *fmt, ...)
{
    Record(fmt);
}

void Com_DPrintf(int, const char *fmt, ...)
{
    Record(fmt);
}

int Com_sprintf(char *dest, unsigned int size, const char *fmt, ...)
{
    (void)fmt;
    if (dest && size)
        dest[0] = 0;
    return 0;
}

void __cdecl Com_Error(errorParm_t, const char *, ...)
{
    std::fprintf(stderr, "unexpected Com_Error\n");
    std::exit(2);
}


const dvar_s *__cdecl Dvar_RegisterBool(
    const char *, bool value, uint16_t, const char *)
{
    g_net_noudp_latch.current.enabled = value;
    return &g_net_noudp_latch;
}

const dvar_s *__cdecl Dvar_RegisterString(
    const char *, const char *, uint16_t, const char *)
{
    return &g_net_ip_latch;
}

const dvar_s *__cdecl Dvar_RegisterInt(
    const char *, int value, DvarLimits, uint16_t, const char *)
{
    g_net_port_latch.current.integer = value;
    return &g_net_port_latch;
}

void __cdecl Sys_Sleep(uint32_t)
{
}

bool Sys_IsMainThread()
{
    return true;
}


// ---------------------------------------------------------------------------
// Checks
// ---------------------------------------------------------------------------
namespace
{
netadr_t MakeAdr(const int a, const int b, const int c, const int d)
{
    netadr_t adr;
    std::memset(&adr, 0, sizeof(adr));
    adr.type = NA_IP;
    adr.ip[0] = static_cast<unsigned char>(a);
    adr.ip[1] = static_cast<unsigned char>(b);
    adr.ip[2] = static_cast<unsigned char>(c);
    adr.ip[3] = static_cast<unsigned char>(d);
    adr.port = 0;
    return adr;
}

void CheckLanClassification()
{
    Check(Sys_IsLANAddress_IgnoreSubnet(MakeAdr(10, 0, 0, 1)), "lan 10/8");
    Check(Sys_IsLANAddress_IgnoreSubnet(MakeAdr(127, 0, 0, 1)), "lan 127/8");
    Check(Sys_IsLANAddress_IgnoreSubnet(MakeAdr(192, 168, 1, 1)), "lan 192.168/16");
    Check(Sys_IsLANAddress_IgnoreSubnet(MakeAdr(172, 16, 0, 1)), "lan 172.16/12");
    Check(Sys_IsLANAddress_IgnoreSubnet(MakeAdr(169, 254, 0, 1)), "lan 169.254/16");
    Check(!Sys_IsLANAddress_IgnoreSubnet(MakeAdr(8, 8, 8, 8)), "not-lan 8.8.8.8");
    Check(!Sys_IsLANAddress_IgnoreSubnet(MakeAdr(172, 32, 0, 1)), "not-lan 172.32");

    netadr_t loopback;
    std::memset(&loopback, 0, sizeof(loopback));
    loopback.type = NA_LOOPBACK;
    Check(Sys_IsLANAddress_IgnoreSubnet(loopback), "lan loopback");

    // The public entry point agrees with the subnet rule for RFC1918 space.
    Check(Sys_IsLANAddress(MakeAdr(10, 1, 2, 3)), "Sys_IsLANAddress 10/8");
    Check(!Sys_IsLANAddress(MakeAdr(203, 0, 113, 7)), "Sys_IsLANAddress not-lan");
}

void CheckAddressResolution()
{
    netadr_t adr;
    std::memset(&adr, 0, sizeof(adr));

    // A dotted-quad literal must resolve without a resolver round trip, so it
    // behaves the same whether or not DNS is provisioned.
    Check(Sys_StringToAdr("127.0.0.1", &adr) == qtrue, "resolve loopback literal");
    Check(adr.type == NA_IP, "resolved type is NA_IP");
    Check(adr.ip[0] == 127 && adr.ip[3] == 1, "resolved loopback octets");

    std::memset(&adr, 0, sizeof(adr));
    Check(Sys_StringToAdr("192.168.0.10", &adr) == qtrue, "resolve private literal");
    Check(adr.ip[0] == 192 && adr.ip[3] == 10, "resolved private octets");

    std::memset(&adr, 0, sizeof(adr));
    Check(Sys_StringToAdr("", &adr) == qfalse, "empty host fails closed");
    Check(Sys_StringToAdr(nullptr, &adr) == qfalse, "null host fails closed");
    Check(Sys_StringToAdr("256.256.256.256", &adr) == qfalse, "out-of-range literal fails closed");
}


void CheckNetLayerLifecycle()
{
    // A closed layer must tolerate traffic attempts rather than crash: the
    // headless server calls Sys_SendPacket before and after NET_Init.
    netadr_t to = MakeAdr(127, 0, 0, 1);
    unsigned char payload[4] = {1, 2, 3, 4};
    Check(Sys_SendPacket(4, payload, to) == 0, "send before init is rejected");
    Check(NET_ErrorString() != nullptr, "error string is never null");

    // NET_Sleep must never be a no-op panic on a zero or negative request.
    NET_Sleep(0);
    NET_Sleep(-1);

    // Shutdown before init is a no-op, so teardown can run unconditionally.
    NET_Shutdown();
    Check(true, "shutdown before init is safe");
}
// net_ip names an interface: NET_Init binds the game socket to it (retail
// NET_IPSocket) instead of every interface, and a datagram sent there comes
// back out of Sys_GetPacket.
void CheckNamedInterfaceBind()
{
    static char ip[] = "127.0.0.1";
    g_net_ip_latch.current.string = ip;
    g_log.clear();
    NET_Init();

    bool named = false;
    bool wildcard = false;
    for (const std::string &line : g_log)
    {
        named = named || line == "Opening IP socket: %s:%i\n";
        wildcard = wildcard || line == "Opening IP socket: localhost:%i\n";
    }
    Check(named && !wildcard, "a named net_ip binds that interface");

    SysSocketHandle sender = nullptr;
    Check(Sys_SocketOpenUdp(0, true, &sender) == SysSocketOpenStatus::Opened, "sender opens");
    const SysSocketAddress target{{127, 0, 0, 1}, static_cast<uint16_t>(g_net_port_latch.current.integer)};
    const unsigned char payload[5] = {'p', 'i', 'n', 'g', 0};
    Check(Sys_SocketSendTo(sender, payload, sizeof(payload), &target) == SysSocketSendStatus::Sent,
        "datagram sent to the named interface");

    unsigned char buffer[64] = {};
    msg_t message{};
    message.data = buffer;
    message.maxsize = sizeof(buffer);
    netadr_t from{};
    bool received = false;
    for (int attempt = 0; attempt < 200 && !received; ++attempt)
    {
        received = Sys_GetPacket(&from, &message) == qtrue;
        if (!received)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Check(received && message.cursize == 5 && std::memcmp(buffer, payload, 5) == 0,
        "the datagram arrives through Sys_GetPacket");

    (void)Sys_SocketClose(&sender);
    NET_Shutdown();
    g_net_ip_latch.current.string = nullptr;
}
} // namespace


int main()
{
    CheckLanClassification();
    CheckAddressResolution();
    CheckNetLayerLifecycle();
    CheckNamedInterfaceBind();

    if (g_failures == 0)
        std::printf("net_local: %d checks passed\n", g_checks);
    else
        std::fprintf(stderr, "net_local: %d of %d checks failed at %s\n",
            g_failures, g_checks, g_stage);
    return g_failures == 0 ? 0 : 1;
}
