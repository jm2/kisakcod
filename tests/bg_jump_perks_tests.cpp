// bg_jump_perks_tests.cpp: the production jump physics (bgame/bg_jump.cpp) and
// perk table (bgame/bg_perks_mp.cpp) at native width, compiled as the headless
// server compiles them. The dvars are registered through the production
// Jump_RegisterDvars/Perks_RegisterDvars into a stub registry, so the jump
// checks run against the shipped defaults (39-unit jump, 18-unit step, MP
// slowdown on). The pmove hooks the jump reaches are recorded by the stubs.

#include <bgame/bg_local.h>
#include <bgame/bg_public.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace
{
int g_failures = 0;
uint32_t g_groundSurface = 0;
std::deque<dvar_t> g_dvars;
std::vector<std::string> g_dvarNames;
std::vector<std::pair<int, uint32_t>> g_events;
std::vector<int> g_animEvents;

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

dvar_t *NewDvar(const char *const name)
{
    g_dvarNames.emplace_back(name);
    g_dvars.emplace_back();
    dvar_t *const dvar = &g_dvars.back();
    dvar->name = name;
    return dvar;
}

void TestPerks()
{
    Check(BG_GetPerkIndexForName("specialty_gpsjammer") == 0, "first perk");
    Check(BG_GetPerkIndexForName("specialty_rof") == 3, "perk by name");
    Check(BG_GetPerkIndexForName("SPECIALTY_FragGrenade") == 18, "perk names are case-insensitive");
    Check(BG_GetPerkIndexForName("specialty_unknown") == 20 && BG_GetPerkIndexForName(nullptr) == 20,
        "unknown and null map past the table");
    Check(perk_sprintMultiplier && Near(perk_sprintMultiplier->current.value, 2.0)
            && perk_grenadeDeath && std::strcmp(perk_grenadeDeath->current.string, "frag_grenade_short_mp") == 0,
        "perk dvar defaults");
}

std::unique_ptr<playerState_s> NewPlayer()
{
    auto ps = std::make_unique<playerState_s>();
    ps->gravity = 800;
    ps->origin[2] = 100.0f;
    ps->aimSpreadScale = 10.0f;
    return ps;
}

void TestStartAndArc()
{
    auto ps = NewPlayer();
    pmove_t pm{};
    pml_t pml{};
    pm.ps = ps.get();
    pm.cmd.serverTime = 5000;
    pml.walking = 1;
    Jump_Start(&pm, &pml, jump_height->current.value);
    Check(Near(ps->velocity[2], std::sqrt(2.0 * 39.0 * 800.0)), "launch speed reaches the jump height");
    Check((ps->pm_flags & PMF_JUMPING) != 0 && ps->jumpOriginZ == 100.0f && ps->jumpTime == 5000
            && ps->groundEntityNum == ENTITYNUM_NONE && !pml.walking,
        "jump state");
    Check(Near(ps->aimSpreadScale, 74.0), "jumping adds spread");

    float step = 0.0f;
    const float low[3] = {0.0f, 0.0f, 110.0f};
    const float near[3] = {0.0f, 0.0f, 130.0f};
    const float over[3] = {0.0f, 0.0f, 140.0f};
    Check(Jump_GetStepHeight(ps.get(), low, &step) && Near(step, 18.0), "full step low in the arc");
    Check(Jump_GetStepHeight(ps.get(), near, &step) && Near(step, 9.0), "step trimmed to the apex");
    Check(!Jump_GetStepHeight(ps.get(), over, &step), "no step above the apex");

    // 30 units up, rising from 20: the speed is cut to what reaches 139.
    const float before[3] = {0.0f, 0.0f, 120.0f};
    ps->origin[2] = 130.0f;
    ps->velocity[2] = 300.0f;
    Jump_ClampVelocity(ps.get(), before);
    Check(Near(ps->velocity[2], std::sqrt(800.0 * 18.0)), "rising speed clamped below the apex");
    Check(!Jump_IsPlayerAboveMax(ps.get()), "below the apex");
    ps->origin[2] = 139.5f;
    Check(Jump_IsPlayerAboveMax(ps.get()), "above the apex");

    ps->aimSpreadScale = 250.0f;
    Jump_Start(&pm, &pml, 39.0f);
    Check(ps->aimSpreadScale == 255.0f, "spread capped at 255");
}

void TestLandingSlowdown()
{
    auto ps = NewPlayer();
    ps->pm_flags = PMF_JUMPING;
    ps->jumpOriginZ = 100.0f;
    ps->velocity[0] = 100.0f;
    Jump_ApplySlowdown(ps.get());
    Check(ps->pm_time == 1800 && Near(ps->velocity[0], 65.0), "low landing slows to 65%");

    ps->pm_time = 0;
    ps->origin[2] = 120.0f;
    ps->velocity[0] = 100.0f;
    Jump_ApplySlowdown(ps.get());
    Check(ps->pm_time == 1200 && Near(ps->velocity[0], 50.0), "high landing slows to 50%");

    ps->pm_time = 1700;
    Check(Near(Jump_ReduceFriction(ps.get()), 2.5), "friction peaks at 2.5");
    ps->pm_time = 850;
    Check(Near(Jump_ReduceFriction(ps.get()), 1.75), "friction eases with the timer");
    Check(Near(Jump_GetLandFactor(ps.get()), 1.75), "land factor follows the friction");

    ps->pm_time = 2000;
    Check(Near(Jump_ReduceFriction(ps.get()), 1.0) && (ps->pm_flags & PMF_JUMPING) == 0 && ps->jumpOriginZ == 0.0f,
        "an expired slowdown clears the jump");

    ps->pm_time = 0;
    Jump_ActivateSlowdown(ps.get());
    Check((ps->pm_flags & PMF_JUMPING) != 0 && ps->pm_time == 1800, "slowdown activation");
}

void TestCheck()
{
    auto ps = NewPlayer();
    pmove_t pm{};
    pml_t pml{};
    pm.ps = ps.get();
    pm.cmd.serverTime = 5000;
    pm.cmd.buttons = 0x400;
    pm.oldcmd.buttons = 0x400;
    Check(!Jump_Check(&pm, &pml) && (pm.cmd.buttons & 0x400) == 0, "a held jump button does not re-jump");

    pm.cmd.buttons = 0x400;
    pm.oldcmd.buttons = 0;
    pm.cmd.forwardmove = -10;
    g_groundSurface = 7;
    Check(Jump_Check(&pm, &pml), "jump from the ground");
    Check(g_events.size() == 1 && g_events[0].first == EV_JUMP && g_events[0].second == 7, "surface jump event");
    Check(g_animEvents.size() == 1 && g_animEvents[0] == ANIM_ET_JUMPBK, "backward jump anim");

    // Off a ladder while facing into it: the view is reflected about the ladder normal.
    ps->pm_flags = PMF_LADDER;
    ps->jumpTime = 0;
    ps->vLadderVec[0] = -1.0f;
    pml.forward[0] = 1.0f;
    pm.cmd.forwardmove = 10;
    Check(Jump_Check(&pm, &pml), "jump off a ladder");
    Check(g_events.size() == 2 && g_events[1].second == 0x15, "ladder jump event");
    Check(Near(ps->velocity[0], -128.0) && Near(ps->velocity[1], 0.0) && (ps->pm_flags & PMF_LADDER) == 0,
        "ladder push-off");
    Check(g_animEvents.size() == 2 && g_animEvents[1] == ANIM_ET_JUMP, "forward jump anim");

    pm.cmd.serverTime = 5200;
    Check(!Jump_Check(&pm, &pml), "no jump within 500 ms of the last");
}
} // namespace

// The engine boundary the jump and perk code reaches.
void MyAssertHandler(const char *file, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "engine assert %s:%d %s\n", file, line, fmt);
    std::abort();
}

const dvar_s *__cdecl Dvar_RegisterFloat(const char *name, float value, DvarLimits, uint16_t, const char *)
{
    dvar_t *const dvar = NewDvar(name);
    dvar->current.value = value;
    return dvar;
}

const dvar_s *__cdecl Dvar_RegisterBool(const char *name, bool value, uint16_t, const char *)
{
    dvar_t *const dvar = NewDvar(name);
    dvar->current.enabled = value;
    return dvar;
}

const dvar_s *__cdecl Dvar_RegisterString(const char *name, const char *value, uint16_t, const char *)
{
    dvar_t *const dvar = NewDvar(name);
    dvar->current.string = value;
    return dvar;
}

int __cdecl PM_GetEffectiveStance(const playerState_s *)
{
    return 0;
}

uint32_t __cdecl PM_GroundSurfaceType(pml_t *)
{
    return g_groundSurface;
}

void __cdecl BG_AddPredictableEventToPlayerstate(entity_event_t newEvent, uint32_t eventParm, playerState_s *)
{
    g_events.emplace_back(newEvent, eventParm);
}

int __cdecl BG_AnimScriptEvent(playerState_s *, scriptAnimEventTypes_t event, int, int)
{
    g_animEvents.push_back(event);
    return 0;
}

int main()
{
    Jump_RegisterDvars();
    Perks_RegisterDvars();
    Check(g_dvarNames.size() == 15 && jump_height && Near(jump_height->current.value, 39.0)
            && jump_slowdownEnable && jump_slowdownEnable->current.enabled,
        "jump and perk dvars registered with their defaults");
    TestPerks();
    TestStartAndArc();
    TestLandingSlowdown();
    TestCheck();
    if (g_failures == 0)
        std::puts("bg jump and perk contracts passed");
    return g_failures == 0 ? 0 : 1;
}
