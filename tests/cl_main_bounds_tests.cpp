// cl_main_bounds_tests.cpp: CL_ServersResponsePacket's master-list bound,
// CL_RconLogin's terminated password copy, and CL_GetFreePing's oldest-slot
// choice (#206 C1). The production client TU is compiled here; section GC
// drops the code the checks never reach.
#include <client_mp/client_mp.h>
#include <qcommon/cmd.h>
#include <qcommon/qcommon.h>

#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

#define WEAK __attribute__((weak))

void __cdecl CL_ServersResponsePacket(netadr_t from, msg_t *msg);
ping_t *__cdecl CL_GetFreePing();
void CL_RconLogin();

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

std::uint32_t g_now = 100000;
int g_argc = 0;
const char *g_argv[3] = {};
char *g_copyDest = nullptr;

class GuardedTail
{
public:
    GuardedTail(const unsigned char *bytes, const std::size_t count)
    {
        const long pageSize = sysconf(_SC_PAGESIZE);
        m_pageSize = static_cast<std::size_t>(pageSize);
        m_base = static_cast<unsigned char *>(mmap(nullptr, m_pageSize * 2,
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (m_base == MAP_FAILED)
        {
            m_base = nullptr;
            return;
        }
        if (mprotect(m_base + m_pageSize, m_pageSize, PROT_NONE) != 0)
        {
            munmap(m_base, m_pageSize * 2);
            m_base = nullptr;
            return;
        }
        m_tail = m_base + m_pageSize - count;
        std::memcpy(m_tail, bytes, count);
    }

    ~GuardedTail()
    {
        if (m_base)
            munmap(m_base, m_pageSize * 2);
    }

    unsigned char *tail() const { return m_tail; }

private:
    unsigned char *m_base = nullptr;
    unsigned char *m_tail = nullptr;
    std::size_t m_pageSize = 0;
};

void TestMasterListBound()
{
    // One complete entry followed by the terminator's leading backslash. The
    // old loop test read three bytes past that last byte.
    const unsigned char packet[] = {'\\', 127, 0, 0, 1, 0x1F, 0x90, '\\'};
    GuardedTail page(packet, sizeof(packet));
    CHECK(page.tail() != nullptr);
    if (!page.tail())
        return;

    msg_t msg{};
    msg.data = page.tail();
    msg.cursize = static_cast<int>(sizeof(packet));
    cls.numglobalservers = 0;
    CL_ServersResponsePacket(netadr_t{}, &msg);
    CHECK(cls.numglobalservers == 1);
}

void TestIncompleteEntry()
{
    // Six bytes after the delimiter are not enough for four address bytes,
    // the port, and the next delimiter.
    const unsigned char packet[] = {'\\', 127, 0, 0, 1, 0x1F, 0x90};
    msg_t msg{};
    msg.data = const_cast<unsigned char *>(packet);
    msg.cursize = static_cast<int>(sizeof(packet));
    cls.numglobalservers = 0;
    CL_ServersResponsePacket(netadr_t{}, &msg);
    CHECK(cls.numglobalservers == 0);
}

void TestRconTermination()
{
    const char *const longPassword = "01234567890123456789012";
    g_argc = 3;
    g_argv[2] = longPassword;
    g_copyDest = nullptr;
    CL_RconLogin();
    CHECK(g_copyDest != nullptr);
    char *const password = g_copyDest;
    CHECK(password && std::strcmp(password, longPassword) == 0);

    g_argv[2] = "xy";
    g_copyDest = nullptr;
    CL_RconLogin();
    CHECK(g_copyDest == password && password && std::strcmp(password, "xy") == 0);
}

void TestOldestPing()
{
    g_now = 100000;
    std::memset(cl_pinglist, 0, sizeof(cl_pinglist));
    for (int index = 0; index < 16; ++index)
    {
        cl_pinglist[index].adr.port = 1;
        cl_pinglist[index].start = static_cast<int32_t>(g_now) - 100 - index * 10;
    }

    CHECK(CL_GetFreePing() == &cl_pinglist[15]);
}
} // namespace

WEAK int __cdecl NET_CompareAdrSigned(netadr_t *, netadr_t *) { return 0; }
WEAK std::uint32_t KISAK_CDECL Sys_Milliseconds() { return g_now; }
WEAK int __cdecl Cmd_Argc() { return g_argc; }
WEAK const char *__cdecl Cmd_Argv(int index) { return g_argv[index]; }
WEAK void QDECL Com_Printf(int, const char *, ...) {}
WEAK void Com_DPrintf(int, const char *, ...) {}
WEAK void I_strncpyz(char *dest, const char *src, int size)
{
    g_copyDest = dest;
    std::strncpy(dest, src, static_cast<std::size_t>(size) - 1);
    dest[size - 1] = 0;
}

int main(int argc, char **argv)
{
    const char *const only = argc > 1 ? argv[1] : nullptr;
    if (!only || std::strcmp(only, "master-list") == 0)
        TestMasterListBound();
    if (!only || std::strcmp(only, "incomplete-entry") == 0)
        TestIncompleteEntry();
    if (!only || std::strcmp(only, "rcon") == 0)
        TestRconTermination();
    if (!only || std::strcmp(only, "ping") == 0)
        TestOldestPing();

    if (g_failures)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("cl_main bounds: ok");
    return 0;
}
