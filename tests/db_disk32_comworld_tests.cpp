// db_disk32_comworld_tests.cpp: the 64-bit ComWorld loader (NOW row 12) on
// hand-built disk32 zone images (disk32_fixture.hpp). Beyond the fixture's
// seams, only the asset pool (Load_ComWorldAsset) is replaced; the primary
// lights live in the fixture's native storage.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>

#include <cstring>

namespace
{
using namespace disk32_test;

ComWorld g_pool[4]; // what Load_ComWorldAsset published

// A light's scalars. Every value differs from every other, in both lights,
// so a field read at a wrong offset or from the wrong element reads a wrong value.
struct Light
{
    std::uint8_t bytes[4]; // type, canUseShadowMap, exponent, unused
    float color[3];
    float dir[3];
    float origin[3];
    float radius;
    float cosHalfFov[3]; // outer, inner, expanded
    float rotationLimit;
    float translationLimit;
};

constexpr Light kSun{{1, 2, 3, 4}, {0.5f, 0.25f, 0.125f}, {1.5f, 2.5f, 3.5f}, {10.f, 20.f, 30.f}, 400.f,
                     {0.75f, 0.875f, 0.9375f}, 5.5f, 6.5f};
constexpr Light kSpot{{5, 6, 7, 8}, {0.0625f, 0.03125f, 0.015625f}, {4.5f, 5.25f, 6.75f}, {40.f, 50.f, 60.f}, 700.f,
                      {0.625f, 0.5625f, 0.53125f}, 7.25f, 8.25f};

struct File : FileBuilder<File>
{
    // The 16-byte retail record.
    File &Record(std::uint32_t name, std::int32_t isInUse, std::uint32_t lightCount, std::uint32_t lights)
    {
        return Word(name).Word(static_cast<std::uint32_t>(isInUse)).Word(lightCount).Word(lights);
    }
    // One 68-byte retail ComPrimaryLight.
    File &Light(const ::Light &light, std::uint32_t defName)
    {
        Word(static_cast<std::uint32_t>(light.bytes[0]) | light.bytes[1] << 8 | light.bytes[2] << 16
             | static_cast<std::uint32_t>(light.bytes[3]) << 24);
        for (float value : light.color)
            Float(value);
        for (float value : light.dir)
            Float(value);
        for (float value : light.origin)
            Float(value);
        Float(light.radius);
        for (float value : light.cosHalfFov)
            Float(value);
        return Float(light.rotationLimit).Float(light.translationLimit).Word(defName);
    }
};

// A zone with the two blocks a ComWorld touches: temp (0) and virtual (4).
using Zone = disk32_test::Zone<256>;

ComWorld *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadComWorldPtrDisk32, slotValue);
}

bool Is(const Zone &zone, const char *text, const char *expected)
{
    return zone.Holds(text) && !std::strcmp(text, expected);
}

bool Matches(const ComPrimaryLight &native, const Light &light)
{
    return native.type == light.bytes[0] && native.canUseShadowMap == light.bytes[1]
        && native.exponent == light.bytes[2] && native.unused == light.bytes[3]
        && !std::memcmp(native.color, light.color, sizeof(light.color))
        && !std::memcmp(native.dir, light.dir, sizeof(light.dir))
        && !std::memcmp(native.origin, light.origin, sizeof(light.origin)) && native.radius == light.radius
        && native.cosHalfFovOuter == light.cosHalfFov[0] && native.cosHalfFovInner == light.cosHalfFov[1]
        && native.cosHalfFovExpanded == light.cosHalfFov[2] && native.rotationLimit == light.rotationLimit
        && native.translationLimit == light.translationLimit;
}

void TestInlineWorld()
{
    Zone zone;
    // name 0..15; the two lights 16..152; then the second light's defName.
    File().Record(kInline, 1, 2, kInline).Text("maps/mp/mp_bog").Light(kSun, 0).Light(kSpot, kInline).Text("spot");
    const ComWorld *const world = Load(kInline);
    Expect(world == &g_pool[0] && g_published == 1, "inline world publishes one pool entry");
    if (world != &g_pool[0])
        return;
    Expect(Is(zone, world->name, "maps/mp/mp_bog") && world->name == reinterpret_cast<char *>(zone.virt),
           "name streams first and points at its bytes in block 4");
    Expect(world->isInUse == 1 && world->primaryLightCount == 2, "the scalars convert from their retail offsets");
    Expect(InArena(world->primaryLights) && g_arenaUsed == 2 * sizeof(ComPrimaryLight),
           "the two native 72-byte lights live in native storage, above 4 GiB");
    if (!InArena(world->primaryLights))
        return;
    Expect(Matches(world->primaryLights[0], kSun) && Matches(world->primaryLights[1], kSpot),
           "every light field converts from its retail offset in its own element");
    Expect(!world->primaryLights[0].defName && Is(zone, world->primaryLights[1].defName, "spot")
               && world->primaryLights[1].defName == reinterpret_cast<char *>(zone.virt + 16 + 2 * 68),
           "a null defName stays null and an inline one follows the whole array in block 4");
    Expect(!std::memcmp(zone.virt + 16, g_file.data() + 16 + 15, 2 * 68),
           "the disk32 lights stay at their retail block-4 offset");
    Expect(!std::memcmp(zone.temp, g_file.data(), sizeof(disk32::ComWorldDisk32)),
           "the disk32 record is streamed into the temp block at the retail offset");
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 16 + 2 * 68 + 5,
           "every disk byte is consumed and block 4 advances by the retail extent");
}

void TestEmptyAndAbsentLights()
{
    Zone zone;
    File().Record(kInline, 0, 0, kInline).Text("maps/mp/mp_empty"); // name 0..17
    File().Record(kInline, 0, 0, 0).Text("maps/mp/mp_none");          // name 20..36
    const ComWorld *const empty = Load(kInline);
    const ComWorld *const none = Load(kInline);
    Expect(empty == &g_pool[0] && none == &g_pool[1], "both worlds publish");
    if (empty != &g_pool[0] || none != &g_pool[1])
        return;
    Expect(empty->primaryLights == reinterpret_cast<ComPrimaryLight *>(zone.virt + 20) && !empty->primaryLightCount,
           "an empty inline array still points at its 4-aligned stream position, as on x86");
    Expect(!none->primaryLights && g_arenaUsed == 0 && g_read == g_file.size(),
           "a null array stays null and neither world takes native storage");
}

void TestSharedInlineAndOffsets()
{
    Zone zone;
    File()
        .Record(kInline, 1, 1, kInline).Text("mp_shared") // name 4..14, after the 4-byte alias slot
        .Light(kSun, kInline).Text("sun")                 // light 16..84, defName 84..88
        .Record(VirtualOffset(4), 0, 1, kInline)
        .Light(kSpot, VirtualOffset(84));                 // light 88..156
    const ComWorld *const shared = Load(disk32::kSharedInline);
    Expect(shared == &g_pool[0] && Is(zone, g_pool[0].name, "mp_shared"), "shared-inline world publishes");
    if (shared != &g_pool[0])
        return;
    Expect(reinterpret_cast<std::uintptr_t>(shared) > UINT32_MAX,
           "the pool lies above 4 GiB, so a narrowed pointer would differ");
    Expect(Load(VirtualOffset(0)) == shared, "an alias token resolves to the full native pointer");
    const ComWorld *const second = Load(kInline);
    Expect(second == &g_pool[1] && g_read == g_file.size(), "the second record streams after the first");
    if (second != &g_pool[1] || !InArena(second->primaryLights) || !InArena(shared->primaryLights))
        return;
    Expect(second->name == shared->name && second->primaryLights[0].defName == shared->primaryLights[0].defName,
           "name and defName offset tokens resolve to the earlier strings");
    Expect(Matches(second->primaryLights[0], kSpot) && second->primaryLights != shared->primaryLights,
           "each world's lights are its own");
    Expect(!Load(0) && g_published == 2, "a null token loads nothing");
}

struct Malformed
{
    const char *what;
    void (*build)();
    std::uintptr_t slot;
    const char *error;
    std::size_t arena = kArenaBytes;
};

void RunOff()
{
    for (int i = 0; i < 70; ++i) // 280 bytes: past the 256-byte block
        File().Word(0x42424242);
}

const Malformed kMalformed[] = {
    {"truncated record", [] { File().Word(kInline).Word(1); }, kInline, "ended unexpectedly"},
    {"null name", [] { File().Record(0, 1, 0, 0); }, kInline, "no name"},
    {"unmapped name offset", [] { File().Record(VirtualOffset(40), 1, 0, 0); }, kInline, "string offset"},
    {"unterminated name", [] { File().Record(kInline, 1, 0, 0); RunOff(); }, kInline, "Unterminated"},
    {"negative light count", [] { File().Record(kInline, 1, 0x80000000u, kInline).Text("w"); },
     kInline, "primary-light count"},
    {"light bytes overflow", [] { File().Record(kInline, 1, 0x02000000u, kInline).Text("w"); },
     kInline, "primary-light count"},
    {"lights past their block", [] { File().Record(kInline, 1, 4, kInline).Text("w"); RunOff(); },
     kInline, "exceeds stream block"},
    {"truncated lights", [] { File().Record(kInline, 1, 2, kInline).Text("w").Light(kSun, 0); },
     kInline, "ended unexpectedly"},
    {"unmapped defName offset", [] { File().Record(kInline, 1, 1, kInline).Text("w").Light(kSun, VirtualOffset(200)); },
     kInline, "string offset"},
    {"unterminated defName", [] { File().Record(kInline, 1, 1, kInline).Text("w").Light(kSun, kInline); RunOff(); },
     kInline, "Unterminated"},
    {"native storage exhausted",
     [] { File().Record(kInline, 1, 2, kInline).Text("w").Light(kSun, 0).Light(kSpot, 0); },
     kInline, "exhausted", sizeof(ComPrimaryLight)},
    {"unmapped alias", [] {}, VirtualOffset(16), "alias offset"},
    {"slot wider than a token", [] {}, std::uintptr_t{1} << 32, "no disk32 token"},
};

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone;
        g_arenaCapacity = test.arena;
        test.build();
        ExpectDrop(test.what, test.error, [&] { Load(test.slot); });
    }
}
} // namespace

void __cdecl Load_ComWorldAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    ComWorld &entry = g_pool[g_published++];
    entry = *header->comWorld;
    Expect(entry.name && entry.name[0] != '\0', "a published world has a name");
    header->comWorld = &entry;
}

int main()
{
    return Run({TestInlineWorld, TestEmptyAndAbsentLights, TestSharedInlineAndOffsets, TestMalformedFailsClosed});
}
