// bg_slidemove_tests.cpp: the production player slide and step code
// (bgame/bg_slidemove.cpp) over the production pmove helpers (bg_pmove.cpp:
// PM_playerTrace, PM_ClipVelocity, PM_ProjectVelocity) and jump state
// (bg_jump.cpp), compiled as the headless server compiles them. Only the
// collision world is stubbed: G_TraceCapsule sweeps the player's box against
// a few axis-aligned solids. Section GC drops the pmove paths the moves never
// reach; com_math_test_stubs.cpp's MyAssertHandler aborts on any engine assert.

#include <bgame/bg_local.h>
#include <bgame/bg_public.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>

namespace
{
int g_failures = 0;

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
};

std::vector<Solid> g_world;

constexpr float kFar = 1000.0f;
constexpr float kSurfaceEpsilon = 0.125f;
const Solid kFloor{{-kFar, -kFar, -100.0f}, {kFar, kFar, 0.0f}};

// Where a ray from start along delta enters an expanded solid, or false.
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
        const float near = delta[i] > 0.0f ? solid.mins[i] : solid.maxs[i];
        const float far = delta[i] > 0.0f ? solid.maxs[i] : solid.mins[i];
        const float t0 = (near - start[i]) / delta[i];
        const float t1 = (far - start[i]) / delta[i];
        if (t0 > tEnter)
        {
            tEnter = t0;
            *axis = i;
            *sign = delta[i] > 0.0f ? -1.0f : 1.0f;
        }
        tExit = std::fmin(tExit, t1);
    }
    if (tEnter > tExit || tEnter < 0.0f || tEnter > 1.0f)
        return false;
    *enter = tEnter;
    return true;
}

bool Inside(const Solid &solid, const float *const point)
{
    for (int i = 0; i < 3; ++i)
    {
        if (point[i] <= solid.mins[i] || point[i] >= solid.maxs[i])
            return false;
    }
    return true;
}

std::unique_ptr<playerState_s> NewPlayer(const float x, const float y, const float vx, const float vy)
{
    auto ps = std::make_unique<playerState_s>();
    ps->origin[0] = x;
    ps->origin[1] = y;
    ps->origin[2] = kSurfaceEpsilon;
    ps->velocity[0] = vx;
    ps->velocity[1] = vy;
    ps->gravity = 800;
    ps->commandTime = 1000;
    ps->groundEntityNum = ENTITYNUM_WORLD;
    ps->pm_flags = PMF_WALKING; // keeps PM_ShouldMakeFootsteps off its dvar
    return ps;
}

struct Move
{
    pmove_t pm{};
    pml_t pml{};

    Move(playerState_s *const ps, const float frametime, const bool onGround)
    {
        pm.ps = ps;
        pm.tracemask = 1;
        pm.mins[0] = pm.mins[1] = -15.0f;
        pm.maxs[0] = pm.maxs[1] = 15.0f;
        pm.maxs[2] = 70.0f;
        pml.frametime = frametime;
        if (onGround)
        {
            pml.groundPlane = 1;
            pml.walking = 1;
            pml.groundTrace.normal[2] = 1.0f;
            pml.groundTrace.walkable = true;
        }
    }
};

void TestPermute()
{
    const float velocity[3] = {1.0f, 0.0f, 0.0f};
    const float planes[3][3] = {{0.7071f, 0.7071f, 0.0f}, {0.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}};
    int32_t permutation[3] = {};
    const double into = PM_PermuteRestrictiveClipPlanes(velocity, 3, planes, permutation);
    Check(Near(into, -1.0, 1e-6), "most restrictive plane's dot");
    Check(permutation[0] == 2 && permutation[1] == 1 && permutation[2] == 0, "planes ordered by how hard they oppose");
}

void TestOpenMove()
{
    g_world = {kFloor};
    auto ps = NewPlayer(0.0f, 0.0f, 100.0f, 50.0f);
    Move move(ps.get(), 0.5f, false);
    Check(!PM_SlideMove(&move.pm, &move.pml, 0), "an open move does not bump");
    Check(Near(ps->origin[0], 50.0, 1e-4) && Near(ps->origin[1], 25.0, 1e-4), "an open move covers the distance");
}

void TestWallSlide()
{
    g_world = {kFloor, {{100.0f, -kFar, -100.0f}, {200.0f, kFar, kFar}}};
    auto ps = NewPlayer(0.0f, 0.0f, 200.0f, 100.0f);
    Move move(ps.get(), 1.0f, false);
    Check(PM_SlideMove(&move.pm, &move.pml, 0), "the wall bumps the move");
    Check(ps->origin[0] > 84.5f && ps->origin[0] <= 85.0f, "stopped at the wall");
    Check(Near(ps->origin[1], 100.0, 0.5), "slid along the wall for the rest of the frame");
    Check(ps->velocity[0] <= 0.0f && ps->velocity[0] > -1.0f && Near(ps->velocity[1], 100.0, 1e-3),
        "into-wall speed clipped, along-wall speed kept");
}

void TestCorner()
{
    g_world = {kFloor, {{100.0f, -kFar, -100.0f}, {200.0f, kFar, kFar}},
        {{-kFar, 100.0f, -100.0f}, {kFar, 200.0f, kFar}}};
    auto ps = NewPlayer(0.0f, 0.0f, 200.0f, 200.0f);
    Move move(ps.get(), 1.0f, false);
    Check(PM_SlideMove(&move.pm, &move.pml, 0), "the corner bumps the move");
    Check(ps->origin[0] > 84.0f && ps->origin[0] <= 85.0f && ps->origin[1] > 84.0f && ps->origin[1] <= 85.0f,
        "stopped in the corner");
    Check(std::hypot(ps->velocity[0], ps->velocity[1]) < 1.0f, "the crease leaves no horizontal speed");
}

void TestStepUp()
{
    g_world = {kFloor, {{50.0f, -kFar, -100.0f}, {kFar, kFar, 16.0f}}};
    auto ps = NewPlayer(0.0f, 0.0f, 200.0f, 0.0f);
    Move move(ps.get(), 0.5f, true);
    PM_StepSlideMove(&move.pm, &move.pml, 0);
    Check(Near(ps->origin[0], 100.0, 0.01) && Near(ps->origin[2], 16.0 + kSurfaceEpsilon, 0.01),
        "climbed the 16-unit stair");
    Check(Near(move.pm.viewChange, 16.0, 0.01), "the view eases over the step");
    // Speed after a step: 0.2 + 0.8 * (1 - 16 / 18) of the original.
    Check(Near(ps->velocity[0], 200.0 * (0.2 + 0.8 * (1.0 - 16.0 / 18.0)), 0.5), "a step costs speed");
}

void TestStepTooHigh()
{
    g_world = {kFloor, {{50.0f, -kFar, -100.0f}, {kFar, kFar, 30.0f}}};
    auto ps = NewPlayer(0.0f, 0.0f, 200.0f, 0.0f);
    Move move(ps.get(), 0.5f, true);
    PM_StepSlideMove(&move.pm, &move.pml, 0);
    Check(ps->origin[0] > 34.5f && ps->origin[0] <= 35.0f && ps->origin[2] < 1.0f, "a 30-unit ledge blocks the move");
    Check(move.pm.viewChange == 0.0f, "no view change without a step");
}
} // namespace

// The collision world the pmove trace handler reaches.
void __cdecl G_TraceCapsule(trace_t *results, const float *start, const float *mins, const float *maxs,
    const float *end, int, int)
{
    *results = trace_t();
    results->fraction = 1.0f;
    const float delta[3] = {end[0] - start[0], end[1] - start[1], end[2] - start[2]};
    const float length = std::sqrt(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
    for (const Solid &solid : g_world)
    {
        // Sweep the box's origin against the solid grown by the box.
        Solid grown{};
        for (int i = 0; i < 3; ++i)
        {
            grown.mins[i] = solid.mins[i] - maxs[i];
            grown.maxs[i] = solid.maxs[i] - mins[i];
        }
        if (Inside(grown, start))
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
        }
    }
}

void __cdecl G_PlayerEvent(int, int)
{
}

// bg_misc.cpp's dvar; PM_ShouldMakeFootsteps reads it only for a running player.
const dvar_t *player_footstepsThreshhold = nullptr;

uint16_t __cdecl Trace_GetEntityHitId(const trace_t *trace)
{
    return trace->hitType == TRACE_HITTYPE_ENTITY ? trace->hitId : ENTITYNUM_NONE;
}

char __cdecl BG_CheckProne(int, const float *, float, float, float, float *, float *, bool, bool, bool, uint8_t,
    proneCheckType_t, float)
{
    std::abort(); // only reached for a prone player
}

void __cdecl BG_AddPredictableEventToPlayerstate(entity_event_t, uint32_t, playerState_s *)
{
    std::abort(); // footsteps stay off for a walking player
}

int main()
{
    TestPermute();
    TestOpenMove();
    TestWallSlide();
    TestCorner();
    TestStepUp();
    TestStepTooHigh();
    if (g_failures == 0)
        std::puts("bg slidemove contracts passed");
    return g_failures == 0 ? 0 : 1;
}
