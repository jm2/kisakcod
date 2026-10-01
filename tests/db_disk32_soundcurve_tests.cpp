// db_disk32_soundcurve_tests.cpp: the 64-bit SndCurve loader (NOW row 12) on
// hand-built disk32 zone images (disk32_fixture.hpp). Beyond the fixture's
// seams, only the asset pool (Load_SndCurveAsset) is replaced.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>

#include <cstring>
#include <initializer_list>
#include <limits>

namespace
{
using namespace disk32_test;

SndCurve g_pool[4]; // what Load_SndCurveAsset published

// A curve's knots as (x, y) pairs. Knots past knotCount are junk the loader
// copies without checking, as the 32-bit loader does.
struct Knots
{
    float values[8][2];
};

constexpr Knots kFalloff{{{0.0f, 1.0f}, {0.5f, 0.25f}, {1.0f, 0.0f}, {9.5f, -3.0f},
                          {7.25f, 6.0f}, {-1.0f, 2.5f}, {3.0f, 4.0f}, {8.0f, 8.5f}}};
constexpr Knots kSteps{{{0.0f, 0.0f}, {0.125f, 0.5f}, {0.25f, 0.75f}, {0.375f, 0.875f},
                        {0.5f, 0.9375f}, {0.625f, 0.96875f}, {0.75f, 0.984375f}, {1.0f, 1.0f}}};

struct File : FileBuilder<File>
{
    // The 72-byte retail record: filename, knotCount, then 8 (x, y) knots.
    File &Record(std::uint32_t filename, std::int32_t knotCount, const Knots &knots)
    {
        Word(filename).Word(static_cast<std::uint32_t>(knotCount));
        for (const auto &knot : knots.values)
            Float(knot[0]).Float(knot[1]);
        return *this;
    }
};

// A zone with the two blocks a SndCurve touches: temp (0) and virtual (4).
using Zone = disk32_test::Zone<96>;

SndCurve *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadSndCurvePtrDisk32, slotValue);
}

bool Is(const Zone &zone, const char *text, const char *expected)
{
    return zone.Holds(text) && !std::strcmp(text, expected);
}

bool SameKnots(const SndCurve &curve, const Knots &knots)
{
    return !std::memcmp(curve.knots, knots.values, sizeof(knots.values));
}

void TestInlineCurves()
{
    Zone zone;
    File().Record(kInline, 3, kFalloff).Text("sound/falloff/default");
    File().Record(kInline, 8, kSteps).Text("sound/falloff/steps");
    const SndCurve *const falloff = Load(kInline);
    Expect(falloff == &g_pool[0] && g_published == 1, "inline curve publishes one pool entry");
    if (falloff != &g_pool[0])
        return;
    Expect(Is(zone, falloff->filename, "sound/falloff/default")
               && falloff->filename == reinterpret_cast<char *>(zone.virt),
           "filename points at its bytes in block 4");
    Expect(falloff->knotCount == 3 && SameKnots(*falloff, kFalloff),
           "knotCount and all 8 knots, junk past the count included, convert from their retail offsets");
    Expect(!std::memcmp(zone.temp, g_file.data(), sizeof(disk32::SndCurveDisk32)),
           "the disk32 record is streamed into the temp block at the retail offset");
    Expect(DB_GetStreamPos() == zone.virt + 22, "block 4 advances by the filename");

    const SndCurve *const steps = Load(kInline);
    Expect(steps == &g_pool[1], "the second record publishes the second pool entry");
    if (steps != &g_pool[1])
        return;
    Expect(steps->knotCount == 8 && SameKnots(*steps, kSteps) && Is(zone, steps->filename, "sound/falloff/steps")
               && g_read == g_file.size(),
           "an eight-knot curve converts and every disk byte is consumed");
}

void TestSharedInlineAndOffsets()
{
    Zone zone;
    File()
        .Record(kInline, 3, kFalloff).Text("sound/falloff/shared") // after the 4-byte alias slot
        .Record(VirtualOffset(4), 8, kSteps);
    const SndCurve *const shared = Load(disk32::kSharedInline);
    Expect(shared == &g_pool[0] && Is(zone, g_pool[0].filename, "sound/falloff/shared"),
           "shared-inline curve publishes");
    if (shared != &g_pool[0])
        return;
    Expect(reinterpret_cast<std::uintptr_t>(shared) > UINT32_MAX,
           "the pool lies above 4 GiB, so a narrowed pointer would differ");
    Expect(Load(VirtualOffset(0)) == shared, "an alias token resolves to the full native pointer");
    const SndCurve *const second = Load(kInline);
    Expect(second == &g_pool[1] && g_read == g_file.size(), "the second record streams after the first");
    if (second != &g_pool[1])
        return;
    Expect(second->filename == shared->filename && SameKnots(*second, kSteps),
           "a filename offset token resolves to the earlier string; the knots are the second record's");
    Expect(!Load(0) && g_published == 2, "a null token loads nothing");
}

// kFalloff with one knot changed.
Knots With(std::initializer_list<float> knot, int index)
{
    Knots knots = kFalloff;
    knots.values[index][0] = *knot.begin();
    knots.values[index][1] = *(knot.begin() + 1);
    return knots;
}

struct Malformed
{
    const char *what;
    void (*build)();
    std::uintptr_t slot;
    const char *error;
};

void RunOff()
{
    for (int i = 0; i < 30; ++i)
        File().Word(0x42424242);
}

const Malformed kMalformed[] = {
    {"truncated record", [] { File().Record(kInline, 3, kFalloff); g_file.resize(70); }, kInline, "ended unexpectedly"},
    {"null filename", [] { File().Record(0, 3, kFalloff); }, kInline, "no name"},
    {"unmapped filename offset", [] { File().Record(VirtualOffset(40), 3, kFalloff); }, kInline, "string offset"},
    {"shared-inline filename", [] { File().Record(disk32::kSharedInline, 3, kFalloff).Text("a"); },
     kInline, "string offset"},
    {"unterminated filename", [] { File().Record(kInline, 3, kFalloff); RunOff(); }, kInline, "Unterminated"},
    {"one knot", [] { File().Record(kInline, 1, kFalloff).Text("a"); }, kInline, "Invalid fast-file sound curve"},
    {"nine knots", [] { File().Record(kInline, 9, kSteps).Text("a"); }, kInline, "Invalid fast-file sound curve"},
    {"negative knot count", [] { File().Record(kInline, -2, kFalloff).Text("a"); },
     kInline, "Invalid fast-file sound curve"},
    {"first x not 0", [] { File().Record(kInline, 3, With({0.25f, 1.0f}, 0)).Text("a"); },
     kInline, "Invalid fast-file sound curve"},
    {"last x not 1", [] { File().Record(kInline, 3, With({0.75f, 0.0f}, 2)).Text("a"); },
     kInline, "Invalid fast-file sound curve"},
    {"x not increasing", [] { File().Record(kInline, 3, With({0.0f, 0.25f}, 1)).Text("a"); },
     kInline, "Invalid fast-file sound curve"},
    {"y above 1", [] { File().Record(kInline, 3, With({0.5f, 1.5f}, 1)).Text("a"); },
     kInline, "Invalid fast-file sound curve"},
    {"NaN y", [] { File().Record(kInline, 3, With({0.5f, std::numeric_limits<float>::quiet_NaN()}, 1)).Text("a"); },
     kInline, "Invalid fast-file sound curve"},
    {"unmapped alias", [] {}, VirtualOffset(16), "alias offset"},
    {"slot wider than a token", [] {}, std::uintptr_t{1} << 32, "no disk32 token"},
};

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone;
        test.build();
        ExpectDrop(test.what, test.error, [&] { Load(test.slot); });
    }
}
} // namespace

void __cdecl Load_SndCurveAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    SndCurve &entry = g_pool[g_published++];
    entry = *header->sndCurve;
    Expect(entry.filename && entry.filename[0] != '\0', "a published curve has a filename");
    header->sndCurve = &entry;
}

int main()
{
    return Run({TestInlineCurves, TestSharedInlineAndOffsets, TestMalformedFailsClosed});
}
