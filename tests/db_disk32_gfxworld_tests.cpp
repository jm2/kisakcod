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
struct Zone : disk32_test::Zone<2048, 1024, 4096>
{
    Zone()
    {
        DB_SetInsertedPointer(DB_InsertPointer(DBAliasKind::GfxImage), DBAliasKind::GfxImage, &g_image);
        DB_SetInsertedPointer(DB_InsertPointer(DBAliasKind::GfxLightDef), DBAliasKind::GfxLightDef, &g_lightDef);
    }
};

// The record: two cells, one reflection probe, one brush model, two
// surfaces (both static), one static model, two primary lights past none,
// then the names, three indices, two sky surfaces and the sky image alias,
// the sun light, the probe and its texture, two planes and two nodes with
// the scene-entity bits, and the cells.
struct Record
{
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(kRecordBytes);

    Record()
    {
        Set(0x000, kInline).Set(0x004, kInline).Set(0x010, 3).Set(0x014, kInline).Set(0x018, 2).Set(0x020, 2);
        Set(0x024, kInline).Set(0x028, VirtualOffset(0)).Set(0x0D8, 0).Set(0x0DC, 2).Set(0x0E4, 1);
        Set(0x0E8, kInline).Set(0x0EC, kInline).Set(0x0F0, 2).Set(0x100, 16).Set(0x104, kInline).Set(0x150, 1);
        Set(0x154, kInline).Set(0x220, kInline).Set(0x244, 1).Set(0x248, 2).Set(0x24C, 0).Set(0x28C, kInline);
        Set(0x0C8, kInline).Set(0x008, 2).Set(0x0F4, kInline).Set(0x00C, 2).Set(0x0F8, kInline).Set(0x0FC, kInline);
    }
    Record &Set(std::uint32_t at, std::uint32_t value)
    {
        for (std::uint32_t byte = 0; byte < 4; ++byte)
            bytes[at + byte] = static_cast<std::uint8_t>(value >> (8 * byte));
        return *this;
    }
};

// What a test breaks in the cells.
struct CellOptions
{
    std::int32_t secondTreeCount = 1;  // the second cell's trees
    float firstMinX = 0;               // the first cell's bounds start here
    std::uint16_t smodelIndex = 0;     // the first tree's static-model index
    std::int32_t childrenOffset = 44;  // the first tree's children, one disk node on
    std::uint32_t portalCell = 0;      // the first portal's cell token, 0 for the second cell
    std::uint8_t portalVertices = 3;
    std::uint8_t portalSideX = 12;     // the first portal plane's x side
    std::uint8_t secondProbeCount = 1; // the second cell's probe indices
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
    File &Write(const Record &record, const Sun &sun = {}, const CellOptions &cells = {})
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
        Short(5).Short(6);
        return Cells(cells);
    }
    // A cell: unit bounds from minX, its tree, portal, cull-group and probe counts.
    File &Cell(float minX, std::int32_t trees, std::uint32_t cullGroups, std::uint8_t probes)
    {
        Float(minX).Float(0).Float(0).Float(1).Float(1).Float(1).Word(static_cast<std::uint32_t>(trees)).Word(kInline);
        Word(1).Word(kInline).Word(cullGroups).Word(cullGroups ? kInline : 0).Word(probes).Word(probes ? kInline : 0);
        return *this;
    }
    // A tree node: unit bounds, then its counts, indices token and children.
    File &Tree(std::uint16_t children, std::uint16_t surfaces, std::uint16_t start, std::uint16_t smodels,
               std::int32_t childrenOffset)
    {
        Float(0).Float(0).Float(0).Float(1).Float(1).Float(1).Short(children).Short(surfaces).Short(start);
        Short(0).Short(0).Short(smodels).Word(smodels ? kInline : 0);
        return Word(static_cast<std::uint32_t>(childrenOffset));
    }
    // A portal naming cell, its plane facing +x, then its vertices.
    File &Portal(std::uint32_t cell, std::uint8_t vertices, std::uint8_t sideX)
    {
        Word(0).Word(0).Word(0).Float(1).Float(0).Float(0).Float(-1).Word(sideX | 4u << 8 | 8u << 16);
        Word(cell).Word(kInline).Word(vertices);
        for (int value = 0; value < 6; ++value)
            Float(static_cast<float>(value));
        for (int value = 0; value < 3 * vertices; ++value)
            Float(static_cast<float>(value));
        return *this;
    }
    // Two cells at 152 portaled to each other. The first: three trees at 264
    // (a root of two leaves, its static-model index at 396), a portal at 400
    // and its vertices at 468, a cull-group index at 504 and a probe index at
    // 508. The second: a tree at 512, a portal at 556 and its vertices at 624,
    // and a probe index at 660.
    File &Cells(const CellOptions &o)
    {
        Cell(o.firstMinX, 3, 1, 1).Cell(0, o.secondTreeCount, 0, o.secondProbeCount);
        Tree(2, 2, 0, 1, o.childrenOffset).Tree(0, 1, 0, 0, 0).Tree(0, 1, 1, 0, 0).Short(o.smodelIndex);
        Portal(o.portalCell ? o.portalCell : VirtualOffset(152 + 56), o.portalVertices, o.portalSideX);
        Word(0).Text("");
        Tree(0, 0, 0, 0, 0).Portal(VirtualOffset(152), 3, 12);
        return Text("");
    }
};

GfxWorld *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadGfxWorldPtrDisk32, slotValue);
}

// The native records the load converted so far, in native storage's order:
// the sun light, the probe, then the probe's texture.
// The first native cell, after the prefix: its trees, portal and index arrays.
bool FirstCellConverted(const Zone &zone, const GfxCell *cells)
{
    const GfxAabbTree *const trees = cells[0].aabbTree;
    return trees == reinterpret_cast<const GfxAabbTree *>(g_arena + 280) && trees[0].childCount == 2
        && trees[0].childrenOffset == static_cast<std::int32_t>(sizeof(GfxAabbTree)) && trees[2].startSurfIndex == 1
        && trees[0].smodelIndexes == reinterpret_cast<const std::uint16_t *>(zone.virt + 396)
        && cells[0].cullGroups == reinterpret_cast<const int *>(zone.virt + 504)
        && cells[0].reflectionProbes == zone.virt + 508;
}

// The cells' portals, each naming the other native cell.
bool PortalsConverted(const Zone &zone, const GfxCell *cells)
{
    const GfxPortal &portal = cells[0].portals[0];
    return portal.cell == &cells[1] && portal.vertices == reinterpret_cast<const float (*)[3]>(zone.virt + 468)
        && portal.plane.side[0] == 12 && !portal.writable.hullPoints && portal.hullAxis[1][2] == 5.f
        && cells[1].portals[0].cell == &cells[0] && cells[1].reflectionProbes == zone.virt + 660 && !cells[1].cullGroups;
}

bool NativePrefixConverted()
{
    const auto *light = reinterpret_cast<const GfxLight *>(g_arena);
    const auto *probe = reinterpret_cast<const GfxReflectionProbe *>(g_arena + 72);
    GfxTexture texture{};
    std::memcpy(&texture, g_arena + 96, sizeof(texture));
    return g_arenaUsed == 696 && light->type == 1 && light->canUseShadowMap == 1 && light->color[0] == 0.5f
        && light->exponent == 3 && light->def == &g_lightDef && probe->origin[2] == 3.f
        && probe->reflectionImage == &g_image && !texture.basemap;
}

// The lightmaps and later parts, which do not load yet, end the load: what
// streamed before them sits at its aligned retail offsets, and what
// converted sits in native storage.
void TestPrefix()
{
    Zone zone;
    File().Write(Record());
    const Drop drop = Catch([] { Load(kInline); });
    std::uint16_t indices[3] = {};
    std::memcpy(indices, zone.virt + 14, sizeof(indices));
    std::int32_t sky[2] = {};
    std::memcpy(sky, zone.virt + 20, sizeof(sky));
    Expect(std::strstr(drop.message, "no 64-bit loader yet") && g_published == 0, "the load stops past the cells",
           drop.message);
    Expect(!std::strcmp(zone.At(8), "w") && !std::strcmp(zone.At(10), "ba") && indices[2] == 3 && sky[1] == 8,
           "the names, indices and sky surfaces stream into block 4 at their aligned retail offsets");
    Expect(zone.virt[28] == 1 && zone.virt[92 + 12] == 1 && zone.virt[148] == 5, // the probe names alias 0: 0x40000001
           "the sun light, the probe and the nodes stream at their 4- and 2-aligned retail offsets");
    Expect(NativePrefixConverted(), "the sun light, the probe and its texture convert into native storage");
    const auto *const cells = reinterpret_cast<const GfxCell *>(g_arena + 104);
    Expect(FirstCellConverted(zone, cells) && PortalsConverted(zone, cells),
           "the cells convert, with their trees at the native stride and portals naming native cells");
    Expect(DB_GetStreamPos() == zone.virt + 661 && g_read == g_file.size()
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
    {"more decal-free surfaces than static ones", 0x24C, 3, kCells},
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

void TestCellBreaksFailClosed()
{
    const std::pair<CellOptions, const char *> breaks[] = {
        {{0}, "world cell layout"},
        {{1, 2.f}, "world cell layout"},
        {{1, 0, 1}, "invalid static-model index"},
        {{1, 0, 0, 56}, "AABB topology"}, // a native stride, but no whole disk node
        {{1, 0, 0, 44, VirtualOffset(16)}, "portal target is not a cell"},
        {{1, 0, 0, 44, VirtualOffset(152 + 8)}, "portal target is not a cell"},
        {{1, 0, 0, 44, VirtualOffset(152 + 112)}, "portal target is not a cell"},
        {{1, 0, 0, 44, 0, 65}, "portal vertex layout"},
        {{1, 0, 0, 44, kInline}, "portal cell token"},
        {{1, 0, 0, 44, 0, 2}, "portal vertex layout"},
        {{1, 0, 0, 44, 0, 3, 0}, "completed fast-file world portal"},
        {{1, 0, 0, 44, 0, 3, 12, 0}, "world cell layout"},
    };
    for (const auto &[cells, error] : breaks)
    {
        Zone zone;
        File().Write(Record(), Sun{}, cells);
        ExpectDrop("a malformed cell", error, [] { Load(kInline); });
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
    return Run({TestPrefix, TestSunAligned, TestSunBreaksFailClosed, TestCellBreaksFailClosed, TestMalformedFailsClosed});
}
