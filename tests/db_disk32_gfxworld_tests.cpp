// db_disk32_gfxworld_tests.cpp: the 64-bit world loader (NOW row 12) on
// hand-built disk32 zone images (disk32_fixture.hpp), with Image's,
// LightDef's and Material's real steps for the images, light defs and
// materials it names. The retail offsets here are written out by hand, apart
// from the schema. Beyond the fixture's seams, only the asset pools
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

GfxImage g_image; // the aliases Zone registers at block-4 offsets 0, 4 and 8
GfxLightDef g_lightDef;
Material g_material;

constexpr std::uint32_t kRecordBytes = 732;

// Every zone starts block 4 with an image, a light-def and a material alias,
// as earlier assets leave them.
struct Zone : disk32_test::Zone<2048, 1024, 4096>
{
    Zone()
    {
        DB_SetInsertedPointer(DB_InsertPointer(DBAliasKind::GfxImage), DBAliasKind::GfxImage, &g_image);
        DB_SetInsertedPointer(DB_InsertPointer(DBAliasKind::GfxLightDef), DBAliasKind::GfxLightDef, &g_lightDef);
        DB_SetInsertedPointer(DB_InsertPointer(DBAliasKind::Material), DBAliasKind::Material, &g_material);
        std::memset(runtime, 0xAA, sizeof(runtime)); // so the zero-filled runtime arrays show
    }
};

// The record: two cells, one reflection probe, one brush model, two
// surfaces (both static), one static model, two primary lights past none,
// then the names, three indices, two sky surfaces and the sky image alias,
// the sun light, the probe and its texture, two planes and two nodes with
// the scene-entity bits, and the cells; then a lightmap naming the image,
// a light grid of two rows, the lightmap textures, the brush model, material
// memory naming the material, a vertex and three layer bytes, the sun's
// sprite material and the outdoor image, one dynamic model and brush with
// their runtime arrays, and both primary lights' shadow geometry and light
// regions.
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
        Set(0x108, 1).Set(0x10C, kInline).Set(0x118, 2).Set(0x11C, 3u << 16).Set(0x128, 1).Set(0x12C, kInline);
        Set(0x130, 3).Set(0x134, kInline).Set(0x138, 1).Set(0x13C, kInline).Set(0x140, 1).Set(0x144, kInline);
        Set(0x148, kInline).Set(0x14C, kInline).Set(0x174, 1).Set(0x178, kInline).Set(0x180, VirtualOffset(8));
        Set(0x030, 1).Set(0x034, kInline).Set(0x03C, 3).Set(0x040, kInline).Set(0x21C, VirtualOffset(0));
        Set(0x224, kInline).Set(0x228, kInline).Set(0x230, kInline).Set(0x238, kInline).Set(0x2B4, 1).Set(0x2B8, 1);
        Set(0x234, kInline).Set(0x23C, kInline).Set(0x240, kInline);
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

// What a test breaks in the primary lights' shadow geometry and regions.
struct Shadows
{
    std::uint16_t firstSurface = 1;        // the first light's sorted-surface index
    std::uint16_t smodel = 0;              // the second light's static-model index
    std::uint32_t secondSurfaces = kInline; // the second light's sorted-surface token
    std::uint32_t hulls = kInline;         // the first region's hull token
    std::uint32_t axes = kInline;          // its hull's axis token
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
    // (4-aligned) at 12, 14, 18 and 24, the sun light at 32, the probe at 96,
    // the planes at 112 and the nodes at 152; block 1 holds the probe's
    // texture and then the scene-entity bits.
    File &Write(const Record &record, const Sun &sun = {}, const CellOptions &cells = {}, bool lightmap = true,
                bool rows = true, const Shadows &shadows = {})
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
        return Cells(cells).Middle(lightmap, rows).PrimaryLights(shadows);
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
    // Two cells at 156 portaled to each other. The first: three trees at 268
    // (a root of two leaves, its static-model index at 400), a portal at 404
    // and its vertices at 472, a cull-group index at 508 and a probe index at
    // 512. The second: a tree at 516, a portal at 560 and its vertices at 628,
    // and a probe index at 664.
    File &Cells(const CellOptions &o)
    {
        Cell(o.firstMinX, 3, 1, 1).Cell(0, o.secondTreeCount, 0, o.secondProbeCount);
        Tree(2, 2, 0, 1, o.childrenOffset).Tree(0, 1, 0, 0, 0).Tree(0, 1, 1, 0, 0).Short(o.smodelIndex);
        Portal(o.portalCell ? o.portalCell : VirtualOffset(156 + 56), o.portalVertices, o.portalSideX);
        Word(0).Text("");
        Tree(0, 0, 0, 0, 0).Portal(VirtualOffset(156), 3, 12);
        return Text("");
    }
    // Past the cells, 4-aligned: the lightmap at 668; the light grid's row
    // starts at 676, raw rows at 680, entry at 684 and colors at 688; the
    // brush model at 856, material memory at 912, the vertex at 920 and the
    // layer bytes at 964. A test may leave out the lightmap or the row starts.
    File &Middle(bool lightmap, bool rows)
    {
        if (lightmap)
            Word(VirtualOffset(0)).Word(0);
        if (rows)
            Short(7).Short(8);
        Text("ab").Word(9);
        for (int value = 0; value < 42; ++value)
            Word(static_cast<std::uint32_t>(value));
        for (int value = 0; value < 14; ++value)
            Float(static_cast<float>(value));
        Word(VirtualOffset(8)).Word(77);
        for (int value = 0; value < 11; ++value)
            Float(static_cast<float>(value));
        return Text("xy");
    }
    // Past the layer bytes, 4-aligned: the two shadow geometries at 968, the
    // first's surface index at 992, the second's two at 994 and its
    // static-model index at 998 (each only 2-aligned); the two regions at
    // 1000, the first's hull at 1016 and the hull's axis at 1096.
    File &PrimaryLights(const Shadows &o)
    {
        Word(1).Word(kInline).Word(0).Word(2 | 1u << 16).Word(o.secondSurfaces).Word(kInline);
        Short(o.firstSurface);
        if (o.secondSurfaces)
            Short(0).Short(1);
        Short(o.smodel).Word(1).Word(o.hulls).Word(0).Word(0);
        if (!o.hulls)
            return *this;
        for (int value = 0; value < 18; ++value)
            Float(static_cast<float>(value));
        Word(1).Word(o.axes);
        if (o.axes)
            Float(0).Float(0).Float(1).Float(2).Float(3);
        return *this;
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
        && trees[0].smodelIndexes == reinterpret_cast<const std::uint16_t *>(zone.virt + 400)
        && cells[0].cullGroups == reinterpret_cast<const int *>(zone.virt + 508)
        && cells[0].reflectionProbes == zone.virt + 512;
}

// The cells' portals, each naming the other native cell.
bool PortalsConverted(const Zone &zone, const GfxCell *cells)
{
    const GfxPortal &portal = cells[0].portals[0];
    return portal.cell == &cells[1] && portal.vertices == reinterpret_cast<const float (*)[3]>(zone.virt + 472)
        && portal.plane.side[0] == 12 && !portal.writable.hullPoints && portal.hullAxis[1][2] == 5.f
        && cells[1].portals[0].cell == &cells[0] && cells[1].reflectionProbes == zone.virt + 664
        && !cells[1].cullGroups;
}

bool NativePrefixConverted()
{
    const auto *light = reinterpret_cast<const GfxLight *>(g_arena);
    const auto *probe = reinterpret_cast<const GfxReflectionProbe *>(g_arena + 72);
    GfxTexture texture{};
    std::memcpy(&texture, g_arena + 96, sizeof(texture));
    return light->type == 1 && light->canUseShadowMap == 1 && light->color[0] == 0.5f
        && light->exponent == 3 && light->def == &g_lightDef && probe->origin[2] == 3.f
        && probe->reflectionImage == &g_image && !texture.basemap;
}

// Past the cells: the lightmap naming the image, then its two textures
// (zeroed native storage), then the material memory naming the material.
bool MiddleConverted()
{
    const auto *lightmap = reinterpret_cast<const GfxLightmapArray *>(g_arena + 696);
    const auto *memory = reinterpret_cast<const MaterialMemory *>(g_arena + 728);
    GfxTexture textures[2]{};
    std::memcpy(textures, g_arena + 712, sizeof(textures));
    return lightmap->primary == &g_image && !lightmap->secondary && !textures[0].basemap
        && !textures[1].basemap && memory->material == &g_material && memory->memory == 77;
}

// Block 1 zero-fills, 4-aligned past the probe's texture and the scene-entity
// bits (2052 bytes): the lightmap textures, two cell-caster words, a 6-byte
// dynamic model, a dynamic brush, a model and a brush shadow word, and a
// light byte.
bool RuntimeArraysZeroed(const std::uint8_t *runtime)
{
    for (std::size_t at = 0; at < 2089; ++at)
    {
        if (runtime[at] != (at == 2074 || at == 2075 ? 0xAA : 0))
            return false;
    }
    return runtime[2089] == 0xAA;
}

// The light grid, brush model, material memory and vertex data in block 4.
bool MiddleStreamed(const Zone &zone)
{
    std::uint16_t rows[2] = {};
    std::memcpy(rows, zone.virt + 676, sizeof(rows));
    float model[14] = {};
    std::memcpy(model, zone.virt + 856, sizeof(model));
    float vertex[11] = {};
    std::memcpy(vertex, zone.virt + 920, sizeof(vertex));
    return rows[1] == 8 && !std::strcmp(zone.At(680), "ab") && zone.virt[684] == 9 && zone.virt[688 + 164] == 41
        && model[13] == 13.f && zone.virt[912 + 4] == 77 && vertex[10] == 10.f && !std::strcmp(zone.At(964), "xy");
}

// The two shadow geometries in native storage past the material memory,
// their indices in block 4.
bool ShadowsConverted(const Zone &zone)
{
    const auto *const shadows = reinterpret_cast<const GfxShadowGeometry *>(g_arena + 744);
    return shadows[0].surfaceCount == 1
        && shadows[0].sortedSurfIndex == reinterpret_cast<const std::uint16_t *>(zone.virt + 992)
        && !shadows[0].smodelIndex && shadows[1].smodelCount == 1
        && shadows[1].sortedSurfIndex == reinterpret_cast<const std::uint16_t *>(zone.virt + 994)
        && shadows[1].smodelIndex == reinterpret_cast<const std::uint16_t *>(zone.virt + 998);
}

// Then the two regions and the first's hull, its axis in block 4.
bool RegionsConverted(const Zone &zone)
{
    const auto *const regions = reinterpret_cast<const GfxLightRegion *>(g_arena + 792);
    const GfxLightRegionHull *const hull = regions[0].hulls;
    return g_arenaUsed == 912 && regions[0].hullCount == 1
        && hull == reinterpret_cast<const GfxLightRegionHull *>(g_arena + 824) && hull->kdopHalfSize[8] == 17.f
        && hull->axisCount == 1 && hull->axis == reinterpret_cast<const GfxLightRegionAxis *>(zone.virt + 1096)
        && hull->axis->halfSize == 3.f && !regions[1].hullCount && !regions[1].hulls;
}

// The names, indices and sky surfaces, then the sun light, the probe (naming
// alias 0: 0x40000001) and the nodes, at their aligned retail offsets.
bool StartStreamed(const Zone &zone)
{
    std::uint16_t indices[3] = {};
    std::memcpy(indices, zone.virt + 18, sizeof(indices));
    std::int32_t sky[2] = {};
    std::memcpy(sky, zone.virt + 24, sizeof(sky));
    return !std::strcmp(zone.At(12), "w") && !std::strcmp(zone.At(14), "ba") && indices[2] == 3 && sky[1] == 8
        && zone.virt[32] == 1 && zone.virt[96 + 12] == 1 && zone.virt[152] == 5;
}

// The DPVS, which does not load yet, ends the load: what streamed before it
// sits at its aligned retail offsets, and what converted sits in native
// storage.
void TestPrefix()
{
    Zone zone;
    File().Write(Record());
    const Drop drop = Catch([] { Load(kInline); });
    Expect(std::strstr(drop.message, "DPVS has no 64-bit loader yet") && g_published == 0,
           "the load stops past the primary lights", drop.message);
    Expect(StartStreamed(zone), "the names, indices, sky surfaces, sun light, probe and nodes stream into block 4 "
                                "at their 4- and 2-aligned retail offsets");
    Expect(NativePrefixConverted(), "the sun light, the probe and its texture convert into native storage");
    const auto *const cells = reinterpret_cast<const GfxCell *>(g_arena + 104);
    Expect(FirstCellConverted(zone, cells) && PortalsConverted(zone, cells),
           "the cells convert, with their trees at the native stride and portals naming native cells");
    Expect(MiddleStreamed(zone),
           "the light grid, brush model, material memory and vertex data stream at their aligned retail offsets");
    Expect(MiddleConverted(), "the lightmap, its textures and the material memory convert into native storage");
    Expect(RuntimeArraysZeroed(zone.runtime), "the runtime arrays zero-fill block 1 at their 4- and 1-aligned extents");
    Expect(ShadowsConverted(zone) && RegionsConverted(zone),
           "the shadow geometry and light regions convert into native storage");
    Expect(DB_GetStreamPos() == zone.virt + 1116 && g_read == g_file.size()
               && !std::memcmp(zone.temp, g_file.data(), kRecordBytes),
           "the record streams into the temp block, and block 4 holds exactly what loaded");
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
    {"a negative lightmap count", 0x108, 0xFFFFFFFF, "world array count"},
    {"a light-grid row axis past z", 0x124, 3, "light-grid axes"},
    {"a light-grid column axis past z", 0x128, 3, "light-grid axes"},
    {"light-grid rows ending before they start", 0x11C, 1u << 16, "light-grid rows"},
    {"raw light-grid rows past the block", 0x130, 0x7FFFFFFF, "exceeds stream block"},
    {"a negative material-memory count", 0x174, 0xFFFFFFFF, "world array count"},
    {"vertex bytes past 32 bits", 0x030, 0x10000000, "vertex-buffer bytes"},
    {"vertex layer bytes past the block", 0x03C, 0x7FFFFFF0, "exceeds stream block"},
    {"an unmapped sun sprite material", 0x180, VirtualOffset(64), "alias offset"},
    {"an unmapped sun flare material", 0x184, VirtualOffset(64), "alias offset"},
    {"an unmapped outdoor image", 0x21C, VirtualOffset(64), "alias offset"},
    {"dynamic models past block 1", 0x2B4, 1000, "exceeds stream block 1"},
    {"dynamic brushes past block 1", 0x2B8, 1000, "exceeds stream block 1"},
    {"primary lights without shadow geometry", 0x23C, 0, "world primary lights"},
    {"primary lights without light regions", 0x240, 0, "world primary lights"},
};

// With two indices and no sky surfaces the sun light's record starts
// 4-aligned past them.
// Without the lightmap the row starts follow the cells 2-aligned at 666;
// without the row starts too, the raw rows follow at 665 and the entry
// 4-aligned at 668. Without the dynamic brush and shadow words, the light
// bytes follow the 6-byte dynamic model 1-aligned in block 1.
void TestGridAligned()
{
    Record record;
    record.Set(0x108, 0).Set(0x10C, 0).Set(0x148, 0).Set(0x14C, 0).Set(0x228, 0).Set(0x230, 0).Set(0x234, 0);
    {
        Zone zone;
        File().Write(record, Sun{}, CellOptions{}, false);
        Catch([] { Load(kInline); });
        Expect(zone.virt[666] == 7 && !std::strcmp(zone.At(670), "ab") && zone.virt[676] == 9,
               "the row starts follow the cells 2-aligned");
        Expect(zone.runtime[2066] == 0 && zone.runtime[2067] == 0xAA,
               "the light bytes follow the dynamic model 1-aligned");
    }
    Zone zone;
    File().Write(record.Set(0x12C, 0), Sun{}, CellOptions{}, false, false);
    Catch([] { Load(kInline); });
    Expect(!std::strcmp(zone.At(665), "ab") && zone.virt[668] == 9, "the raw rows follow the cells 1-aligned");
}

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
    Expect(zone.virt[22] == 0 && zone.virt[24] == 1, "the sun light starts 4-aligned at 24, past the indices at 18");
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
        {{1, 0, 0, 44, VirtualOffset(20)}, "portal target is not a cell"},
        {{1, 0, 0, 44, VirtualOffset(156 + 8)}, "portal target is not a cell"},
        {{1, 0, 0, 44, VirtualOffset(156 + 112)}, "portal target is not a cell"},
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

void TestShadowBreaksFailClosed()
{
    const std::pair<Shadows, const char *> breaks[] = {
        {{2}, "shadow geometry has an invalid index"}, // the static surfaces number 2
        {{1, 1}, "shadow geometry has an invalid index"}, // the static models number 1
        {{1, 0, 0}, "pointer/count for world shadow geometry"},
        {{1, 0, kInline, 0}, "light-region hulls"},
        {{1, 0, kInline, kInline, 0}, "light-region axes"},
    };
    for (const auto &[shadows, error] : breaks)
    {
        Zone zone;
        File().Write(Record(), Sun{}, CellOptions{}, true, true, shadows);
        ExpectDrop("a malformed primary light", error, [] { Load(kInline); });
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

// Materials and techniques resolve only by alias; their TUs link all the same.
void __cdecl Load_MaterialAsset(XAssetHeader *)
{
    Expect(false, "no material loads");
}

void __cdecl Load_MaterialTechniqueSetAsset(XAssetHeader *)
{
    Expect(false, "no technique set loads");
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
    return Run({TestPrefix, TestGridAligned, TestSunAligned, TestSunBreaksFailClosed, TestCellBreaksFailClosed,
                TestShadowBreaksFailClosed, TestMalformedFailsClosed});
}
