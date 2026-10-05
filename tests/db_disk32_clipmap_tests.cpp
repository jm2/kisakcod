// db_disk32_clipmap_tests.cpp: the 64-bit clip-map loader (NOW row 12) on
// hand-built disk32 zone images (disk32_fixture.hpp), with MapEnts' real step
// for the map entities and XModel's for static models. The retail offsets
// here are written out by hand, apart from the schema. Beyond the fixture's
// seams, only the asset pools are replaced.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>
#include <database/db_load_legacy_bridge.h>

#include <cstring>
#include <utility>
#include <vector>

namespace
{
using namespace disk32_test;

clipMap_t g_maps[2]; // what Load_ClipMapAsset published
MapEnts g_ents[2];   // what Load_MapEntsAsset published
int g_entCount = 0;
XModel g_model;      // the model alias RecordWithPointers' static model names

constexpr std::uint32_t kRecordBytes = 284;

struct Zone : disk32_test::Zone<1024>
{
    Zone() { g_entCount = 0; }
};

// The record: a pattern in its box model and checksum, and the counts and
// tokens of TestArrays' arrays.
struct Record
{
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(kRecordBytes);

    Record(bool full = true)
    {
        if (!full)
            return;
        for (std::uint32_t word = 0; word < 18; ++word)
            Set(0x0AC + word * 4, 0xB0 + word);
        Set(0x000, kInline).Set(0x004, 1).Set(0x118, 0xC5C5C5C5);
        Array(0x008, 2).Array(0x018, 1).Array(0x028, 3).Array(0x038, 1).Array(0x048, 2).Array(0x050, 1);
        Array(0x058, 1).Array(0x060, 11).Set(0x068, kInline).Array(0x06C, 1).Array(0x07C, 1).Array(0x084, 1);
        Set(0x094, 2).Set(0x098, 1).Set(0x09C, kInline).Set(0x0A4, kInline).Set(0x0F4, 2 | 1u << 16);
        Set(0x100, kInline).Set(0x10C, kInline).Set(0x110, kInline);
    }
    Record &Set(std::uint32_t at, std::uint32_t value)
    {
        for (std::uint32_t byte = 0; byte < 4; ++byte)
            bytes[at + byte] = static_cast<std::uint8_t>(value >> (8 * byte));
        return *this;
    }
    // A count and the inline token after it.
    Record &Array(std::uint32_t at, std::uint32_t count)
    {
        return Set(at, count).Set(at + 4, kInline);
    }
};

struct File : FileBuilder<File>
{
    File &Bytes(std::uint32_t count, std::uint8_t value)
    {
        g_file.insert(g_file.end(), count, value);
        return *this;
    }
    // The record, then its name and the arrays in Load_clipMap_t's order, each
    // filled with its own byte value, then the map entities.
    File &Write(const Record &record, bool full = true)
    {
        g_file.insert(g_file.end(), record.bytes.begin(), record.bytes.end());
        if (!full)
            return Bytes(72, 0x23);
        Text("cm").Bytes(40, 0x11).Bytes(72, 0x22).Bytes(3, 0x33).Bytes(44, 0x44).Bytes(4, 0x55).Bytes(4, 0x66);
        Bytes(12, 0x77).Bytes(66, 0x88).Bytes(8, 0x99).Bytes(28, 0xAA).Bytes(32, 0xBB).Bytes(72, 0xCC).Bytes(2, 0xDD);
        return Word(kInline).Word(kInline).Word(2).Text("me").Text("e");
    }
};

clipMap_t *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadClipMapPtrDisk32, slotValue);
}

// count bytes at the block offset all hold value.
bool Filled(const std::uint8_t *at, std::uint32_t count, std::uint8_t value)
{
    for (std::uint32_t index = 0; index < count; ++index)
        if (at[index] != value)
            return false;
    return true;
}

// An array of count bytes holding value, at its 4-, 2- or 1-aligned retail
// block-4 offset.
template <typename T>
bool At(const Zone &zone, const T *array, std::uint32_t offset, std::uint32_t count, std::uint8_t value)
{
    const auto *bytes = reinterpret_cast<const std::uint8_t *>(array);
    return bytes == zone.virt + offset && Filled(bytes, count, value);
}

bool FirstArraysPlaced(const Zone &zone, const clipMap_t &map)
{
    return At(zone, map.planes, 4, 40, 0x11) && At(zone, map.materials, 44, 72, 0x22)
        && At(zone, map.brushEdges, 116, 3, 0x33) && At(zone, map.leafs, 120, 44, 0x44)
        && At(zone, map.leafbrushes, 164, 4, 0x55) && At(zone, map.leafsurfaces, 168, 4, 0x66)
        && At(zone, map.verts, 172, 12, 0x77);
}

bool LastArraysPlaced(const Zone &zone, const clipMap_t &map)
{
    return At(zone, map.triIndices, 184, 66, 0x88) && At(zone, map.triEdgeIsWalkable, 250, 8, 0x99)
        && At(zone, map.borders, 260, 28, 0xAA) && At(zone, map.aabbTrees, 288, 32, 0xBB)
        && At(zone, map.cmodels, 320, 72, 0xCC) && At(zone, map.visibility, 392, 2, 0xDD);
}

// The box model and checksum, and the counts.
bool ScalarsConverted(const clipMap_t &map)
{
    std::uint32_t boxWord = 0;
    std::memcpy(&boxWord, reinterpret_cast<const std::uint8_t *>(&map.box_model) + 17 * 4, sizeof(boxWord));
    return boxWord == 0xB0 + 17 && map.checksum == 0xC5C5C5C5 && map.isInUse == 1 && map.planeCount == 2
        && map.numBrushEdges == 3 && map.triCount == 11 && map.numClusters == 2 && map.dynEntCount[0] == 2
        && map.dynEntCount[1] == 1;
}

// Poses, clients and links in block 1: two poses, one client in the second
// list, two links.
bool DynEntitiesPlaced(const Zone &zone, const clipMap_t &map)
{
    const auto at = [&](const void *pointer, std::uint32_t offset) {
        return reinterpret_cast<const std::uint8_t *>(pointer) == zone.runtime + offset;
    };
    return at(map.dynEntPoseList[0], 0) && !map.dynEntPoseList[1] && !map.dynEntClientList[0]
        && at(map.dynEntClientList[1], 64) && at(map.dynEntCollList[0], 76) && Filled(zone.runtime, 116, 0);
}

void TestArrays()
{
    Zone zone;
    File().Write(Record());
    const clipMap_t *const map = Load(kInline);
    Expect(map == &g_maps[0] && g_published == 1, "an inline clip map publishes one pool entry");
    if (map != &g_maps[0])
        return;
    Expect(map->name == zone.At(0) && !std::strcmp(map->name, "cm"), "the name points at its bytes in block 4");
    Expect(ScalarsConverted(*map), "the counts, the box model and the checksum convert");
    Expect(FirstArraysPlaced(zone, *map) && LastArraysPlaced(zone, *map),
           "each array stays at its aligned retail block-4 offset");
    Expect(map->mapEnts == &g_ents[0] && !std::strcmp(map->mapEnts->name, "me"), "the map entities load through MapEnts' step");
    Expect(DynEntitiesPlaced(zone, *map), "the dynamic entities' arrays take zero-filled block 1");
    Expect(!map->staticModelList && !map->brushes && !map->box_brush && !map->dynEntDefList[0], "the rest is null");
    Expect(!std::memcmp(zone.temp, g_file.data(), kRecordBytes) && g_read == g_file.size(),
           "the record streams into the temp block and every disk byte is consumed");
}

// Two planes, a static model naming the model alias, a material, a brush
// side naming the second plane, three brush edges, two nodes (the first
// naming the first plane, the second its own plane inline), three leaf-brush
// nodes (the first two with a brush index inline each, the third children),
// and two partitions (the first with a border inline, the second naming it).
Record RecordWithPointers()
{
    Record record(false);
    record.Set(0x000, kInline).Array(0x008, 2).Array(0x010, 1).Array(0x018, 1).Array(0x020, 1).Array(0x028, 3);
    return std::move(record.Array(0x030, 2).Array(0x040, 3).Array(0x074, 2));
}

struct Bytes : FileBuilder<Bytes>
{
    Bytes &Short(std::uint16_t value)
    {
        g_file.push_back(static_cast<std::uint8_t>(value));
        g_file.push_back(static_cast<std::uint8_t>(value >> 8));
        return *this;
    }
};

// Block 4: the model alias (0..4), the name, the planes at 8, the static
// model at 48, the material at 128, the side at 200, the edges at 212, the
// nodes 4-aligned at 216 and the second's plane at 232, the leaf-brush nodes
// at 252 and their indices 2-aligned at 312 and 314, the partitions
// 4-aligned at 316 and the first's border at 340.
void WritePointerArrays(std::uint32_t sidePlane = VirtualOffset(28), std::uint32_t border = VirtualOffset(340))
{
    File().Write(RecordWithPointers(), false);
    g_file.resize(g_file.size() - 72); // Write(false) adds a material; it comes later here
    Bytes file;
    file.Text("cm");
    for (int value = 0; value < 10; ++value)
        file.Float(static_cast<float>(value));
    file.Short(5).Short(0).Word(VirtualOffset(0));
    for (int value = 0; value < 18; ++value)
        file.Float(static_cast<float>(value) + 0.5f);
    g_file.insert(g_file.end(), 72, 0x22);
    file.Word(sidePlane).Word(1).Short(4).Short(3).Short(0x3333).Text("");
    file.Word(VirtualOffset(8)).Short(1).Short(2).Word(kInline).Short(3).Short(4).Float(9).Float(8).Float(7).Float(6).Float(5);
    file.Word(1 | 1u << 16).Word(7).Word(kInline).Word(0).Word(0);
    file.Word(2 | 1u << 16).Word(9).Word(kInline).Word(0).Word(0);
    file.Word(0 | 0u << 16).Word(8).Float(1.5f).Float(2.5f).Short(3).Short(4);
    file.Short(1).Short(6);
    file.Word(3 | 1u << 8).Word(5).Word(kInline).Word(4 | 1u << 8).Word(6).Word(border);
    for (int value = 0; value < 7; ++value)
        file.Float(static_cast<float>(value));
}

void Aliases()
{
    DB_SetInsertedPointer(DB_InsertPointer(DBAliasKind::XModel), DBAliasKind::XModel, &g_model);
}

// The first node's disk record, its plane token first, sits at offset.
bool FirstNodeAt(const Zone &zone, std::uint32_t offset)
{
    const std::uint32_t token = VirtualOffset(8);
    return !std::memcmp(zone.virt + offset, &token, sizeof(token));
}

bool PlanesNamed(const Zone &zone, const clipMap_t &map)
{
    return map.brushsides[0].plane == reinterpret_cast<const cplane_s *>(zone.virt + 28)
        && map.brushsides[0].materialNum == 1 && map.brushsides[0].firstAdjacentSideOffset == 4
        && map.brushsides[0].edgeCount == 3 && map.nodes[0].plane == reinterpret_cast<const cplane_s *>(zone.virt + 8)
        && map.nodes[1].plane == reinterpret_cast<const cplane_s *>(zone.virt + 232) && map.nodes[1].children[1] == 4
        && map.brushEdges == zone.virt + 212 && FirstNodeAt(zone, 216);
}

// A leaf-brush node's single brush index, at its 2-aligned block-4 offset.
bool BrushIndexAt(const Zone &zone, const cLeafBrushNode_s &node, std::uint32_t offset, std::uint16_t value)
{
    return node.leafBrushCount == 1 && node.data.leaf.brushes == reinterpret_cast<const std::uint16_t *>(zone.virt + offset)
        && node.data.leaf.brushes[0] == value;
}

bool LeafBrushNodesConverted(const Zone &zone, const clipMap_t &map)
{
    const cLeafBrushNode_s &children = map.leafbrushNodes[2];
    return map.leafbrushNodes[0].axis == 1 && map.leafbrushNodes[0].contents == 7
        && BrushIndexAt(zone, map.leafbrushNodes[0], 312, 1) && BrushIndexAt(zone, map.leafbrushNodes[1], 314, 6)
        && children.contents == 8 && children.data.children.dist == 1.5f
        && children.data.children.range == 2.5f && children.data.children.childOffset[1] == 4;
}

bool PartitionsConverted(const Zone &zone, const clipMap_t &map)
{
    const auto *const border = reinterpret_cast<const CollisionBorder *>(zone.virt + 340);
    return map.partitions[0].triCount == 3 && map.partitions[0].borderCount == 1 && map.partitions[0].firstTri == 5
        && map.partitions[0].borders == border && map.partitions[1].borders == border && border->length == 6.f;
}

bool AllNative(const clipMap_t &map)
{
    return InArena(map.staticModelList) && InArena(map.brushsides) && InArena(map.nodes)
        && InArena(map.leafbrushNodes) && InArena(map.partitions);
}

void TestPointerArrays()
{
    Zone zone;
    Aliases();
    WritePointerArrays();
    const clipMap_t *const map = Load(kInline);
    Expect(map == &g_maps[0] && AllNative(*map), "the arrays with pointers convert into native storage");
    if (!map || !InArena(map->staticModelList))
        return;
    const cStaticModel_s &model = map->staticModelList[0];
    Expect(model.writable.nextModelInWorldSector == 5 && model.xmodel == &g_model && model.origin[0] == 0.5f
               && model.absmax[2] == 17.5f,
           "a static model converts and names its model");
    Expect(PlanesNamed(zone, *map), "brush sides and nodes name planes, by offset or inline");
    Expect(LeafBrushNodesConverted(zone, *map), "leaf-brush nodes name brush indices or keep their children");
    Expect(PartitionsConverted(zone, *map), "partitions name their borders, inline or by offset");
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 368, "every disk byte is consumed");
}

void TestPointerArraysFailClosed()
{
    const std::pair<std::uint32_t, const char *> sides[] = {
        {kInline, "brush-side plane token"}, {0, "brush-side plane token"}, {VirtualOffset(900), "pointer offset"}};
    for (const auto &[plane, error] : sides)
    {
        Zone zone;
        Aliases();
        WritePointerArrays(plane);
        ExpectDrop("a malformed brush-side plane", error, [] { Load(kInline); });
    }
    Zone zone;
    Aliases();
    WritePointerArrays(VirtualOffset(28), VirtualOffset(900));
    ExpectDrop("an unmapped border offset", "pointer offset", [] { Load(kInline); });
}

void TestPlaneOffsetAndAlias()
{
    Zone zone;
    File().Write(Record());
    Record second(false); // its name and planes by offset, one material of its own
    second.Set(0x000, VirtualOffset(4)).Set(0x008, 2).Set(0x00C, VirtualOffset(8)).Array(0x018, 1); // past the alias slot
    File().Write(second, false);
    const clipMap_t *const first = Load(disk32::kSharedInline);
    const clipMap_t *const other = Load(kInline);
    Expect(first && other && other->planes == first->planes && other->name == first->name,
           "a later clip map names the planes and the name by offset");
    Expect(first && Load(VirtualOffset(0)) == first, "an alias token resolves to the full native pointer");
    // Planes named 20 bytes before the end of what block 4 holds: the two
    // planes run past it.
    second.Set(0x00C, VirtualOffset(380));
    File().Write(second, false);
    const Drop drop = Catch([] { Load(kInline); });
    Expect(std::strstr(drop.message, "unmaterialized") && g_published == 2,
           "planes named past what block 4 holds fail closed", drop.message);
}

struct Malformed
{
    const char *what;
    std::uint32_t at;
    std::uint32_t value;
    const char *error;
};

constexpr const char *kLayout = "brush layout";
constexpr const char *kNotYet = "no 64-bit loader yet";

const Malformed kMalformed[] = {
    {"no planes", 0x008, 0, kLayout},
    {"65537 planes", 0x008, 65537, kLayout},
    {"no materials", 0x018, 0, kLayout},
    {"brush sides without a token", 0x020, 1, kLayout},
    {"triangle indices past 32 bits", 0x060, 0x40000000, kLayout},
    {"visibility past 32 bits", 0x098, 0x40000000, kLayout},
    {"a negative border count", 0x06C, 0xFFFFFFFF, "array count"},
    {"brushes", 0x090, kInline, kNotYet},
    {"a box brush", 0x0A8, kInline, kNotYet},
    {"dynamic-entity defs", 0x0FC, kInline, kNotYet},
    {"an unmapped plane offset", 0x00C, VirtualOffset(900), "clipmap planes"},
    {"a null name", 0x000, 0, "has no name"},
};

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone;
        File().Write(Record().Set(test.at, test.value));
        ExpectDrop(test.what, test.error, [] { Load(kInline); });
    }
    for (std::size_t keep : {std::size_t{100}, std::size_t{300}, std::size_t{560}})
    {
        Zone zone;
        File().Write(Record());
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
    return Run({TestArrays, TestPointerArrays, TestPlaneOffsetAndAlias, TestPointerArraysFailClosed,
                TestMalformedFailsClosed});
}
