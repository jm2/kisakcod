// native64_runtime_layout_test: contract tests for the bead-5 (ki-4omyh)
// RUNTIME_SIZE migration of the runtime-only structs that used to fail the
// win64/lin64/a64 builds as raw `static_assert(sizeof(T) == N)`. Each
// migrated struct now asserts the ILP32 retail size on 32-bit targets and
// the natural widened size on 64-bit targets via RUNTIME_SIZE(T, n32, n64).
// This test pins both halves of that contract:
//
//   1. Headers whose include chain compiles in a portable TU
//      (universal/q_parse.h, game/enthandle.h, bgame/bg_local.h,
//      game_mp/g_main_mp.h) are included for real, so the migrated
//      RUNTIME_SIZE asserts inside them fire on this target and the widened
//      sizes are re-checked at runtime against the exact constants the
//      headers declare.
//
//   2. Headers whose include chain is still production-bound
//      (game_mp/g_public_mp.h -> xanim/xanim.h -> gfx_d3d/r_font.h,
//      game/game_public.h, DynEntity/DynEntity_client.h,
//      aim_assist/aim_assist.h) cannot be included at 64-bit until bead 7
//      migrates the asset-class asserts in xanim/xmodel/snd/gfx. Their
//      runtime-only structs are mirrored member-for-member in this TU.
//      Each mirror asserts the same RUNTIME_SIZE(n32, n64) constants as its
//      engine counterpart, so the constants are proven to be the compiler's
//      natural layout on BOTH widths, not guesses.
//
// The n32 arm is the Windows x86 contract: it must stay byte-identical to
// the retail ILP32 layout. The n64 arm is the LP64/LLP64 layout the 64-bit
// engine will use. Sizes were measured from the compiler on both
// x86_64-linux-gnu and x86_64-w64-mingw32 and are identical there (no
// width-load-bearing `long` in any migrated struct).

#include <universal/kisak_abi.h>

#include <universal/q_parse.h>
#include <game/enthandle.h>
#include <bgame/bg_local.h>
#include <game_mp/g_main_mp.h>

#include <script/scr_variable.h>

#include <cstdint>
#include <cstdio>

// Forward declarations for pointer-only members of mirrored structs; the
// mirrors never dereference these, they only need the pointer width.
struct gentity_s;
struct gclient_s;
struct game_hudelem_s;
struct XModel;
struct XAnimTree_s;
struct playerState_s;

namespace native64_runtime_layout_test
{
namespace
{
int g_failures = 0;
int g_runs = 0;

bool Evaluate(bool cond, const char *const expr, const char *const file, int line)
{
    ++g_runs;
    if (!cond)
    {
        std::fprintf(stderr, "native64_runtime_layout_test: %s:%d: %s\n", file, line, expr);
        ++g_failures;
        return false;
    }
    return true;
}
}  // namespace
}  // namespace native64_runtime_layout_test

#define CHECK(expr) \
    native64_runtime_layout_test::Evaluate((expr), #expr, __FILE__, __LINE__)

namespace
{
// Width selector mirroring the RUNTIME_SIZE macro semantics: the runtime
// checks below spell both constants (n32, n64) and take the one this
// target's pointer width selects, exactly like the compile-time asserts in
// the headers and mirrors.
constexpr std::size_t Width(std::size_t n32, std::size_t n64)
{
    return KISAK_ARCH_64BIT ? n64 : n32;
}

// ---------------------------------------------------------------------------
// Portable-header runtime re-checks. The compile-time RUNTIME_SIZE asserts
// already ran while including the real headers; these pin the exact declared
// constants so a constant edit that silently breaks one width surfaces here.
// ---------------------------------------------------------------------------
void CheckPortableHeaderWidening()
{
    // universal/q_parse.h (ki-4omyh): parser globals hold char*/const char*.
    CHECK(sizeof(parseInfo_t) == Width(0x420, 0x438));
    CHECK(sizeof(ParseThreadInfo) == Width(0x460C, 0x4798));
    CHECK(sizeof(com_parse_mark_t) == Width(0x14, 0x20));

    // game/enthandle.h (ki-4omyh): the entity-handle slot carries a void*.
    CHECK(sizeof(EntHandleInfo) == Width(0x8, 0x10));

    // bgame/bg_local.h (ki-4omyh): the 17 runtime anim/ps globals that used
    // to hard-fail the 64-bit asserts. clientInfo_t/bgs_t* are MP-only.
    CHECK(sizeof(pml_t) == Width(0x80, 0x88));
    CHECK(sizeof(animStringItem_t) == Width(0x8, 0x10));
    CHECK(sizeof(animConditionTable_t) == Width(0x8, 0x10));
    CHECK(sizeof(shellshock_t) == Width(0x20, 0x28));
    CHECK(sizeof(animScriptCommand_t) == Width(0x10, 0x18));
    CHECK(sizeof(animScriptItem_t) == Width(0x100, 0x140));
    CHECK(sizeof(animScript_t) == Width(0x204, 0x408));
    CHECK(sizeof(animScriptData_t) == Width(0x9A9D0, 0xC8390));
    CHECK(sizeof(lerpFrame_t) == Width(0x30, 0x38));
    CHECK(sizeof(clientInfo_t) == Width(0x4CC, 0x4E8));
    CHECK(sizeof(bgs_t_human) == Width(0x10, 0x18));
    CHECK(sizeof(bgs_t) == Width(0xADD08, 0xDBDE8));
    CHECK(sizeof(CEntPlayerInfo) == Width(0xC, 0x10));
    CHECK(sizeof(CEntFx) == Width(0x8, 0x10));
    CHECK(sizeof(BulletTraceResults) == Width(0x44, 0x50));
    CHECK(sizeof(viewState_t) == Width(0x24, 0x30));
    CHECK(sizeof(weaponState_t) == Width(0x54, 0x60));

    // game_mp/g_main_mp.h (ki-4omyh): the handler vtable and the MP level
    // globals both embed pointers.
    CHECK(sizeof(entityHandler_t) == Width(0x28, 0x48));
    CHECK(sizeof(level_locals_t) == Width(0x2E6C, 0x3098));
}

// ---------------------------------------------------------------------------
// Mirrors for headers whose include chain cannot compile in a portable TU
// yet (bead 7 still has raw asset-class asserts in xanim/xmodel/snd/gfx).
// Member lists are transcribed verbatim from the engine headers; the
// RUNTIME_SIZE constants are the exact ones the headers declare. Where the
// engine header's member type is itself a portable engine type (clientInfo_t
// from bgame/bg_local.h, trace_t from universal/q_shared.h, scr_entref_t
// from script/scr_variable.h) the mirror uses that real type, so the layout
// arithmetic is the engine's own.
// ---------------------------------------------------------------------------

// game_mp/g_public_mp.h (ki-4omyh)
struct MirBuiltinFunctionDef
{
    const char *actionString;
    void (*actionFunc)();
    std::int32_t type;
};
RUNTIME_SIZE(MirBuiltinFunctionDef, 0xC, 0x18);

struct MirBuiltinMethodDef
{
    const char *actionString;
    void (*actionFunc)(scr_entref_t);
    std::int32_t type;
};
RUNTIME_SIZE(MirBuiltinMethodDef, 0xC, 0x18);

struct MirUseList
{
    gentity_s *ent;
    float score;
};
RUNTIME_SIZE(MirUseList, 0x8, 0x10);

struct MirGameTypeScript
{
    char pszScript[64];
    char pszName[64];
    std::int32_t bTeamBased;
};
RUNTIME_SIZE(MirGameTypeScript, 0x84, 0x84);

struct MirScrDataS
{
    std::int32_t main;
    std::int32_t startupgametype;
    std::int32_t playerconnect;
    std::int32_t playerdisconnect;
    std::int32_t playerdamage;
    std::int32_t playerkilled;
    std::int32_t votecalled;
    std::int32_t playervote;
    std::int32_t playerlaststand;
    std::int32_t iNumGameTypes;
    MirGameTypeScript list[32];
};
RUNTIME_SIZE(MirScrDataS, 0x10A8, 0x10A8);

struct MirCorpseInfo
{
    XAnimTree_s *tree;
    std::int32_t entnum;
    std::int32_t levelTime; // mirrors corpseInfo_t.time (level-time counter)
    clientInfo_t ci; // real type from bgame/bg_local.h
    bool falling;
    // padding byte
    // padding byte
    // padding byte
};
RUNTIME_SIZE(MirCorpseInfo, 0x4DC, 0x500);

struct MirScrData
{
    std::int32_t levelscript;
    std::int32_t gametypescript;
    MirScrDataS gametype;
    std::int32_t delete_;
    std::int32_t initstructs;
    std::int32_t createstruct;
    MirCorpseInfo playerCorpseInfo[8];
};
RUNTIME_SIZE(MirScrData, 0x379C, 0x38C0);

// game/game_public.h (ki-4omyh). fieldtype_t is a fixed-underlying-type
// enum (int32_t / __int32), so the mirrors spell that exact width.
struct MirClientFields
{
    const char *name;
    std::int32_t ofs;
    std::int32_t type; // fieldtype_t
    void (*setter)(gclient_s *, const MirClientFields *);
    void (*getter)(gclient_s *, const MirClientFields *);
};
RUNTIME_SIZE(MirClientFields, 0x14, 0x20);

struct MirVehicleLocalPhysics
{
    trace_t groundTrace; // real type from universal/q_shared.h
    std::int32_t hasGround;
    std::int32_t onGround;
};
RUNTIME_SIZE(MirVehicleLocalPhysics, 0x34, 0x38);

struct MirGameHudelemField
{
    const char *name;
    std::int32_t ofs;
    std::int32_t type; // fieldtype_t
    std::int32_t mask;
    std::int32_t shift;
    void (*setter)(game_hudelem_s *, int);
    void (*getter)(game_hudelem_s *, int);
};
RUNTIME_SIZE(MirGameHudelemField, 0x1C, 0x28);

// DynEntity/DynEntity_client.h (ki-4omyh). DynEntityDef stays raw for bead 7.
struct MirDynEntityAreaParms
{
    const float *mins;
    const float *maxs;
    std::int32_t contentMask;
    std::uint16_t *list;
    std::uint16_t maxCount;
    std::uint16_t count;
};
RUNTIME_SIZE(MirDynEntityAreaParms, 0x14, 0x28);

struct MirBreakablePiece
{
    const XModel *model;
    std::int32_t physObjId;
    std::uint16_t lightingHandle;
    bool active;
    // padding byte
};
RUNTIME_SIZE(MirBreakablePiece, 0xC, 0x10);

struct MirDynEntityProps
{
    const char *name;
    bool clientOnly;
    bool clipMove;
    bool usePhysics;
    bool destroyable;
};
RUNTIME_SIZE(MirDynEntityProps, 0x8, 0x10);

// aim_assist/aim_assist.h (ki-4omyh)
struct MirAimInput
{
    float deltaTime;
    float pitch;
    float pitchAxis;
    float pitchMax;
    float yaw;
    float yawAxis;
    float yawMax;
    float forwardAxis;
    float rightAxis;
    std::int32_t buttons;
    std::int32_t localClientNum;
    const playerState_s *ps;
};
RUNTIME_SIZE(MirAimInput, 0x30, 0x38);

void CheckMirroredStructWidening()
{
    // game_mp/g_public_mp.h mirrors
    CHECK(sizeof(MirBuiltinFunctionDef) == Width(0xC, 0x18));
    CHECK(sizeof(MirBuiltinMethodDef) == Width(0xC, 0x18));
    CHECK(sizeof(MirUseList) == Width(0x8, 0x10));
    CHECK(sizeof(MirGameTypeScript) == Width(0x84, 0x84));
    CHECK(sizeof(MirScrDataS) == Width(0x10A8, 0x10A8));
    CHECK(sizeof(MirCorpseInfo) == Width(0x4DC, 0x500));
    CHECK(sizeof(MirScrData) == Width(0x379C, 0x38C0));

    // game/game_public.h mirrors
    CHECK(sizeof(MirClientFields) == Width(0x14, 0x20));
    CHECK(sizeof(MirVehicleLocalPhysics) == Width(0x34, 0x38));
    CHECK(sizeof(MirGameHudelemField) == Width(0x1C, 0x28));

    // DynEntity/DynEntity_client.h mirrors
    CHECK(sizeof(MirDynEntityAreaParms) == Width(0x14, 0x28));
    CHECK(sizeof(MirBreakablePiece) == Width(0xC, 0x10));
    CHECK(sizeof(MirDynEntityProps) == Width(0x8, 0x10));

    // aim_assist/aim_assist.h mirror
    CHECK(sizeof(MirAimInput) == Width(0x30, 0x38));
}

}  // namespace

int main()
{
    CheckPortableHeaderWidening();
    CheckMirroredStructWidening();

    if (native64_runtime_layout_test::g_failures != 0)
    {
        std::fprintf(stderr,
                     "native64_runtime_layout_test: %d of %d checks FAILED\n",
                     native64_runtime_layout_test::g_failures,
                     native64_runtime_layout_test::g_runs);
        return 1;
    }
    std::printf("native64_runtime_layout_test: %d checks passed\n",
                native64_runtime_layout_test::g_runs);
    return 0;
}
