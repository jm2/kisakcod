#include <cstddef>
#include <cstdint>
#include <type_traits>

#if !defined(_MSC_VER)
#define __int32 int
#endif

// Keep this forward declaration byte-for-byte aligned with bg_public.h. The
// source contract pins the production spelling; compiling this declaration
// followed by teams.h makes Clang/GCC/MSVC diagnose an underlying-type drift.
enum team_t : __int32;
#include "game/teams.h"

enum MissileStageProbe : __int32
{
    MISSILE_STAGE_PROBE = 0
};

enum MissileFlightModeProbe : __int32
{
    MISSILE_FLIGHT_MODE_PROBE = 0
};

struct MissileLayoutProbe
{
#if defined(KISAK_SP)
    float predictLandPos[3];
    int predictLandTime;
    int timestamp;
#endif
    float time;
    std::int32_t timeOfBirth;
    float travelDist;
    float surfaceNormal[3];
    team_t team;
#if defined(KISAK_SP)
    int thrownBack;
#endif
    float curvature[3];
    float targetOffset[3];
    MissileStageProbe stage;
    MissileFlightModeProbe flightMode;
};

static_assert(std::is_same_v<std::underlying_type_t<team_t>, int>);
static_assert(sizeof(team_t) == sizeof(std::int32_t));

#if defined(KISAK_MP)
static_assert(sizeof(MissileLayoutProbe) == 0x3C);
static_assert(offsetof(MissileLayoutProbe, time) == 0x0);
static_assert(offsetof(MissileLayoutProbe, timeOfBirth) == 0x4);
static_assert(offsetof(MissileLayoutProbe, travelDist) == 0x8);
static_assert(offsetof(MissileLayoutProbe, surfaceNormal) == 0xC);
static_assert(offsetof(MissileLayoutProbe, team) == 0x18);
static_assert(offsetof(MissileLayoutProbe, curvature) == 0x1C);
static_assert(offsetof(MissileLayoutProbe, targetOffset) == 0x28);
static_assert(offsetof(MissileLayoutProbe, stage) == 0x34);
static_assert(offsetof(MissileLayoutProbe, flightMode) == 0x38);
#elif defined(KISAK_SP)
static_assert(sizeof(MissileLayoutProbe) == 0x54);
static_assert(offsetof(MissileLayoutProbe, predictLandPos) == 0x0);
static_assert(offsetof(MissileLayoutProbe, predictLandTime) == 0xC);
static_assert(offsetof(MissileLayoutProbe, timestamp) == 0x10);
static_assert(offsetof(MissileLayoutProbe, time) == 0x14);
static_assert(offsetof(MissileLayoutProbe, timeOfBirth) == 0x18);
static_assert(offsetof(MissileLayoutProbe, travelDist) == 0x1C);
static_assert(offsetof(MissileLayoutProbe, surfaceNormal) == 0x20);
static_assert(offsetof(MissileLayoutProbe, team) == 0x2C);
static_assert(offsetof(MissileLayoutProbe, thrownBack) == 0x30);
static_assert(offsetof(MissileLayoutProbe, curvature) == 0x34);
static_assert(offsetof(MissileLayoutProbe, targetOffset) == 0x40);
static_assert(offsetof(MissileLayoutProbe, stage) == 0x4C);
static_assert(offsetof(MissileLayoutProbe, flightMode) == 0x50);
#else
#error "missile layout compile test requires KISAK_MP or KISAK_SP"
#endif

int main()
{
    return 0;
}
