// Production functions are extracted at configure time, never copied here.
// Math/engine services are doubles; console and score records are the real
// declarations. WeaponDef below is deliberately a different native layout:
// the behavioral fixture must not accidentally validate old byte offsets.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#ifndef _MSC_VER
#define __cdecl
#endif
static void Require(bool value, const char *expression, int line)
{
    if (!value) {
        std::fprintf(stderr, "line %d: %s\n", line, expression);
        std::abort();
    }
}
#define CHECK(value) Require(bool(value), #value, __LINE__)
#define iassert(value) CHECK(value)
#define bcassert(value, count) CHECK(static_cast<unsigned>(value) < static_cast<unsigned>(count))

namespace {
namespace math_tests {
static float rsqrtInput;
static void Vec3Sub(const float *a, const float *b, float *out)
{ for (int i = 0; i < 3; ++i) out[i] = a[i] - b[i]; }
static float Vec3LengthSq(const float *v)
{ return v[0]*v[0] + v[1]*v[1] + v[2]*v[2]; }
static void Vec3Copy(const float *in, float *out)
{ for (int i = 0; i < 3; ++i) out[i] = in[i]; }
static void Vec3Mad(const float *a, float scale, const float *b, float *out)
{ for (int i = 0; i < 3; ++i) out[i] = a[i] + scale*b[i]; }
static float I_rsqrt(float value)
{ rsqrtInput = value; return 1.0f/std::sqrt(value); }
static float Vec2Length(const float *v) { return std::hypot(v[0], v[1]); }
static float I_fabs(float v) { return std::fabs(v); }
constexpr float EQUAL_EPSILON = 0.001f;
struct GfxParticleCloud { float radius[2]; };
#include "offset.inc"
#include "particle.inc"
static bool Near(float a, float b) { return std::fabs(a-b) < 0.0001f; }
static void Run()
{
    float goal[3]{4, 6, 3}, pos[3]{1, 2, 3};
    BG_LerpOffset(goal, 2, pos);
    CHECK(rsqrtInput == 25);
    CHECK(Near(pos[0], 2.2f) && Near(pos[1], 3.6f) && pos[2] == 3);
    BG_LerpOffset(goal, 20, pos);
    CHECK(pos[0] == 4 && pos[1] == 6 && pos[2] == 3);
    rsqrtInput = -1;
    BG_LerpOffset(goal, 2, pos);
    CHECK(rsqrtInput == -1); // No reciprocal of zero for an identical goal.
    float origin[3]{};
    BG_LerpOffset(goal, 0, origin);
    CHECK(origin[0] == 0 && origin[1] == 0 && origin[2] == 0);
    const GfxParticleCloud cloud{{10, 7}};
    for (const auto &up : {std::vector<float>{3,4}, {3,-4}, {-3,4}}) {
        float axis[2][2]{};
        RB_CreateParticleCloud2dAxis(&cloud, up.data(), &axis);
        CHECK(Near(axis[0][0]*axis[1][0] + axis[0][1]*axis[1][1], 0));
        CHECK(Near(Vec2Length(axis[0]), 10));
        CHECK(axis[0][0]*axis[1][1] - axis[0][1]*axis[1][0] > 0);
    }
    const float up[2]{0,0};
    float axis[2][2]{};
    RB_CreateParticleCloud2dAxis(&cloud, up, &axis);
    CHECK(axis[0][0] == 10 && axis[1][1] == 7);
    CHECK(axis[0][1] == 0 && axis[1][0] == 0);
    const GfxParticleCloud small{{2,1}};
    const float projectedUp[2]{3,4};
    RB_CreateParticleCloud2dAxis(&small, projectedUp, &axis);
    CHECK(Near(Vec2Length(axis[0]), 2) && Near(Vec2Length(axis[1]), 5));
}
}

namespace console_tests {
#include "console_types.inc"
#include "console_dest.inc"
static Console con{};
constexpr unsigned GAMEMSG_WINDOW_COUNT = 4;
static std::vector<MessageWindow *> windows;
static void Con_ResetMessageWindowTimes(MessageWindow *window, int32_t)
{ windows.push_back(window); }
#ifdef KISAK_MP
struct Client { int32_t serverTime; };
static Client client{};
static Client *CL_GetLocalClientGlobals(int32_t n) { CHECK(n == 0); return &client; }
static void Con_NudgeMessageWindowTimes(MessageWindow *window, int32_t, int32_t)
{ windows.push_back(window); }
#endif
static void Con_ClearMessageWindow(MessageWindow *window);
#include "console_jump.inc"
#ifdef KISAK_MP
#include "console_nudge.inc"
#endif
#include "console_clear.inc"
#include "console_get.inc"
#include "console_mini.inc"
#include "console_active.inc"
static void CheckWindows()
{
    CHECK(windows.size() == 7);
    CHECK(windows[0] == &con.consoleWindow);
    for (int i=0; i<4; ++i) CHECK(windows[static_cast<size_t>(i)+1] == &con.messageBuffer[0].gamemsgWindows[i]);
    CHECK(windows[5] == &con.messageBuffer[0].miniconWindow);
    CHECK(windows[6] == &con.messageBuffer[0].errorWindow);
}
static void Run()
{
    windows.clear(); Con_TimeJumped(0, 100); CheckWindows();
#ifdef KISAK_MP
    windows.clear(); Con_TimeNudged(0, 50); CheckWindows();
#endif
    auto &buffer = con.messageBuffer[0];
    CHECK(Con_GetDestWindow(0, CON_DEST_CONSOLE) == &con.consoleWindow);
    CHECK(Con_GetDestWindow(0, CON_DEST_MINICON) == &buffer.miniconWindow);
    CHECK(Con_GetDestWindow(0, CON_DEST_ERROR) == &buffer.errorWindow);
    for (unsigned i=0; i<4; ++i) {
        CHECK(Con_GetDestWindow(0, static_cast<print_msg_dest_t>(i+3)) == &buffer.gamemsgWindows[i]);
        auto &w = buffer.gamemsgWindows[i];
        w.messages=buffer.gamemsgMessages[i]; w.lines=buffer.gamemsgLines[i];
        w.activeLineCount=1;
        CHECK(Con_IsGameMessageWindowActive(0,i));
    }
    Con_ClearNotify(0);
    for (unsigned i=0; i<4; ++i) CHECK(!Con_IsGameMessageWindowActive(0,i));
    buffer.miniconWindow.messages=buffer.miniconMessages; buffer.miniconWindow.lines=buffer.miniconLines;
    buffer.errorWindow.messages=buffer.errorMessages; buffer.errorWindow.lines=buffer.errorLines;
    buffer.miniconWindow.activeLineCount=4; buffer.errorWindow.activeLineCount=2;
    Con_ClearMiniConsole(0); Con_ClearErrors(0);
    CHECK(buffer.miniconWindow.activeLineCount==0 && buffer.errorWindow.activeLineCount==0);
}
}

namespace score_tests {
struct Material;
#include "score_type.inc"
struct cg_s { int teamScores[2]; score_t scores[5]; int numScores; };
static bool CG_ClientScoreIsBetter(score_t *, score_t *);
#include "score.inc"
static void Run()
{
    cg_s cg{}; cg.numScores=5; cg.teamScores[0]=123; cg.teamScores[1]=456;
    for (int i=0; i<5; ++i) { cg.scores[i].client=i; cg.scores[i].score=50-10*i; }
    cg.scores[4].score=60;
    CG_SortSingleClientScore(&cg,4);
    CHECK(cg.scores[0].client==4 && cg.scores[4].client==3);
    cg.scores[0].score=0;
    CG_SortSingleClientScore(&cg,0);
    for (int i=0; i<5; ++i) CHECK(cg.scores[i].client==i);
    CHECK(cg.teamScores[0]==123 && cg.teamScores[1]==456);
    cg.scores[1].score=cg.scores[0].score; cg.scores[0].deaths=5;
    CG_SortSingleClientScore(&cg,1);
    CHECK(cg.scores[0].client==1);
    cg.numScores=1; CG_SortSingleClientScore(&cg,0);
    CHECK(cg.scores[0].client==1);
}
}

namespace weapon_tests {
struct WeaponDef {
    const void *nativePointers[4]{};
    int iRaiseTime=101, iAltRaiseTime=102, quickRaiseTime=103;
    int iFirstRaiseTime=104, iEmptyRaiseTime=105;
    int playerAnimType=11, weapClass=12;
    unsigned altWeaponIndex=0;
};
struct playerState_s {
    unsigned weapon=1; int weaponstate=1; int pm_flags=0, weapFlags=0;
    uint32_t weapons[4]{}, weaponold[4]{};
    float aimSpreadScale=0; int clientNum=0;
};
struct pmove_t { playerState_s *ps; struct { int weapon=2; } cmd; };
constexpr int PMF_LADDER=1, PMF_SPRINTING=2, WEAPON_READY=0;
constexpr int EV_FIRST_RAISE_WEAPON=1, EV_RAISE_WEAPON=2;
#define WEAPONSTATE_DROPPING(value) ((value) == 1)
#ifdef KISAK_MP
constexpr int ANIM_ET_RAISEWEAPON=3;
#endif
static WeaponDef defs[3];
static bool emptyClip=false, inactive=false;
static unsigned actualTime=0, actualAnim=0;
static int actualAlt=-1;
[[maybe_unused]] static int conditions[2]{};
static WeaponDef *BG_GetWeaponDef(unsigned i) { CHECK(i<3); return &defs[i]; }
static unsigned BG_GetNumWeapons() { return 3; }
static bool Mantle_IsWeaponInactive(playerState_s *) { return inactive; }
static bool Com_BitCheckAssert(const uint32_t *bits, int i, int) { CHECK(i>=0 && i<128); return (bits[i/32] & (1u << (i%32))) != 0; }
static void Com_BitSetAssert(uint32_t *bits, int i, int) { CHECK(i>=0 && i<128); bits[i/32] |= 1u << (i%32); }
static bool PM_WeaponClipEmpty(playerState_s *) { return emptyClip; }
static void PM_StartWeaponAnim(playerState_s *, int) {}
static void PM_AddEvent(playerState_s *, int) {}
static void BG_TakeClipOnlyWeaponIfEmpty(playerState_s *, unsigned) {}
static void PM_Weapon_BeginWeaponRaise(playerState_s *, unsigned anim, unsigned time, float, int alt)
{ actualAnim=anim; actualTime=time; actualAlt=alt; }
#ifdef KISAK_MP
static void BG_AnimScriptEvent(playerState_s *, int, int, int) {}
static void BG_SetConditionBit(int, int condition, int value) { CHECK(condition>=0 && condition<2); conditions[condition]=value; }
#endif
#include "weapon.inc"
static void Case(bool first, bool quick, bool empty, bool alt, unsigned time, unsigned anim)
{
    playerState_s ps{}; ps.weapons[0]=7; ps.weaponold[0]=first ? 0u : 7u;
    pmove_t pm{&ps,{2}}; emptyClip=empty; defs[1].altWeaponIndex=alt ? 2u : 0u;
    actualTime=0; actualAlt=-1;
    PM_Weapon_FinishWeaponChange(&pm,quick);
    CHECK(ps.weapon==2 && actualTime==time && actualAnim==anim);
    CHECK(actualAlt==int(alt));
#ifdef KISAK_MP
    CHECK(conditions[0]==11 && conditions[1]==12);
#endif
}
static void Run()
{
    Case(false,false,false,false,101,11);
    Case(false,false,false,true,102,18);
    Case(false,true,false,false,103,20);
    Case(true,false,false,false,104,12);
    Case(true,true,true,false,105,22);
}
}
} // namespace

#ifdef KISAK_MP
int RunUpstreamB3199MpContracts()
#else
int RunUpstreamB3199SpContracts()
#endif
{
    math_tests::Run(); console_tests::Run(); score_tests::Run(); weapon_tests::Run();
    return 0;
}
