// db_disk32_gfxworld_tests.cpp: the 64-bit world loader (NOW row 12) on
// hand-built disk32 zone images (disk32_fixture.hpp), with Image's and
// LightDef's real steps for the images and light defs it names. The retail offsets here are written out by hand,
// apart from the schema. Beyond the fixture's seams, only the asset pools
// are replaced.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>

#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace
{
using namespace disk32_test;

GfxImage g_image; // the aliases Zone registers at block-4 offsets 0 and 4
GfxLightDef g_lightDef;

constexpr std::uint32_t kRecordBytes = 732;

// Every zone starts block 4 with an image and a light-def alias, as earlier
// assets leave them.
struct Zone : disk32_test::Zone<2048, 1024, 2048>
{
    Zone()
    {
        DB_SetInsertedPointer(DB_InsertPointer(DBAliasKind::GfxImage), DBAliasKind::GfxImage, &g_image);
        DB_SetInsertedPointer(DB_InsertPointer(DBAliasKind::GfxLightDef), DBAliasKind::GfxLightDef, &g_lightDef);
    }
};

// The record: one cell, one reflection probe, one brush model, two
// surfaces (one static, one without decals), two primary lights past none,
// then the names, three indices, two sky surfaces and the sky image alias,
// the sun light, the probe and its texture, and two planes and two nodes
// with the scene-entity bits.
struct Record
{
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(kRecordBytes);

    Record()
    {
        Set(0x000, kInline).Set(0x004, kInline).Set(0x010, 3).Set(0x014, kInline).Set(0x018, 2).Set(0x020, 2);
        Set(0x024, kInline).Set(0x028, VirtualOffset(0)).Set(0x0D8, 0).Set(0x0DC, 2).Set(0x0E4, 1);
        Set(0x0E8, kInline).Set(0x0EC, kInline).Set(0x0F0, 1).Set(0x100, 16).Set(0x104, kInline).Set(0x150, 1);
        Set(0x154, kInline).Set(0x220, kInline).Set(0x248, 1).Set(0x24C, 1).Set(0x28C, kInline);
        Set(0x0C8, kInline).Set(0x008, 2).Set(0x0F4, kInline).Set(0x00C, 2).Set(0x0F8, kInline).Set(0x0FC, kInline);
    }
    Record &Set(std::uint32_t at, std::uint32_t value)
    {
        for (std::uint32_t byte = 0; byte < 4; ++byte)
            bytes[at + byte] = static_cast<std::uint8_t>(value >> (8 * byte));
        return *this;
    }
};

// What a test breaks in the sun light.
struct Sun
{
    std::uint8_t type = 1; // directional
    std::uint8_t shadowMap = 1;
    float red = 0.5f;
    std::uint32_t def = VirtualOffset(4);
};

struct File : FileBuilder<File>
{
    File &Short(std::uint16_t value)
    {
        g_file.push_back(static_cast<std::uint8_t>(value));
        g_file.push_back(static_cast<std::uint8_t>(value >> 8));
        return *this;
    }
    // The record, the names, the indices (2-aligned) and the sky surfaces
    // (4-aligned) at 8, 10, 14 and 20, the sun light at 28, the probe at 92,
    // the planes at 108 and the nodes at 148; block 1 holds the probe's
    // texture and then the scene-entity bits.
    File &Write(const Record &record, const Sun &sun = {})
    {
        g_file.insert(g_file.end(), record.bytes.begin(), record.bytes.end());
        Text("w").Text("ba").Short(1).Short(2).Short(3).Word(7).Word(8);
        Word(sun.type | sun.shadowMap << 8).Float(sun.red);
        for (int value = 1; value < 12; ++value)
            Float(static_cast<float>(value));
        Word(3).Word(4).Word(sun.def);
        Float(1).Float(2).Float(3).Word(VirtualOffset(0));
        for (int value = 0; value < 10; ++value)
            Float(static_cast<float>(value));
        return Short(5).Short(6);
    }
};

GfxWorld *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadGfxWorldPtrDisk32, slotValue);
}

// The native records the load converted so far, in native storage's order:
// the sun light, the probe, then the probe's texture.
bool NativePrefixConverted()
{
    const auto *light = reinterpret_cast<const GfxLight *>(g_arena);
    const auto *probe = reinterpret_cast<const GfxReflectionProbe *>(g_arena + 72);
    GfxTexture texture{};
    std::memcpy(&texture, g_arena + 96, sizeof(texture));
    return g_arenaUsed == 104 && light->type == 1 && light->canUseShadowMap == 1 && light->color[0] == 0.5f
        && light->exponent == 3 && light->def == &g_lightDef && probe->origin[2] == 3.f
        && probe->reflectionImage == &g_image && !texture.basemap;
}

// The cells, which do not load yet, end the load: what streamed before them
// sits at its aligned retail offsets, and what converted sits in native
// storage.
void TestPrefix()
{
    Zone zone;
    File().Write(Record());
    const Drop drop = Catch([] { Load(kInline); });
    std::uint16_t indices[3] = {};
    std::memcpy(indices, zone.virt + 14, sizeof(indices));
    std::int32_t sky[2] = {};
    std::memcpy(sky, zone.virt + 20, sizeof(sky));
    Expect(std::strstr(drop.message, "no 64-bit loader yet") && g_published == 0, "the load stops at the cells",
           drop.message);
    Expect(!std::strcmp(zone.At(8), "w") && !std::strcmp(zone.At(10), "ba") && indices[2] == 3 && sky[1] == 8,
           "the names, indices and sky surfaces stream into block 4 at their aligned retail offsets");
    Expect(zone.virt[28] == 1 && zone.virt[92 + 12] == 1 && zone.virt[148] == 5, // the probe names alias 0: 0x40000001
           "the sun light, the probe and the nodes stream at their 4- and 2-aligned retail offsets");
    Expect(NativePrefixConverted(), "the sun light, the probe and its texture convert into native storage");
    Expect(DB_GetStreamPos() == zone.virt + 152 && g_read == g_file.size()
               && !std::memcmp(zone.temp, g_file.data(), kRecordBytes),
           "the record streams into the temp block, and block 4 holds exactly the prefix");
}

struct Malformed
{
    const char *what;
    std::uint32_t at;
    std::uint32_t value;
    const char *error;
};

constexpr const char *kCells = "world cell array";
constexpr const char *kLookups = "world cell lookup arrays";
constexpr const char *kVisibility = "world visibility counts";

const Malformed kMalformed[] = {
    {"no cells", 0x0F0, 0, kCells},
    {"1025 cells", 0x0F0, 1025, kCells},
    {"cells without a token", 0x104, 0, kCells},
    {"cell bits of another size", 0x100, 32, kCells},
    {"a negative surface count", 0x018, 0xFFFFFFFF, kCells},
    {"more decal-free surfaces than static ones", 0x24C, 2, kCells},
    {"static surfaces past the world's", 0x248, 3, kCells},
    {"no reflection probes", 0x0E4, 0, kLookups},
    {"255 reflection probes", 0x0E4, 255, kLookups},
    {"probes without a token", 0x0E8, 0, kLookups},
    {"probe textures without a token", 0x0EC, 0, kLookups},
    {"cull groups without a token", 0x0E0, 1, kLookups},
    {"a negative cull-group count", 0x0E0, 0xFFFFFFFF, kLookups},
    {"no brush models", 0x150, 0, kLookups},
    {"brush models without a token", 0x154, 0, kLookups},
    {"no cell-caster bits", 0x220, 0, kVisibility},
    {"no sorted surfaces", 0x28C, 0, kVisibility},
    {"the sun past the primary lights", 0x0D8, 2, kVisibility},
    {"dynamic-model shadows past 32 bits", 0x2B4, 0x80000000, kVisibility},
    {"a null name", 0x000, 0, "has no name"},
    {"a negative index count", 0x010, 0xFFFFFFFF, "array count"},
    {"an unmapped sky image", 0x028, VirtualOffset(64), "alias offset"},
    {"an unmapped sun-light alias", 0x0C8, VirtualOffset(400), "alias offset"},
    {"planes without a token", 0x0F4, 0, "world planes"},
    {"an unmapped plane offset", 0x0F4, VirtualOffset(1900), "world pointer offset"},
    {"no nodes token, nodes counted", 0x0F8, 0, "no 64-bit loader yet"},
};

// With two indices and no sky surfaces the sun light's record starts
// 4-aligned past them.
void TestSunAligned()
{
    Zone zone;
    Record record;
    record.Set(0x010, 2).Set(0x020, 0).Set(0x024, 0);
    g_file.insert(g_file.end(), record.bytes.begin(), record.bytes.end());
    File file;
    file.Text("w").Text("ba").Short(1).Short(2).Word(1 | 1u << 8).Float(0.5f);
    for (int value = 1; value < 12; ++value)
        file.Float(static_cast<float>(value));
    file.Word(3).Word(4).Word(VirtualOffset(4));
    Catch([] { Load(kInline); });
    Expect(zone.virt[18] == 0 && zone.virt[20] == 1, "the sun light starts 4-aligned at 20, past the indices at 14");
}

void TestSunBreaksFailClosed()
{
    const std::pair<Sun, const char *> suns[] = {
        {{2}, "sun light"}, {{1, 2}, "sun light"}, {{1, 1, std::numeric_limits<float>::infinity()}, "sun light"},
        {{1, 1, 0.5f, VirtualOffset(64)}, "alias offset"}};
    for (const auto &[sun, error] : suns)
    {
        Zone zone;
        File().Write(Record(), sun);
        ExpectDrop("a malformed sun light", error, [] { Load(kInline); });
    }
}

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone;
        File().Write(Record().Set(test.at, test.value));
        ExpectDrop(test.what, test.error, [] { Load(kInline); });
    }
    Zone zone;
    File().Write(Record());
    g_file.resize(kRecordBytes - 4);
    ExpectDrop("a truncated record", "ended unexpectedly", [] { Load(kInline); });
    ExpectDrop("an unmapped alias", "alias offset", [] { Load(VirtualOffset(16)); });
}
} // namespace

void __cdecl Load_GfxWorldAsset(XAssetHeader *)
{
    ++g_published; // nothing publishes until every part loads
}

// The sky names its image by alias, so no image loads; Image's TU links all the same.
void __cdecl Load_GfxImageAsset(XAssetHeader *)
{
    Expect(false, "no image loads");
}

void __cdecl Load_LightDefAsset(XAssetHeader *)
{
    Expect(false, "no light def loads");
}

void __cdecl DB_LoadedExternalData(std::int32_t)
{
    Expect(false, "no image loads");
}

int main()
{
    return Run({TestPrefix, TestSunAligned, TestSunBreaksFailClosed, TestMalformedFailsClosed});
}
