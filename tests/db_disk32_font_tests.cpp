// db_disk32_font_tests.cpp: the 64-bit Font loader (NOW row 12) on hand-built
// disk32 zone images (disk32_fixture.hpp). Beyond the fixture's seams, only
// the asset pool (Load_FontAsset) is replaced.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>

#include <cstring>

namespace
{
using namespace disk32_test;

Font_s g_pool[4]; // what Load_FontAsset published

constexpr std::int32_t kGlyphs = 96; // Load_Font's minimum
constexpr std::size_t kGlyphBytes = kGlyphs * sizeof(Glyph);

struct File : FileBuilder<File>
{
    // The 24-byte retail record.
    File &Record(std::uint32_t name, std::int32_t pixelHeight, std::int32_t glyphCount, std::uint32_t material,
                 std::uint32_t glow, std::uint32_t glyphs)
    {
        Word(name).Word(static_cast<std::uint32_t>(pixelHeight)).Word(static_cast<std::uint32_t>(glyphCount));
        return Word(material).Word(glow).Word(glyphs);
    }
    // count 24-byte glyphs; every byte differs from its neighbours.
    File &Glyphs(std::int32_t count)
    {
        for (std::int32_t byte = 0; byte < count * 24; ++byte)
            g_file.push_back(static_cast<std::uint8_t>(byte * 7 + 3));
        return *this;
    }
};

// A zone with the two blocks a font touches: temp (0) and virtual (4).
using Zone = disk32_test::Zone<4096>;

Font_s *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadFontPtrDisk32, slotValue);
}

bool Is(const Zone &zone, const char *text, const char *expected)
{
    return zone.Holds(text) && !std::strcmp(text, expected);
}

void TestInlineFont()
{
    Zone zone;
    g_arenaCapacity = 0; // the glyphs keep their layout; no native storage
    // Block 4: the name (0..13), then the glyphs 4-aligned at 16.
    File().Record(kInline, 0x11, kGlyphs + 1, 0, 0, kInline).Text("fonts/normal").Glyphs(kGlyphs + 1);
    const Font_s *const font = Load(kInline);
    Expect(font == &g_pool[0] && g_published == 1, "an inline font publishes one pool entry");
    if (font != &g_pool[0])
        return;
    Expect(Is(zone, font->fontName, "fonts/normal") && font->fontName == zone.At(0),
           "the name points at its bytes in block 4");
    Expect(font->pixelHeight == 0x11 && font->glyphCount == kGlyphs + 1, "the scalars convert from their retail offsets");
    Expect(!font->material && !font->glowMaterial, "null material tokens stay null");
    Expect(font->glyphs == reinterpret_cast<const Glyph *>(zone.virt + 16)
               && !std::memcmp(zone.virt + 16, g_file.data() + 24 + 13, kGlyphBytes + sizeof(Glyph)),
           "the glyphs stream 4-aligned in block 4 and the native pointer points at them");
    Expect(!std::memcmp(zone.temp, g_file.data(), sizeof(disk32::FontDisk32)),
           "the disk32 record is streamed into the temp block at the retail offset");
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 16 + kGlyphBytes + sizeof(Glyph),
           "every disk byte is consumed and block 4 advances by the retail extent");
}

void TestSharedInlineAndOffsets()
{
    Zone zone;
    // Block 4: the alias slot (0), the name (4..7), then the glyphs at 8.
    File().Record(kInline, 12, kGlyphs, 0, 0, kInline).Text("fa").Glyphs(kGlyphs);
    File().Record(VirtualOffset(4), 20, kGlyphs, 0, 0, VirtualOffset(8));
    const Font_s *const shared = Load(disk32::kSharedInline);
    Expect(shared == &g_pool[0] && Is(zone, g_pool[0].fontName, "fa"), "a shared-inline font publishes");
    if (shared != &g_pool[0])
        return;
    Expect(reinterpret_cast<std::uintptr_t>(shared) > UINT32_MAX,
           "the pool lies above 4 GiB, so a narrowed pointer would differ");
    Expect(Load(VirtualOffset(0)) == shared, "an alias token resolves to the full native pointer");
    const Font_s *const second = Load(kInline);
    Expect(second == &g_pool[1] && g_read == g_file.size(), "the second record streams after the first");
    if (second != &g_pool[1])
        return;
    Expect(second->fontName == shared->fontName && second->glyphs == shared->glyphs
               && second->glyphs == reinterpret_cast<const Glyph *>(zone.virt + 8) && second->pixelHeight == 20,
           "name and glyph offset tokens resolve to the earlier bytes");
    Expect(!Load(0) && g_published == 2, "a null token loads nothing");
}

struct Malformed
{
    const char *what;
    void (*build)();
    std::uintptr_t slot;
    const char *error;
    std::uint32_t tempBytes = 128;
    bool prior = false; // Prior() loads first
};

void RunOff()
{
    for (int i = 0; i < 1040; ++i) // 4160 bytes: past the 4096-byte block
        File().Word(0x42424242);
}

// A well-formed font whose 96 glyphs sit at block-4 offset 4, after "p".
void Prior()
{
    File().Record(kInline, 1, kGlyphs, 0, 0, kInline).Text("p").Glyphs(kGlyphs);
}

const Malformed kMalformed[] = {
    {"truncated record", [] { File().Word(kInline).Word(1); }, kInline, "ended unexpectedly"},
    {"record past the temp block", [] { File().Record(kInline, 1, kGlyphs, 0, 0, kInline); }, kInline,
     "exceeds stream block", 16},
    {"too few glyphs", [] { File().Record(kInline, 1, kGlyphs - 1, 0, 0, kInline).Text("a"); }, kInline,
     "glyph table"},
    {"too many glyphs", [] { File().Record(kInline, 1, 65537, 0, 0, kInline).Text("a"); }, kInline, "glyph table"},
    {"negative glyph count", [] { File().Record(kInline, 1, -96, 0, 0, kInline).Text("a"); }, kInline, "glyph table"},
    {"null glyphs", [] { File().Record(kInline, 1, kGlyphs, 0, 0, 0).Text("a"); }, kInline, "glyph table"},
    {"null name", [] { File().Record(0, 1, kGlyphs, 0, 0, kInline).Glyphs(kGlyphs); }, kInline, "no name"},
    {"unmapped name offset", [] { File().Record(VirtualOffset(8), 1, kGlyphs, 0, 0, kInline); }, kInline,
     "string offset"},
    {"name runs off its block", [] { File().Record(kInline, 1, kGlyphs, 0, 0, kInline); RunOff(); }, kInline,
     "Unterminated"},
    {"an inline material", [] { File().Record(kInline, 1, kGlyphs, kInline, 0, kInline).Text("a"); }, kInline,
     "material"},
    {"a glow material offset", [] { File().Record(kInline, 1, kGlyphs, 0, VirtualOffset(0), kInline).Text("a"); },
     kInline, "material"},
    {"truncated glyphs", [] { File().Record(kInline, 1, kGlyphs, 0, 0, kInline).Text("a").Glyphs(kGlyphs - 1); },
     kInline, "ended unexpectedly"},
    {"glyphs past their block", [] { File().Record(kInline, 1, 200, 0, 0, kInline).Text("a").Glyphs(200); },
     kInline, "exceeds stream block"},
    {"unmapped glyph offset", [] { File().Record(kInline, 1, kGlyphs, 0, 0, VirtualOffset(64)).Text("a"); },
     kInline, "pointer offset"},
    {"glyph offset longer than its glyphs",
     [] { Prior(); File().Record(kInline, 1, kGlyphs + 1, 0, 0, VirtualOffset(4)).Text("a"); }, kInline,
     "pointer offset", 128, true},
    {"shared-inline glyph token", [] { File().Record(kInline, 1, kGlyphs, 0, 0, disk32::kSharedInline).Text("a"); },
     kInline, "pointer offset"},
    {"unmapped alias", [] {}, VirtualOffset(16), "alias offset"},
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
            Expect(Load(kInline) == &g_pool[0], test.what, "(its prior font did not load)");
            g_published = 0;
        }
        ExpectDrop(test.what, test.error, [&] { Load(test.slot); });
    }
}
} // namespace

void __cdecl Load_FontAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    Font_s &entry = g_pool[g_published++];
    entry = *header->font;
    Expect(entry.fontName && entry.fontName[0] != '\0', "a published font has a name");
    header->font = &entry;
}

int main()
{
    return Run({TestInlineFont, TestSharedInlineAndOffsets, TestMalformedFailsClosed});
}
