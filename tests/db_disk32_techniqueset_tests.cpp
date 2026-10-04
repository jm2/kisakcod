// db_disk32_techniqueset_tests.cpp: the 64-bit TechniqueSet loader (NOW row 12)
// on hand-built disk32 zone images (disk32_fixture.hpp). Beyond the fixture's
// seams, only the asset pool (Load_MaterialTechniqueSetAsset) is replaced.
// Vertex declarations are not converted yet, so every technique fails closed at
// its first pass's declaration, after its header and passes load.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>

#include <algorithm>
#include <cstring>
#include <iterator>

namespace
{
using namespace disk32_test;

MaterialTechniqueSet g_pool[4]; // what Load_MaterialTechniqueSetAsset published

struct File : FileBuilder<File>
{
    // The 148-byte retail set: technique slot `at` holds `token`, every other
    // is null. The flag bytes and the remapped pointer hold junk.
    File &Set(std::uint32_t name, std::uint32_t format, std::uint32_t at = 34, std::uint32_t token = 0)
    {
        Word(name).Word(0x00CD0100u | format).Word(0xDEADBEEF);
        for (std::uint32_t slot = 0; slot < 34; ++slot)
            Word(slot == at ? token : 0);
        return *this;
    }
    File &Technique(std::uint32_t name, std::uint32_t flags, std::uint32_t passes)
    {
        return Word(name).Word(flags | passes << 16);
    }
    // A 20-byte pass with one stable argument.
    File &Pass(std::uint32_t decl, std::uint32_t shaders = kInline, std::uint32_t stable = 1, std::uint32_t args = kInline)
    {
        return Word(decl).Word(shaders).Word(shaders).Word(stable << 16 | 0x01000000u).Word(args);
    }
};

using Zone = disk32_test::Zone<512>;

MaterialTechniqueSet *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadMaterialTechniqueSetPtrDisk32, slotValue);
}

bool Is(const Zone &zone, const char *text, const char *expected)
{
    return zone.Holds(text) && !std::strcmp(text, expected);
}

void TestEmptySets()
{
    Zone zone;
    File().Set(kInline, 7).Text("ts_empty");     // after the alias slot: 4..13
    File().Set(VirtualOffset(4), 11);
    const MaterialTechniqueSet *const shared = Load(disk32::kSharedInline);
    const MaterialTechniqueSet *const named = Load(kInline);
    Expect(shared == &g_pool[0] && named == &g_pool[1], "sets without techniques publish");
    Expect(Is(zone, shared->name, "ts_empty") && shared->worldVertFormat == 7 && named->worldVertFormat == 11
               && !shared->hasBeenUploaded && !shared->remappedTechniqueSet && shared->unused[0] == 0xCD,
           "the scalars convert; the upload flag and the remapped set start clear");
    Expect(!*std::max_element(std::begin(shared->techniques), std::end(shared->techniques))
               && named->name == shared->name,
           "null technique tokens stay null, and a name offset resolves");
    Expect(reinterpret_cast<std::uintptr_t>(shared) > UINT32_MAX && Load(VirtualOffset(0)) == shared,
           "a set alias token resolves to the full native pointer");
    Expect(g_read == g_file.size() && !g_arenaUsed, "every disk byte is consumed and no native storage is used");
}

void TestTechniqueUpToDeclaration()
{
    Zone zone;
    // Block 4: name (0..3), the technique (4..12) and its two passes (12..52).
    File().Set(kInline, 2, 5, kInline).Text("ts").Technique(kInline, 0x8021, 2).Pass(kInline).Pass(kInline, kInline, 3);
    ExpectDrop("a technique with a declaration", "declarations are not converted", [] { Load(kInline); });
    const auto *const technique = reinterpret_cast<const MaterialTechnique *>(g_arena);
    Expect(g_arenaUsed == offsetof(MaterialTechnique, passArray) + 2 * sizeof(MaterialPass)
               && technique->flags == 0x21 && technique->passCount == 2,
           "the technique converts into native storage, its flags losing bit 15");
    const MaterialPass &pass = technique->passArray[0];
    Expect(pass.stableArgCount == 1 && pass.customSamplerFlags == 1 && !pass.perPrimArgCount && !pass.perObjArgCount,
           "the first pass's scalars convert");
    Expect(!std::memcmp(zone.virt + 4, g_file.data() + 148 + 3, 48) && g_read == g_file.size(),
           "the retail technique and passes stay at their 4-aligned block-4 offset");
}

struct Malformed
{
    const char *what;
    void (*build)();
    const char *error;
    std::size_t arena = kArenaBytes;
};

// A set whose technique 0 is inline: its header, then one pass.
File Prefix(std::uint32_t flags = 0, std::uint32_t passes = 1, std::uint32_t name = kInline)
{
    File().Set(kInline, 0, 0, kInline).Text("s").Technique(name, flags, passes);
    return File();
}

const Malformed kMalformed[] = {
    {"truncated set", [] { File().Word(kInline).Word(0); }, "ended unexpectedly"},
    {"null set name", [] { File().Set(0, 0); }, "technique set header"},
    {"vertex format 12", [] { File().Set(kInline, 12).Text("s"); }, "technique set header"},
    {"empty set name", [] { File().Set(kInline, 0).Text(""); }, "set has no name"},
    {"technique named by offset", [] { File().Set(kInline, 0, 3, VirtualOffset(8)).Text("s"); }, "by offset"},
    {"technique without a name", [] { Prefix(0, 1, 0).Pass(kInline); }, "technique header"},
    {"technique with no passes", [] { Prefix(0, 0); }, "technique header"},
    {"technique with five passes", [] { Prefix(0, 5); }, "technique header"},
    {"technique flag bit 6", [] { Prefix(0x40, 1).Pass(kInline); }, "technique header"},
    {"truncated passes", [] { Prefix(0, 2).Pass(kInline); }, "ended unexpectedly"},
    {"pass without a declaration", [] { Prefix().Pass(0); }, "pass header"},
    {"pass without shaders", [] { Prefix().Pass(kInline, 0); }, "pass header"},
    {"pass without arguments", [] { Prefix().Pass(kInline, kInline, 0, 0); }, "pass header"},
    {"pass with a count but no arguments", [] { Prefix().Pass(kInline, kInline, 1, 0); }, "pass header"},
    {"pass with 65 arguments", [] { Prefix().Pass(kInline, kInline, 65); }, "pass header"},
    {"native storage exhausted", [] { Prefix().Pass(kInline); }, "exhausted",
     offsetof(MaterialTechnique, passArray) + sizeof(MaterialPass) - 8},
    {"unmapped set alias", [] {}, "alias offset"},
};

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone;
        g_arenaCapacity = test.arena;
        test.build();
        const std::uintptr_t slot = g_file.empty() ? VirtualOffset(16) : kInline;
        ExpectDrop(test.what, test.error, [&] { Load(slot); });
    }
    Zone zone;
    ExpectDrop("slot wider than a token", "no disk32 token", [] { Load(std::uintptr_t{1} << 32); });
}
} // namespace

void __cdecl Load_MaterialTechniqueSetAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    MaterialTechniqueSet &entry = g_pool[g_published++];
    entry = *header->techniqueSet;
    Expect(entry.name && entry.name[0] != '\0', "a published set has a name");
    header->techniqueSet = &entry;
}

int main()
{
    return Run({TestEmptySets, TestTechniqueUpToDeclaration, TestMalformedFailsClosed});
}
