// db_disk32_material_tests.cpp: the 64-bit Material loader (NOW row 12) on
// hand-built disk32 zone images (disk32_fixture.hpp), with TechniqueSet's real
// steps for the technique sets. Beyond the fixture's seams, only the two asset
// pools are replaced. Texture tables are not converted yet.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>

#include <cstring>
#include <limits>

namespace
{
using namespace disk32_test;

Material g_pool[4];
MaterialTechniqueSet g_sets[2];
int g_materials = 0;
int g_setCount = 0;

constexpr std::uint32_t kDrawSurfLow = 0x89ABCDEF;
constexpr std::uint32_t kDrawSurfHigh = 0x01234567;

struct File : FileBuilder<File>
{
    // The 80-byte retail record; every technique slot's state entry is 255.
    File &Material(std::uint32_t name, std::uint32_t set, std::uint32_t constants, std::uint32_t constantCount,
                   std::uint32_t states, std::uint32_t stateCount, std::uint32_t sortKey = 7,
                   std::uint32_t textures = 0, std::uint32_t textureCount = 0)
    {
        Word(name).Word(0x0403'0000u | sortKey << 8 | 0x21).Word(kDrawSurfLow).Word(kDrawSurfHigh).Word(0x8000'0001u);
        Word(0xCDCD'0000u | 0x1234);
        for (int i = 0; i < 34; ++i)
            g_file.push_back(0xFF);
        Word(textureCount | constantCount << 8 | stateCount << 16 | 0x05u << 24);
        g_file.push_back(0x06); // cameraRegion, then a padding byte
        g_file.push_back(0xCD);
        return Word(set).Word(textures).Word(constants).Word(states);
    }
    File &Set(std::uint32_t name) // an empty 148-byte technique set
    {
        Word(name).Word(0).Word(0);
        for (int i = 0; i < 34; ++i)
            Word(0);
        return *this;
    }
    File &Constant(std::uint32_t hash, float value)
    {
        Word(hash).Word(0x6162'6300).Word(0).Word(0);
        return Float(value).Float(value + 1).Float(value + 2).Float(value + 3);
    }
};

using Zone = disk32_test::Zone<2048>;

::Material *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadMaterialPtrDisk32, slotValue);
}

bool Is(const Zone &zone, const char *text, const char *expected)
{
    return zone.Holds(text) && !std::strcmp(text, expected);
}

void Reset()
{
    g_materials = g_setCount = 0;
}

void ExpectScalars(const ::Material &material)
{
    std::uint64_t drawSurf = 0;
    std::memcpy(&drawSurf, &material.info.drawSurf, sizeof(drawSurf));
    Expect(material.info.gameFlags == 0x21 && material.info.sortKey == 7 && material.info.textureAtlasRowCount == 3
               && material.info.textureAtlasColumnCount == 4 && material.info.surfaceTypeBits == 0x8000'0001u
               && material.info.hashIndex == 0x1234,
           "the info scalars convert from their retail offsets");
    Expect(drawSurf == (std::uint64_t{kDrawSurfHigh} << 32 | kDrawSurfLow) && material.stateBitsEntry[33] == 0xFF
               && material.stateFlags == 5 && material.cameraRegion == 6,
           "the draw surface copies whole, and the state entries and flags convert");
}

// The tables TestInlineMaterial builds: two constants and two state bits.
void ExpectTables(const Zone &zone, const ::Material &material)
{
    Expect(material.constantCount == 2 && material.constantTable == reinterpret_cast<const MaterialConstantDef *>(zone.virt + 16)
               && material.constantTable[1].nameHash == 20 && material.constantTable[1].literal[3] == 8.f,
           "the constants stream 16-aligned into block 4, and the native pointer points at them");
    Expect(material.stateBitsCount == 2 && material.stateBitsTable == reinterpret_cast<const GfxStateBits *>(zone.virt + 80)
               && material.stateBitsTable[1].loadBits[0] == 0x0101 && !material.textureTable,
           "the state bits stream into block 4, and there is no texture table");
}

void TestInlineMaterial()
{
    Zone zone;
    Reset();
    // Block 4: the name (0..6), the set's name (6..9), two constants 16-aligned
    // (16..80) and two state bits (80..96). The set record uses the temp block.
    File().Material(kInline, kInline, kInline, 2, kInline, 2).Text("mat/a").Set(kInline).Text("ts");
    File().Constant(10, 1.f).Constant(20, 5.f).Word(0).Word(1).Word(0x0000'0101).Word(2);
    const ::Material *const material = Load(kInline);
    Expect(material == &g_pool[0] && g_setCount == 1, "a material and its technique set publish");
    if (material != &g_pool[0])
        return;
    ExpectScalars(*material);
    Expect(Is(zone, material->info.name, "mat/a") && material->techniqueSet == &g_sets[0]
               && Is(zone, g_sets[0].name, "ts"),
           "the name loads, and the technique set loads through TechniqueSet's step");
    ExpectTables(zone, *material);
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 96 && !g_arenaUsed,
           "every disk byte is consumed and no native storage is used");
}

// Material b names a's objects; c's present but empty tables become null.
void ExpectNamed(const ::Material &a, const ::Material &b, const ::Material &c)
{
    Expect(b.techniqueSet == a.techniqueSet && b.constantTable == a.constantTable && b.stateBitsTable == a.stateBitsTable,
           "set, constant and state offsets resolve to the earlier objects");
    Expect(c.techniqueSet == a.techniqueSet && !c.constantTable && !c.stateBitsTable,
           "present but empty tables become null");
}

void TestSharedAndOffsets()
{
    Zone zone;
    Reset();
    // Material A (-2): its alias slot (0), "a" (4..6), the set's alias slot
    // (8..12) and "ts", one constant (16..48) and one state (48..56).
    File().Material(kInline, disk32::kSharedInline, kInline, 1, kInline, 1).Text("a").Set(kInline).Text("ts");
    File().Constant(3, 1.f).Word(0).Word(0);
    // Material B names A's set, constant and state by offset; its tables, when
    // present but empty, only align or check their tokens.
    File().Material(kInline, VirtualOffset(8), VirtualOffset(16), 1, VirtualOffset(48), 1).Text("b");
    File().Material(kInline, VirtualOffset(8), kInline, 0, VirtualOffset(48), 0).Text("c");
    const ::Material *const a = Load(disk32::kSharedInline);
    const ::Material *const b = Load(kInline);
    const ::Material *const c = Load(kInline);
    Expect(a == &g_pool[0] && b == &g_pool[1] && c == &g_pool[2] && g_setCount == 1, "three materials publish");
    if (c != &g_pool[2])
        return;
    Expect(reinterpret_cast<std::uintptr_t>(a) > UINT32_MAX && Load(VirtualOffset(0)) == a,
           "a material alias token resolves to the full native pointer");
    ExpectNamed(*a, *b, *c);
    Expect(g_read == g_file.size(), "every disk byte is consumed");
}

struct Malformed
{
    const char *what;
    void (*build)();
    const char *error;
};

const Malformed kMalformed[] = {
    {"truncated record", [] { File().Word(kInline).Word(0); }, "ended unexpectedly"},
    {"constants without a table", [] { File().Material(kInline, kInline, 0, 1, 0, 0); }, "material tables"},
    {"states without a table", [] { File().Material(kInline, kInline, 0, 0, 0, 2); }, "material tables"},
    {"textures without a table", [] { File().Material(kInline, kInline, 0, 0, 0, 0, 7, 0, 1); }, "material tables"},
    {"a texture table", [] { File().Material(kInline, kInline, 0, 0, 0, 0, 7, kInline, 1).Text("m").Set(kInline)
                                 .Text("s"); }, "texture tables are not converted"},
    {"null name", [] { File().Material(0, kInline, 0, 0, 0, 0).Set(kInline).Text("s"); }, "semantics"},
    {"sort key 64", [] { File().Material(kInline, kInline, 0, 0, 0, 0, 64).Text("m").Set(kInline).Text("s"); },
     "semantics"},
    {"no technique set", [] { File().Material(kInline, 0, 0, 0, 0, 0).Text("m"); }, "semantics"},
    {"unsorted constants", [] { File().Material(kInline, kInline, kInline, 2, 0, 0).Text("m").Set(kInline).Text("s")
                                    .Constant(9, 1.f).Constant(9, 1.f); }, "semantics"},
    {"non-finite constant", [] { File().Material(kInline, kInline, kInline, 1, 0, 0).Text("m").Set(kInline).Text("s")
                                     .Constant(1, (std::numeric_limits<float>::infinity)()); }, "Non-finite"},
    {"unsafe state bits", [] { File().Material(kInline, kInline, 0, 0, kInline, 1).Text("m").Set(kInline).Text("s")
                                   .Word(0x0000'0600).Word(0); }, "Unsafe"},
    {"truncated constants", [] { File().Material(kInline, kInline, kInline, 2, 0, 0).Text("m").Set(kInline).Text("s")
                                     .Constant(1, 1.f); }, "ended unexpectedly"},
    {"unmapped constant offset", [] { File().Material(kInline, kInline, VirtualOffset(512), 1, 0, 0).Text("m")
                                          .Set(kInline).Text("s"); }, "pointer offset"},
    {"misaligned constant offset", [] { File().Material(kInline, kInline, VirtualOffset(4), 1, 0, 0).Text("mmmmmmm")
                                            .Set(kInline).Text("s"); }, "pointer offset"},
    {"state offset past its bytes", [] { File().Material(kInline, kInline, 0, 0, VirtualOffset(0), 1).Text("m")
                                             .Set(kInline).Text("s"); }, "pointer offset"},
    {"unmapped technique set", [] { File().Material(kInline, VirtualOffset(64), 0, 0, 0, 0).Text("m"); },
     "alias offset"},
    {"unmapped material alias", [] {}, "alias offset"},
};

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone;
        Reset();
        test.build();
        const std::uintptr_t slot = g_file.empty() ? VirtualOffset(16) : kInline;
        ExpectDrop(test.what, test.error, [&] { Load(slot); });
    }
    Zone zone;
    ExpectDrop("slot wider than a token", "no disk32 token", [] { Load(std::uintptr_t{1} << 32); });
}
} // namespace

void __cdecl Load_MaterialAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    ::Material &entry = g_pool[g_materials++];
    ++g_published;
    entry = *header->material;
    header->material = &entry;
}

void __cdecl Load_MaterialTechniqueSetAsset(XAssetHeader *header)
{
    // DB_MediaRemapTechniqueSet leaves an unremapped set naming itself.
    MaterialTechniqueSet &entry = g_sets[g_setCount++];
    entry = *header->techniqueSet;
    entry.remappedTechniqueSet = &entry;
    header->techniqueSet = &entry;
}

int main()
{
    return Run({TestInlineMaterial, TestSharedAndOffsets, TestMalformedFailsClosed});
}
