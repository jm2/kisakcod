// Production declarations and bodies; trace and string services are fixture doubles.
#include <universal/kisak_abi.h>
#include <universal/surfaceflags.h>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include "collision_table.inc"
namespace {
void Require(bool value, const char *expression, int line)
{
    if (!value) {
        std::fprintf(stderr, "collision value contract line %d: %s\n", line, expression);
        std::abort();
    }
}
#define CHECK(value) Require(bool(value), #value, __LINE__)
#define iassert(value) CHECK(value)
[[noreturn]] void MyAssertHandler(const char *, int, int, const char *, ...) { std::abort(); }
int I_stricmp(const char *left, const char *right)
{
    while (*left && std::tolower(static_cast<unsigned char>(*left)) == std::tolower(static_cast<unsigned char>(*right))) {
        ++left;
        ++right;
    }
    return std::tolower(static_cast<unsigned char>(*left)) - std::tolower(static_cast<unsigned char>(*right));
}
#include "collision_from_name.inc"
#include "collision_to_name.inc"
#include "collision_table_expected.inc"
#define KISAK_SP 1
#include "collision_contents.inc"
namespace sp {
#include "collision_physics_masks.inc"
static_assert(MASK_CHARACTER == 0x0200c000);
static_assert(static_cast<uint32_t>(MASK_IGNORE_CHARACTERS) == 0xfdff3fff);
static_assert(PHYS_WORLD_CLIPMASK == 0x0280e491);
}
#undef KISAK_SP
#undef MASK_CHARACTER
#undef MASK_IGNORE_CHARACTERS
#define KISAK_MP 1
#include "collision_contents.inc"
namespace mp {
#include "collision_physics_masks.inc"
static_assert(MASK_CHARACTER == 0x02000000);
static_assert(static_cast<uint32_t>(MASK_IGNORE_CHARACTERS) == 0xfdffffff);
static_assert(PHYS_WORLD_CLIPMASK == 0x02806c91);
}
#undef KISAK_MP
static_assert(CONTENTS_NODROP == 0x80000000u);
static_assert(CONTENTS_ANY_TRIGGER == 0x405c0008);
static_assert(MASK_SHOT == 0x02806831);
static_assert(MASK_PLAYERSOLID == 0x02810011);
static_assert(MASK_DEADSOLID == 0x00810011);
static_assert(MASK_AIMTARGET_VISIBILITY == 0x00803003);
static_assert(MASK_PLAYER_VISIBILITY == 0x02803001);
static_assert(MASK_ADS_DOF_TRACE == 0x00806c31);
static_assert(MASK_HELI_DUST_TRACE == 0x00000831);
static_assert(MASK_WEAPONCLIP == 0x00002080);
static_assert(MASK_ALL == -1 && MASK_SOLID == 1 && MASK_WATER == 32);
static_assert(SURF_INVALID == -1 && SURF_NONE == 0);
static_assert(SURF_TYPECOUNT == 29 && SURF_TYPE_MASK == 0x01f00000 && SURF_TYPE_SHIFT == 20);
static_assert(SURF_HDRPORTAL == 0x40);
constexpr uint32_t contentsBits[] = {
    CONTENTS_SOLID, CONTENTS_FOLIAGE, CONTENTS_NONCOLLIDING, CONTENTS_VEHICLETRIGGER,
    CONTENTS_GLASS, CONTENTS_WATER, CONTENTS_CANSHOOTCLIP, CONTENTS_MISSILECLIP,
    CONTENTS_ITEM, CONTENTS_VEHICLECLIP, CONTENTS_ITEMCLIP, CONTENTS_SKY,
    CONTENTS_AI_NOSIGHT, CONTENTS_CLIPSHOT, CONTENTS_ACTOR, CONTENTS_FAKE_ACTOR,
    CONTENTS_PLAYERCLIP, CONTENTS_MONSTERCLIP, CONTENTS_AXISTRIGGER, CONTENTS_ALLIESTRIGGER,
    CONTENTS_NEUTRALTRIGGER, CONTENTS_USE, CONTENTS_NONSENTIENTTRIGGER, CONTENTS_VEHICLE,
    CONTENTS_MANTLE, CONTENTS_PLAYER, CONTENTS_CORPSE, CONTENTS_DETAIL,
    CONTENTS_STRUCTURAL, CONTENTS_TRANSLUCENT, CONTENTS_PLAYERTRIGGER, CONTENTS_NODROP
};
static_assert(CONTENTS_AI_AVOID == 4 && CONTENTS_LOOKAT == 0x20000000);
void CheckContents()
{
    static_assert(sizeof(contentsBits) / sizeof(contentsBits[0]) == 32);
    for (uint32_t bit = 0; bit < 32; ++bit) CHECK(contentsBits[bit] == (1u << bit));
}
void CheckTable()
{
    for (int i = 0; i < 60; ++i) {
        const auto &actual = infoParms[i];
        const auto &expected = frozenSurfaceTable[i];
        CHECK((actual.name == nullptr) == (expected.name == nullptr));
        if (actual.name) CHECK(std::strcmp(actual.name, expected.name) == 0);
        CHECK(actual.clearSolid == expected.clearSolid);
        CHECK(actual.surfaceFlags == expected.surfaceFlags);
        CHECK(actual.contents == expected.contents);
        CHECK(actual.toolFlags == expected.toolFlags);
    }
    for (int i = 1; i <= 28; ++i) {
        const char *name = frozenSurfaceTable[i - 1].name;
        CHECK(Com_SurfaceTypeFromName(name) == i);
        CHECK(std::strcmp(Com_SurfaceTypeToName(i), name) == 0);
    }
    CHECK(Com_SurfaceTypeFromName("FOLIAGE") == 8);
    CHECK(Com_SurfaceTypeFromName("opaqueglass") == -1); // Existing 28-entry name lookup boundary.
    CHECK(Com_SurfaceTypeFromName("unknown") == -1);
    CHECK(Com_SurfaceTypeFromName("DEFAULT") == 0);
    for (int value : {-1, 0, 29, 31}) CHECK(std::strcmp(Com_SurfaceTypeToName(value), "default") == 0);
}
void CheckSurfaceExtraction()
{
    for (uint32_t type = 0; type < 32; ++type) {
        const uint32_t encoded = type << 20;
        CHECK(SURF_TYPEINDEX(encoded) == type);
        CHECK(SURF_TYPEINDEX(encoded | ~0x01f00000u) == type);
        for (uint32_t bit = 0; bit < 32; ++bit) {
            const uint32_t flags = encoded | (1u << bit);
            CHECK(SURF_TYPEINDEX(flags) == ((flags >> 20) & 31));
            CHECK(SURF_TYPEINDEX(static_cast<int32_t>(flags)) == static_cast<int32_t>((flags >> 20) & 31));
        }
    }
}
struct trace_t { bool startsolid; int contents; };
struct pmove_t { int handler; int tracemask; int numtouch; int touchents[32]; };
constexpr int ENTITYNUM_WORLD = 2046;
uint16_t hitEntity{};
uint16_t Trace_GetEntityHitId(const trace_t *) { return hitEntity; }
#include "collision_add_touch.inc"
int traceCalls{};
int observedMasks[2]{};
int traceContents{};
bool traceStartsSolid{};
void Trace(trace_t *result, const float *, const float *, const float *, const float *, int, int mask)
{
    CHECK(traceCalls < 2);
    observedMasks[traceCalls++] = mask;
    result->startsolid = traceStartsSolid;
    result->contents = traceContents;
}
struct TraceHandler { decltype(&Trace) trace; };
TraceHandler pmoveHandlers[] = {{Trace}};
#include "collision_player_trace.inc"
void CheckPlayerTrace(int contents, bool startSolid, uint16_t entity)
{
    traceContents = contents;
    traceStartsSolid = startSolid;
    hitEntity = entity;
    traceCalls = 0;
    pmove_t pm{};
    pm.handler = 0;
    pm.tracemask = -1;
    trace_t result{};
    const float point[3]{};
    PM_playerTrace(&pm, &result, point, point, point, point, 1, -1);
    const bool retry = startSolid && (static_cast<uint32_t>(contents) & 0x02000000u) != 0;
    CHECK(traceCalls == (retry ? 2 : 1));
    CHECK(observedMasks[0] == -1);
    CHECK(static_cast<uint32_t>(pm.tracemask) == (retry ? 0xfdffffffu : 0xffffffffu));
    if (retry) CHECK(static_cast<uint32_t>(observedMasks[1]) == 0xfdffffffu);
    CHECK(pm.numtouch == int(retry && entity != 2046));
    if (pm.numtouch) CHECK(pm.touchents[0] == entity);
}
void CheckPlayerFiltering()
{
    for (bool solid : {false, true})
        for (uint16_t entity : {uint16_t(0), uint16_t(2046)}) {
            CheckPlayerTrace(0, solid, entity);
            CheckPlayerTrace(-1, solid, entity);
            for (uint32_t bit = 0; bit < 32; ++bit) CheckPlayerTrace(static_cast<int32_t>(1u << bit), solid, entity);
        }
}
} // namespace
void RunCollisionValueContracts()
{
    CheckContents();
    CheckTable();
    CheckSurfaceExtraction();
    CheckPlayerFiltering();
}
