// Current production bodies and records, with engine contexts doubled.
#include <universal/kisak_abi.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <type_traits>
#ifndef _MSC_VER
#define __int32 std::int32_t
#endif
namespace {
void Require(bool value, const char *expression, int line)
{
    if (!value) {
        std::fprintf(stderr, "animation value contract line %d: %s\n", line, expression);
        std::abort();
    }
}
#define CHECK(value) Require(bool(value), #value, __LINE__)
#define iassert(value) CHECK(value)
#include "anim_scriptAnimConditions_t.inc"
#include "anim_scriptAnimPerkStates_t.inc"
#include "anim_scriptAnimWeaponPositions_t.inc"
#include "anim_scriptAnimNoteType_t.inc"
#include "anim_scriptAnimMoveTypes_t.inc"
#include "anim_scriptAnimEventTypes_t.inc"
#include "anim_animBodyPart_t.inc"
#include "anim_aistateEnum_t.inc"
#include "anim_scriptAnimStrafeStates_t.inc"
#include "anim_animScriptParseMode_t.inc"
#include "anim_MantleAnims.inc"
#include "anim_PlayerSpreadOverrideState.inc"
#include "anim_PmStanceFrontBack.inc"
#include "anim_weapClass_t.inc"
#include "animation_value_expected.inc"
#include "animation_record.inc"
static_assert(sizeof(animation_s) == 104 && alignof(animation_s) == 8);
static_assert(offsetof(animation_s, movetype) == 88 && offsetof(animation_s, noteType) == 96);
static_assert(std::is_same_v<decltype(animation_s::noteType), scriptAnimNoteType_t>);
struct snd_alias_list_t;
#include "animation_condition_record.inc"
#include "animation_command_record.inc"
#include "animation_item_record.inc"
#include "animation_script_record.inc"
static_assert(std::extent_v<decltype(animScriptItem_t::conditions)> == 10);
#include "animation_defines.inc"
void CheckDefines()
{
    static_assert(std::extent_v<decltype(numDefines)> == 10);
    for (auto &value : numDefines) value = 123;
#include "animation_clear_defines.inc"
    for (const auto value : numDefines) CHECK(value == 0);
}
struct animScriptData_t {
    animation_s animations[4];
    uint32_t numAnimations;
    animScript_t scriptEvents[NUM_ANIM_EVENTTYPES];
};
struct { int anim_user; } context{};
auto *bgs = &context;
void BG_CheckThread() {}
#include "animation_notes_body.inc"
void CheckNotes()
{
    for (int animUser : {0, 1}) {
        for (int parts = 0; parts < 16; ++parts) {
            animScriptData_t data{};
            data.numAnimations = 3;
            for (auto &animation : data.animations) animation.noteType = ANIM_NOTE_RELOAD;
            animScriptItem_t reload{};
            reload.numCommands = 1;
            reload.commands[0].bodyPart[0] = static_cast<int16_t>(parts & 3);
            reload.commands[0].bodyPart[1] = static_cast<int16_t>(parts >> 2);
            reload.commands[0].animIndex[0] = 1;
            reload.commands[0].animIndex[1] = 2;
            data.scriptEvents[10].numItems = 1;
            data.scriptEvents[10].items[0] = &reload;
            context.anim_user = animUser;
            BG_SetupAnimNoteTypes(&data);
            CHECK(data.animations[0].noteType == 0);
            CHECK(data.animations[1].noteType == int(animUser == 0 && (parts & 3) != 0));
            CHECK(data.animations[2].noteType == int(animUser == 0 && (parts >> 2) != 0));
            CHECK(data.animations[3].noteType == 1);
        }
    }
}
struct playerState_s {
    int viewHeightTarget;
    int spreadOverrideState;
    int spreadOverride;
    float viewHeightCurrent;
    uint32_t perks;
};
#include "animation_stance_body.inc"
#include "animation_stance_ex_body.inc"
void CheckStances()
{
    for (int height : {(std::numeric_limits<int>::min)(), -1, 0, 11, 12, 22, 40, 60, (std::numeric_limits<int>::max)()}) {
        playerState_s ps{};
        ps.viewHeightTarget = height;
        const int expected = height == 22 || height == 40 ? 2 : height == 11 ? 1 : 0;
        CHECK(PM_GetEffectiveStance(&ps) == expected);
    }
    for (int stance = 0; stance < 3; ++stance)
        for (int backward : {0, 1, -1}) CHECK(PM_GetStanceEx(stance, backward) == stance + (backward ? 3 : 0));
}
#include "mantle_transition_record.inc"
#include "mantle_transitions.inc"
float I_fabs(float value) { return std::fabs(value); }
#include "mantle_find_body.inc"
void CheckMantles()
{
    constexpr int over[7] = {8, 8, 9, 9, 9, 10, 10};
    for (int i = 0; i < 7; ++i) {
        CHECK(s_mantleTrans[i].upAnimIndex == i + 1);
        CHECK(s_mantleTrans[i].overAnimIndex == over[i]);
        CHECK(s_mantleTrans[i].height == float(57 - 6 * i));
        CHECK(Mantle_FindTransition(10, float(67 - 6 * i)) == i);
        if (i < 6) CHECK(Mantle_FindTransition(10, float(64 - 6 * i)) == i); // Tie chooses earlier entry.
    }
    CHECK(Mantle_FindTransition(0, 1) == 6);
    CHECK(Mantle_FindTransition(0, 100) == 0);
}
struct WeaponDef {
    float fHipSpreadProneMin;
    float fHipSpreadDuckedMin;
    float fHipSpreadStandMin;
    float hipSpreadProneMax;
    float hipSpreadDuckedMax;
    float hipSpreadStandMax;
};
namespace sp {
#include "animation_spread_body.inc"
}
namespace mp {
struct { struct { float value; } current; } spreadDvar{{0.5f}};
auto *perk_weapSpreadMultiplier = &spreadDvar;
#define KISAK_MP 1
#include "animation_spread_body.inc"
#undef KISAK_MP
}
void CheckSpread()
{
    const WeaponDef weapon{1, 3, 5, 7, 9, 11};
    constexpr float heights[] = {11, 40, 60};
    for (int state = -1; state <= 3; ++state) {
        for (int stance = 0; stance < 3; ++stance) {
            playerState_s ps{};
            ps.viewHeightCurrent = heights[stance];
            ps.spreadOverrideState = state;
            ps.spreadOverride = 20;
            const float expectedMin = state == 2 ? 20.0f : float(1 + 2 * stance);
            const float expectedMax = state == 1 || state == 2 ? 20.0f : float(7 + 2 * stance);
            float low{};
            float high{};
            sp::BG_GetSpreadForWeapon(&ps, &weapon, &low, &high);
            CHECK(low == expectedMin && high == expectedMax);
            ps.perks = 2;
            mp::BG_GetSpreadForWeapon(&ps, &weapon, &low, &high);
            CHECK(low == expectedMin * 0.5f && high == expectedMax * 0.5f);
        }
    }
}
#include "animation_move_name_body.inc"
constexpr const char *moveTypeNames[] = {"ANIM_MT_UNUSED", "Idle", "Crouching Idle", "Prone Idle", "Walk", "Walk Backward",
    "Crouching Walk", "Crouching Walk Backward", "Prone Crawl", "Prone Crawl Backward", "Run", "Run Backward",
    "Crouching Run", "Crouching Run Backward", "Turning Right", "Turning Left", "Turning Right Crouching",
    "Turning Left Crouching", "Climbing Up", "Climbing Down", "Sprinting"};
void CheckMoveNames()
{
    for (int i = 0; i < 21; ++i) CHECK(std::strcmp(GetMoveTypeName(i), moveTypeNames[i]) == 0);
    CHECK(std::strcmp(GetMoveTypeName(-1), "Unknown") == 0);
    CHECK(std::strcmp(GetMoveTypeName(21), "Unknown") == 0);
}
} // namespace
void RunAnimationValueContracts()
{
    CheckDefines();
    CheckNotes();
    CheckStances();
    CheckMantles();
    CheckSpread();
    CheckMoveNames();
}
