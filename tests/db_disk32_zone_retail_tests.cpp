// db_disk32_zone_retail_tests.cpp: asset-list shapes retail zones use that the
// main synthetic zone (db_disk32_zone_tests.cpp) does not, loaded through the
// real 64-bit path by disk32_zone_harness.cpp.
//
// A retail zone names an earlier top-level asset through that asset's header
// slot in the XAsset record array: retail code_post_gfx_mp lists a technique
// set inline, then a material whose technique-set token is the offset of the
// set's header slot. With no script strings the records start block 4, so
// record i's header slot is block-4 offset 8 * i + 4.
//
// It names any other block-4 pointer slot an earlier asset filled the same
// way: a material's texture names the image slot in an earlier material's
// texture table, and a menu list names the slot in an earlier list's menu
// array where an inline menu loaded.

#include "disk32_zone_harness.hpp"

#include <gfx_d3d/r_material.h>
#include <ui/ui_shared.h>

namespace
{
using namespace zone_test;

constexpr std::uint32_t HeaderSlot(std::uint32_t index)
{
    return 8 * index + 4;
}

void WriteTechniqueSet(Image &z) // inline: no alias slot; no techniques
{
    z.Word(kInline).Word(0).Word(0xDEADBEEF).Fill(34 * 4).V("retail/techset");
}
void WriteImage(Image &z) // inline; no pixels
{
    z.Word(2).Word(0).Fill(24).Word(kInline).V("retail/img");
}
// Block-4 slots the writers record for later tokens.
struct Slots
{
    std::uint32_t textureImage, menu;
} g_at{};

// One texture, whose image slot the second material names. Both references
// are the earlier assets' header slots.
void WriteMaterial(Image &z)
{
    z.Word(kInline).Word(1u << 8).Fill(16).Fill(34, 0xFF).Fill(1, 1).Fill(5); // no technique: state 255
    z.Word(Virt(HeaderSlot(0))).Word(kInline).Word(0).Word(0).V("retail/material");
    g_at.textureImage = z.VAlloc(12) + 8;
    z.Word(1).Word(0x02010201).Word(Virt(HeaderSlot(1)));
}
// Its texture is the first material's, named through that table's image slot.
void WriteSecondMaterial(Image &z)
{
    z.Word(kInline).Word(1u << 8).Fill(16).Fill(34, 0xFF).Fill(1, 1).Fill(5);
    z.Word(Virt(HeaderSlot(0))).Word(kInline).Word(0).Word(0).V("retail/material2");
    z.VAlloc(12);
    z.Word(1).Word(0x02010201).Word(Virt(g_at.textureImage));
}
// One inline menu with no items, which loads where its array slot is.
void WriteMenuList(Image &z)
{
    z.Word(kInline).Word(1).Word(kInline).V("retail/menus");
    g_at.menu = z.VAlloc(4);
    z.Word(kInline);
    z.Record(0x11C, {{0x00, kInline}}).V("retail/menu");
}
// Its menu is the first list's, named through that list's array slot.
void WriteSecondMenuList(Image &z)
{
    z.Word(kInline).Word(1).Word(kInline).V("retail/menus2");
    z.VAlloc(4);
    z.Word(Virt(g_at.menu));
}

const Asset kZone[] = {
    {ASSET_TYPE_TECHNIQUE_SET, kInline, "retail/techset", WriteTechniqueSet},
    {ASSET_TYPE_IMAGE, kInline, "retail/img", WriteImage},
    {ASSET_TYPE_MATERIAL, kInline, "retail/material", WriteMaterial},
    {ASSET_TYPE_MATERIAL, kInline, "retail/material2", WriteSecondMaterial},
    {ASSET_TYPE_MENULIST, kInline, "retail/menus", WriteMenuList},
    {ASSET_TYPE_MENULIST, kInline, "retail/menus2", WriteSecondMenuList},
};
} // namespace

std::span<const Asset> zone_test::ZoneAssets()
{
    return kZone;
}

std::span<const char *const> zone_test::ZoneScriptStrings()
{
    return {};
}

void zone_test::CheckZone()
{
    const Material *material = Find(ASSET_TYPE_MATERIAL, "retail/material").material;
    Expect(material && Is(material->techniqueSet, ASSET_TYPE_TECHNIQUE_SET, "retail/techset")
               && material->textureCount == 1 && Is(material->textureTable[0].u.image, ASSET_TYPE_IMAGE, "retail/img"),
           "a material names its technique set and image through their header slots");
    const Material *second = Find(ASSET_TYPE_MATERIAL, "retail/material2").material;
    Expect(second && second->textureCount == 1 && Is(second->textureTable[0].u.image, ASSET_TYPE_IMAGE, "retail/img"),
           "a material names an image through an earlier texture table's slot");
    const MenuList *menus = Find(ASSET_TYPE_MENULIST, "retail/menus").menuList;
    const MenuList *menus2 = Find(ASSET_TYPE_MENULIST, "retail/menus2").menuList;
    Expect(menus && menus->menuCount == 1 && Is(menus->menus[0], ASSET_TYPE_MENU, "retail/menu") && menus2
               && menus2->menuCount == 1 && menus2->menus[0] == menus->menus[0],
           "a menu list names a menu through an earlier list's array slot");
}
