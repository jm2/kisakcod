// db_disk32_clipmap_tests.cpp: the 64-bit clip-map loader (NOW row 12) on
// hand-built disk32 zone images (disk32_fixture.hpp), with MapEnts' real step
// for the map entities and XModel's for static models. The retail offsets
// here are written out by hand, apart from the schema; Placer tracks where
// each array lands in block 4. Beyond the fixture's seams, only the asset
// pools are replaced.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>
#include <database/db_load_legacy_bridge.h>

#include <cstring>
#include <limits>
#include <vector>

namespace
{
using namespace disk32_test;

clipMap_t g_maps[2]; // what Load_ClipMapAsset published
MapEnts g_ents[2];   // what Load_MapEntsAsset published
int g_entCount = 0;
XModel g_model;      // the model alias at block-4 offset 0 that static models name

constexpr std::uint32_t kRecordBytes = 284;

// Every zone starts block 4 with the model alias, as an earlier asset leaves it.
struct Zone : disk32_test::Zone<2048>
{
    Zone()
    {
        g_entCount = 0;
        DB_SetInsertedPointer(DB_InsertPointer(DBAliasKind::XModel), DBAliasKind::XModel, &g_model);
    }
};

// Where each part lands in block 4, as the loader allocates it.
struct Placer
{
    std::uint32_t at = 4; // past the model alias
    std::uint32_t Place(std::uint32_t alignment, std::uint32_t bytes)
    {
        at = (at + alignment - 1) / alignment * alignment;
        const std::uint32_t placed = at;
        at += bytes;
        return placed;
    }
};

// The record: a pattern in its box model and checksum, and each part's count
// and inline token.
struct Record
{
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(kRecordBytes);

    Record &Set(std::uint32_t at, std::uint32_t value)
    {
        for (std::uint32_t byte = 0; byte < 4; ++byte)
            bytes[at + byte] = static_cast<std::uint8_t>(value >> (8 * byte));
        return *this;
    }
    Record &Array(std::uint32_t at, std::uint32_t count)
    {
        return Set(at, count).Set(at + 4, kInline);
    }
};

Record WholeRecord()
{
    Record record;
    for (std::uint32_t word = 0; word < 18; ++word)
        record.Set(0x0AC + word * 4, 0xB0 + word);
    record.Set(0x000, kInline).Set(0x004, 1).Set(0x118, 0xC5C5C5C5);
    record.Array(0x008, 2).Array(0x010, 1).Array(0x018, 1).Array(0x020, 2).Array(0x028, 6).Array(0x030, 2);
    record.Array(0x038, 1).Array(0x040, 3).Array(0x048, 2).Array(0x050, 1).Array(0x058, 1).Array(0x060, 11);
    record.Set(0x068, kInline).Array(0x06C, 1).Array(0x074, 2).Array(0x07C, 1).Array(0x084, 1).Array(0x08C, 2);
    record.Set(0x094, 2).Set(0x098, 1).Set(0x09C, kInline).Set(0x0A4, kInline).Set(0x0A8, kInline);
    return std::move(record.Set(0x0F4, 2 | 1u << 16).Set(0x100, kInline).Set(0x10C, kInline).Set(0x110, kInline));
}

// What a test breaks in the stream after the record.
struct Options
{
    std::uint32_t sidePlane = 0;      // the side's plane token, 0 for the second plane
    std::uint8_t lastEdge = 0;        // the brush's last adjacent side
    std::uint32_t brushSides = 0;     // the brush's sides token, 0 for the side
    std::uint32_t brushEdges = 0;     // the brush's adjacency token, 0 for the edges
    std::uint32_t brushSideCount = 1; // its numsides
    std::uint8_t planeType = 0;       // the first plane's type
    std::int32_t boxContents = -1;
    std::int16_t firstAxialOffset = 0; // the first brush's first axial adjacency slot
};

struct File : FileBuilder<File>
{
    Placer placer;
    std::uint32_t planes = 0, staticModel = 0, side = 0, edges = 0, nodes = 0, nodePlane = 0, leafNodes = 0;
    std::uint32_t indices = 0, partitions = 0, border = 0, brush = 0, box = 0;

    File &Byte(std::uint8_t value)
    {
        g_file.push_back(value);
        return *this;
    }
    File &Short(std::uint16_t value)
    {
        return Byte(static_cast<std::uint8_t>(value)).Byte(static_cast<std::uint8_t>(value >> 8));
    }
    File &Fill(std::uint32_t alignment, std::uint32_t count, std::uint8_t value)
    {
        placer.Place(alignment, count);
        g_file.insert(g_file.end(), count, value);
        return *this;
    }
    File &Plane(float x, float y, float z, std::uint8_t type, std::uint8_t signbits)
    {
        return Float(x).Float(y).Float(z).Float(1).Byte(type).Byte(signbits).Short(0);
    }
    // A brush: unit bounds, or a box brush's sentinels, then its sides,
    // materials, adjacency and edge counts.
    File &Brush(bool isBox, std::uint32_t sides, std::uint32_t edges, std::uint32_t sideCount, std::int32_t contents,
                std::int16_t firstAxialOffset = 0)
    {
        const float low = isBox ? (std::numeric_limits<float>::max)() : 0.f;
        const float high = isBox ? -(std::numeric_limits<float>::max)() : 1.f;
        Float(low).Float(low).Float(low).Word(static_cast<std::uint32_t>(contents)).Float(high).Float(high).Float(high);
        Word(sideCount).Word(sides);
        for (int axis = 0; axis < 6; ++axis)
            Short(isBox ? 0xFFFF : 0);
        Word(edges).Short(static_cast<std::uint16_t>(firstAxialOffset));
        for (int slot = 1; slot < 6; ++slot)
            Short(0);
        g_file.insert(g_file.end(), 6 + 10, 0); // no axial edges, then the padding
        return *this;
    }
    File &Write(const Record &record, const Options &o = {})
    {
        g_file.insert(g_file.end(), record.bytes.begin(), record.bytes.end());
        Text("cm");
        placer.Place(1, 3);
        planes = placer.Place(4, 40);
        Plane(1, 0, 0, o.planeType, 0).Plane(0, -1, 0, 3, 2);
        staticModel = placer.Place(4, 80);
        Short(5).Short(0).Word(VirtualOffset(0));
        for (int value = 0; value < 18; ++value)
            Float(static_cast<float>(value) + 0.5f);
        Fill(4, 72, 0x22);
        side = placer.Place(4, 24); // two sides, three edges each
        Word(o.sidePlane ? o.sidePlane : VirtualOffset(planes + 20)).Word(0).Short(0).Short(3);
        Word(VirtualOffset(planes)).Word(0).Short(0).Short(3);
        edges = placer.Place(1, 6);
        Byte(1).Byte(2).Byte(o.lastEdge).Byte(0).Byte(1).Byte(2);
        return WriteNodes().WriteRest(o);
    }
    // Two nodes (the second's plane inline), the leaf, the leaf brushes, and
    // three leaf-brush nodes (two with an index inline each, then children).
    File &WriteNodes()
    {
        nodes = placer.Place(4, 16);
        Word(VirtualOffset(planes)).Short(1).Short(2).Word(kInline).Short(3).Short(4);
        nodePlane = placer.Place(4, 20);
        Float(9).Float(8).Float(7).Float(6).Float(5);
        Fill(4, 44, 0x44).Fill(2, 4, 0x55);
        leafNodes = placer.Place(4, 60);
        Word(1 | 1u << 16).Word(7).Word(kInline).Word(0).Word(0);
        Word(2 | 1u << 16).Word(9).Word(kInline).Word(0).Word(0);
        Word(0).Word(8).Float(1.5f).Float(2.5f).Short(3).Short(4);
        indices = placer.Place(2, 2);
        Short(1);
        placer.Place(2, 2);
        return Short(6);
    }
    // The flat arrays after the nodes, two partitions (the second naming the
    // first's border), two brushes (each naming a side and three edges), the
    // visibility, the map entities and the box brush.
    File &WriteRest(const Options &o)
    {
        Fill(4, 4, 0x66).Fill(4, 12, 0x77).Fill(2, 66, 0x88).Fill(1, 8, 0x99).Fill(4, 28, 0xAA);
        partitions = placer.Place(4, 24);
        border = placer.Place(4, 28);
        Word(3 | 1u << 8).Word(5).Word(kInline).Word(4 | 1u << 8).Word(6).Word(VirtualOffset(border));
        for (int value = 0; value < 7; ++value)
            Float(static_cast<float>(value));
        Fill(4, 32, 0xBB).Fill(4, 72, 0xCC);
        brush = placer.Place(16, 160);
        Brush(false, o.brushSides ? o.brushSides : VirtualOffset(side), o.brushEdges ? o.brushEdges : VirtualOffset(edges),
              o.brushSideCount, 1, o.firstAxialOffset);
        Brush(false, VirtualOffset(side + 12), VirtualOffset(edges + 3), 1, 1);
        Fill(1, 2, 0xDD);
        Word(kInline).Word(kInline).Word(2).Text("me").Text("e");
        placer.Place(1, 5);
        box = placer.Place(16, 80);
        return Brush(true, 0, 0, 0, o.boxContents);
    }
};

clipMap_t *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadClipMapPtrDisk32, slotValue);
}

bool Filled(const std::uint8_t *at, std::uint32_t count, std::uint8_t value)
{
    for (std::uint32_t index = 0; index < count; ++index)
        if (at[index] != value)
            return false;
    return true;
}

template <typename T>
const T *At(const Zone &zone, std::uint32_t offset)
{
    return reinterpret_cast<const T *>(zone.virt + offset);
}

bool LaterArraysPlaced(const Zone &zone, const clipMap_t &map);

// count bytes of value at array, inside block 4.
bool FilledIn(const Zone &zone, const void *array, std::uint8_t value, std::uint32_t count)
{
    const auto *bytes = static_cast<const std::uint8_t *>(array);
    return bytes >= zone.virt && Filled(bytes, count, value);
}

// The flat arrays, each in block 4 with its bytes; the planes, edges and
// walkable bits where they belong.
bool FlatArraysPlaced(const Zone &zone, const clipMap_t &map, const File &file)
{
    return map.planes == At<cplane_s>(zone, file.planes) && map.planes[1].signbits == 2
        && FilledIn(zone, map.materials, 0x22, 72) && map.brushEdges == At<std::uint8_t>(zone, file.edges)
        && FilledIn(zone, map.leafs, 0x44, 44) && FilledIn(zone, map.leafbrushes, 0x55, 4)
        && FilledIn(zone, map.leafsurfaces, 0x66, 4) && FilledIn(zone, map.verts, 0x77, 12)
        && FilledIn(zone, map.triIndices, 0x88, 66) && LaterArraysPlaced(zone, map);
}

bool LaterArraysPlaced(const Zone &zone, const clipMap_t &map)
{
    return FilledIn(zone, map.triEdgeIsWalkable, 0x99, 8)
        && map.triEdgeIsWalkable == reinterpret_cast<const std::uint8_t *>(map.triIndices) + 66 // 1-aligned
        && FilledIn(zone, map.borders, 0xAA, 28) && FilledIn(zone, map.aabbTrees, 0xBB, 32)
        && FilledIn(zone, map.cmodels, 0xCC, 72) && FilledIn(zone, map.visibility, 0xDD, 2);
}

// The box model and checksum, and the counts.
bool ScalarsConverted(const clipMap_t &map)
{
    std::uint32_t boxWord = 0;
    std::memcpy(&boxWord, reinterpret_cast<const std::uint8_t *>(&map.box_model) + 17 * 4, sizeof(boxWord));
    return boxWord == 0xB0 + 17 && map.checksum == 0xC5C5C5C5 && map.isInUse == 1 && map.planeCount == 2
        && map.numBrushEdges == 6 && map.triCount == 11 && map.numClusters == 2 && map.dynEntCount[0] == 2
        && map.dynEntCount[1] == 1;
}

bool NodesConverted(const Zone &zone, const clipMap_t &map, const File &file)
{
    const auto index = [&](const cLeafBrushNode_s &node, std::uint32_t offset, std::uint16_t value) {
        return node.leafBrushCount == 1 && node.data.leaf.brushes == At<std::uint16_t>(zone, offset)
            && node.data.leaf.brushes[0] == value;
    };
    const cLeafBrushNode_s &children = map.leafbrushNodes[2];
    return map.nodes[0].plane == map.planes && map.nodes[1].plane == At<cplane_s>(zone, file.nodePlane)
        && map.nodes[1].children[1] == 4 && index(map.leafbrushNodes[0], file.indices, 1)
        && index(map.leafbrushNodes[1], file.indices + 2, 6) && children.data.children.range == 2.5f
        && children.data.children.childOffset[1] == 4;
}

bool PartitionsAndModelConverted(const clipMap_t &map, const File &file, const Zone &zone)
{
    const cStaticModel_s &model = map.staticModelList[0];
    return map.partitions[0].triCount == 3 && map.partitions[0].firstTri == 5
        && map.partitions[0].borders == At<CollisionBorder>(zone, file.border)
        && map.partitions[1].borders == map.partitions[0].borders && model.xmodel == &g_model
        && model.writable.nextModelInWorldSector == 5 && model.absmax[2] == 17.5f;
}

bool BoxConverted(const clipMap_t &map, const File &file, const Zone &zone);

// Each brush names its side and edges; the box brush converts apart.
bool BrushesConverted(const clipMap_t &map, const File &file, const Zone &zone)
{
    const cbrush_t &brush = map.brushes[0];
    return InArena(map.brushes) && brush.numsides == 1 && brush.sides == map.brushsides
        && map.brushes[1].sides == map.brushsides + 1 && map.brushes[1].baseAdjacentSide == map.brushEdges + 3
        && map.brushsides[0].plane == map.planes + 1 && brush.baseAdjacentSide == map.brushEdges && brush.maxs[2] == 1.f
        && BoxConverted(map, file, zone);
}

bool BoxConverted(const clipMap_t &map, const File &file, const Zone &zone)
{
    return InArena(map.box_brush) && map.box_brush->contents == -1 && !map.box_brush->sides
        && At<std::uint8_t>(zone, file.box)[12] == 0xFF; // the box brush's record, contents -1, stays in block 4
}

void TestWholeMap()
{
    Zone zone;
    File file;
    file.Write(WholeRecord());
    const clipMap_t *const map = Load(kInline);
    Expect(map == &g_maps[0] && g_published == 1, "an inline clip map publishes one pool entry");
    if (map != &g_maps[0] || !InArena(map->leafbrushNodes))
        return;
    Expect(map->name == zone.At(4) && !std::strcmp(map->name, "cm"), "the name points at its bytes in block 4");
    Expect(ScalarsConverted(*map), "the counts, the box model and the checksum convert");
    Expect(FlatArraysPlaced(zone, *map, file), "each flat array stays at its aligned retail block-4 offset");
    Expect(NodesConverted(zone, *map, file), "nodes and leaf-brush nodes convert and name what they name");
    Expect(PartitionsAndModelConverted(*map, file, zone), "partitions and static models convert");
    Expect(BrushesConverted(*map, file, zone), "the brush names its side and edges; the box brush converts");
    Expect(map->mapEnts == &g_ents[0] && !std::strcmp(map->mapEnts->name, "me"), "the map entities load");
    Expect(map->dynEntPoseList[0] == reinterpret_cast<DynEntityPose *>(zone.runtime)
               && map->dynEntClientList[1] == reinterpret_cast<DynEntityClient *>(zone.runtime + 64)
               && map->dynEntCollList[0] == reinterpret_cast<DynEntityColl *>(zone.runtime + 76),
           "the dynamic entities' arrays take block 1");
    Expect(!std::memcmp(zone.temp, g_file.data(), kRecordBytes) && g_read == g_file.size()
               && DB_GetStreamPos() == zone.virt + file.box + 80,
           "the record streams into the temp block and every disk byte is consumed");
}

void TestOffsetsAndAliases()
{
    Zone zone;
    File file;
    file.placer.at = 8; // past the model alias and the clip map's own alias slot
    file.Write(WholeRecord());
    Record second; // its name, planes and box brush by offset, one material of its own
    second.Set(0x000, VirtualOffset(8)).Set(0x008, 2).Set(0x00C, VirtualOffset(file.planes)).Array(0x018, 1);
    second.Set(0x0A8, VirtualOffset(file.box));
    g_file.insert(g_file.end(), second.bytes.begin(), second.bytes.end());
    g_file.insert(g_file.end(), 72, 0x23);
    const clipMap_t *const first = Load(disk32::kSharedInline);
    const clipMap_t *const other = Load(kInline);
    Expect(first && other && other->planes == first->planes && other->name == first->name
               && other->box_brush == first->box_brush,
           "a later clip map names the planes, the name and the box brush by offset");
    Expect(first && Load(VirtualOffset(4)) == first, "an alias token resolves to the full native pointer");
    // Planes named 20 bytes before the end of what block 4 holds: the two
    // planes run past it.
    second.Set(0x00C, VirtualOffset(file.box + 80 + 72 - 20)); // past the second map's material
    g_file.insert(g_file.end(), second.bytes.begin(), second.bytes.end());
    const Drop drop = Catch([] { Load(kInline); });
    Expect(std::strstr(drop.message, "unmaterialized") && g_published == 2,
           "planes named past what block 4 holds fail closed", drop.message);
}

struct Malformed
{
    const char *what;
    std::uint32_t at; // a word of WholeRecord() set to value (isInUse, 1, when the stream breaks)
    std::uint32_t value;
    const char *error;
    Options options = {};
};

constexpr const char *kLayout = "brush layout";
constexpr const char *kGraph = "brush graph";

const Malformed kMalformed[] = {
    {"no planes", 0x008, 0, kLayout},
    {"65537 planes", 0x008, 65537, kLayout},
    {"no materials", 0x018, 0, kLayout},
    {"triangle indices past 32 bits", 0x060, 0x40000000, kLayout},
    {"visibility past 32 bits", 0x098, 0x40000000, kLayout},
    {"a negative border count", 0x06C, 0xFFFFFFFF, "array count"},
    {"dynamic-entity defs", 0x0FC, kInline, "no 64-bit loader yet"},
    {"an unmapped plane offset", 0x00C, VirtualOffset(900), "clipmap planes"},
    {"a null name", 0x000, 0, "has no name"},
    {"an unmapped box-brush alias", 0x0A8, VirtualOffset(40), "alias offset"},
    {"no box brush", 0x0A8, 0, kGraph},
    {"an inline side plane", 0x004, 1, "brush-side plane token", {kInline}},
    {"an unmapped side plane", 0x004, 1, "pointer offset", {VirtualOffset(900)}},
    {"an edge naming no side", 0x004, 1, "adjacency data", {0, 7}},
    {"inline brush sides", 0x004, 1, "inline fast-file clipmap brush sides", {0, 0, kInline}},
    {"brush sides outside the side array", 0x004, 1, "clipmap brush sides", {0, 0, VirtualOffset(8)}},
    {"251 brush sides", 0x004, 1, "side count", {0, 0, 0, 0, 251}},
    {"inline adjacency", 0x004, 1, "inline fast-file clipmap brush adjacency", {0, 0, 0, kInline}},
    {"a plane of the wrong type", 0x004, 1, kGraph, {0, 0, 0, 0, 1, 1}},
    {"a box brush that is solid", 0x004, 1, kGraph, {0, 0, 0, 0, 1, 0, 1}},
    {"an adjacency slot out of order", 0x004, 1, "adjacency layout", {0, 0, 0, 0, 1, 0, -1, 1}},
};

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone;
        Record record = WholeRecord();
        record.Set(test.at, test.value);
        File().Write(record, test.options);
        ExpectDrop(test.what, test.error, [] { Load(kInline); });
    }
    for (std::size_t keep : {std::size_t{100}, std::size_t{300}, std::size_t{560}})
    {
        Zone zone;
        File().Write(WholeRecord());
        g_file.resize(keep);
        ExpectDrop("a truncated clip map", "ended unexpectedly", [] { Load(kInline); });
    }
}
} // namespace

void __cdecl Load_ClipMapAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    clipMap_t &entry = g_maps[g_published++];
    entry = *header->clipMap;
    header->clipMap = &entry;
}

XAssetList *varXAssetList; // the envelope's native list (db_disk32_envelope.cpp)

// Static models name their model by alias here, so no model or material
// loads; XModel's TU and its families' link all the same.
void __cdecl Load_XModelAsset(XAssetHeader *)
{
    Expect(false, "no model loads");
}

void __cdecl Load_MaterialAsset(XAssetHeader *)
{
    Expect(false, "no material loads");
}

void __cdecl Load_MaterialTechniqueSetAsset(XAssetHeader *)
{
    Expect(false, "no technique set loads");
}

void __cdecl Load_PhysPresetAsset(XAssetHeader *)
{
    Expect(false, "no preset loads");
}

void __cdecl Load_GfxImageAsset(XAssetHeader *)
{
    Expect(false, "no image loads");
}

void __cdecl DB_LoadedExternalData(std::int32_t)
{
    Expect(false, "no image loads");
}

void __cdecl Load_GetCurrentZoneHandle(uint8_t *handle)
{
    *handle = 7;
}

db::load_legacy_bridge::LegacyBridgeStatus db::load_legacy_bridge::DbLoadLegacyBridge::TryAddUser4(std::uint32_t) noexcept
{
    Expect(false, "loading marks no script string");
    return LegacyBridgeStatus::Success;
}

bool db::load_legacy_bridge::DbLoadLegacyBridge::InSession() noexcept
{
    return false;
}

void __cdecl Load_MapEntsAsset(XAssetHeader *header)
{
    g_ents[g_entCount] = *header->mapEnts;
    header->mapEnts = &g_ents[g_entCount++];
}

int main()
{
    return Run({TestWholeMap, TestOffsetsAndAliases, TestMalformedFailsClosed});
}
