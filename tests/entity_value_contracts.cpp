// Production declarations and bodies; engine services are minimal doubles.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <type_traits>
#ifndef _MSC_VER
#define __cdecl
#define __int32 std::int32_t
#endif
#ifdef __clang__
#pragma clang diagnostic ignored "-Wdollar-in-identifier-extension"
#endif
namespace {
void Require(bool value, const char *expression, int line)
{
    if (!value) {
        std::fprintf(stderr, "entity value contract line %d: %s\n", line, expression);
        std::abort();
    }
}
#define CHECK(value) Require(bool(value), #value, __LINE__)
#include "turret_flags_enum.inc"
#include "physics_world_enum.inc"
#include "physics_geom_enum.inc"
namespace mp {
#define KISAK_MP 1
#include "entity_type_enums.inc"
#undef KISAK_MP
}
namespace sp {
#define KISAK_SP 1
#include "entity_type_enums.inc"
#undef KISAK_SP
}
#include "entity_value_expected.inc"
static_assert(sizeof(decltype(TURRET_REQUIRES_AI)) == 4);
static_assert(sizeof(PhysWorld) == 4 && sizeof(PhysicsGeomType) == 4);
static_assert(sizeof(sp::entityType_t) == 4 && sizeof(mp::entityType_t) == 4);
int diagnostics;
constexpr bool alwaysfails = false;
void MyAssertHandler(const char *, int, int, const char *, ...) { ++diagnostics; }
const char *va(const char *, ...) { return "fixture diagnostic"; }
struct dxWorld {};
struct { dxWorld *world[3]; } physGlob{};
#include "physics_world_index_body.inc"
void CheckWorldLookup()
{
    dxWorld worlds[4];
    for (int i = 0; i < 3; ++i) physGlob.world[i] = &worlds[i];
    diagnostics = 0;
    for (int i = 0; i < 3; ++i) CHECK(Phys_IndexFromODEWorld(&worlds[i]) == i);
    CHECK(diagnostics == 0);
    CHECK(Phys_IndexFromODEWorld(&worlds[3]) == 3);
    CHECK(diagnostics == 1);
    for (auto &world : physGlob.world) world = nullptr;
}
namespace sp {
#include "entity_event_checks.inc"
#include "entity_light_vis_body.inc"
using scr_entref_t = int;
struct TurretInfo { int flags; };
struct gentity_s { TurretInfo *pTurretInfo; unsigned int classname; };
struct {
    unsigned int auto_ai;
    unsigned int manual;
    unsigned int manual_ai;
    unsigned int auto_nonai;
} scr_const{};
TurretInfo turret{};
gentity_s entity{};
unsigned int mode;
struct ScriptFailure {};
gentity_s *GetEntity(scr_entref_t entref) { CHECK(entref == 0); return &entity; }
const char *SL_ConvertToString(unsigned int) { return "turret"; }
void Scr_Error(const char *) { throw ScriptFailure{}; }
unsigned int Scr_GetConstString(int index) { CHECK(index == 0); return mode; }
#include "turret_mode_body.inc"
void CheckTurretModes()
{
    scr_const.auto_ai = 1;
    scr_const.manual = 2;
    scr_const.manual_ai = 3;
    scr_const.auto_nonai = 4;
    entity.pTurretInfo = &turret;
    entity.classname = 0;
    constexpr int modeBits[] = {3, 0, 1, 2};
    for (unsigned int nextMode = 1; nextMode <= 4; ++nextMode) {
        mode = nextMode;
        for (int flags = 0; flags < 65536; ++flags) {
            turret.flags = flags;
            GScr_SetMode(0);
            CHECK(turret.flags == ((flags & ~3) | modeBits[nextMode - 1]));
        }
        turret.flags = (std::numeric_limits<int>::min)();
        GScr_SetMode(0);
        CHECK(turret.flags == ((std::numeric_limits<int>::min)() | modeBits[nextMode - 1]));
    }
    mode = 5;
    turret.flags = 0x1234;
    bool rejected = false;
    try { GScr_SetMode(0); } catch (const ScriptFailure &) { rejected = true; }
    CHECK(rejected && turret.flags == 0x1234);
}
void CheckEntityClassification()
{
    for (int kind = -1; kind <= 18; ++kind) {
        diagnostics = 0;
        GScr_ValidateLightVis(kind);
        CHECK(diagnostics == int(kind != 0 && kind != 5));
    }
    for (int kind : {(std::numeric_limits<int>::min)(), -1, 0, 16, 17, 18, (std::numeric_limits<int>::max)()}) {
        const auto entityKind = static_cast<entityType_t>(kind);
        const bool event = static_cast<uint32_t>(kind) >= 17;
        CHECK(eventChecks[0](entityKind) == !event);
        CHECK(eventChecks[1](entityKind) == event);
        CHECK(eventChecks[2](entityKind) == event);
    }
}
} // namespace sp
} // namespace
void RunEntityValueContracts()
{
    CheckWorldLookup();
    sp::CheckTurretModes();
    sp::CheckEntityClassification();
}
