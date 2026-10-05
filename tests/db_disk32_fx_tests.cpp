// db_disk32_fx_tests.cpp: the 64-bit FX loader (NOW row 12) on hand-built
// disk32 zone images (disk32_fixture.hpp). Each image is described in the
// hand-written FX mirrors (fx_fastfile_disk32.h) and also run through the FX
// converter (fx_fastfile_native_disk32.cpp), the oracle the loader replaces
// (docs/design/FASTFILE_LOADER.md, "FX"): both must convert a well-formed
// effect alike and both must reject one that breaks a rule. Beyond the
// fixture's seams, only the asset pool (Load_FxEffectDefAsset) is replaced.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>

#include <EffectsCore/fx_fastfile_native_disk32.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace
{
using namespace disk32_test;
namespace ff = fx::fastfile;

FxEffectDef g_effects[4]; // what Load_FxEffectDefAsset published

using Zone = disk32_test::Zone<2048>;

// An effect as the hand-written mirrors describe it, with each element's samples.
struct Effect
{
    ff::FxEffectDefDisk32 record{{kInline}, 2, 32 + 8, 0, 0, 0, 0, {0}};
    std::string name = "fx_test";
    std::vector<ff::FxElemDefDisk32> elems;
    std::vector<std::vector<ff::FxElemVelStateSampleDisk32>> vel;
    std::vector<std::vector<ff::FxElemVisStateSampleDisk32>> vis;
};

// A visible element with two samples of each kind; ranges and bounds hold
// distinct values.
ff::FxElemDefDisk32 Element(ff::FxElemTypeDisk32 type, std::uint8_t visuals)
{
    ff::FxElemDefDisk32 elem{};
    float value = 1.f;
    for (ff::FxFloatRangeDisk32 *range : {&elem.spawnRange, &elem.fadeInRange, &elem.fadeOutRange,
                                          &elem.spawnOffsetRadius, &elem.spawnOffsetHeight, &elem.initialRotation,
                                          &elem.gravity, &elem.reflectionFactor, &elem.emitDist,
                                          &elem.emitDistVariance, &elem.spawnOrigin[2], &elem.spawnAngles[1],
                                          &elem.angularVelocity[0]})
        *range = {value++, value++};
    elem.flags = 0x41;
    elem.spawnFrustumCullRadius = 7.f;
    elem.lifeSpanMsec = {1000, 50};
    elem.spawnDelayMsec = {-20, 10};
    elem.elemType = type;
    elem.visualCount = visuals;
    elem.velIntervalCount = 1;
    elem.visStateIntervalCount = 1;
    elem.velSamples.token = {kInline};
    elem.visSamples.token = {kInline};
    elem.collMaxs[2] = 3.f;
    elem.sortOrder = 5;
    elem.lightingFrac = 6;
    elem.useItemClip = 1;
    return elem;
}

// A looping omni light spawning 3 times 100 ms apart, then a one-shot sprite
// with no visuals spawning 1..3 times.
Effect TwoElements()
{
    Effect effect;
    effect.name = "fx_two"; // 7 bytes: the elements start 4-aligned after a pad byte
    // FX_Convert's size: the record, elements, samples and name.
    effect.record = {{kInline}, 0x10, 32 + 2 * 252 + 2 * (2 * 96 + 2 * 48) + 7, 200, 1, 1, 0, {kInline}};
    effect.elems = {Element(ff::FxElemTypeDisk32::OmniLight, 1), Element(ff::FxElemTypeDisk32::SpriteBillboard, 0)};
    effect.elems[0].spawn = {100, 3};
    effect.elems[1].spawn = {1, 2};
    for (std::size_t index = 0; index < effect.elems.size(); ++index)
    {
        effect.vel.emplace_back(2);
        effect.vis.emplace_back(2);
        effect.vel[index][1].world.totalDelta.amplitude[2] = 8.f + static_cast<float>(index);
        effect.vis[index][1].amplitude.scale = 9.f + static_cast<float>(index);
    }
    return effect;
}

struct File : FileBuilder<File>
{
    template <typename T>
    File &Bytes(const T &value)
    {
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(&value);
        g_file.insert(g_file.end(), bytes, bytes + sizeof(value));
        return *this;
    }
    template <typename T>
    File &All(const std::vector<T> &values)
    {
        for (const T &value : values)
            Bytes(value);
        return *this;
    }
    // The record, its inline name, its elements, then each element's samples.
    File &Write(const Effect &e)
    {
        Bytes(e.record);
        if (e.record.name.token.isInline())
            Text(e.name);
        if (!e.record.elemDefs.token.isNull())
            All(e.elems);
        for (std::size_t index = 0; index < e.elems.size(); ++index)
        {
            if (!e.elems[index].velSamples.token.isNull())
                All(e.vel[index]);
            if (!e.elems[index].visSamples.token.isNull())
                All(e.vis[index]);
        }
        return *this;
    }
};

const FxEffectDef *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadFxEffectDefHandleDisk32, slotValue);
}

// TestRecord's scalars, and no elements in no native storage.
bool Converted(const FxEffectDef &effect)
{
    return effect.flags == 0x10 && effect.totalSize == 40 && effect.msecLoopingLife == 0
        && !effect.elemDefCountLooping && !effect.elemDefs && g_arenaUsed == 0;
}

void TestRecord()
{
    Zone zone;
    Effect effect;
    effect.record = {{kInline}, 0x10, 32 + 8, 0, 0, 0, 0, {0}};
    File().Write(effect);
    const FxEffectDef *const loaded = Load(kInline);
    Expect(loaded == &g_effects[0] && g_published == 1, "an inline effect publishes one pool entry");
    if (loaded != &g_effects[0])
        return;
    Expect(zone.Holds(loaded->name) && loaded->name == zone.At(0) && !std::strcmp(loaded->name, "fx_test"),
           "the name points at its bytes in block 4");
    Expect(Converted(*loaded), "the record's scalars convert, and no elements take no native storage");
    Expect(!std::memcmp(zone.temp, g_file.data(), sizeof(effect.record)),
           "the disk32 record is streamed into the temp block at the retail offset");
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 8,
           "every disk byte is consumed and block 4 advances by the name");
}

// The oracle: the FX converter on the same records, resolving the name to
// its own copy.
struct Oracle
{
    std::unique_ptr<ff::FxFastFileNativeDisk32Workspace> workspace =
        std::make_unique<ff::FxFastFileNativeDisk32Workspace>();
    std::vector<std::uint64_t> storage;
    FxEffectDef *effect = nullptr;

    static bool Resolve(void *context, ff::FxFastFileDisk32ReferenceKind, const disk32::PointerToken *,
                        disk32::PointerToken, ff::FxFastFileDisk32ResolvedReference *out) noexcept
    {
        const auto *const name = static_cast<const std::string *>(context);
        *out = {};
        out->pointer = name->c_str();
        out->retainedByteCount = name->size() + 1;
        out->retainedAlignment = 1;
        return true;
    }
    static bool Attest(void *, ff::FxFastFileDisk32SourceSpanKind, const disk32::PointerToken *,
                       disk32::PointerToken, const void *, std::uint64_t, std::size_t) noexcept
    {
        return true;
    }

    explicit Oracle(const Effect &e)
    {
        std::vector<ff::FxFastFileElemDefDisk32View> views(e.elems.size());
        for (std::size_t index = 0; index < e.elems.size(); ++index)
        {
            const auto count = [](const auto &values) { return static_cast<std::uint32_t>(values.size()); };
            views[index].velocitySamples = {e.vel[index].empty() ? nullptr : e.vel[index].data(), count(e.vel[index])};
            views[index].visibilitySamples = {e.vis[index].empty() ? nullptr : e.vis[index].data(),
                                              count(e.vis[index])};
        }
        const auto elements = static_cast<std::uint32_t>(e.elems.size());
        ff::FxFastFileEffectDefDisk32View view{&e.record, {elements ? e.elems.data() : nullptr, elements},
                                               {elements ? views.data() : nullptr, elements}, {nullptr, Attest}};
        const ff::FxFastFileDisk32Resolvers resolvers{const_cast<std::string *>(&e.name), Resolve};
        ff::FxFastFileNativeDisk32Plan plan;
        if (ff::TryPlanFxEffectDefDisk32(workspace.get(), view, resolvers, &plan)
            != ff::FxFastFileNativeDisk32Status::Success)
            return;
        storage.resize(plan.outputBytes() / sizeof(std::uint64_t) + 1);
        if (ff::TryMaterializeFxEffectDefDisk32(workspace.get(), plan, storage.data(),
                                                storage.size() * sizeof(std::uint64_t), &effect)
            != ff::FxFastFileNativeDisk32Status::Success)
            effect = nullptr;
    }
};

// An element's bytes with its pointers nulled, padding included.
std::vector<std::uint8_t> Scalars(const FxElemDef &elem)
{
    FxElemDef copy;
    std::memcpy(&copy, &elem, sizeof(copy));
    copy.velSamples = nullptr;
    copy.visSamples = nullptr;
    copy.visuals.markArray = nullptr;
    copy.effectOnImpact.handle = copy.effectOnDeath.handle = copy.effectEmitted.handle = nullptr;
    copy.trailDef = nullptr;
    const auto *bytes = reinterpret_cast<const std::uint8_t *>(&copy);
    return {bytes, bytes + sizeof(copy)};
}

// The record's fields but totalSize, which the converter sets to its own.
bool SameRecord(const FxEffectDef &loaded, const FxEffectDef &expected)
{
    return !std::strcmp(loaded.name, expected.name) && loaded.flags == expected.flags
        && loaded.msecLoopingLife == expected.msecLoopingLife
        && loaded.elemDefCountLooping == expected.elemDefCountLooping
        && loaded.elemDefCountOneShot == expected.elemDefCountOneShot
        && loaded.elemDefCountEmission == expected.elemDefCountEmission && !loaded.elemDefs == !expected.elemDefs;
}

// The loader's effect matches the converter's: its fields, each element's
// scalars and samples.
bool MatchesOracle(const FxEffectDef &loaded, const Oracle &oracle)
{
    const FxEffectDef *const expected = oracle.effect;
    if (!expected || !SameRecord(loaded, *expected))
        return false;
    const int count = loaded.elemDefCountLooping + loaded.elemDefCountOneShot + loaded.elemDefCountEmission;
    for (int index = 0; index < count; ++index)
    {
        const FxElemDef &mine = loaded.elemDefs[index];
        const FxElemDef &theirs = expected->elemDefs[index];
        if (Scalars(mine) != Scalars(theirs)
            || std::memcmp(mine.velSamples, theirs.velSamples, (mine.velIntervalCount + 1u) * sizeof(*mine.velSamples))
            || std::memcmp(mine.visSamples, theirs.visSamples,
                           (mine.visStateIntervalCount + 1u) * sizeof(*mine.visSamples)))
            return false;
    }
    return true;
}

// Each native scalar run holds the bytes at its retail offset: flags through
// the interval counts, the bounds, the emit ranges and the sort bytes.
bool Converted(const FxElemDef &native, const ff::FxElemDefDisk32 &disk)
{
    const auto same = [&](std::size_t at, std::size_t from, std::size_t bytes) {
        return !std::memcmp(reinterpret_cast<const std::uint8_t *>(&native) + at,
                            reinterpret_cast<const std::uint8_t *>(&disk) + from, bytes);
    };
    using D = ff::FxElemDefDisk32;
    return same(0, 0, offsetof(D, velSamples)) && same(offsetof(FxElemDef, collMins), offsetof(D, collMins), 24)
        && same(offsetof(FxElemDef, emitDist), offsetof(D, emitDist), 16)
        && same(offsetof(FxElemDef, sortOrder), offsetof(D, sortOrder), 4);
}

// The spawns, samples and absent visuals TestElementsAndSamples writes.
void ExpectElements(const Zone &zone, const FxElemDef &light, const FxElemDef &sprite)
{
    Expect(light.spawn.looping.intervalMsec == 100 && light.spawn.looping.count == 3
               && sprite.spawn.oneShot.count.base == 1 && sprite.spawn.oneShot.count.amplitude == 2,
           "each element's spawn converts");
    Expect(!light.visuals.instance.anonymous && !sprite.visuals.instance.anonymous && !light.trailDef,
           "the elements name no visuals or trail");
    Expect(reinterpret_cast<const std::uint8_t *>(light.velSamples) == zone.virt + 512
               && reinterpret_cast<const std::uint8_t *>(light.visSamples) == zone.virt + 704
               && reinterpret_cast<const std::uint8_t *>(sprite.velSamples) == zone.virt + 800
               && reinterpret_cast<const std::uint8_t *>(sprite.visSamples) == zone.virt + 992,
           "the samples stay at their 4-aligned retail block-4 offsets, after the elements");
    Expect(sprite.velSamples[1].world.totalDelta.amplitude[2] == 9.f && sprite.visSamples[1].amplitude.scale == 10.f,
           "the samples keep their layout");
}

void TestElementsAndSamples()
{
    Zone zone;
    const Effect effect = TwoElements();
    File().Write(effect);
    const FxEffectDef *const loaded = Load(kInline);
    Expect(loaded == &g_effects[0] && InArena(loaded->elemDefs) && g_arenaUsed == 2 * sizeof(FxElemDef),
           "the two native 288-byte elements live in native storage, above 4 GiB");
    if (loaded != &g_effects[0] || !InArena(loaded->elemDefs))
        return;
    const FxElemDef &light = loaded->elemDefs[0];
    const FxElemDef &sprite = loaded->elemDefs[1];
    Expect(Converted(light, effect.elems[0]) && Converted(sprite, effect.elems[1]),
           "every scalar converts from its retail offset");
    Expect(!std::memcmp(zone.virt + 8, effect.elems.data(), 2 * sizeof(ff::FxElemDefDisk32)),
           "the disk32 elements stay at their 4-aligned retail block-4 offset");
    ExpectElements(zone, light, sprite);
    Expect(MatchesOracle(*loaded, Oracle(effect)), "the effect matches the FX converter's on the same records");
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 1088,
           "every disk byte is consumed and block 4 advances by the retail extent");
}

void TestSharedInlineAndOffsets()
{
    Zone zone;
    Effect effect;
    File().Write(effect);
    effect.record.name.token = {VirtualOffset(4)};
    File().Write(effect);
    const FxEffectDef *const shared = Load(disk32::kSharedInline);
    Expect(shared == &g_effects[0] && reinterpret_cast<std::uintptr_t>(shared) > UINT32_MAX,
           "a shared-inline effect publishes into the pool, above 4 GiB");
    Expect(shared && Load(VirtualOffset(0)) == shared, "an alias token resolves to the full native pointer");
    const FxEffectDef *const second = Load(kInline);
    Expect(second == &g_effects[1] && shared && second->name == shared->name,
           "a name offset token resolves to the earlier string");
    Expect(shared && MatchesOracle(*shared, Oracle(Effect{})), "an empty effect matches the FX converter's");
    Expect(!Load(0) && g_published == 2, "a null token loads nothing");
}

struct Malformed
{
    const char *what;
    void (*edit)(Effect &);
    const char *error;
    std::uint32_t tempBytes = 512;
    std::size_t arena = kArenaBytes;
};

constexpr const char *kNotYet = "no 64-bit loader yet";
constexpr const char *kElement = "effect element";

// The sprite, given one visual and the one-entry atlas that needs.
ff::FxElemDefDisk32 &Visible(Effect &e)
{
    e.elems[1].visualCount = 1;
    e.elems[1].atlas.entryCount = 1;
    e.elems[1].visuals.token = {kInline};
    return e.elems[1];
}

// Rule breaks: the converter must reject each too.
const Malformed kRuleBreaks[] = {
    {"a negative count", [](Effect &e) { e.record.elemDefCountEmission = -1; }, "effect header"},
    {"257 elements", [](Effect &e) { e.record = {{kInline}, 0, 0, 0, 1, 1, 255, {kInline}}; }, "effect header"},
    {"counts that wrap 32 bits",
     [](Effect &e) { e.record = {{kInline}, 0, 0, 0, INT32_MAX, INT32_MAX, 2, {0}}; }, "effect header"},
    {"elements with a null token", [](Effect &e) { e.record.elemDefs.token = {0}; }, "effect header"},
    {"no elements, but a token", [](Effect &e) { e.record.elemDefCountLooping = e.record.elemDefCountOneShot = 0; },
     "effect header"},
    {"a null name", [](Effect &e) { e.record.name.token = {0}; }, "has no name"},
    {"element type 11", [](Effect &e) { e.elems[0].elemType = ff::FxElemTypeDisk32::Count;
                                        e.elems[0].visualCount = 0; }, kElement},
    {"a lifespan of no time", [](Effect &e) { e.elems[0].lifeSpanMsec = {0, 0}; }, kElement},
    {"a delay amplitude past 32767", [](Effect &e) { e.elems[0].spawnDelayMsec.amplitude = 32768; }, kElement},
    {"a delay past a day", [](Effect &e) { e.elems[1].spawnDelayMsec = {86'400'000, 1}; }, kElement},
    {"a looping interval of 0", [](Effect &e) { e.elems[0].spawn = {0, 3}; }, kElement},
    {"a last spawn past a day", [](Effect &e) { e.elems[0].spawn = {86'400'000, 3}; }, kElement},
    {"a one-shot count past the pool", [](Effect &e) { e.elems[1].spawn = {2048, 1}; }, kElement},
    {"an atlas on a light", [](Effect &e) { e.elems[0].atlas.fps = 1; }, kElement},
    {"an atlas of 3 entries", [](Effect &e) { Visible(e).atlas = {0, 0, 0, 0, 1, 1, 3}; }, kElement},
    {"an atlas rate past 32 bits", [](Effect &e) { Visible(e).atlas = {0, 0, 255, 0, 0, 0, 1};
                                                   e.elems[1].lifeSpanMsec = {9'000'000, 0}; }, kElement},
    {"no velocity samples", [](Effect &e) { e.elems[0].velSamples.token = {0}; }, kElement},
    {"no velocity intervals", [](Effect &e) { e.elems[0].velIntervalCount = 0; }, kElement},
    {"a sprite without visual samples", [](Effect &e) { e.elems[1].visSamples.token = {0}; }, kElement},
    {"a runner with visual samples", [](Effect &e) { e.elems[0].elemType = ff::FxElemTypeDisk32::Runner; }, kElement},
    {"a light of two visuals", [](Effect &e) { e.elems[0].visualCount = 2; }, kElement},
    {"a light naming a visual", [](Effect &e) { e.elems[0].visuals.token = {kInline}; }, kElement},
    {"a sprite of no visuals naming one", [](Effect &e) { e.elems[1].visuals.token = {kInline}; }, kElement},
    {"a sprite of 33 visuals", [](Effect &e) { Visible(e).visualCount = 33; }, kElement},
    {"a decal of 17 visuals", [](Effect &e) { Visible(e).elemType = ff::FxElemTypeDisk32::Decal;
                                              e.elems[1].visualCount = 17; }, kElement},
    {"a sprite with a trail", [](Effect &e) { e.elems[1].trailDef.token = {kInline}; }, kElement},
    {"a one-shot trail", [](Effect &e) { e.elems[1].elemType = ff::FxElemTypeDisk32::Trail;
                                         e.elems[1].trailDef.token = {kInline}; }, kElement},
    {"a wrong looping life", [](Effect &e) { e.record.msecLoopingLife = 300; }, "looping life"},
    {"a wrong size", [](Effect &e) { e.record.totalSize += 4; }, "effect size"},
};

const Malformed kMalformed[] = {
    {"a visual", [](Effect &e) { Visible(e); }, kNotYet},
    {"an effect on impact", [](Effect &e) { e.elems[0].effectOnImpact.token = {kInline}; }, kNotYet},
    {"an effect on death", [](Effect &e) { e.elems[1].effectOnDeath.token = {VirtualOffset(0)}; }, kNotYet},
    {"an emitted effect", [](Effect &e) { e.elems[0].effectEmitted.token = {kInline}; }, kNotYet},
    {"a trail", [](Effect &e) { e.elems[0].elemType = ff::FxElemTypeDisk32::Trail;
                                e.elems[0].visualCount = 0;
                                e.elems[0].trailDef.token = {kInline}; }, kNotYet},
    {"an unmapped name offset", [](Effect &e) { e.record.name.token = {VirtualOffset(40)}; }, "string offset"},
    {"a name past its block", [](Effect &e) { e.name.assign(2100, 'n'); }, "Unterminated"},
    {"elements past their block", [](Effect &e) { e.name.assign(1700, 'n'); }, "exceeds stream block"},
    {"samples past their block", [](Effect &e) { e.name.assign(1100, 'n'); }, "exceeds stream block"},
    {"native storage exhausted", [](Effect &) {}, "exhausted", 512, sizeof(FxElemDef)},
    {"a record past the temp block", [](Effect &) {}, "exceeds stream block", 16},
};

void TestRuleBreaksFailClosed()
{
    for (const Malformed &test : kRuleBreaks)
    {
        Zone zone;
        Effect effect = TwoElements();
        test.edit(effect);
        File().Write(effect);
        ExpectDrop(test.what, test.error, [] { Load(kInline); });
        Expect(!Oracle(effect).effect, test.what, "is accepted by the FX converter");
    }
}

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone(test.tempBytes);
        g_arenaCapacity = test.arena;
        Effect effect = TwoElements();
        test.edit(effect);
        File().Write(effect);
        ExpectDrop(test.what, test.error, [] { Load(kInline); });
    }
    for (std::size_t cut : {std::size_t{40}, std::size_t{600}, std::size_t{1100}})
    {
        Zone zone;
        File().Write(TwoElements());
        g_file.resize(g_file.size() - cut); // in the samples, the elements, the record
        ExpectDrop("a truncated effect", "ended unexpectedly", [] { Load(kInline); });
    }
    Zone zone;
    ExpectDrop("an unmapped alias", "alias offset", [] { Load(VirtualOffset(16)); });
    ExpectDrop("an alias token in the temp block", "alias offset", [] { Load(1); });
    ExpectDrop("a slot wider than a token", "no disk32 token", [] { Load(std::uintptr_t{1} << 32); });
}
} // namespace

void __cdecl Load_FxEffectDefAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    FxEffectDef &entry = g_effects[g_published++];
    entry = *header->fx;
    Expect(entry.name && entry.name[0] != '\0', "a published effect has a name");
    header->fx = &entry;
}

int main()
{
    return Run({TestRecord, TestElementsAndSamples, TestSharedInlineAndOffsets, TestRuleBreaksFailClosed,
                TestMalformedFailsClosed});
}
