// db_disk32_zone_tests.cpp: one synthetic zone holding the server-closure
// families the 64-bit loader converts (NOW row 12, gate G2), loaded through the
// real path by disk32_zone_harness.cpp. Each writer streams one asset as its
// family's loader reads it. An inserted family's record streams into the temp
// block and its strings into block 4; a -2 header first takes a 4-byte alias
// slot in block 4, which later offset tokens name. A completed family's record
// streams into block 4 itself.

#include "disk32_zone_harness.hpp"

#include <cstring>
#include <iterator>

namespace
{
using namespace zone_test;

// Block-4 offsets the writers record for later tokens.
struct Offsets
{
    std::uint32_t rawFile, physPreset;
} g_at{};

void WriteRawFile(Image &z)
{
    z.Word(kInline).Word(2).Word(kInline).V("e2e/a.gsc").V("hi");
}
void WriteSharedRawFile(Image &z)
{
    g_at.rawFile = z.VAlloc(4);
    z.Word(kInline).Word(0).Word(0).V("e2e/b.cfg");
}
void WriteNothing(Image &) {} // an offset header token streams nothing

void WriteStringTable(Image &z) // 2 x 1: the record and value tokens in block 4
{
    z.VAlloc(16);
    z.Word(kInline).Word(2).Word(1).Word(kInline).V("e2e/table");
    z.VAlloc(8);
    z.Word(kInline).Word(kInline).V("a").V("b");
}
void WritePhysPreset(Image &z)
{
    g_at.physPreset = z.VAlloc(4);
    z.Word(kInline).Word(3).Float(2.5f).Float(0.5f).Float(0.25f).Float(1).Float(1).Word(kInline);
    z.Float(0.125f).Float(4).Word(1).V("e2e/phys").V("wood");
}
const Asset kZone[] = {
    {ASSET_TYPE_RAWFILE, kInline, "e2e/a.gsc", WriteRawFile},
    {ASSET_TYPE_RAWFILE, kShared, "e2e/b.cfg", WriteSharedRawFile},
    {ASSET_TYPE_RAWFILE, 0, "e2e/b.cfg", WriteNothing, &g_at.rawFile},
    {ASSET_TYPE_STRINGTABLE, kInline, "e2e/table", WriteStringTable},
    {ASSET_TYPE_PHYSPRESET, kShared, "e2e/phys", WritePhysPreset},
};
const char *const kScriptStrings[] = {"e2e_tag"};
} // namespace

std::span<const Asset> zone_test::ZoneAssets()
{
    return kZone;
}

std::span<const char *const> zone_test::ZoneScriptStrings()
{
    return kScriptStrings;
}

// The fields each family converted, and every reference: each names the
// published asset the pool lookup finds.
void zone_test::CheckZone()
{
    const RawFile *raw = Find(ASSET_TYPE_RAWFILE, "e2e/a.gsc").rawfile;
    Expect(raw && raw->len == 2 && !std::strcmp(raw->buffer, "hi"), "the raw file's bytes load");
    const StringTable *table = Find(ASSET_TYPE_STRINGTABLE, "e2e/table").stringTable;
    Expect(table && table->columnCount == 2 && !std::strcmp(table->values[1], "b"), "the table's values load");
    const PhysPreset *phys = Find(ASSET_TYPE_PHYSPRESET, "e2e/phys").physPreset;
    Expect(phys && phys->mass == 2.5f && phys->tempDefaultToCylinder && !std::strcmp(phys->sndAliasPrefix, "wood"),
           "the preset's fields load");
}
