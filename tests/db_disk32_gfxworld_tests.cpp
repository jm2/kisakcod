// db_disk32_gfxworld_tests.cpp: the 64-bit world loader (NOW row 12) on
// hand-built disk32 zone images (disk32_fixture.hpp), with Image's real step
// for the images it names. The retail offsets here are written out by hand,
// apart from the schema. Beyond the fixture's seams, only the asset pools
// are replaced.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>

#include <cstring>
#include <vector>

namespace
{
using namespace disk32_test;

GfxImage g_image; // the image alias at block-4 offset 0 that the sky names

constexpr std::uint32_t kRecordBytes = 732;

// Every zone starts block 4 with an image alias, as an earlier asset leaves it.
struct Zone : disk32_test::Zone<2048, 1024>
{
    Zone()
    {
        DB_SetInsertedPointer(DB_InsertPointer(DBAliasKind::GfxImage), DBAliasKind::GfxImage, &g_image);
    }
};

// The record: one cell, one reflection probe, one brush model, two
// surfaces (one static, one without decals), two primary lights past none,
// then the names, three indices, two sky surfaces and the sky image alias.
struct Record
{
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(kRecordBytes);

    Record()
    {
        Set(0x000, kInline).Set(0x004, kInline).Set(0x010, 3).Set(0x014, kInline).Set(0x018, 2).Set(0x020, 2);
        Set(0x024, kInline).Set(0x028, VirtualOffset(0)).Set(0x0D8, 0).Set(0x0DC, 2).Set(0x0E4, 1);
        Set(0x0E8, kInline).Set(0x0EC, kInline).Set(0x0F0, 1).Set(0x100, 16).Set(0x104, kInline).Set(0x150, 1);
        Set(0x154, kInline).Set(0x220, kInline).Set(0x248, 1).Set(0x24C, 1).Set(0x28C, kInline);
    }
    Record &Set(std::uint32_t at, std::uint32_t value)
    {
        for (std::uint32_t byte = 0; byte < 4; ++byte)
            bytes[at + byte] = static_cast<std::uint8_t>(value >> (8 * byte));
        return *this;
    }
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
    // (4-aligned): block 4 holds them at 4, 6, 10 and 16.
    File &Write(const Record &record)
    {
        g_file.insert(g_file.end(), record.bytes.begin(), record.bytes.end());
        return Text("w").Text("ba").Short(1).Short(2).Short(3).Word(7).Word(8);
    }
};

GfxWorld *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadGfxWorldPtrDisk32, slotValue);
}

// The probes, which do not load yet, end the load: what streamed before them
// is the record, the names, the indices and the sky surfaces, in place.
void TestPrefix()
{
    Zone zone;
    File().Write(Record());
    const Drop drop = Catch([] { Load(kInline); });
    std::uint16_t indices[3] = {};
    std::memcpy(indices, zone.virt + 10, sizeof(indices));
    std::int32_t sky[2] = {};
    std::memcpy(sky, zone.virt + 16, sizeof(sky));
    Expect(std::strstr(drop.message, "no 64-bit loader yet") && g_published == 0, "the load stops at the probes",
           drop.message);
    Expect(!std::strcmp(zone.At(4), "w") && !std::strcmp(zone.At(6), "ba") && indices[2] == 3 && sky[1] == 8,
           "the names, indices and sky surfaces stream into block 4 at their aligned retail offsets");
    Expect(DB_GetStreamPos() == zone.virt + 24 && g_read == g_file.size()
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
    {"a sun light", 0x0C8, kInline, "no 64-bit loader yet"},
};

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

void __cdecl DB_LoadedExternalData(std::int32_t)
{
    Expect(false, "no image loads");
}

int main()
{
    return Run({TestPrefix, TestMalformedFailsClosed});
}
