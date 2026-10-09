// db_disk32_material_tests.cpp: the 64-bit Material loader (NOW row 12) on
// hand-built disk32 zone images (disk32_fixture.hpp), with TechniqueSet's real
// and Image's steps for the references. Beyond the fixture's seams, only the
// three asset pools, the external-data count and the renderer's water hook
// are replaced. It builds headless, which checks a water's image against its
// source grids, and as a client (material-client), which picmips the water.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>
#include <gfx_d3d/r_water.h>

#include <cstring>
#include <limits>

namespace
{
using namespace disk32_test;

Material g_pool[4];
MaterialTechniqueSet g_sets[2];
GfxImage g_images[4];

#ifdef KISAK_DEDI_HEADLESS
constexpr bool kClient = false;
#else
constexpr bool kClient = true;
#endif
int g_picmips = 0; // Load_PicmipWater calls
int g_imageCount = 0;
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
    File &Texture(std::uint32_t hash, std::uint32_t semantic, std::uint32_t token, std::uint32_t sampler = 1)
    {
        return Word(hash).Word(0x0201u | sampler << 16 | semantic << 24).Word(token);
    }
    // A 36-byte image with no texture: a water image of `size` squared unless
    // `semantic` says otherwise.
    File &Image(std::uint32_t size, std::uint32_t semantic = 11)
    {
        Word(3).Word(0).Word(semantic << 24).Word(0).Word(0).Word(0).Word(size | size << 16).Word(0x00050001u);
        return Word(kInline).Text("img");
    }
    // A 68-byte water of `size` squared samples, then its samples, then its
    // image; a bad amplitude or frequency is non-finite or negative.
    File &Water(std::uint32_t size, std::uint32_t h0 = kInline, bool badAmplitude = false, bool badFrequency = false,
                std::uint32_t imageSize = 0)
    {
        Word(0).Word(h0).Word(kInline).Word(size).Word(size).Float(64).Float(64).Float(9.8f).Float(2).Float(1).Float(0);
        Float(0.5f).Float(1).Float(2).Float(3).Float(4).Word(kInline);
        if (!h0)
            return *this;
        const float first = badAmplitude ? (std::numeric_limits<float>::infinity)() : 0.5f;
        for (std::uint32_t n = 0; n < size * size; ++n)
            Float(n ? 0.25f : first).Float(0);
        for (std::uint32_t n = 0; n < size * size; ++n)
            Float(badFrequency && n == 3 ? -1.f : static_cast<float>(n));
        return Image(imageSize ? imageSize : size);
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
    g_materials = g_setCount = g_imageCount = 0;
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

void ExpectWater(const Zone &zone, const water_t *water);

// The texture table TestTextures builds: an image, then a water.
void ExpectTextures(const Zone &zone, const MaterialTextureDef *textures)
{
    const MaterialTextureDef &image = textures[0];
    Expect(image.nameHash == 5 && image.nameStart == 1 && image.nameEnd == 2 && image.samplerState == 1
               && image.semantic == 2 && image.u.image == &g_images[0],
           "an image texture converts, its image loading through Image's step");
    ExpectWater(zone, textures[1].u.water);
}

// The water TestTextures builds: 4 x 4 samples.
void ExpectWater(const Zone &zone, const water_t *water)
{
    if (!InArena(water))
        return Expect(false, "a water converts into native storage");
    Expect(water->M == 4 && water->N == 4 && water->amplitude == 0.5f && water->codeConstant[3] == 4.f
               && water->writable.floatTime == -3.402823466e+38F,
           "a water's scalars convert, its clock reset");
    Expect(water->H0 == reinterpret_cast<const complex_s *>(zone.virt + 100) && water->H0[0].real == 0.5f
               && water->wTerm == reinterpret_cast<const float *>(zone.virt + 228) && water->wTerm[15] == 15.f
               && water->image == &g_images[1],
           "its samples stay in block 4 after it, and its image loads");
}

void TestTextures()
{
    Zone zone;
    Reset();
    g_picmips = 0;
    // Block 4: "m" (0..2), "s" (2..4), the table (4..28), the first image's
    // name (28..32), the water (32..100), its amplitudes (100..228) and
    // frequencies (228..292), then its image's name. Records use the temp block.
    File().Material(kInline, kInline, 0, 0, 0, 0, 7, kInline, 2).Text("m").Set(kInline).Text("s");
    File().Texture(5, 2, kInline).Texture(9, 11, kInline).Image(8, 2).Water(4);
    File().Material(kInline, kInline, 0, 0, 0, 0, 7, VirtualOffset(4), 2).Text("n").Set(kInline).Text("t");
    const ::Material *const material = Load(kInline);
    Expect(material == &g_pool[0] && g_imageCount == 2 && InArena(material->textureTable), "a textured material publishes");
    if (material == &g_pool[0] && InArena(material->textureTable))
        ExpectTextures(zone, material->textureTable);
    Expect(Load(kInline) == &g_pool[1] && g_pool[1].textureTable == material->textureTable && g_read == g_file.size(),
           "a texture-table offset resolves to the earlier native table");
    Expect(g_picmips == (kClient ? 1 : 0), "a client picmips the water once, the shared table included; a server never");
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

// A material with `count` textures, up to its texture table.
File Textured(std::uint32_t count = 1)
{
    File().Material(kInline, kInline, 0, 0, 0, 0, 7, kInline, count).Text("m").Set(kInline).Text("s");
    return File();
}

const Malformed kMalformed[] = {
    {"truncated record", [] { File().Word(kInline).Word(0); }, "ended unexpectedly"},
    {"constants without a table", [] { File().Material(kInline, kInline, 0, 1, 0, 0); }, "material tables"},
    {"states without a table", [] { File().Material(kInline, kInline, 0, 0, 0, 2); }, "material tables"},
    {"textures without a table", [] { File().Material(kInline, kInline, 0, 0, 0, 0, 7, 0, 1); }, "material tables"},
    {"texture without a payload", [] { Textured().Texture(5, 2, 0); }, "texture header"},
    {"texture semantic 12", [] { Textured().Texture(5, 12, kInline); }, "texture header"},
    {"texture without a filter", [] { Textured().Texture(5, 2, kInline, 0x08); }, "texture header"},
    {"unordered textures", [] { Textured(2).Texture(9, 2, kInline).Texture(5, 2, kInline).Image(4, 2).Image(4, 2); },
     "Unordered"},
    {"unmapped image offset", [] { Textured().Texture(5, 2, VirtualOffset(512)); }, "alias offset"},
    {"water grid of 3", [] { Textured().Texture(5, 11, kInline).Water(3, kInline); }, "water header"},
    {"water without amplitudes", [] { Textured().Texture(5, 11, kInline).Water(4, 0); }, "water header"},
    {"non-finite amplitude", [] { Textured().Texture(5, 11, kInline).Water(4, kInline, true); }, "frequency data"},
    {"negative frequency", [] { Textured().Texture(5, 11, kInline).Water(4, kInline, false, true); }, "frequency data"},
    {"water image of another size", [] { Textured().Texture(5, 11, kInline).Water(4, kInline, false, false, 8); }, "image contract"},
    {"unmapped water offset", [] { Textured().Texture(5, 11, VirtualOffset(512)); }, "alias offset"},
    {"texture table offset of another length", [] { Textured().Texture(5, 2, kInline).Image(4, 2);
         File().Material(kInline, kInline, 0, 0, 0, 0, 7, VirtualOffset(4), 2).Text("n").Set(kInline).Text("t");
         Load(kInline); g_published = 0; },
     "alias offset"},
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

void __cdecl Load_GfxImageAsset(XAssetHeader *header)
{
    g_images[g_imageCount] = *header->image;
    header->image = &g_images[g_imageCount++];
}

void __cdecl DB_LoadedExternalData(std::int32_t)
{
    Expect(false, "an image with no texture accounts no external data");
}

void __cdecl Load_MaterialTechniqueSetAsset(XAssetHeader *header)
{
    // DB_MediaRemapTechniqueSet leaves an unremapped set naming itself.
    MaterialTechniqueSet &entry = g_sets[g_setCount++];
    entry = *header->techniqueSet;
    entry.remappedTechniqueSet = &entry;
    header->techniqueSet = &entry;
}

// The renderer's water hook: it counts, and, like Load_PicmipWater at
// r_picmip_water 0, checks the image against the source grid.
bool __cdecl Load_PicmipWater(water_t **waterRef)
{
    ++g_picmips;
    const water_t *const water = *waterRef;
    if (!water->image || water->image->width != water->M || water->image->height != water->N)
    {
        Com_Error(ERR_DROP, "Invalid material water image contract");
        return false;
    }
    return true;
}

int main()
{
    return Run({TestInlineMaterial, TestTextures, TestSharedAndOffsets, TestMalformedFailsClosed});
}
