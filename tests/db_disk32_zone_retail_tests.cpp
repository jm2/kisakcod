// db_disk32_zone_retail_tests.cpp: asset-list shapes retail zones use that the
// main synthetic zone (db_disk32_zone_tests.cpp) does not, loaded through the
// real 64-bit path by disk32_zone_harness.cpp.
//
// A retail zone names an earlier top-level asset through that asset's header
// slot in the XAsset record array: retail code_post_gfx_mp lists a technique
// set inline, then a material whose technique-set token is the offset of the
// set's header slot. With no script strings the records start block 4, so
// record i's header slot is block-4 offset 8 * i + 4.

#include "disk32_zone_harness.hpp"

#include <gfx_d3d/r_material.h>

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
// One texture. Both references are the earlier assets' header slots.
void WriteMaterial(Image &z)
{
    z.Word(kInline).Word(1u << 8).Fill(16).Fill(34, 0xFF).Fill(1, 1).Fill(5); // no technique: state 255
    z.Word(Virt(HeaderSlot(0))).Word(kInline).Word(0).Word(0).V("retail/material");
    z.VAlloc(12);
    z.Word(1).Word(0x02010201).Word(Virt(HeaderSlot(1)));
}

const Asset kZone[] = {
    {ASSET_TYPE_TECHNIQUE_SET, kInline, "retail/techset", WriteTechniqueSet},
    {ASSET_TYPE_IMAGE, kInline, "retail/img", WriteImage},
    {ASSET_TYPE_MATERIAL, kInline, "retail/material", WriteMaterial},
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
}
