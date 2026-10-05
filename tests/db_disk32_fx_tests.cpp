// db_disk32_fx_tests.cpp: the 64-bit FX loader (NOW row 12) on hand-built
// disk32 zone images (disk32_fixture.hpp). Each image is described in the
// hand-written FX mirrors (fx_fastfile_disk32.h) and also run through the FX
// converter (fx_fastfile_native_disk32.cpp), the oracle the loader replaces
// (docs/design/FASTFILE_LOADER.md, "FX"): both must convert a well-formed
// effect alike and both must reject one that breaks a rule. Beyond the
// fixture's seams, only the asset pools and the effect lookup by name
// (Load_FxEffectDefFromName) are replaced; materials load through Material's
// real step, and models resolve through XModel's.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>
#include <database/db_load_legacy_bridge.h>

#include <EffectsCore/fx_fastfile_native_disk32.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
using namespace disk32_test;
namespace ff = fx::fastfile;

FxEffectDef g_effects[4]; // what Load_FxEffectDefAsset published
FxEffectDef g_named;      // what every effect name resolves to
std::vector<std::string> g_lookups; // the names Load_FxEffectDefFromName looked up
Material g_materials[2];  // what Load_MaterialAsset published
int g_materialCount = 0;
MaterialTechniqueSet g_sets[2];
int g_setCount = 0;
Material g_aliasMaterial; // the earlier zone assets RegisterAliases names
XModel g_model;

using Zone = disk32_test::Zone<2048>;

// An effect as the hand-written mirrors describe it, with each element's samples.
struct Effect
{
    ff::FxEffectDefDisk32 record{{kInline}, 2, 32 + 8, 0, 0, 0, 0, {0}};
    std::string name = "fx_test";
    std::vector<ff::FxElemDefDisk32> elems;
    std::vector<std::vector<ff::FxElemVelStateSampleDisk32>> vel;
    std::vector<std::vector<ff::FxElemVisStateSampleDisk32>> vis;
    // Each element's mark pairs or visual tokens past one, then the bytes its
    // visuals and effect names stream (materials, names).
    std::vector<std::vector<ff::FxElemMarkVisualsDisk32>> marks;
    std::vector<std::vector<ff::FxElemVisualsDisk32>> visuals;
    std::vector<std::vector<std::uint8_t>> tails;
    // The first element's trail, if it names one: its record, vertices and indices.
    ff::FxTrailDefDisk32 trail{};
    std::vector<ff::FxTrailVertexDisk32> trailVerts;
    std::vector<std::uint16_t> trailInds;
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
            if (index < e.tails.size())
                All(e.marks[index]).All(e.visuals[index]).All(e.tails[index]);
            if (!index && !e.elems[0].trailDef.token.isNull())
                Bytes(e.trail).All(e.trailVerts).All(e.trailInds);
        }
        return *this;
    }
};

// Block 4 starts with two aliases, as earlier assets of the zone leave them:
// a material at offset 0 and a model at 4. Nothing is published or looked up.
void RegisterAliases()
{
    g_materialCount = g_setCount = 0;
    g_lookups.clear();
    DB_SetInsertedPointer(DB_InsertPointer(DBAliasKind::Material), DBAliasKind::Material, &g_aliasMaterial);
    DB_SetInsertedPointer(DB_InsertPointer(DBAliasKind::XModel), DBAliasKind::XModel, &g_model);
}

// A minimal material, inline: no textures, constants or state bits, and an
// empty technique set.
std::vector<std::uint8_t> MaterialBytes()
{
    const std::size_t start = g_file.size();
    File file;
    file.Word(kInline).Word(0x07).Word(0).Word(0).Word(0).Word(0);
    for (int i = 0; i < 34; ++i)
        file.Bytes(std::uint8_t{0xFF});
    file.Word(0).Bytes(std::uint16_t{0}).Word(kInline).Word(0).Word(0).Word(0).Text("mat").Word(kInline).Word(0).Word(0);
    for (int i = 0; i < 34; ++i)
        file.Word(0);
    file.Text("ts");
    std::vector<std::uint8_t> bytes(g_file.begin() + static_cast<std::ptrdiff_t>(start), g_file.end());
    g_file.resize(start);
    return bytes;
}

std::vector<std::uint8_t> TextBytes(std::string_view text)
{
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    bytes.push_back(0);
    return bytes;
}

// Five one-shot elements, one of each kind of visual: a sprite with an
// inline material and an effect on impact, a decal whose marks name the
// material alias, a model element of two visuals naming the model alias, a
// runner and a sound element, each naming theirs inline.
Effect VisualEffect()
{
    using T = ff::FxElemTypeDisk32;
    Effect effect;
    effect.name = "fx_vis";
    effect.record = {{kInline}, 0, 32 + 5 * 252 + 5 * 192 + 4 * 96 + 8 + 8 + 7, 0, 0, 5, 0, {kInline}};
    effect.elems = {Element(T::SpriteBillboard, 1), Element(T::Decal, 1), Element(T::Model, 2),
                    Element(T::Runner, 1), Element(T::Sound, 1)};
    std::vector<std::uint8_t> sprite = MaterialBytes();
    const std::vector<std::uint8_t> hit = TextBytes("fx_hit");
    sprite.insert(sprite.end(), hit.begin(), hit.end());
    effect.tails = {sprite, {}, {}, TextBytes("fx_child"), TextBytes("snd_x")};
    ff::FxElemMarkVisualsDisk32 mark{};
    mark.materials[0].token = mark.materials[1].token = {VirtualOffset(0)};
    const ff::FxElemVisualsDisk32 model{{VirtualOffset(4)}};
    effect.marks = {{}, {mark}, {}, {}, {}};
    effect.visuals = {{}, {}, {model, model}, {}, {}};
    for (std::size_t index = 0; index < effect.elems.size(); ++index)
    {
        ff::FxElemDefDisk32 &elem = effect.elems[index];
        elem.spawn = {1, 0};
        elem.visuals.token = {kInline};
        effect.vel.emplace_back(2);
        effect.vis.emplace_back(elem.elemType == T::Runner ? 0 : 2);
    }
    effect.elems[0].atlas.entryCount = effect.elems[1].atlas.entryCount = 1;
    effect.elems[0].effectOnImpact.token = {kInline};
    effect.elems[3].visStateIntervalCount = 0;
    effect.elems[3].visSamples.token = {0};
    return effect;
}

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
// Each element's resolved references in the converter's order: its visuals
// (mark pairs, one visual, or the array), then the effects it names.
std::vector<const void *> References(const FxEffectDef &effect)
{
    std::vector<const void *> references;
    const int count = effect.elemDefCountLooping + effect.elemDefCountOneShot + effect.elemDefCountEmission;
    for (int index = 0; index < count; ++index)
    {
        const FxElemDef &elem = effect.elemDefs[index];
        const bool light = elem.elemType == 6 || elem.elemType == 7;
        for (int visual = 0; visual < elem.visualCount && !light; ++visual)
        {
            if (elem.elemType == 9)
                references.insert(references.end(), elem.visuals.markArray[visual].materials,
                                  elem.visuals.markArray[visual].materials + 2);
            else
                references.push_back(elem.visualCount == 1 ? elem.visuals.instance.anonymous
                                                           : elem.visuals.array[visual].anonymous);
        }
        for (const FxEffectDefRef &ref : {elem.effectOnImpact, elem.effectOnDeath, elem.effectEmitted})
            if (ref.handle)
                references.push_back(ref.handle);
    }
    return references;
}

// The oracle: the FX converter on the same records. Its resolver hands back
// the effect's name, then the given references in order, or stand-ins.
struct Oracle
{
    std::unique_ptr<ff::FxFastFileNativeDisk32Workspace> workspace =
        std::make_unique<ff::FxFastFileNativeDisk32Workspace>();
    std::vector<std::uint64_t> storage;
    FxEffectDef *effect = nullptr;
    const std::string *name;
    std::vector<const void *> references;
    std::size_t next = 0;

    static bool Resolve(void *context, ff::FxFastFileDisk32ReferenceKind kind, const disk32::PointerToken *,
                        disk32::PointerToken, ff::FxFastFileDisk32ResolvedReference *out) noexcept
    {
        using K = ff::FxFastFileDisk32ReferenceKind;
        alignas(8) static const std::uint8_t opaque[sizeof(FxEffectDef)]{};
        auto *const oracle = static_cast<Oracle *>(context);
        const void *pointer = opaque;
        if (kind == K::SoundName)
            pointer = "snd";
        else if (kind == K::EffectNameReference)
            pointer = &g_named;
        if (kind == K::EffectName)
            pointer = oracle->name->c_str();
        else if (oracle->next < oracle->references.size())
            pointer = oracle->references[oracle->next++];
        *out = {};
        out->pointer = pointer;
        const bool text = kind == K::EffectName || kind == K::SoundName;
        out->retainedByteCount = text ? std::string_view(static_cast<const char *>(pointer)).size() + 1 : sizeof(FxEffectDef);
        out->retainedAlignment = text ? 1 : alignof(FxEffectDef);
        return true;
    }
    static bool Attest(void *, ff::FxFastFileDisk32SourceSpanKind, const disk32::PointerToken *,
                       disk32::PointerToken, const void *, std::uint64_t, std::size_t) noexcept
    {
        return true;
    }

    explicit Oracle(const Effect &e, std::vector<const void *> resolved = {})
        : name(&e.name), references(std::move(resolved))
    {
        std::vector<ff::FxFastFileElemDefDisk32View> views(e.elems.size());
        for (std::size_t index = 0; index < e.elems.size(); ++index)
            views[index] = View(e, index);
        const auto elements = static_cast<std::uint32_t>(e.elems.size());
        ff::FxFastFileEffectDefDisk32View view{&e.record, {elements ? e.elems.data() : nullptr, elements},
                                               {elements ? views.data() : nullptr, elements}, {nullptr, Attest}};
        const ff::FxFastFileDisk32Resolvers resolvers{this, Resolve};
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

    // An element's spans: its samples, mark pairs and visual array.
    static ff::FxFastFileElemDefDisk32View View(const Effect &e, std::size_t index)
    {
        const auto span = [](const auto &values) {
            using T = typename std::decay_t<decltype(values)>::value_type;
            return ff::FxFastFileDisk32Span<T>{values.empty() ? nullptr : values.data(),
                                               static_cast<std::uint32_t>(values.size())};
        };
        ff::FxFastFileElemDefDisk32View view{span(e.vel[index]), span(e.vis[index])};
        if (index < e.tails.size())
        {
            view.visuals = span(e.visuals[index]);
            view.markVisuals = span(e.marks[index]);
        }
        if (!index && !e.elems[0].trailDef.token.isNull())
        {
            view.trail = &e.trail;
            view.trailVertices = span(e.trailVerts);
            view.trailIndices = span(e.trailInds);
        }
        return view;
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

// Two trails alike: their scalars, vertices and indices.
bool SameTrail(const FxTrailDef *mine, const FxTrailDef *theirs)
{
    if (!mine || !theirs)
        return mine == theirs;
    return mine->scrollTimeMsec == theirs->scrollTimeMsec && mine->repeatDist == theirs->repeatDist
        && mine->splitDist == theirs->splitDist && mine->vertCount == theirs->vertCount
        && mine->indCount == theirs->indCount
        && !std::memcmp(mine->verts, theirs->verts, static_cast<std::size_t>(mine->vertCount) * sizeof(FxTrailVertex))
        && !std::memcmp(mine->inds, theirs->inds, static_cast<std::size_t>(mine->indCount) * 2);
}

// The loader's effect matches the converter's: its fields, each element's
// scalars, samples and trail.
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
            || (mine.visSamples && std::memcmp(mine.visSamples, theirs.visSamples,
                                               (mine.visStateIntervalCount + 1u) * sizeof(*mine.visSamples)))
            || !SameTrail(mine.trailDef, theirs.trailDef))
            return false;
    }
    return References(loaded) == References(*expected);
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

using BigZone = disk32_test::Zone<4096>;

// The visuals VisualEffect writes.
void ExpectVisuals(const BigZone &zone, const FxElemDef *elems)
{
    Expect(elems[0].visuals.instance.material == &g_materials[0] && g_materialCount == 1
               && !std::strcmp(g_materials[0].info.name, "mat"),
           "an inline material loads through Material's step");
    Expect(InArena(elems[1].visuals.markArray) && elems[1].visuals.markArray[0].materials[0] == &g_aliasMaterial
               && elems[1].visuals.markArray[0].materials[1] == &g_aliasMaterial,
           "a decal's mark pair converts into native storage and names the material alias");
    Expect(InArena(elems[2].visuals.array) && elems[2].visuals.array[0].model == &g_model
               && elems[2].visuals.array[1].model == &g_model,
           "two visuals convert into native storage and resolve through XModel's step");
    Expect(elems[3].visuals.instance.effectDef.handle == &g_named && elems[0].effectOnImpact.handle == &g_named
               && g_lookups == std::vector<std::string>{"fx_hit", "fx_child"},
           "effect names resolve by name, in stream order");
    Expect(zone.Holds(elems[4].visuals.instance.soundName) && !std::strcmp(elems[4].visuals.instance.soundName, "snd_x"),
           "a sound name points at its bytes in block 4");
}

void TestVisuals()
{
    BigZone zone;
    RegisterAliases();
    const Effect effect = VisualEffect();
    File().Write(effect);
    const FxEffectDef *const loaded = Load(kInline);
    Expect(loaded == &g_effects[0] && InArena(loaded->elemDefs), "an effect of five visuals publishes");
    if (loaded != &g_effects[0] || !InArena(loaded->elemDefs))
        return;
    ExpectVisuals(zone, loaded->elemDefs);
    Expect(MatchesOracle(*loaded, Oracle(effect, References(*loaded))),
           "the effect matches the FX converter's, each reference in its place");
    Expect(g_read == g_file.size(), "every disk byte is consumed");
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

constexpr const char *kTrail = "effect trail";
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

// Rule breaks in VisualEffect, which the converter must reject too.
const Malformed kVisualRuleBreaks[] = {
    {"a mark naming no material", [](Effect &e) { e.marks[1][0].materials[1].token = {0}; }, "effect visual"},
    {"a visual array naming none", [](Effect &e) { e.visuals[2][1].token = {0}; }, "effect visual"},
};

const Malformed kVisualFaults[] = {
    {"an unmapped model alias", [](Effect &e) { e.visuals[2][0].token = {VirtualOffset(64)}; }, "alias offset"},
    {"an unmapped sound name", [](Effect &e) { e.elems[4].visuals.token = {VirtualOffset(4000)}; }, "string offset"},
    {"an unmapped effect name", [](Effect &e) { e.elems[2].effectOnDeath.token = {VirtualOffset(4000)}; },
     "string offset"},
    {"visual tokens past their block", [](Effect &e) { e.name.assign(1932, 'n'); }, "exceeds stream block"},
};

const Malformed kMalformed[] = {
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

// VisualEffect broken as test says; the converter must reject a rule break too.
void ExpectVisualBreak(const Malformed &test, bool rule)
{
    BigZone zone;
    RegisterAliases();
    Effect effect = VisualEffect();
    test.edit(effect);
    File().Write(effect);
    ExpectDrop(test.what, test.error, [] { Load(kInline); });
    Expect(!rule || !Oracle(effect).effect, test.what, "is accepted by the FX converter");
}

void TestVisualBreaksFailClosed()
{
    for (const Malformed &test : kVisualRuleBreaks)
        ExpectVisualBreak(test, true);
    for (const Malformed &test : kVisualFaults)
        ExpectVisualBreak(test, false);
}

// TwoElements with a looping trail first: two vertices, four indices.
Effect TrailEffect()
{
    Effect effect = TwoElements();
    effect.elems[0] = Element(ff::FxElemTypeDisk32::Trail, 0);
    effect.elems[0].spawn = {100, 3};
    effect.elems[0].trailDef.token = {kInline};
    effect.trail = {10, 5, 2, 2, {{kInline}}, 4, {{kInline}}};
    effect.trailVerts = {{{1, 2}, {3, 4}, 5}, {{6, 7}, {8, 9}, 10}};
    effect.trailInds = {0, 1, 1, 0};
    // An effect on impact whose 7-byte name leaves the trail record a pad byte.
    effect.elems[0].effectOnImpact.token = {kInline};
    effect.tails = {TextBytes("fx_hit"), {}};
    effect.marks.resize(2);
    effect.visuals.resize(2);
    effect.record.totalSize += 28 + 4 * (20 + 2); // FX_Convert sizes vertices by the index count
    return effect;
}

// Where TrailEffect's trail streams: its record after the effect name, then
// its vertices and indices.
void ExpectTrailPlaced(const Zone &zone, const FxTrailDef &trail, const ff::FxTrailDefDisk32 &disk)
{
    Expect(!std::memcmp(zone.virt + 808, &disk, sizeof(disk)),
           "the disk32 trail record stays 4-aligned after the effect name");
    Expect(reinterpret_cast<const std::uint8_t *>(trail.verts) == zone.virt + 836 && trail.verts[1].texCoord == 10
               && reinterpret_cast<const std::uint8_t *>(trail.inds) == zone.virt + 876 && trail.inds[1] == 1,
           "its vertices and indices stay at their retail block-4 offsets, after its record");
}

void TestTrail()
{
    Zone zone;
    const Effect effect = TrailEffect();
    File().Write(effect);
    const FxEffectDef *const loaded = Load(kInline);
    const FxTrailDef *const trail = loaded && InArena(loaded->elemDefs) ? loaded->elemDefs[0].trailDef : nullptr;
    Expect(InArena(trail) && trail->scrollTimeMsec == 10 && trail->repeatDist == 5 && trail->splitDist == 2,
           "the trail converts into native storage");
    if (!InArena(trail))
        return;
    ExpectTrailPlaced(zone, *trail, effect.trail);
    Expect(MatchesOracle(*loaded, Oracle(effect, References(*loaded))),
           "the effect matches the FX converter's, trail included");
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 1172,
           "every disk byte is consumed and block 4 advances by the retail extent");
}

// Trail breaks, which the converter must reject too.
const Malformed kTrailBreaks[] = {
    {"a repeat distance of 0", [](Effect &e) { e.trail.repeatDist = 0; }, kTrail},
    {"a split distance of 0", [](Effect &e) { e.trail.splitDist = 0; }, kTrail},
    {"65 trail vertices", [](Effect &e) { e.trail.vertCount = 65; }, kTrail},
    {"130 trail indices", [](Effect &e) { e.trail.indCount = 130; e.trailInds.resize(130); }, kTrail},
    {"an odd index count", [](Effect &e) { e.trail.indCount = 3; e.trailInds.pop_back(); }, kTrail},
    {"more vertices than indices", [](Effect &e) { e.trail.indCount = 2; e.trail.vertCount = 3;
                                                   e.trailInds.resize(2); e.trailVerts.resize(3); }, kTrail},
    {"an index past the vertices", [](Effect &e) { e.trailInds[3] = 2; }, kTrail},
    {"no vertex token", [](Effect &e) { e.trail.verts.token = {0}; e.trailVerts.clear(); }, kTrail},
    {"no index token", [](Effect &e) { e.trail.inds.token = {0}; e.trailInds.clear(); }, kTrail},
};

void TestTrailBreaksFailClosed()
{
    for (const Malformed &test : kTrailBreaks)
    {
        Zone zone;
        Effect effect = TrailEffect();
        test.edit(effect);
        File().Write(effect);
        ExpectDrop(test.what, test.error, [] { Load(kInline); });
        Expect(!Oracle(effect).effect, test.what, "is accepted by the FX converter");
    }
    Zone zone;
    File().Write(TrailEffect());
    g_file.resize(g_file.size() - 300); // in the indices
    ExpectDrop("a truncated trail", "ended unexpectedly", [] { Load(kInline); });
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

void __cdecl Load_FxEffectDefFromName(const char **name)
{
    // DB_FindXAssetHeader: the effect a name names, here always g_named.
    if (*name)
    {
        g_lookups.emplace_back(*name);
        *reinterpret_cast<const FxEffectDef **>(name) = &g_named;
    }
}

void __cdecl Load_MaterialAsset(XAssetHeader *header)
{
    g_materials[g_materialCount] = *header->material;
    header->material = &g_materials[g_materialCount++];
}

void __cdecl Load_MaterialTechniqueSetAsset(XAssetHeader *header)
{
    MaterialTechniqueSet &entry = g_sets[g_setCount++];
    entry = *header->techniqueSet;
    entry.remappedTechniqueSet = &entry; // as DB_MediaRemapTechniqueSet leaves an unremapped set
    header->techniqueSet = &entry;
}

// Models only resolve by alias here, and no material has textures, so no
// model, preset or image loads; db_disk32_xmodel.cpp and db_disk32_image.cpp
// link for the steps the loader calls.
XAssetList *varXAssetList;

void __cdecl Load_XModelAsset(XAssetHeader *)
{
    Expect(false, "no model loads");
}

void __cdecl Load_PhysPresetAsset(XAssetHeader *)
{
    Expect(false, "no preset loads");
}

void __cdecl Load_GfxImageAsset(XAssetHeader *)
{
    Expect(false, "no image loads");
}

void __cdecl DB_LoadedExternalData(std::int32_t)
{
    Expect(false, "no image loads");
}

void __cdecl Load_GetCurrentZoneHandle(uint8_t *handle)
{
    *handle = 7;
}

db::load_legacy_bridge::LegacyBridgeStatus db::load_legacy_bridge::DbLoadLegacyBridge::TryAddUser4(std::uint32_t) noexcept
{
    Expect(false, "loading marks no script string");
    return LegacyBridgeStatus::Success;
}

bool db::load_legacy_bridge::DbLoadLegacyBridge::InSession() noexcept
{
    return false;
}

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
    return Run({TestRecord, TestElementsAndSamples, TestVisuals, TestTrail, TestSharedInlineAndOffsets,
                TestRuleBreaksFailClosed, TestTrailBreaksFailClosed,
                TestVisualBreaksFailClosed, TestMalformedFailsClosed});
}
