// sv_maxclients_tests.cpp: the production SV_ChangeMaxClients
// (server_mp/sv_init_mp.cpp) at native width. When the operator lowers
// sv_maxclients, it parks the connected slots in a temp block, clears the
// client array and copies them back. It sized the block, and the clear, with
// x86's sizeof(client_t), which is smaller than the 64-bit struct: the parked
// copies ran past the block (ASan reports it) and the clear left each slot's
// tail as it was.
//
// The engine boundary is weak, with the production dvar system; --gc-sections
// drops the rest of the server.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <qcommon/qcommon.h>
#include <qcommon/sys_sync.h>
#include <qcommon/threads.h>
#include <server_mp/server_mp.h>
#include <universal/com_memory.h>

#define WEAK __attribute__((weak))

void __cdecl SV_ChangeMaxClients(); // sv_init_mp.cpp, not in a header

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

char &Tail(client_t &cl)
{
    return reinterpret_cast<char *>(&cl)[sizeof(client_t) - 1]; // the struct's last byte
}
} // namespace

WEAK void MyAssertHandler(const char *filename, int line, int, const char *, ...)
{
    std::fprintf(stderr, "assert %s:%d\n", filename, line);
    std::abort();
}
WEAK void Com_Error(errorParm_t, const char *fmt, ...)
{
    std::fprintf(stderr, "Com_Error: %s\n", fmt);
    std::abort();
}
WEAK void Com_Printf(int, const char *, ...) {}
WEAK void Com_DPrintf(int, const char *, ...) {}
WEAK void Com_PrintWarning(int, const char *, ...) {}
WEAK void Com_PrintError(int, const char *, ...) {}
WEAK void Com_Memset(void *dest, int value, size_t count) { std::memset(dest, value, count); }
WEAK bool __cdecl Com_LogFileOpen() { return false; }

// The dvar system's own boundary: strings keep the engine's semantics, the
// locks are single-threaded, and the rest is inert.
WEAK const char *CopyString(const char *in) { return strdup(in ? in : ""); }
WEAK void __cdecl FreeString(const char *str) { std::free(const_cast<char *>(str)); }
WEAK bool __cdecl Vec4Compare(const float *a, const float *b) { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3]; }
WEAK void __cdecl Dvar_AddCommands() {}
WEAK void KISAK_CDECL Sys_LockRead(FastCriticalSection *) {}
WEAK void KISAK_CDECL Sys_UnlockRead(FastCriticalSection *) {}
WEAK void KISAK_CDECL Sys_LockWrite(FastCriticalSection *) {}
WEAK void KISAK_CDECL Sys_UnlockWrite(FastCriticalSection *) {}
va_info_t g_vaInfo; // value 1: the one thread's va() buffers
WEAK void *__cdecl Sys_GetValue(int valueIndex)
{
    return valueIndex == 1 ? &g_vaInfo : nullptr;
}
// A heap block of exactly the requested size, so ASan sees a copy past it.
WEAK uint32_t *Hunk_AllocateTempMemory(int size, const char *) { return static_cast<uint32_t *>(std::malloc(size)); }
WEAK void Hunk_FreeTempMemory(char *buf) { std::free(buf); }

int main()
{
    Dvar_Init();
    Dvar_SetFromStringByName("sv_maxclients", "4"); // what "+set" does before the server starts
    SV_BoundMaxClients(1);
    CHECK(sv_maxclients->current.integer == 4);

    // Slot 0 is connected; slots 1 to 3 are free but hold stale bytes.
    svs.clients[0].header.state = 2;
    Tail(svs.clients[0]) = 0x5A;
    Tail(svs.clients[1]) = 0x66;
    Tail(svs.clients[2]) = 0x77;

    // The operator lowers sv_maxclients; it applies at the next map.
    Dvar_SetFromStringByNameFromSource("sv_maxclients", "2", DVAR_SOURCE_EXTERNAL); // as the console sets it
    SV_ChangeMaxClients();

    CHECK(sv_maxclients->current.integer == 2);
    CHECK(svs.clients[0].header.state == 2); // the connected client survives, whole
    CHECK(Tail(svs.clients[0]) == 0x5A);
    CHECK(svs.clients[1].header.state == 0); // the free slot is cleared, whole
    CHECK(Tail(svs.clients[1]) == 0);

    if (g_failures == 0)
        std::printf("sv_maxclients: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
