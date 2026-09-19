// Compile production hint policies against small engine service doubles.
#include <universal/kisak_abi.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <type_traits>
#ifndef _MSC_VER
#define __int32 std::int32_t
#endif
namespace {
void Require(bool value, const char *expression, int line)
{
    if (!value) {
        std::fprintf(stderr, "hint value contract line %d: %s\n", line, expression);
        std::abort();
    }
}
#define CHECK(value) Require(bool(value), #value, __LINE__)
#define iassert(value) CHECK(value)
#include "hint_enum.inc"
#include "hint_offset.inc"
#include "hint_weapon_enum.inc"
#include "hint_inventory_enum.inc"
#include "hint_trajectory_enum.inc"
#include "hint_value_expected.inc"
struct Material;
struct MediaSP {
#include "hint_materials_sp.inc"
};
struct MediaMP {
#include "hint_materials_mp.inc"
};
static_assert(std::extent_v<decltype(MediaSP::hintMaterials)> == 133);
static_assert(std::extent_v<decltype(MediaMP::hintMaterials)> == 133);
struct WeaponDef { weapType_t weapType; weapInventoryType_t inventoryType; };
WeaponDef weapons[128]{};
struct playerState_s { unsigned weapon; unsigned weapons[4]; };
struct gclient_s { playerState_s ps; };
struct gentity_s { struct { struct { int item; int brushmodel; } index; } s; };
struct Item { int giType; };
Item bg_itemlist[2048]{};
constexpr int IT_WEAPON = 1;
int primaryCount{};
[[noreturn]] void MyAssertHandler(const char *, int, int, const char *, ...)
{
    std::abort();
}
WeaponDef *BG_GetWeaponDef(unsigned index) { CHECK(index < 128); return &weapons[index]; }
bool BG_PlayerHasWeapon(const playerState_s *ps, unsigned index)
{
    return (ps->weapons[index / 32] & (1u << (index % 32))) != 0;
}
bool Com_BitCheckAssert(const unsigned *bits, int index, int size)
{
    CHECK(size == 16 && index >= 0 && index < 128);
    return (bits[index / 32] & (1u << (index % 32))) != 0;
}
int BG_PlayerWeaponCountPrimaryTypes(const playerState_s *) { return primaryCount; }
namespace sp {
#include "hint_item_sp.inc"
}
namespace mp {
// Preserve the existing production boolean expression, including its precedence.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wparentheses"
#endif
#include "hint_item_mp.inc"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
}
void CheckHint(int index, int itemKind, int itemInventory, int heldInventory, bool owned, int count)
{
    const unsigned weapon = unsigned(index % 128);
    gentity_s entity{};
    entity.s.index.item = index;
    entity.s.index.brushmodel = index;
    gclient_s client{};
    client.ps.weapon = weapon == 127 ? 0u : 127u;
    client.ps.weapons[weapon / 32] = owned ? 1u << (weapon % 32) : 0u;
    weapons[weapon].weapType = static_cast<weapType_t>(itemKind);
    weapons[weapon].inventoryType = static_cast<weapInventoryType_t>(itemInventory);
    weapons[client.ps.weapon].weapType = WEAPTYPE_BULLET;
    weapons[client.ps.weapon].inventoryType = static_cast<weapInventoryType_t>(heldInventory);
    bg_itemlist[index].giType = 1;
    primaryCount = count;
    const bool canReplace = heldInventory == 0 || heldInventory == 3;
    const bool room = count < 2;
    const int hint = int(weapon) + 4;
    const int expectedSP = !owned && itemKind != 1 && (canReplace || room) ? hint : 0;
    const bool itemUsesPrimary = itemInventory == 0 || itemInventory == 3;
    const int expectedMP = !owned && (canReplace || !itemUsesPrimary || room) ? hint : 0;
    CHECK(sp::Player_GetItemCursorHint(&client, &entity) == expectedSP);
    CHECK(mp::Player_GetItemCursorHint(&client, &entity) == expectedMP);
}
void CheckItemPolicies()
{
    // Cover every weapon index and all 16 alternate-model banks.
    for (int index = 0; index < 2048; ++index) CheckHint(index, 0, 0, 0, false, 2);
    // Frozen SP/MP differences: grenades and non-primary inventory are not interchangeable.
    for (int index : {1, 127, 128, 2047})
        for (int kind = 0; kind < 4; ++kind)
            for (int item = 0; item < 4; ++item)
                for (int held = 0; held < 4; ++held)
                    for (bool owned : {false, true})
                        for (int count : {0, 1, 2, 3}) CheckHint(index, kind, item, held, owned, count);
}
} // namespace
void RunHintValueContracts() { CheckItemPolicies(); }
