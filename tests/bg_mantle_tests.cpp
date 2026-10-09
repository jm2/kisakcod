// bg_mantle_tests.cpp: the production mantle code (bgame/bg_mantle.cpp) over
// the production pmove trace (bg_pmove.cpp: PM_trace) and angle math
// (com_math.cpp, com_angle.cpp), compiled as the headless server compiles
// them. The dvars register through Mantle_RegisterDvars into a stub registry,
// so the checks run against the shipped defaults. Stubbed: the collision world
// (G_TraceCapsule sweeps the box through axis-aligned solids, some carrying
// the mantle surface flags) and the mantle animations (fixed lengths and
// straight-line root deltas, so a finished mantle must land on its end
// position exactly).

#include <bgame/bg_local.h>
#include <bgame/bg_public.h>
#include <xanim/xanim.h>

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <limits>
#include <memory>
#include <vector>

// bg_mantle.cpp's globals; no header declares them.
extern const dvar_t *mantle_enable;
extern const dvar_t *mantle_check_range;
extern const dvar_t *mantle_check_radius;
extern const dvar_t *mantle_check_angle;
extern const dvar_t *mantle_view_yawcap;
extern XAnim_s *s_mantleAnims;

namespace
{
int g_failures = 0;
std::deque<dvar_t> g_dvars;
// s_mantleTrans heights (internal to bg_mantle.cpp); up anim i rises by kUpHeight[i - 1].
constexpr float kUpHeight[7] = {57.0f, 51.0f, 45.0f, 39.0f, 33.0f, 27.0f, 21.0f};
std::vector<int> g_events;
int g_animCalls = 0;

void Check(const bool ok, const char *const what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

bool Near(const double a, const double b, const double tolerance)
{
    return std::fabs(a - b) <= tolerance;
}

struct Solid
{
    float mins[3];
    float maxs[3];
    int surfaceFlags;
};

constexpr float kFar = 1000.0f;
constexpr float kSurfaceEpsilon = 0.125f;
constexpr int kMantleOn = 0x2000000;
constexpr int kMantleOver = 0x4000000;
const Solid kFloor{{-kFar, -kFar, -100.0f}, {kFar, kFar, 0.0f}, 0};
std::vector<Solid> g_world;

// Animation stand-ins: up anims rise by their transition's height, over anims
// carry the player 31 units forward.
constexpr int kUpMsec = 400;
constexpr int kOverMsec = 300;
alignas(8) uint8_t g_animStorage[8];

bool InsideOpen(const Solid &solid, const float *const point)
{
    for (int i = 0; i < 3; ++i)
    {
        if (point[i] <= solid.mins[i] || point[i] >= solid.maxs[i])
            return false;
    }
    return true;
}

// Slab test: where a ray from start along delta enters solid, or false.
bool EnterSolid(const Solid &solid, const float *const start, const float *const delta, float *const enter,
    int *const axis, float *const sign)
{
    float tEnter = -std::numeric_limits<float>::infinity();
    float tExit = std::numeric_limits<float>::infinity();
    for (int i = 0; i < 3; ++i)
    {
        if (delta[i] == 0.0f)
        {
            if (start[i] <= solid.mins[i] || start[i] >= solid.maxs[i])
                return false;
            continue;
        }
        const bool forward = delta[i] > 0.0f;
        const float t0 = ((forward ? solid.mins[i] : solid.maxs[i]) - start[i]) / delta[i];
        const float t1 = ((forward ? solid.maxs[i] : solid.mins[i]) - start[i]) / delta[i];
        if (t0 > tEnter)
        {
            tEnter = t0;
            *axis = i;
            *sign = forward ? -1.0f : 1.0f;
        }
        tExit = std::fmin(tExit, t1);
    }
    if (tEnter > tExit || tEnter < 0.0f || tEnter > 1.0f)
        return false;
    *enter = tEnter;
    return true;
}

std::unique_ptr<playerState_s> NewPlayer()
{
    auto ps = std::make_unique<playerState_s>();
    ps->origin[2] = kSurfaceEpsilon;
    ps->groundEntityNum = ENTITYNUM_WORLD;
    return ps;
}

struct Move
{
    pmove_t pm{};
    pml_t pml{};

    explicit Move(playerState_s *const ps)
    {
        pm.ps = ps;
        pm.tracemask = 1;
        pm.cmd.buttons = 0x400; // jump
        pml.forward[0] = 1.0f;
    }
};

// A crate whose near face is at x = 30 and whose top is at height.
std::vector<Solid> CrateWorld(const float height, const int surfaceFlags)
{
    return {kFloor, {{30.0f, -kFar, -100.0f}, {200.0f, kFar, height}, surfaceFlags}};
}

void TestDefaultsAndTransitions()
{
    Check(mantle_enable && mantle_enable->current.enabled && Near(mantle_check_range->current.value, 20.0, 0)
            && Near(mantle_check_radius->current.value, 0.1, 1e-6) && Near(mantle_check_angle->current.value, 60.0, 0)
            && Near(mantle_view_yawcap->current.value, 60.0, 0),
        "mantle dvar defaults");
    Check(Mantle_FindTransition(0.0f, 57.0f) == 0 && Mantle_FindTransition(0.0f, 40.0f) == 3
            && Mantle_FindTransition(0.0f, 22.0f) == 6 && Mantle_FindTransition(10.0f, 100.0f) == 0,
        "nearest mantle transition by height");
}

void RunMantleToEnd(Move &move, playerState_s *const ps)
{
    move.pml.msec = 100;
    for (int step = 0; step < 32 && (ps->pm_flags & PMF_MANTLE) != 0; ++step)
        Mantle_Move(&move.pm, ps, &move.pml);
}

void TestMantleOn()
{
    g_world = CrateWorld(40.0f, kMantleOn);
    auto ps = NewPlayer();
    Move move(ps.get());
    Mantle_Check(&move.pm, &move.pml);
    Check((ps->pm_flags & PMF_MANTLE) != 0 && (ps->eFlags & 0x8000) != 0 && move.pm.mantleStarted,
        "mantle onto a 40-unit crate starts");
    Check(ps->mantleState.transIndex == 3 && (ps->mantleState.flags & 1) == 0 && (ps->mantleState.flags & 8) != 0,
        "39-unit transition, mantle on (not over), hint set");
    Check(Near(move.pm.mantleEndPos[0], 16.0, 0.01) && Near(move.pm.mantleEndPos[2], 40.0 + kSurfaceEpsilon, 0.01),
        "end position on the ledge");
    Check(move.pm.mantleDuration == kUpMsec, "mantle on lasts the up animation");
    // Starting point is the end position less the whole animation delta.
    Check(Near(ps->origin[2], 40.0 + kSurfaceEpsilon - 39.0, 0.01), "start offset by the up animation");
    Check(Mantle_IsWeaponInactive(ps.get()), "weapon lowered while mantling onto a ledge");

    RunMantleToEnd(move, ps.get());
    Check((ps->pm_flags & PMF_MANTLE) == 0 && (ps->mantleState.flags & 0x10) != 0 && !move.pm.mantleStarted,
        "mantle finishes");
    Check(Near(ps->origin[0], move.pm.mantleEndPos[0], 0.01) && Near(ps->origin[2], move.pm.mantleEndPos[2], 0.01),
        "the animation carries the player onto the ledge");
    Check(g_animCalls == 4, "one animation update per move");
    Check(g_events.size() == 1 && g_events[0] == EV_STANCE_FORCE_STAND && (ps->eFlags & 0x8000) == 0,
        "standing room at the end forces stand");
}

void TestMantleOver()
{
    // A 22-unit wall, 1 thick, flagged for mantling over: the 21-unit
    // transition, whose low vault keeps the weapon up. The wall must clear the
    // player's box at the end position (31 past the ledge), or the drop trace
    // lands on the wall and the mantle falls back to mantling on.
    g_world = {kFloor, {{30.0f, -kFar, -100.0f}, {31.0f, kFar, 22.0f}, kMantleOver}};
    g_events.clear();
    g_animCalls = 0;
    auto ps = NewPlayer();
    Move move(ps.get());
    Mantle_Check(&move.pm, &move.pml);
    Check((ps->pm_flags & PMF_MANTLE) != 0 && (ps->mantleState.flags & 1) != 0 && ps->mantleState.transIndex == 6,
        "mantle over a 22-unit wall");
    // Over: 31 units past the ledge and up to 18 units down, if that is clear.
    Check(Near(move.pm.mantleEndPos[0], 47.0, 0.01) && Near(move.pm.mantleEndPos[2], 22.0 + kSurfaceEpsilon - 18.0, 0.01),
        "end position past the wall");
    Check(move.pm.mantleDuration == kUpMsec + kOverMsec, "mantle over lasts both animations");
    Check(!Mantle_IsWeaponInactive(ps.get()), "the low vault keeps the weapon");
    RunMantleToEnd(move, ps.get());
    Check((ps->pm_flags & PMF_MANTLE) == 0 && Near(ps->origin[0], 47.0, 0.01)
            && Near(ps->origin[2], move.pm.mantleEndPos[2], 0.01) && g_animCalls == 7,
        "vaulted past the wall");

    // A wall too thick to clear at the end position falls back to mantling on.
    g_world = {kFloor, {{30.0f, -kFar, -100.0f}, {40.0f, kFar, 22.0f}, kMantleOver}};
    auto thick = NewPlayer();
    Move onto(thick.get());
    Mantle_Check(&onto.pm, &onto.pml);
    Check((thick->pm_flags & PMF_MANTLE) != 0 && (thick->mantleState.flags & 1) == 0
            && Near(onto.pm.mantleEndPos[0], 16.0, 0.01) && onto.pm.mantleDuration == kUpMsec,
        "a thick wall is mantled onto, not over");
}

void TestNoMantle()
{
    struct Case
    {
        std::vector<Solid> world;
        uint32_t pmFlags;
        const char *what;
    };
    const Case cases[] = {
        {CrateWorld(40.0f, 0), 0, "a surface without mantle flags"},
        {CrateWorld(100.0f, kMantleOn), 0, "a ledge above 60 units"},
        {CrateWorld(40.0f, kMantleOn), PMF_MANTLE, "a player already mantling"},
    };
    for (const Case &c : cases)
    {
        g_world = c.world;
        auto ps = NewPlayer();
        ps->pm_flags = c.pmFlags;
        Move move(ps.get());
        Mantle_Check(&move.pm, &move.pml);
        Check((ps->mantleState.flags & 8) == 0 && !move.pm.mantleStarted, c.what);
    }

    // Facing 70 degrees off the surface normal is past the 60-degree check.
    g_world = CrateWorld(40.0f, kMantleOn);
    auto ps = NewPlayer();
    Move move(ps.get());
    move.pml.forward[0] = std::cos(70.0f * 3.14159265f / 180.0f);
    move.pml.forward[1] = std::sin(70.0f * 3.14159265f / 180.0f);
    Mantle_Check(&move.pm, &move.pml);
    Check(!move.pm.mantleStarted, "not facing the mantle surface");

    // Without jump the ledge is only hinted.
    move.pml.forward[0] = 1.0f;
    move.pml.forward[1] = 0.0f;
    move.pm.cmd.buttons = 0;
    Mantle_Check(&move.pm, &move.pml);
    Check((ps->mantleState.flags & 8) != 0 && !move.pm.mantleStarted, "hint without jump");
}

void TestCapView()
{
    auto ps = NewPlayer();
    ps->pm_flags = PMF_MANTLE;
    ps->mantleState.yaw = 0.0f;
    ps->viewangles[1] = 30.0f;
    Mantle_CapView(ps.get());
    Check(Near(ps->viewangles[1], 30.0, 1e-4), "a turn inside the cap is kept");
    ps->viewangles[1] = 100.0f;
    Mantle_CapView(ps.get());
    Check(Near(ps->viewangles[1], 60.0, 1e-3), "a turn past the cap is held at 60 degrees");
}
} // namespace

// The engine boundary bg_mantle.cpp and the pmove trace reach.
void MyAssertHandler(const char *file, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "engine assert %s:%d %s\n", file, line, fmt);
    std::abort();
}

char *QDECL va(const char *, ...)
{
    static char text[] = "";
    return text;
}

const dvar_s *__cdecl Dvar_RegisterFloat(const char *name, float value, DvarLimits, uint16_t, const char *)
{
    g_dvars.emplace_back();
    g_dvars.back().name = name;
    g_dvars.back().current.value = value;
    return &g_dvars.back();
}

const dvar_s *__cdecl Dvar_RegisterBool(const char *name, bool value, uint16_t, const char *)
{
    g_dvars.emplace_back();
    g_dvars.back().name = name;
    g_dvars.back().current.enabled = value;
    return &g_dvars.back();
}

int __cdecl XAnimGetLengthMsec(const XAnim_s *anims, uint32_t animIndex)
{
    if (anims != reinterpret_cast<const XAnim_s *>(g_animStorage))
        std::abort();
    return animIndex >= 8 ? kOverMsec : kUpMsec;
}

void __cdecl XAnimGetAbsDelta(const XAnim_s *anims, uint32_t animIndex, float *rot, float *trans, float time)
{
    if (anims != reinterpret_cast<const XAnim_s *>(g_animStorage))
        std::abort();
    rot[0] = 0.0f;
    rot[1] = 1.0f;
    trans[0] = trans[1] = trans[2] = 0.0f;
    if (animIndex >= 8)
        trans[0] = 31.0f * time;
    else
        trans[2] = kUpHeight[animIndex - 1] * time;
}

void __cdecl G_TraceCapsule(trace_t *results, const float *start, const float *mins, const float *maxs,
    const float *end, int, int)
{
    *results = trace_t();
    results->fraction = 1.0f;
    const float delta[3] = {end[0] - start[0], end[1] - start[1], end[2] - start[2]};
    const float length = std::sqrt(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
    for (const Solid &solid : g_world)
    {
        Solid grown{};
        for (int i = 0; i < 3; ++i)
        {
            grown.mins[i] = solid.mins[i] - maxs[i];
            grown.maxs[i] = solid.maxs[i] - mins[i];
        }
        if (InsideOpen(grown, start))
        {
            results->startsolid = results->allsolid = true;
            results->fraction = 0.0f;
            return;
        }
        float enter = 0.0f;
        int axis = 0;
        float sign = 0.0f;
        if (length <= 0.0f || !EnterSolid(grown, start, delta, &enter, &axis, &sign))
            continue;
        const float fraction = std::fmax(0.0f, (enter * length - kSurfaceEpsilon) / length);
        if (fraction < results->fraction)
        {
            results->fraction = fraction;
            results->normal[0] = results->normal[1] = results->normal[2] = 0.0f;
            results->normal[axis] = sign;
            results->walkable = axis == 2 && sign > 0.0f;
            results->surfaceFlags = solid.surfaceFlags;
        }
    }
}

void __cdecl G_PlayerEvent(int, int)
{
}

void __cdecl BG_AddPredictableEventToPlayerstate(entity_event_t newEvent, uint32_t, playerState_s *)
{
    g_events.push_back(newEvent);
}

int32_t __cdecl BG_AnimScriptAnimation(playerState_s *, aistateEnum_t, scriptAnimMoveTypes_t, int32_t)
{
    ++g_animCalls;
    return 0;
}

int32_t __cdecl BG_AnimScriptEvent(playerState_s *, scriptAnimEventTypes_t, int32_t, int32_t)
{
    return 0;
}

int main()
{
    Mantle_RegisterDvars();
    s_mantleAnims = reinterpret_cast<XAnim_s *>(g_animStorage);
    TestDefaultsAndTransitions();
    TestMantleOn();
    TestMantleOver();
    TestNoMantle();
    TestCapView();
    if (g_failures == 0)
        std::puts("bg mantle contracts passed");
    return g_failures == 0 ? 0 : 1;
}
