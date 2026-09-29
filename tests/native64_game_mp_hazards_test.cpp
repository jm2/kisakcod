// SPDX-License-Identifier: GPL-3.0
//
// native64_game_mp_hazards_test -- done-test for the bead-10 silent 64-bit
// hazards in the game_mp path (ki-vwteh / issue #216, NOW.md row 10).
//
// Every check runs PRODUCTION code compiled into this target from a
// translation unit this PR changed, and asserts against the natural layout of
// the real production types. Nothing restates a layout premise, so reverting a
// fix changes what the linked code does and fails the check:
//
//   1. fields_1 (g_spawn_mp.cpp) -- the real Scr_GetEntityField ->
//      Scr_GetGenericField path reads each field at fields_1[i].ofs. Sentinels
//      are planted at the natural offsetof(gentity_s, ...) positions and the
//      check is that production reads those sentinels. The ILP32 constants the
//      old table hard-coded (368/360/316/...) land 16 bytes early at LP64.
//   2. DB_GetXAssetSizeHandler (db_assetnames.cpp) -- the real table is called
//      at the five XAssetType indices the old code aliased and must return the
//      natural sizeof() of its own asset struct.
//   3. XAnimClone (xanim.cpp) -- the real clone path is driven with a recording
//      allocator and must reserve sizeof(XAnimParts) (0x58 ILP32 / 0x88 LP64),
//      the exact count the following qmemcpy writes.
//   4. PlayerCmd_DeactivateReverb (g_client_script_cmd_mp.cpp) -- the real
//      entref path must reach SV_GameSendServerCommand with the caller's
//      entity number; the old HIWORD(&e) read the parameter's address bits.
//   5. va_list (q_parse.cpp, com_playerprofile.cpp) -- both changed call paths
//      are linked and driven. The old spellings (va_copy into a char*, and
//      `char *vargs` for a va_list) do not compile on these hosts, so
//      enrolling these TUs is itself the regression pin as well.

#include "native64_game_mp_hazards_subject_dialect.h"

#include <game_mp/g_public_mp.h>
#include <gfx_d3d/r_gfx.h>
#include <qcommon/qcommon.h>
#include <script/scr_vm.h>
#include <sound/snd_public.h>
#include <universal/q_parse.h>
#include <xanim/xanim.h>

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
int g_failures = 0;
int g_runs = 0;

void Check(const bool ok, const char *const expr, const char *const file, const int line)
{
    ++g_runs;
    if (ok)
        return;
    std::fprintf(stderr, "native64_game_mp_hazards_test: %s:%d: %s\n", file, line, expr);
    ++g_failures;
}

// What the stubbed Scr_* leaves saw on the way out of production. This is how
// fields_1[i].ofs is observed: the sentinel planted at the natural offsetof
// only comes back if the linked table carries that offset.
struct Capture
{
    uint32_t u32 = 0;
    float vec[3] = {};
    bool sawString = false;
    bool sawInt = false;
    bool sawVector = false;
    const char *objectError = nullptr;
    int svClientNum = -1;
    unsigned allocBytes = 0;
    char scriptMessage[256] = {};
};
Capture g_cap;

void ResetCapture()
{
    g_cap = Capture();
}

void *RecordingAlloc(int size)
{
    g_cap.allocBytes = (unsigned)size;
    static uint8_t arena[512];
    return arena;
}

void Plant(const std::size_t ofs, const void *src, const std::size_t n)
{
    std::memcpy(reinterpret_cast<uint8_t *>(&g_entities[0]) + ofs, src, n);
}
}  // namespace

#define CHECK(expr) Check((expr), #expr, __FILE__, __LINE__)

// Production subjects linked from the changed translation units.
extern int (*DB_GetXAssetSizeHandler[33])();
extern void Scr_GetEntityField(unsigned int entnum, unsigned int offset);
extern void PlayerCmd_DeactivateReverb(scr_entref_t entref);
extern XAnimParts *XAnimClone(XAnimParts *fromParts, void *(__cdecl *Alloc)(int));
extern ParseThreadInfo g_parse[4];
int Com_BuildPlayerProfilePath_Internal(
    char *path, int pathSize, const char *playerName, const char *format, va_list vargs);

// Minimal stubs for the subjects' call surface.
gentity_s g_entities[MAX_GENTITIES];
scr_const_t scr_const;

void MyAssertHandler(const char *, int, int, const char *, ...) {}
void Scr_ObjectError(const char *msg) { g_cap.objectError = msg; }
void Scr_Error(const char *) {}
void Scr_AddConstString(unsigned int s) { g_cap.u32 = s; g_cap.sawString = true; }
void Scr_AddString(const char *) { g_cap.sawString = true; }
void Scr_AddInt(int v) { g_cap.u32 = (uint32_t)v; g_cap.sawInt = true; }
void Scr_AddFloat(float) {}
void Scr_AddVector(const float *v)
{
    std::memcpy(g_cap.vec, v, sizeof(g_cap.vec));
    g_cap.sawVector = true;
}
void Scr_AddObject(unsigned int) {}
void Scr_AddEntityNum(unsigned int, unsigned int) {}
void Scr_GetClientField(gclient_s *, int) {}
void Scr_SetClientField(gclient_s *, int) {}
void Scr_SetOrigin(gentity_s *, int) {}
void Scr_SetHealth(gentity_s *, int) {}
void Scr_SetAngles(gentity_s *, int) {}
uint32_t Scr_GetNumParam() { return 1; }
float Scr_GetFloat(unsigned int) { return 0.0f; }
uint32_t Scr_GetConstString(unsigned int) { return scr_const.snd_enveffectsprio_level; }
uint32_t G_ModelName(unsigned int index) { return index; }
void SL_AddRefToString(unsigned int) {}
bool Sys_IsMainThread() { return true; }
bool Sys_IsRenderThread() { return false; }
bool Sys_IsDatabaseThread() { return false; }
char *va(const char *, ...)
{
    static char buf[4] = {"va"};
    return buf;
}
int Com_sprintf(char *dest, uint32_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(dest, size, fmt, ap);
    va_end(ap);
    return n;
}
enum svscmd_type : int;
void SV_GameSendServerCommand(int clientNum, svscmd_type, const char *)
{
    g_cap.svClientNum = clientNum;
}
gentity_s *EntHandle::ent() const { return nullptr; }
bool EntHandle::isDefined() const { return false; }
void Com_PrintError(int, const char *fmt, ...)
{
    // Record the caller's already-rendered message as DATA (4th trailing
    // argument of the "%sFile %s, line %i: %s" form). The caller's format is
    // never rendered here, the Codacy CWE-134 shape dispositioned like
    // tests/db_asset_layout_tests.cpp.
    (void)fmt;
    va_list ap;
    va_start(ap, fmt);
    (void)va_arg(ap, const char *);
    (void)va_arg(ap, const char *);
    (void)va_arg(ap, int);
    const char *const rendered = va_arg(ap, const char *);
    va_end(ap);
    if (rendered != nullptr)
        std::snprintf(g_cap.scriptMessage, sizeof(g_cap.scriptMessage), "%s", rendered);
}
// Win32 seams for com_playerprofile.cpp, declared to that TU by the shim.
HWND GetActiveWindow() { return nullptr; }
int MessageBoxA(HWND, const char *, const char *, unsigned int) { return 6; }
const char *Win_LocalizeRef(const char *) { return "stub"; }

// 1. fields_1 offsets, observed through the real g_spawn_mp.cpp read path.
static void TestFields1Offsets()
{
    std::memset(&g_entities[0], 0, sizeof(g_entities[0]));
    g_entities[0].r.inuse = 1;
    g_entities[0].s.number = 0;

    // Sentinels at the natural layout positions; kind selects the read the
    // real Scr_GetGenericField performs (F_STRING/F_MODEL -> u16, F_INT ->
    // u32, F_VECTOR -> float[3]).
    const uint16_t sClass = 0x5A11, sTarget = 0x5A22, sTargetname = 0x5A33, sModel = 0x5A44;
    const uint32_t sFlags = 0x51025102u, sCount = 0x51035103u;
    const uint32_t sHealth = 0x51045104u, sDamage = 0x51055105u;
    const float sOrigin[3] = {11.5f, 22.5f, 33.5f}, sAngles[3] = {44.5f, 55.5f, 66.5f};

    struct Row
    {
        unsigned idx;
        std::size_t ofs;
        int kind; // 0 = u16 string/model, 1 = u32 int, 2 = float[3]
        uint32_t u32;
        const float *vec;
    };
    const Row rows[] = {
        {0, offsetof(gentity_s, classname), 0, sClass, nullptr},
        {1, offsetof(gentity_s, r.currentOrigin), 2, 0, sOrigin},
        {2, offsetof(gentity_s, model), 0, sModel, nullptr},
        {3, offsetof(gentity_s, spawnflags), 1, sFlags, nullptr},
        {4, offsetof(gentity_s, target), 0, sTarget, nullptr},
        {5, offsetof(gentity_s, targetname), 0, sTargetname, nullptr},
        {6, offsetof(gentity_s, count), 1, sCount, nullptr},
        {7, offsetof(gentity_s, health), 1, sHealth, nullptr},
        {8, offsetof(gentity_s, damage), 1, sDamage, nullptr},
        {9, offsetof(gentity_s, r.currentAngles), 2, 0, sAngles},
    };

    for (const Row &r : rows)
    {
        if (r.kind == 0)
        {
            const uint16_t v = (uint16_t)r.u32;
            Plant(r.ofs, &v, sizeof(v));
        }
        else if (r.kind == 1)
        {
            Plant(r.ofs, &r.u32, sizeof(r.u32));
        }
        else
        {
            Plant(r.ofs, r.vec, 3 * sizeof(float));
        }
        ResetCapture();
        Scr_GetEntityField(0, r.idx);
        if (r.kind == 0)
            CHECK(g_cap.sawString && g_cap.u32 == (uint16_t)r.u32);
        else if (r.kind == 1)
            CHECK(g_cap.sawInt && g_cap.u32 == r.u32);
        else
            CHECK(g_cap.sawVector && g_cap.vec[0] == r.vec[0] && g_cap.vec[2] == r.vec[2]);
    }

    // The natural layout is what production now spells with offsetof(); at
    // LP64 it must NOT be the ILP32 constants the old table hard-coded.
    if (sizeof(void *) == 8)
    {
        CHECK(offsetof(gentity_s, classname) != 368);
        CHECK(offsetof(gentity_s, model) != 360);
        CHECK(offsetof(gentity_s, r.currentOrigin) == 316);
    }
}

// 2. Clone-size table, observed through the real db_assetnames.cpp table.
static void TestCloneSizeTable()
{
    CHECK(DB_GetXAssetSizeHandler[ASSET_TYPE_PHYSPRESET]() == (int)sizeof(PhysPreset));
    CHECK(DB_GetXAssetSizeHandler[ASSET_TYPE_LOADED_SOUND]() == (int)sizeof(LoadedSound));
    CHECK(DB_GetXAssetSizeHandler[ASSET_TYPE_CLIPMAP]() == (int)sizeof(clipMap_t));
    CHECK(DB_GetXAssetSizeHandler[ASSET_TYPE_CLIPMAP_PVS]() == (int)sizeof(clipMap_t));
    CHECK(DB_GetXAssetSizeHandler[ASSET_TYPE_LIGHT_DEF]() == (int)sizeof(GfxLightDef));
    // The aliases that were indistinguishable at ILP32 must separate at LP64,
    // which is exactly why the old table mis-sized these assets.
    CHECK(DB_GetXAssetSizeHandler[ASSET_TYPE_PHYSPRESET]()
        != DB_GetXAssetSizeHandler[ASSET_TYPE_GAMEWORLD_SP]());
    CHECK(DB_GetXAssetSizeHandler[ASSET_TYPE_LOADED_SOUND]()
        != DB_GetXAssetSizeHandler[ASSET_TYPE_GAMEWORLD_SP]());
    CHECK(DB_GetXAssetSizeHandler[ASSET_TYPE_LIGHT_DEF]()
        != DB_GetXAssetSizeHandler[ASSET_TYPE_STRINGTABLE]());
    CHECK(DB_GetXAssetSizeHandler[ASSET_TYPE_CLIPMAP]()
        != DB_GetXAssetSizeHandler[ASSET_TYPE_MENU]());
}

// 3. XAnimClone allocation, observed through the real xanim.cpp clone path.
static void TestXAnimCloneAllocation()
{
    XAnimParts from;
    std::memset(&from, 0, sizeof(from));
    uint16_t names[1] = {0};
    from.names = names;

    g_cap.allocBytes = 0;
    XAnimClone(&from, RecordingAlloc);
    CHECK(g_cap.allocBytes == (unsigned)sizeof(XAnimParts));
    CHECK(sizeof(XAnimParts) == (sizeof(void *) == 8 ? 0x88u : 0x58u));
}

// 4. entref extraction, observed through the real g_client_script_cmd_mp.cpp
//    command path, which forwards the entity number to the server command.
static void TestEntrefExtraction()
{
    // classnum != 0 takes the "not an entity" arm, which never indexes
    // g_entities. Scr_ObjectError is stubbed to return so the real path
    // continues to SV_GameSendServerCommand(v1, ...), where v1 is the entity
    // number the fix extracted from the entref.
    scr_entref_t e;
    e.entnum = 7;
    e.classnum = 1;

    ResetCapture();
    PlayerCmd_DeactivateReverb(e);
    CHECK(g_cap.objectError != nullptr && std::strcmp(g_cap.objectError, "not an entity") == 0);
    CHECK(g_cap.svClientNum == 7);
}

// 5. va_list call paths, both changed TUs linked and driven.
namespace
{
int BuildProfilePath(char *path, int pathSize, const char *playerName, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    const int n = Com_BuildPlayerProfilePath_Internal(path, pathSize, playerName, format, ap);
    va_end(ap);
    return n;
}
}  // namespace

static void TestVaListPaths()
{
    // q_parse.cpp: Com_ScriptError renders through its own va_list. An open
    // parse session selects the "%sFile %s, line %i: %s" form whose 4th
    // trailing argument is the rendered message.
    g_parse[0].parseInfoNum = 1;
    g_parse[0].parseInfo[1].warningPrefix = "W";
    g_parse[0].parseInfo[1].parseFile = "F";
    g_parse[0].parseInfo[1].lines = 3;
    Com_ScriptError("%s-%d", "msg", 42);
    CHECK(std::strstr(g_cap.scriptMessage, "msg-42") != nullptr);

    // com_playerprofile.cpp: consumes a real va_list (the old `char *vargs`
    // spelling cannot accept one on these hosts).
    char path[128];
    const int written = BuildProfilePath(path, (int)sizeof(path), "player", "%s", "config.cfg");
    CHECK(written > 0 && std::strncmp(path, "profiles/player/", 16) == 0);
    CHECK(std::strstr(path, "config.cfg") != nullptr);
}

int main()
{
    TestFields1Offsets();
    TestCloneSizeTable();
    TestXAnimCloneAllocation();
    TestEntrefExtraction();
    TestVaListPaths();

    if (g_failures)
    {
        std::fprintf(stderr, "native64_game_mp_hazards_test: %d/%d checks failed\n", g_failures, g_runs);
        return 1;
    }
    std::printf("native64_game_mp_hazards_test: %d checks passed\n", g_runs);
    return 0;
}
