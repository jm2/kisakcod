// com_sndalias_unload_tests.cpp: the sound alias unload paths of the production
// universal/com_sndalias.cpp, compiled as the headless server compiles it.
// Com_UnloadSoundAliasSounds and Com_UnloadSoundAliases must use
// g_sa.aliasInfo[system].head. The decompiled form reached it through
// soundFileInfo[-4] with the x86 12-byte stride: at 64-bit that lands in the
// wrong alias list for every system but the first, and the sounds path
// truncated the pointer to 32 bits. Com_PickSoundAliasFromList's seed step
// must wrap as unsigned, as x86 does, not overflow a signed int.

#include <universal/com_sndalias.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

// File-local in com_sndalias.cpp; Com_UnloadSoundAliases is its only caller.
void __cdecl Com_UnloadSoundAliasSounds(snd_alias_system_t system);

namespace
{
int g_failures = 0;
int g_asserts = 0;

void Check(const bool ok, const char *const what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

snd_alias_list_t *g_hashSlots[4];
SoundFile g_files[2];
snd_alias_t g_aliases[2];
}

void __cdecl MyAssertHandler(const char *, int, int, const char *, ...)
{
    ++g_asserts;
}

void __cdecl Cmd_RemoveCommand(const char *)
{
}

void __cdecl Com_PrintWarning(int, const char *, ...)
{
}

namespace
{
void ResetGlobals()
{
    std::memset(&g_sa, 0, sizeof(g_sa));
    for (auto &slot : g_hashSlots)
        slot = reinterpret_cast<snd_alias_list_t *>(&g_sa);
    g_sa.hash = g_hashSlots;
    g_sa.hashSize = 4;
    g_sa.hashUsed = 3;
}

// Every system clears its own list head and leaves the other two alone.
void TestUnloadClearsOwnHead()
{
    snd_alias_t aliases[3][1] = {};
    for (int system = SASYS_UI; system < SASYS_COUNT; ++system)
    {
        ResetGlobals();
        for (int other = SASYS_UI; other < SASYS_COUNT; ++other)
        {
            g_sa.aliasInfo[other].head = aliases[other];
            g_sa.aliasInfo[other].count = 0;
        }
        g_sa.aliasInfo[system].count = 1;
        g_sa.initialized[system] = 1;

        g_asserts = 0;
        Com_UnloadSoundAliases(static_cast<snd_alias_system_t>(system));

        Check(g_asserts == 0, "unload raises no assert");
        Check(g_sa.aliasInfo[system].head == nullptr, "unload clears the system's head");
        Check(g_sa.aliasInfo[system].count == 0, "unload clears the system's count");
        Check(g_sa.initialized[system] == 0, "unload clears initialized");
        Check(g_sa.hashUsed == 0 && g_hashSlots[0] == nullptr && g_hashSlots[3] == nullptr,
            "unload empties the hash");
        for (int other = SASYS_UI; other < SASYS_COUNT; ++other)
        {
            if (other != system)
                Check(g_sa.aliasInfo[other].head == aliases[other], "unload keeps the other heads");
        }
    }
}

// The loaded-sound flag (mode 1 in bits 6-7) clears exists through the full
// 64-bit head pointer.
void TestUnloadSoundsUsesFullHead()
{
    SoundFile *const files = g_files;
    snd_alias_t *const aliases = g_aliases;
    ResetGlobals();
    files[0].exists = 1;
    files[1].exists = 1;
    aliases[0].soundFile = &files[0];
    aliases[0].flags = 1 << 6;
    aliases[1].soundFile = &files[1];
    aliases[1].flags = 2 << 6;
    g_sa.aliasInfo[SASYS_CGAME].head = aliases;
    g_sa.aliasInfo[SASYS_CGAME].count = 2;

    Com_UnloadSoundAliasSounds(SASYS_CGAME);

    Check(files[0].exists == 0, "loaded alias sound is marked unloaded");
    Check(files[1].exists == 1, "streamed alias sound is left alone");
}
}

namespace
{
snd_alias_t g_pick[2];

// Two aliases from a seed whose step overflows int: the seed advances once,
// with the x86 wrap.
void TestPickWrapsSeed()
{
    snd_alias_t *const pick = g_pick;
    ResetGlobals();
    pick[0].probability = 1.0f;
    pick[1].probability = 1.0f;
    snd_alias_list_t list{};
    list.head = pick;
    list.count = 2;
    g_sa.randSeed = 2531011;
    Com_PickSoundAliasFromList(&list);
    const uint32_t wrapped = static_cast<uint32_t>(214013ull * 2531011u + 2531011u);
    Check(g_sa.randSeed == static_cast<int>(wrapped), "alias pick wraps the seed");
}
}

int main()
{
    TestUnloadClearsOwnHead();
    TestUnloadSoundsUsesFullHead();
    TestPickWrapsSeed();
    if (g_failures)
        return 1;
    std::puts("com_sndalias unload: ok");
    return 0;
}
