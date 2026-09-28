// net_local_tests.cpp: runtime contracts for the portable datagram layer
// (qcommon/net_local.cpp) and the POSIX termios console cleaner
// (_platform/posix/posix_syscon.cpp).
//
// These execute real engine code rather than scanning source text: the LAN
// classifier, the address resolver and the console text cleaner are the
// behaviours the headless dedicated build depends on (docs/design/
// PLATFORM_POSIX.md, NOW row 13), and each check here fails if the
// corresponding behaviour is broken.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <qcommon/com_error.h>
#include <qcommon/net_local.h>
#include <qcommon/qcommon.h>
#include <qcommon/sys_console.h>
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

// Byte-order helper the datagram layer uses to convert between netadr_t's
// network-order port and SysSocketAddress's host-order port. q_shared.cpp owns
// the engine definition; a unit test links neither, so it supplies the same
// pure swap (byte order is decided at compile time here because a test only
// ever runs on the host).
__int16 __cdecl BigShort(__int16 l)
{
    return static_cast<__int16>(((static_cast<unsigned short>(l) & 0x00ffu) << 8)
        | ((static_cast<unsigned short>(l) & 0xff00u) >> 8));
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

// Defined by the termios console translation unit (_platform/posix/posix_syscon.cpp).
uint32_t __cdecl Conbuf_CleanText(const char *source, char *target, int sizeofTarget);

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

void CheckConsoleCleanText()
{
    char target[64];

    // Color codes are stripped; a literal caret survives.
    std::memset(target, 0x5a, sizeof(target));
    Check(Conbuf_CleanText("^1red^2green", target, sizeof(target)) == 8, "clean strips colour codes length");
    Check(std::strcmp(target, "redgreen") == 0, "clean strips colour codes text");

    Check(Conbuf_CleanText("a^^b", target, sizeof(target)) == 4, "clean keeps escaped caret length");
    Check(std::strcmp(target, "a^^b") == 0, "clean keeps escaped caret text");

    // CR/LF pairs and single newlines normalize to a bare newline on a
    // terminal, so the user's scrollback never sees a stray CR.
    Check(Conbuf_CleanText("a\r\nb", target, sizeof(target)) == 3, "clean crlf length");
    Check(std::strcmp(target, "a\nb") == 0, "clean crlf text");
    Check(Conbuf_CleanText("a\nb", target, sizeof(target)) == 3, "clean lf length");
    Check(Conbuf_CleanText("a\rb", target, sizeof(target)) == 3, "clean cr length");

    // Degenerate callers fail closed without writing past the buffer.
    Check(Conbuf_CleanText("abc", nullptr, 16) == 0, "clean null target");
    Check(Conbuf_CleanText(nullptr, target, 16) == 0, "clean null source");
    Check(Conbuf_CleanText("abc", target, 0) == 0, "clean zero capacity");

    // An overlong source is truncated to the window, never overflowed.
    char small[8];
    std::memset(small, 0x5a, sizeof(small));
    // The cleaner reserves the last two bytes of the window for a CRLF pair,
    // so a capacity of 8 accepts at most 6 payload bytes.
    const uint32_t written = Conbuf_CleanText("0123456789abcdef", small, 8);
    Check(written == 6, "clean truncates to window");
    Check(small[written] == 0, "clean NUL-terminates the window");
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
} // namespace

int main()
{
    CheckLanClassification();
    CheckAddressResolution();
    CheckConsoleCleanText();
    CheckNetLayerLifecycle();

    if (g_failures == 0)
        std::printf("net_local: %d checks passed\n", g_checks);
    else
        std::fprintf(stderr, "net_local: %d of %d checks failed at %s\n",
            g_failures, g_checks, g_stage);
    return g_failures == 0 ? 0 : 1;
}
