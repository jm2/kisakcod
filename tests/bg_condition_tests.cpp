// BG_SetConditionValue (bgame/bg_animation_mp.cpp) stores a 64-bit condition
// into a client's uint32_t[2] condition pair. On 64-bit targets the pair is
// only 4-byte aligned, so the store must not be a single 64-bit access; the
// sanitizer build (CI's portable ASan + UBSan job) traps a misaligned one.
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <bgame/bg_local.h>
#include <game_mp/g_main_mp.h>

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

bgs_t g_bgs{};
char g_empty[1] = {}; // what the string stubs return
} // namespace

#define WEAK __attribute__((weak))
WEAK void MyAssertHandler(const char *filename, int line, int, const char *, ...)
{
    std::fprintf(stderr, "assert %s:%d\n", filename, line);
    std::abort();
}
WEAK void __cdecl Com_Error(errorParm_t, const char *, ...) { std::abort(); }
WEAK void Com_Printf(int, const char *, ...) {}
WEAK void Com_DPrintf(int, const char *, ...) {}
WEAK void Com_PrintWarning(int, const char *, ...) {}
WEAK void Com_PrintError(int, const char *, ...) {}
WEAK int32_t __cdecl G_IsServerGameSystem(int32_t) { return 0; }
WEAK void __cdecl BG_CheckThread() {}
WEAK char *__cdecl BG_GetPlayerAnimTypeName(int32_t) { return g_empty; }
WEAK char *va(const char *, ...) { return g_empty; }

int main()
{
    bgs = &g_bgs;
    for (uint32_t condition = 0; condition < 10; ++condition)
    {
        const uint64_t value = 0x1122334400000000ull | (condition + 1u);
        BG_SetConditionValue(0, condition, value);
        CHECK(g_bgs.clientinfo[0].clientConditions[condition][0] == condition + 1u);
        CHECK(g_bgs.clientinfo[0].clientConditions[condition][1] == 0x11223344u);
    }
    // Another client's pair is untouched.
    CHECK(g_bgs.clientinfo[1].clientConditions[0][0] == 0);
    CHECK(g_bgs.clientinfo[1].clientConditions[0][1] == 0);
    BG_SetConditionValue(63, 9, 7);
    CHECK(g_bgs.clientinfo[63].clientConditions[9][0] == 7);
    CHECK(g_bgs.clientinfo[63].clientConditions[9][1] == 0);

    if (g_failures == 0)
        std::printf("bg condition store: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
