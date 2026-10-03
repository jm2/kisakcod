// db_disk32_lightdef_tests.cpp: the 64-bit LightDef loader (NOW row 12) on
// hand-built disk32 zone images (disk32_fixture.hpp). Its attenuation image
// loads through Image's real pointer step (db_disk32_image.cpp). Beyond the
// fixture's seams, only the two asset pools (Load_LightDefAsset,
// Load_GfxImageAsset) and the external-data count are replaced.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>

#include <cstring>

namespace
{
using namespace disk32_test;

GfxLightDef g_pool[4]; // what Load_LightDefAsset published
GfxImage g_images[4];  // what Load_GfxImageAsset published
int g_lightDefs = 0;
int g_imageCount = 0;

constexpr std::uint8_t kSampler = 0x2B;
constexpr std::int32_t kLookup = -0x1234567;
constexpr std::size_t kRecordBytes = sizeof(disk32::GfxLightDefDisk32);
constexpr std::size_t kImageBytes = sizeof(disk32::GfxImageDisk32);

struct File : FileBuilder<File>
{
    // The 16-byte retail record. The padding after samplerState holds junk,
    // which the loader must ignore.
    File &Record(std::uint32_t name, std::uint32_t image, std::uint8_t sampler, std::int32_t lookup)
    {
        return Word(name).Word(image).Word(0xCDCDCD00u | sampler).Word(static_cast<std::uint32_t>(lookup));
    }
    // A 36-byte retail GfxImage with no texture; width 0x40 marks it.
    File &Image(std::uint32_t name)
    {
        Word(1).Word(0).Word(0).Word(0).Word(0).Word(0).Word(0x00100040u).Word(0x00030001u);
        return Word(name);
    }
};

// A zone with the two blocks a light def touches: temp (0) and virtual (4).
struct Zone : disk32_test::Zone<128>
{
    explicit Zone(std::uint32_t tempBytes = 128) : disk32_test::Zone<128>(tempBytes)
    {
        g_lightDefs = 0;
        g_imageCount = 0;
    }
    bool Is(const char *text, const char *expected) const { return Holds(text) && !std::strcmp(text, expected); }
};

GfxLightDef *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadGfxLightDefPtrDisk32, slotValue);
}

// The first def of TestInlineLightDefs: its fields, its image and its record.
void ExpectPointDef(const Zone &zone, const GfxLightDef &def)
{
    Expect(zone.Is(def.name, "light_point") && def.name == zone.At(0), "the name points at its bytes in block 4");
    Expect(def.attenuation.samplerState == kSampler && def.lmapLookupStart == kLookup,
           "the scalars convert from their retail offsets");
    Expect(def.attenuation.image == &g_images[0] && zone.Is(g_images[0].name, "lights/att")
               && g_images[0].name == zone.At(12) && g_images[0].width == 0x40,
           "the image token loads through Image's step, its name after the def's");
    Expect(!std::memcmp(zone.temp, g_file.data(), kRecordBytes)
               && !std::memcmp(zone.temp + kRecordBytes, g_file.data() + kRecordBytes + 12, kImageBytes),
           "the def's record, then its image's record, stream into the temp block");
}

void TestInlineLightDefs()
{
    Zone zone;
    // Block 4: the def's name (0..12), its image's name (12..23), then "bare".
    File().Record(kInline, kInline, kSampler, kLookup).Text("light_point").Image(kInline).Text("lights/att");
    File().Record(kInline, 0, 7, 9).Text("bare");
    const GfxLightDef *const def = Load(kInline);
    Expect(def == &g_pool[0] && g_lightDefs == 1 && g_imageCount == 1, "an inline def and its image publish");
    if (def != &g_pool[0])
        return;
    ExpectPointDef(zone, *def);
    const GfxLightDef *const bare = Load(kInline);
    Expect(bare == &g_pool[1] && !bare->attenuation.image && bare->attenuation.samplerState == 7
               && bare->lmapLookupStart == 9 && g_imageCount == 1,
           "a null image token loads no image");
    Expect(!std::memcmp(zone.temp, g_file.data() + kRecordBytes + 12 + kImageBytes + 11, kRecordBytes),
           "the temp block is reclaimed between defs");
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 28 && !g_arenaUsed,
           "every disk byte is consumed, block 4 is current again and no native storage is used");
}

void TestSharedInlineAndOffsets()
{
    Zone zone;
    // Block 4: the def alias slot (0), its name (4..14), the image alias slot
    // (16) and the image's name (20..24). The second def names them by offset.
    File().Record(kInline, disk32::kSharedInline, 1, 2).Text("ld_shared").Image(kInline).Text("att");
    File().Record(VirtualOffset(4), VirtualOffset(16), 3, 4);
    const GfxLightDef *const shared = Load(disk32::kSharedInline);
    Expect(shared == &g_pool[0] && shared->attenuation.image == &g_images[0], "a shared-inline def publishes");
    if (shared != &g_pool[0])
        return;
    Expect(reinterpret_cast<std::uintptr_t>(shared) > UINT32_MAX
               && reinterpret_cast<std::uintptr_t>(&g_images[0]) > UINT32_MAX,
           "the pools lie above 4 GiB, so a narrowed pointer would differ");
    Expect(Load(VirtualOffset(0)) == shared, "a def alias token resolves to the full native pointer");
    const GfxLightDef *const second = Load(kInline);
    Expect(second == &g_pool[1] && g_read == g_file.size(), "the second record streams after the first");
    if (second != &g_pool[1])
        return;
    Expect(second->name == shared->name && second->attenuation.image == &g_images[0] && g_imageCount == 1,
           "name and image offset tokens resolve to the earlier string and pooled image");
    Expect(second->attenuation.samplerState == 3 && second->lmapLookupStart == 4, "its own scalars convert");
    Expect(!Load(0) && g_lightDefs == 2, "a null token loads nothing");
}

struct Malformed
{
    const char *what;
    void (*build)();
    std::uintptr_t slot;
    const char *error;
    std::uint32_t tempBytes = 128;
    std::uintptr_t prior = 0; // a well-formed def loaded first
};

void RunOff()
{
    for (int i = 0; i < 40; ++i)
        File().Word(0x42424242);
}

// The prior def's alias slot is at 0 and its image's at 12, after "pri_def".
void Prior()
{
    File().Record(kInline, disk32::kSharedInline, 0, 0).Text("pri_def").Image(kInline).Text("i");
}

const Malformed kMalformed[] = {
    {"truncated record", [] { File().Word(kInline).Word(0).Word(0); }, kInline, "ended unexpectedly"},
    {"record past the temp block", [] { File().Record(kInline, 0, 0, 0).Text("a"); }, kInline,
     "exceeds stream block", 8},
    {"null name", [] { File().Record(0, 0, 0, 0); }, kInline, "light def has no name"},
    {"unmapped name offset", [] { File().Record(VirtualOffset(8), 0, 0, 0); }, kInline, "string offset"},
    {"name runs off its block", [] { File().Record(kInline, 0, 0, 0); RunOff(); }, kInline, "Unterminated"},
    {"unmapped image offset", [] { File().Record(kInline, VirtualOffset(64), 0, 0).Text("a"); }, kInline,
     "alias offset"},
    {"image token in the temp block", [] { File().Record(kInline, 1, 0, 0).Text("a"); }, kInline, "alias offset"},
    {"image offset naming a light def", [] { Prior(); File().Record(kInline, VirtualOffset(0), 0, 0).Text("a"); },
     kInline, "alias offset", 128, disk32::kSharedInline},
    {"image record past the temp block", [] { File().Record(kInline, kInline, 0, 0).Text("a").Image(kInline); },
     kInline, "exceeds stream block", 40},
    {"truncated image record", [] { File().Record(kInline, kInline, 0, 0).Text("a").Word(1).Word(0); }, kInline,
     "ended unexpectedly"},
    {"image with no name", [] { File().Record(kInline, kInline, 0, 0).Text("a").Image(0); }, kInline,
     "image has no name"},
    {"unmapped def alias", [] {}, VirtualOffset(16), "alias offset"},
    {"def alias naming an image", [] { Prior(); }, VirtualOffset(12), "alias offset", 128, disk32::kSharedInline},
    {"slot wider than a token", [] {}, std::uintptr_t{1} << 32, "no disk32 token"},
};

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone(test.tempBytes);
        test.build();
        if (test.prior)
        {
            Expect(Load(test.prior) == &g_pool[0], test.what, "(its prior def did not load)");
            g_published = 0;
        }
        ExpectDrop(test.what, test.error, [&] { Load(test.slot); });
    }
}
} // namespace

void __cdecl Load_LightDefAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    GfxLightDef &entry = g_pool[g_lightDefs++];
    ++g_published;
    entry = *header->lightDef;
    Expect(entry.name && entry.name[0] != '\0', "a published def has a name");
    header->lightDef = &entry;
}

void __cdecl Load_GfxImageAsset(XAssetHeader *header)
{
    GfxImage &entry = g_images[g_imageCount++];
    ++g_published;
    entry = *header->image;
    header->image = &entry;
}

void __cdecl DB_LoadedExternalData(std::int32_t)
{
    Expect(false, "an image with no texture accounts no external data");
}

int main()
{
    return Run({TestInlineLightDefs, TestSharedInlineAndOffsets, TestMalformedFailsClosed});
}
