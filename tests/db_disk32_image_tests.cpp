// db_disk32_image_tests.cpp: the 64-bit Image loader (NOW row 12) on
// hand-built disk32 zone images (disk32_fixture.hpp). Beyond the fixture's
// seams, only the asset pool (Load_GfxImageAsset) and the external-data
// progress count (DB_LoadedExternalData) are replaced.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_loaders.h> // the pointer step a referring record calls
#include <database/db_disk32_mirrors.h>
#include <gfx_d3d/r_image.h>

#include <cstring>

namespace
{
using namespace disk32_test;

struct Zone;
const Zone *g_zone = nullptr; // the zone the pool stub checks names against
GfxImage g_pool[4];           // what Load_GfxImageAsset published
std::int64_t g_externalBytes = 0;
int g_externalCalls = 0;

// The scalar fields of one record, as disk bytes. Every value differs from its
// neighbours, so a field read at a wrong offset (its native one, say) differs.
struct Scalars
{
    std::int32_t mapType;
    std::uint8_t picmip[2];
    std::uint8_t noPicmip; // any nonzero byte is true
    std::uint8_t semantic;
    std::uint8_t track;
    std::int32_t cardMemory[2];
    std::uint16_t width;
    std::uint16_t height;
    std::uint16_t depth;
    std::uint8_t category;
    std::uint8_t delayLoadPixels; // any nonzero byte is true
};

constexpr Scalars kColor{MAPTYPE_2D, {1, 2}, 0x04, 2, 7, {0x1234, 0x5678}, 64, 32, 1, 3, 0x01};
constexpr Scalars kCube{MAPTYPE_CUBE, {0, 1}, 0x00, 5, 9, {4096, 0}, 16, 8, 6, 3, 0xFF};
constexpr Scalars kExternal{MAPTYPE_2D, {2, 0}, 0x00, 2, 0, {3000, 9}, 128, 256, 1, 3, 0x00};
constexpr Scalars kWater{MAPTYPE_2D, {0, 0}, 0x01, 11, 0, {64, 1}, 64, 64, 1, IMG_CATEGORY_WATER, 0x01};
constexpr std::uint32_t kDxt1 = 0x31545844; // D3DFMT_DXT1
constexpr std::size_t kRecordBytes = sizeof(disk32::GfxImageDisk32);

struct File : FileBuilder<File>
{
    // The 36-byte retail record. The three padding bytes after track hold
    // junk, which the loader must ignore.
    File &Record(const Scalars &s, std::uint32_t texture, std::uint32_t name)
    {
        Word(static_cast<std::uint32_t>(s.mapType)).Word(texture);
        Word(s.picmip[0] | s.picmip[1] << 8 | s.noPicmip << 16 | static_cast<std::uint32_t>(s.semantic) << 24);
        Word(0xCDCDCD00u | s.track);
        Word(static_cast<std::uint32_t>(s.cardMemory[0])).Word(static_cast<std::uint32_t>(s.cardMemory[1]));
        Word(s.width | static_cast<std::uint32_t>(s.height) << 16);
        Word(s.depth | static_cast<std::uint32_t>(s.category) << 16 | static_cast<std::uint32_t>(s.delayLoadPixels) << 24);
        return Word(name);
    }
    // A load definition: its 16-byte header (one 4x4x1 DXT1 level) declaring
    // resourceSize, then `pixels` bytes, which may be fewer than declared.
    File &LoadDef(std::int32_t resourceSize, std::size_t pixels)
    {
        Word(0x00040001u).Word(0x00010004u).Word(kDxt1).Word(static_cast<std::uint32_t>(resourceSize));
        for (std::size_t i = 0; i < pixels; ++i)
            g_file.push_back(static_cast<std::uint8_t>(0xA0 + i));
        return *this;
    }
};

// A zone with the two blocks an image touches: temp (0) and virtual (4).
struct Zone : disk32_test::Zone<128>
{
    explicit Zone(std::uint32_t tempBytes = 64) : disk32_test::Zone<128>(tempBytes)
    {
        g_zone = this;
        g_externalBytes = 0;
        g_externalCalls = 0;
    }
    ~Zone() { g_zone = nullptr; }
    bool Is(const char *text, const char *expected) const { return Holds(text) && !std::strcmp(text, expected); }
};

GfxImage *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadGfxImagePtrDisk32, slotValue);
}

// A converted bool is exactly 0 or 1; reading its byte sees any other value.
unsigned char Byte(const bool &value)
{
    return *reinterpret_cast<const unsigned char *>(&value);
}

bool SamePicmip(const GfxImage &image, const Scalars &s)
{
    return image.picmip.platform[0] == s.picmip[0] && image.picmip.platform[1] == s.picmip[1]
        && Byte(image.noPicmip) == (s.noPicmip != 0);
}

bool Matches(const GfxImage &image, const Scalars &s)
{
    return image.mapType == s.mapType && SamePicmip(image, s) && image.semantic == s.semantic
        && image.track == s.track && image.width == s.width && image.height == s.height && image.depth == s.depth
        && image.category == s.category && !image.texture.basemap;
}

// What the texture step leaves of the delay flag and the external byte count.
bool Payload(const GfxImage &image, bool delayed, std::int32_t bytes, std::int32_t second)
{
    return Byte(image.delayLoadPixels) == delayed && image.cardMemory.platform[0] == bytes
        && image.cardMemory.platform[1] == second;
}

void TestInlineImages()
{
    Zone zone;
    g_arenaCapacity = 0; // the pool copies the native image; no zone-native storage
    File().Record(kColor, kInline, kInline).Text("img/color").LoadDef(8, 8);
    File().Record(kCube, 0, kInline).Text("img/cube");
    const GfxImage *const color = Load(kInline);
    Expect(color == &g_pool[0] && g_published == 1, "an inline image publishes one pool entry");
    if (color != &g_pool[0])
        return;
    Expect(Matches(*color, kColor), "every scalar converts from its retail offset; byte 0x04 is true");
    Expect(Payload(*color, false, 0, 0) && !g_externalCalls, "embedded pixels clear the delay flag and byte count");
    Expect(zone.Is(color->name, "img/color") && color->name == zone.At(0), "the name points at its bytes in block 4");
    Expect(!std::memcmp(zone.temp, g_file.data(), kRecordBytes),
           "the disk32 record is streamed into the temp block at the retail offset");
    Expect(!std::memcmp(zone.temp + kRecordBytes, g_file.data() + kRecordBytes + 10, 16 + 8),
           "the load definition and exactly its 8 pixel bytes follow the record in the temp block");

    const GfxImage *const cube = Load(kInline);
    Expect(cube == &g_pool[1] && Matches(*cube, kCube), "the second record converts after the pixels");
    if (cube != &g_pool[1])
        return;
    Expect(Payload(*cube, true, 4096, 0), "a null texture keeps the delay flag and byte count");
    Expect(!std::memcmp(zone.temp, g_file.data() + kRecordBytes + 10 + 24, kRecordBytes),
           "the temp block is reclaimed between images");
    Expect(zone.Is(cube->name, "img/cube") && DB_GetStreamPos() == zone.virt + 10 + 9 && g_read == g_file.size(),
           "block 4 holds only the two names, and every disk byte is consumed");
    Expect(!g_arenaUsed, "no zone-native storage is used");
}

void TestExternalWaterAndDelayed()
{
    Zone zone;
    File().Record(kExternal, kInline, kInline).Text("ext").LoadDef(0, 0);
    File().Record(kCube, kInline, kInline).Text("delayed").LoadDef(0, 0);
    File().Record(kWater, kInline, kInline).Text("water").LoadDef(0, 0);
    const GfxImage *const external = Load(kInline);
    const GfxImage *const delayed = Load(kInline);
    const GfxImage *const water = Load(kInline);
    Expect(g_published == 3 && water == &g_pool[2] && g_read == g_file.size(), "three images publish");
    if (water != &g_pool[2])
        return;
    Expect(Payload(*external, false, 0, 0) && Matches(*external, kExternal),
           "an external image that is not delayed has its byte count accounted and cleared");
    Expect(g_externalCalls == 1 && g_externalBytes == 3000, "exactly its 3000 bytes are accounted");
    Expect(Payload(*delayed, true, 4096, 0), "a delayed image keeps its flag and byte count");
    Expect(Payload(*water, false, 0, 0) && Matches(*water, kWater), "a water image owns no external payload");
}

void TestSharedInlineAndOffsets()
{
    Zone zone;
    // Block 4: the image alias slot (0), the name (4..14), the texture alias
    // slot (16), then nothing: the second record's strings are offsets.
    File().Record(kColor, disk32::kSharedInline, kInline).Text("img/shared").LoadDef(4, 4);
    File().Record(kColor, VirtualOffset(16), VirtualOffset(4));
    const GfxImage *const shared = Load(disk32::kSharedInline);
    Expect(shared == &g_pool[0] && Matches(g_pool[0], kColor), "a shared-inline image publishes");
    if (shared != &g_pool[0])
        return;
    Expect(reinterpret_cast<std::uintptr_t>(shared) > UINT32_MAX,
           "the pool lies above 4 GiB, so a narrowed pointer would differ");
    Expect(Load(VirtualOffset(0)) == shared, "an alias token resolves to the full native pointer");
    const GfxImage *const second = Load(kInline);
    Expect(second == &g_pool[1] && g_read == g_file.size(), "the second record streams after the first");
    if (second != &g_pool[1])
        return;
    Expect(second->name == shared->name, "a name offset token resolves to the earlier string");
    Expect(Matches(*second, kColor) && Payload(*second, true, 0x1234, 0x5678),
           "a texture offset naming a same-map-type texture loads no pixels and keeps the byte count");
    Expect(DB_GetStreamPos() == zone.virt + 20, "block 4 holds the two alias slots and one name");
    Expect(!Load(0) && g_published == 2, "a null token loads nothing");
}

void TestNestedReference()
{
    Zone zone;
    File().Record(kCube, 0, kInline).Text("img/nested");
    // A referring record (a material's texture table, say) reaches its image
    // token while block 4 is current.
    GfxImage *image = nullptr;
    db::disk32_load::LoadGfxImagePtr(disk32::PointerToken{kInline}, &image);
    Expect(image == &g_pool[0] && Matches(g_pool[0], kCube), "a nested image token publishes the image");
    Expect(!std::memcmp(zone.temp, g_file.data(), kRecordBytes), "its record streams into the temp block");
    Expect(image && image->name == zone.At(0) && DB_GetStreamPos() == zone.virt + 11,
           "block 4 holds only its name and is current again");
}

struct Malformed
{
    const char *what;
    void (*build)();
    std::uintptr_t slot;
    const char *error;
    std::uint32_t tempBytes = 64;
    std::uintptr_t prior = 0; // a well-formed image loaded first
};

void RunOff()
{
    for (int i = 0; i < 40; ++i)
        File().Word(0x42424242);
}

// The prior image's texture alias slot follows the 4-byte name "pri".
void Prior()
{
    File().Record(kColor, disk32::kSharedInline, kInline).Text("pri").LoadDef(0, 0);
}

const Malformed kMalformed[] = {
    {"truncated record", [] { File().Record(kColor, 0, kInline); g_file.resize(30); }, kInline, "ended unexpectedly"},
    {"record past the temp block", [] { File().Record(kColor, 0, kInline).Text("a"); }, kInline,
     "exceeds stream block", 32},
    {"null name", [] { File().Record(kColor, 0, 0); }, kInline, "no name"},
    {"unmapped name offset", [] { File().Record(kColor, 0, VirtualOffset(8)); }, kInline, "string offset"},
    {"name runs off its block", [] { File().Record(kColor, 0, kInline); RunOff(); }, kInline, "Unterminated"},
    {"unmapped alias", [] {}, VirtualOffset(16), "alias offset"},
    {"alias token in the temp block", [] {}, 1, "alias offset"},
    {"slot wider than a token", [] {}, std::uintptr_t{1} << 32, "no disk32 token"},
    {"unmapped texture offset", [] { File().Record(kColor, VirtualOffset(16), kInline).Text("a"); }, kInline,
     "alias offset"},
    {"texture token in the temp block", [] { File().Record(kColor, 1, kInline).Text("a"); }, kInline, "alias offset"},
    {"texture offset naming an image", [] { Prior(); File().Record(kColor, VirtualOffset(0), kInline).Text("a"); },
     kInline, "alias offset", 64, disk32::kSharedInline},
    {"texture offset of another map type", [] { Prior(); File().Record(kCube, VirtualOffset(8), kInline).Text("a"); },
     kInline, "alias offset", 64, disk32::kSharedInline},
    {"load definition past the temp block", [] { File().Record(kColor, kInline, kInline).Text("a").LoadDef(0, 0); },
     kInline, "exceeds stream block", 48},
    {"truncated pixels", [] { File().Record(kColor, kInline, kInline).Text("a").LoadDef(8, 3); }, kInline,
     "ended unexpectedly"},
    {"pixels past their block", [] { File().Record(kColor, kInline, kInline).Text("a").LoadDef(100, 0); RunOff(); },
     kInline, "exceeds stream block"},
    {"negative pixel size", [] { File().Record(kColor, kInline, kInline).Text("a").LoadDef(-1, 0); RunOff(); },
     kInline, "stream request"},
    {"negative external size",
     [] { File().Record({MAPTYPE_2D, {}, 0, 0, 0, {-1, 0}, 1, 1, 1, 3, 0}, kInline, kInline).Text("a").LoadDef(0, 0); },
     kInline, "external image size"},
};

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone(test.tempBytes);
        test.build();
        if (test.prior)
        {
            Expect(Load(test.prior) == &g_pool[0], test.what, "(its prior image did not load)");
            g_published = 0;
        }
        ExpectDrop(test.what, test.error, [&] { Load(test.slot); });
    }
}
} // namespace

void __cdecl Load_GfxImageAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    GfxImage &entry = g_pool[g_published++];
    entry = *header->image;
    Expect(g_zone && g_zone->Holds(entry.name) && entry.name[0] != '\0', "a published image has a name in block 4");
    header->image = &entry;
}

void __cdecl DB_LoadedExternalData(std::int32_t size)
{
    g_externalBytes += size;
    ++g_externalCalls;
}

int main()
{
    return Run({TestInlineImages, TestExternalWaterAndDelayed, TestSharedInlineAndOffsets, TestNestedReference,
                TestMalformedFailsClosed});
}
