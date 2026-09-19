// Current production declarations and bodies, with engine services doubled.
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
#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wparentheses"
#endif
namespace {
void Require(bool value, const char *expression, int line)
{
    if (!value) {
        std::fprintf(stderr, "weapon value contract line %d: %s\n", line, expression);
        std::abort();
    }
}
#define CHECK(value) Require(bool(value), #value, __LINE__)
#define iassert(value) CHECK(value)
struct BoundFailure {};
#define bcassert(index, count) do { if (static_cast<uint32_t>(index) >= static_cast<uint32_t>(count)) throw BoundFailure{}; } while (false)
#include "weapon_state_enum.inc"
#include "weapon_file_enum.inc"
#include "weapon_command_enum.inc"
#include "weapon_value_expected.inc"
static_assert(std::is_same_v<std::underlying_type_t<weaponstate_t>, std::int32_t>);
static_assert(std::is_same_v<std::underlying_type_t<weapAnimFiles_t>, std::int32_t>);
static_assert(std::is_same_v<std::underlying_type_t<weapAnimNumber_t>, std::int32_t>);
struct playerState_s {
    weaponstate_t weaponstate;
    int weaponTime, pm_flags, weapFlags;
    float fWeaponPosFrac;
    int weapAnim, ammoclip[1];
};
int idleCalls;
void PM_Weapon_Idle(playerState_s *ps) { ++idleCalls; ps->weaponstate = WEAPON_READY; }
#include "weapon_prone_body.inc"
void CheckProneInterrupt()
{
    // Frozen accepted states and the four sprint/detonation idle transitions.
    constexpr bool accepted[27] = {
        true, true, true, true, true, false, true, true, true, true, true, true,
        false, false, false, false, false, false, false, false, false,
        true, true, true, true, false, false
    };
    for (int state = 0; state < 27; ++state) {
        playerState_s ps{};
        ps.weaponstate = static_cast<weaponstate_t>(state);
        idleCalls = 0;
        CHECK(bool(PM_InteruptWeaponWithProneMove(&ps)) == accepted[state]);
        CHECK(idleCalls == (state >= 21 && state <= 24 ? 1 : 0));
    }
    for (const int state : {-1, (std::numeric_limits<int>::min)(), 27, (std::numeric_limits<int>::max)()}) {
        playerState_s ps{};
        ps.weaponstate = static_cast<weaponstate_t>(state);
        idleCalls = 0;
        CHECK(PM_InteruptWeaponWithProneMove(&ps) == 1);
        CHECK(idleCalls == 1 && ps.weaponstate == 0);
    }
}
using BOOL = int;
constexpr int WP_NONE = 0, PMF_SIGHT_AIMING = 16;
struct DObj_s {};
struct XAnim_s {};
struct XAnimTree_s {};
struct WeaponDef {
    const char *szInternalName;
    bool aimDownSight;
    int iPositionReloadTransTime;
#include "weapon_animation_slots.inc"
};
struct weaponInfo_s { DObj_s *viewModelDObj; int iPrevAnim; };
struct cg_s { int prevViewmodelWeapon; };
WeaponDef weapon{};
cg_s client{};
XAnimTree_s tree;
int startedAnim, adsAnim, startCalls, diagnostics;
cg_s *CG_GetLocalClientGlobals(int localClientNum) { CHECK(localClientNum == 0); return &client; }
XAnimTree_s *DObjGetTree(DObj_s *) { return &tree; }
int BG_GetViewmodelWeaponIndex(const playerState_s *) { return 1; }
WeaponDef *BG_GetWeaponDef(int index) { CHECK(index == 1); return &weapon; }
int BG_ClipForWeapon(int index) { CHECK(index == 1); return 0; }
bool XAnimHasFinished(XAnimTree_s *input, int index) { CHECK(input == &tree && index >= 1 && index < 31); return true; }
void PlayADSAnim(float, int, DObj_s *, int index) { adsAnim = index; }
void StartWeaponAnim(int, int, DObj_s *, int index, float) { startedAnim = index; ++startCalls; }
void Com_Printf(int, const char *, ...) { ++diagnostics; }
#include "weapon_run_anims_body.inc"
#include "weapon_rate_offsets.inc"
int XAnimGetLengthMsec(XAnim_s *, uint32_t) { CHECK(false); return 0; }
#include "weapon_rate_body.inc"
void CheckAnimationDispatch()
{
    constexpr int fileForCommand[30] = {
        1, 1, 3, 5, 6, 28, 29, 30, 7, 8, 15, 13, 14, 9, 10,
        11, 12, 17, 16, 19, 18, 21, 20, 22, 23, 24, 4, 25, 26, 27
    };
    for (auto &name : weapon.szXAnims) name = "";
    DObj_s object;
    for (int command = 0; command < 30; ++command) {
        for (int toggle : {0, 0x200}) {
            for (int ammo : {0, 1}) {
                playerState_s ps{};
                ps.weapAnim = command | toggle;
                ps.ammoclip[0] = ammo;
                weaponInfo_s info{&object, -1};
                client.prevViewmodelWeapon = 0;
                startedAnim = adsAnim = -1; startCalls = diagnostics = 0;
                WeaponRunXModelAnims(0, &ps, &info);
                const int expected = command <= 1 && !ammo ? 2 : fileForCommand[command];
                CHECK(startCalls == 1 && startedAnim == expected && adsAnim == -1 && diagnostics == 0);
                CHECK(info.iPrevAnim == ps.weapAnim && client.prevViewmodelWeapon == 1);
            }
        }
    }
    weapon.aimDownSight = true;
    for (int aiming : {0, 16}) {
        playerState_s ps{};
        ps.pm_flags = aiming;
        ps.weapAnim = 2;
        weaponInfo_s info{&object, -1};
        adsAnim = -1;
        WeaponRunXModelAnims(0, &ps, &info);
        CHECK(adsAnim == (aiming ? 31 : 32));
    }
    // Storage has 33 slots; the rate helper preserves its exclusive bound of 32.
    static_assert(sizeof(g_animRateOffsets) / sizeof(g_animRateOffsets[0]) == 33);
    static_assert(std::extent_v<decltype(WeaponDef::szXAnims)> == 33);
    XAnim_s anims;
    CHECK(g_animRateOffsets[32] == -1);
    CHECK(GetWeaponAnimRate(&weapon, &anims, 31) == 1.0);
    bool rejected = false;
    try { (void)GetWeaponAnimRate(&weapon, &anims, 32); }
    catch (const BoundFailure &) { rejected = true; }
    CHECK(rejected);
}
} // namespace
void RunWeaponValueContracts()
{
    CheckProneInterrupt();
    CheckAnimationDispatch();
}
