// bgame/bg_animation_mp.cpp in a headless harness: BG_SetConditionValue's
// 64-bit store into a 4-byte-aligned pair (the sanitizer build traps a single
// 64-bit access) and its change log, and BG_ExecuteCommand's second body part,
// which retail 1.8 plays while taking the legs decision from bodyPart[0]
// (0x004052D9; ADR-0009).
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>

#include <bgame/bg_local.h>
#include <game_mp/g_main_mp.h>

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

bgs_t g_bgs{};
char g_empty[1] = {}; // what the string stubs return
dvar_t g_xanimDebug{};
animScriptData_t g_scriptData{};
int g_printfCalls = 0;
bool g_serverGameSystem = false;

void TestExecuteCommandBodyPart()
{
    playerState_s ps{};
    animScriptCommand_t command{};
    command.bodyPart[0] = ANIM_BP_TORSO;
    command.bodyPart[1] = ANIM_BP_LEGS;
    command.animIndex[0] = 0;
    command.animIndex[1] = 1;
    command.animDuration[0] = 10;
    // Torso first, legs second: the legs anim plays, but as in retail the
    // second block's legs check reads bodyPart[0], so no duration comes back.
    CHECK(BG_ExecuteCommand(&ps, &command, 0, 0, 1) == -1);
    CHECK((ps.legsAnim & 0xFFFFFDFF) == 1);
}

void TestConditionChangeLog()
{
    g_serverGameSystem = true;
    g_printfCalls = 0;
    BG_SetConditionValue(0, 0, 11);
    CHECK(g_printfCalls == 1);
    BG_SetConditionValue(0, 0, 11);
    CHECK(g_printfCalls == 1);
    g_serverGameSystem = false;
}

void TestAnimationStateFallback()
{
    const pid_t pid = fork();
    if (pid == 0)
    {
        alarm(5);
        playerState_s ps{};
        _exit(BG_AnimScriptAnimation(&ps, AISTATE_COMBAT, ANIM_MT_IDLE, 1) == -1 ? 0 : 1);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
} // namespace

extern animScriptData_t *globalScriptData;
const dvar_t *xanim_debug = &g_xanimDebug;
const dvar_t *animscript_debug = &g_xanimDebug;

#define WEAK __attribute__((weak))
WEAK void MyAssertHandler(const char *filename, int line, int, const char *, ...)
{
    std::fprintf(stderr, "assert %s:%d\n", filename, line);
    std::abort();
}
WEAK void __cdecl Com_Error(errorParm_t, const char *, ...) { std::abort(); }
WEAK void Com_Printf(int, const char *, ...) { ++g_printfCalls; }
WEAK void Com_DPrintf(int, const char *, ...) {}
WEAK void Com_PrintWarning(int, const char *, ...) {}
WEAK void Com_PrintError(int, const char *, ...) {}
WEAK int32_t __cdecl G_IsServerGameSystem(int32_t) { return g_serverGameSystem ? 1 : 0; }
WEAK void __cdecl BG_CheckThread() {}
WEAK char *__cdecl BG_GetPlayerAnimTypeName(int32_t) { return g_empty; }
WEAK char *va(const char *, ...) { return g_empty; }

int main()
{
    bgs = &g_bgs;
    globalScriptData = &g_scriptData;
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

    TestConditionChangeLog();
    TestExecuteCommandBodyPart();
    TestAnimationStateFallback();

    if (g_failures == 0)
        std::printf("bg condition store: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
