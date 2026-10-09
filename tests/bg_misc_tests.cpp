// bg_misc_tests.cpp: the production shared-game helpers in bgame/bg_misc.cpp
// at native width, compiled as the headless server compiles them: trajectory
// evaluation for every trType, the playerState event ring, the
// playerState-to-entityState conversion the server snapshots (including the
// event routing through pmoveHandlers), and HUD colour fades. The engine
// boundary is stubbed; section GC drops the paths the checks never reach, and
// the stubs that should stay unreached abort.

#include <bgame/bg_local.h>
#include <bgame/bg_public.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace
{
int g_failures = 0;
int g_effectiveStance = 0;
std::vector<int> g_playerEvents;

void Check(const bool ok, const char *const what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

bool Near(const double a, const double b)
{
    return std::fabs(a - b) < 1e-3;
}

bool NearVec(const float *const v, const float x, const float y, const float z)
{
    return Near(v[0], x) && Near(v[1], y) && Near(v[2], z);
}

trajectory_t Trajectory(const trType_t type)
{
    trajectory_t tr{};
    tr.trType = type;
    tr.trTime = 1000;
    tr.trDuration = 500;
    tr.trBase[0] = 1.0f;
    tr.trBase[1] = 2.0f;
    tr.trBase[2] = 3.0f;
    tr.trDelta[0] = 100.0f;
    tr.trDelta[2] = 50.0f;
    return tr;
}

void TestTrajectory()
{
    float at[3] = {};
    float vel[3] = {};

    trajectory_t tr = Trajectory(TR_STATIONARY);
    BG_EvaluateTrajectory(&tr, 1500, at);
    BG_EvaluateTrajectoryDelta(&tr, 1500, vel);
    Check(NearVec(at, 1, 2, 3) && NearVec(vel, 0, 0, 0), "stationary");

    tr = Trajectory(TR_LINEAR);
    BG_EvaluateTrajectory(&tr, 1500, at);
    BG_EvaluateTrajectoryDelta(&tr, 1500, vel);
    Check(NearVec(at, 51, 2, 28) && NearVec(vel, 100, 0, 50), "linear");

    tr = Trajectory(TR_LINEAR_STOP);
    BG_EvaluateTrajectory(&tr, 2000, at);
    BG_EvaluateTrajectoryDelta(&tr, 2000, vel);
    Check(NearVec(at, 51, 2, 28) && NearVec(vel, 0, 0, 0), "linear stop holds after the duration");
    BG_EvaluateTrajectory(&tr, 500, at);
    Check(NearVec(at, 1, 2, 3), "linear stop holds before the start");

    tr = Trajectory(TR_SINE);
    BG_EvaluateTrajectory(&tr, 1125, at);
    BG_EvaluateTrajectoryDelta(&tr, 1000, vel);
    Check(NearVec(at, 101, 2, 53) && NearVec(vel, 50, 0, 25), "sine");

    tr = Trajectory(TR_GRAVITY);
    BG_EvaluateTrajectory(&tr, 1500, at);
    BG_EvaluateTrajectoryDelta(&tr, 1500, vel);
    Check(NearVec(at, 51, 2, -72) && NearVec(vel, 100, 0, -350), "gravity");

    tr = Trajectory(TR_ACCELERATE);
    BG_EvaluateTrajectory(&tr, 1500, at);
    BG_EvaluateTrajectoryDelta(&tr, 1500, vel);
    Check(NearVec(at, 26, 2, 15.5f) && NearVec(vel, 25, 0, 12.5f), "accelerate");

    tr = Trajectory(TR_DECELERATE);
    BG_EvaluateTrajectory(&tr, 1500, at);
    BG_EvaluateTrajectoryDelta(&tr, 1500, vel);
    Check(NearVec(at, 26, 2, 15.5f) && NearVec(vel, 50, 0, 25), "decelerate");
    BG_EvaluateTrajectoryDelta(&tr, 1600, vel);
    Check(NearVec(vel, 0, 0, 0), "decelerate stops after the duration");
}

void TestPlayerStateToEntityState()
{
    auto ps = std::make_unique<playerState_s>();
    auto es = std::make_unique<entityState_s>();
    es->number = 5;
    ps->clientNum = 5;
    ps->origin[0] = 1.6f;
    ps->origin[1] = -2.4f;
    ps->origin[2] = 3.5f;
    ps->viewangles[1] = 90.7f;
    ps->movementDir = -12;
    ps->pm_type = PM_DEAD;
    ps->pm_flags = PMF_SIGHT_AIMING;
    ps->otherFlags = 6;
    ps->weapon = 3;
    ps->weaponmodels[3] = 2;
    ps->groundEntityNum = 1022;
    ps->legsAnim = 17;
    ps->torsoAnim = 18;

    // Four events: 5 and 10 reach the entity; 31 is server-only, 6 single-client.
    const struct
    {
        entity_event_t event;
        uint32_t parm;
    } events[] = {{static_cast<entity_event_t>(5), 9}, {static_cast<entity_event_t>(31), 1},
        {static_cast<entity_event_t>(6), 2}, {static_cast<entity_event_t>(10), 3}};
    for (const auto &event : events)
        BG_AddPredictableEventToPlayerstate(event.event, event.parm, ps.get());
    Check(ps->eventSequence == 4 && ps->events[3] == 10 && ps->eventParms[0] == 9, "playerState event ring");

    BG_PlayerStateToEntityState(ps.get(), es.get(), 1, 1);
    Check(es->lerp.pos.trType == TR_INTERPOLATE && NearVec(es->lerp.pos.trBase, 1, -2, 3)
            && NearVec(es->lerp.apos.trBase, 0, 90, 0),
        "snapped position and angles");
    Check(es->lerp.u.player.movementDir == -12, "movement direction");
    Check((es->lerp.eFlags & 0x20000) != 0 && (es->lerp.eFlags & 0x40000) != 0, "dead and sight-aiming flags");
    Check(es->eType == ET_PLAYER && es->clientNum == 5 && es->weapon == 3 && es->weaponModel == 2
            && es->groundEntityNum == 1022 && es->legsAnim == 17 && es->torsoAnim == 18,
        "player fields");
    Check(es->fTorsoPitch == 0.0f && es->fWaistPitch == 0.0f, "no pitch unless prone");
    Check(es->eventParm == 9 && ps->entityEventSequence == 1, "first event's parm");
    Check(g_playerEvents.size() == 4 && g_playerEvents[1] == 31, "every event reaches the handler");
    Check(es->eventSequence == 2 && es->events[0] == 5 && es->events[1] == 10 && es->eventParms[0] == 9
            && es->eventParms[1] == 3,
        "only broadcast events reach the entity");
    Check(ps->oldEventSequence == 4, "events consumed");

    // Prone: the torso pitch is wrapped to [-180, 180).
    g_effectiveStance = 1;
    ps->fTorsoPitch = 370.0f;
    ps->fWaistPitch = -200.0f;
    BG_PlayerStateToEntityState(ps.get(), es.get(), 0, 0);
    Check(Near(es->fTorsoPitch, 10.0) && Near(es->fWaistPitch, 160.0), "prone pitches wrapped");
    Check(Near(es->lerp.pos.trBase[0], 1.6), "no snap keeps the fraction");
    Check(es->eType == ET_PLAYER && g_playerEvents.size() == 4, "handler 0 reports no events");
    g_effectiveStance = 0;
}

void TestHudColors()
{
    hudelem_s elem{};
    elem.color.r = 200;
    elem.color.g = 100;
    elem.color.b = 50;
    elem.color.a = 255;
    elem.fadeStartTime = 1000;
    elem.fadeTime = 400;
    hudelem_color_t color{};
    BG_LerpHudColors(&elem, 1100, &color);
    Check(color.r == 50 && color.g == 25 && color.b == 12 && color.a == 63, "quarter-way fade");
    BG_LerpHudColors(&elem, 900, &color);
    Check(color.r == 0 && color.a == 0, "fade before it starts");
    BG_LerpHudColors(&elem, 1400, &color);
    Check(color.r == 200 && color.a == 255, "fade complete");
}
} // namespace

// The engine boundary bg_misc.cpp's tested paths reach.
void MyAssertHandler(const char *file, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "engine assert %s:%d %s\n", file, line, fmt);
    std::abort();
}

void Com_Error(errorParm_t, const char *fmt, ...)
{
    std::fprintf(stderr, "Com_Error %s\n", fmt);
    std::abort();
}

void Com_Printf(int, const char *, ...)
{
}

bool __cdecl Dvar_GetBool(const char *)
{
    return false; // showevents
}

int __cdecl PM_GetEffectiveStance(const playerState_s *)
{
    return g_effectiveStance;
}

int __cdecl PM_GetViewHeightLerpTime(const playerState_s *, int, int)
{
    std::abort(); // only reached while a view-height lerp runs
}

void __cdecl G_PlayerEvent(int, int event)
{
    g_playerEvents.push_back(event);
}

void __cdecl G_TraceCapsule(trace_t *, const float *, const float *, const float *, const float *, int, int)
{
    std::abort();
}

int main()
{
    TestTrajectory();
    TestPlayerStateToEntityState();
    TestHudColors();
    if (g_failures == 0)
        std::puts("bg_misc contracts passed");
    return g_failures == 0 ? 0 : 1;
}
