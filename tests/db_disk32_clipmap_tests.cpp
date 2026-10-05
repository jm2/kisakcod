// db_disk32_clipmap_tests.cpp: the 64-bit clip-map loader (NOW row 12) on
// hand-built disk32 zone images (disk32_fixture.hpp), with MapEnts' real step
// for the map entities. The retail offsets here are written out by hand,
// apart from the schema. Beyond the fixture's seams, only the asset pools are
// replaced.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>

#include <cstring>
#include <vector>

namespace
{
using namespace disk32_test;

clipMap_t g_maps[2]; // what Load_ClipMapAsset published
MapEnts g_ents[2];   // what Load_MapEntsAsset published
int g_entCount = 0;

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
    {"static models", 0x014, kInline, kNotYet},
    {"nodes", 0x034, kInline, kNotYet},
    {"leaf-brush nodes", 0x044, kInline, kNotYet},
    {"partitions", 0x078, kInline, kNotYet},
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

void __cdecl Load_MapEntsAsset(XAssetHeader *header)
{
    g_ents[g_entCount] = *header->mapEnts;
    header->mapEnts = &g_ents[g_entCount++];
}

int main()
{
    return Run({TestArrays, TestPlaneOffsetAndAlias, TestMalformedFailsClosed});
}
