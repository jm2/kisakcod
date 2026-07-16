#include <EffectsCore/fx_fastfile_zone_adapter_disk32.h>

#include <EffectsCore/fx_runtime.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace
{
namespace fastfile = fx::fastfile;

using Adapter = fastfile::FxFastFileZoneAdapterDisk32Workspace;
using Arena = fastfile::FxFastFileNativeArena;
using Cursor = fastfile::FxFastFileZoneAdapterCursor;
using Publication = fastfile::FxFastFileZoneAdapterPublication;
using Resolution = fastfile::FxFastFileDisk32ResolvedReference;
using SpanKind = fastfile::FxFastFileDisk32SourceSpanKind;
using Status = fastfile::FxFastFileZoneAdapterDisk32Status;
using Phase = fastfile::FxFastFileZoneAdapterDisk32Phase;
using ElemType = fastfile::FxElemTypeDisk32;

int failures = 0;

void Check(
    const bool condition,
    const char *const expression,
    const int line)
{
    if (condition)
        return;
    std::fprintf(stderr, "line %d: check failed: %s\n", line, expression);
    ++failures;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

// ---------------------------------------------------------------------------
// Wire image: one flat buffer standing in for the materialized XBlock extent.

constexpr std::size_t kWireBytes = 512 * 1024;

class WireImage final
{
public:
    WireImage() : bytes_(new std::uint8_t[kWireBytes]), used_(0)
    {
        std::memset(bytes_.get(), 0, kWireBytes);
    }

    template <typename VALUE>
    VALUE *Append(const VALUE &value)
    {
        return AppendArray(&value, 1);
    }

    template <typename VALUE>
    VALUE *AppendArray(const VALUE *const values, const std::size_t count)
    {
        static_assert(std::is_trivially_copyable_v<VALUE>);
        void *const storage = Reserve(count * sizeof(VALUE), alignof(VALUE));
        CHECK(storage != nullptr);
        if (!storage)
            return nullptr;
        std::memcpy(storage, values, count * sizeof(VALUE));
        return static_cast<VALUE *>(storage);
    }

    const char *AppendString(const char *const value)
    {
        const std::size_t bytes = std::strlen(value) + 1u;
        void *const storage = Reserve(bytes, 1);
        CHECK(storage != nullptr);
        if (!storage)
            return nullptr;
        std::memcpy(storage, value, bytes);
        return static_cast<const char *>(storage);
    }

    [[nodiscard]] bool Contains(
        const void *const address,
        const std::uint64_t byteCount) const noexcept
    {
        const std::uint8_t *const begin = bytes_.get();
        const std::uint8_t *const probe =
            static_cast<const std::uint8_t *>(address);
        return probe >= begin && byteCount <= used_
            && static_cast<std::size_t>(probe - begin) <= used_ - byteCount;
    }

private:
    void *Reserve(const std::size_t byteCount, const std::size_t alignment)
    {
        const std::size_t aligned =
            (used_ + alignment - 1u) & ~(alignment - 1u);
        if (aligned > kWireBytes || byteCount > kWireBytes - aligned)
            return nullptr;
        used_ = aligned + byteCount;
        return bytes_.get() + aligned;
    }

    std::unique_ptr<std::uint8_t[]> bytes_;
    std::size_t used_;
};

bool WireOracle(
    void *const context,
    const void *const address,
    const std::uint64_t byteCount) noexcept
{
    return static_cast<const WireImage *>(context)->Contains(
        address, byteCount);
}

bool RejectingOracle(void *, const void *, std::uint64_t) noexcept
{
    return false;
}

// ---------------------------------------------------------------------------
// Wire graph builders.  These append records in legacy stream order and
// finalize the exact legacy totalSize / msecLoopingLife the converter
// validates, mirroring the converter suite's fixture recipes.

std::uint32_t nextTokenValue = 0x40000000;

disk32::PointerToken NextToken()
{
    return {++nextTokenValue};
}

struct ElemSpec final
{
    ElemType type = ElemType::SpriteBillboard;
    std::uint8_t velIntervals = 1;
    std::uint8_t visIntervals = 1;
    std::uint8_t visualCount = 1;
    bool trail = false;
    std::int32_t vertCount = 3;
    std::int32_t indCount = 4;
    bool effectRefs = false;
};

constexpr std::size_t kMaxBuiltElements = 16;
constexpr std::size_t kMaxBuiltVisuals = 32;

struct BuiltElement final
{
    const fastfile::FxElemVelStateSampleDisk32 *velocity = nullptr;
    std::uint32_t velocityCount = 0;
    const fastfile::FxElemVisStateSampleDisk32 *visibility = nullptr;
    std::uint32_t visibilityCount = 0;
    const fastfile::FxElemVisualsDisk32 *visualsArray = nullptr;
    const fastfile::FxElemMarkVisualsDisk32 *marks = nullptr;
    const fastfile::FxTrailDefDisk32 *trail = nullptr;
    const fastfile::FxTrailVertexDisk32 *trailVertices = nullptr;
    const std::uint16_t *trailIndices = nullptr;
    const char *strings[kMaxBuiltVisuals] = {};
};

struct BuiltEffect final
{
    fastfile::FxEffectDefDisk32 *header = nullptr;
    const char *name = nullptr;
    std::uint64_t nameBytes = 0;
    fastfile::FxElemDefDisk32 *elements = nullptr;
    std::uint32_t elementCount = 0;
    BuiltElement built[kMaxBuiltElements] = {};
};

std::uint32_t AlignSize(const std::uint32_t size, const std::uint32_t alignment)
{
    return (size + alignment - 1u) & ~(alignment - 1u);
}

// Exact legacy Disk32 compact accounting (including the historical indCount
// vertex-capacity rule) that the converter validates against totalSize.
std::uint32_t ComputeLegacyTotalSize(const BuiltEffect &effect)
{
    std::uint32_t bytes = sizeof(fastfile::FxEffectDefDisk32);
    bytes = AlignSize(bytes, alignof(fastfile::FxElemDefDisk32));
    bytes += effect.elementCount
        * static_cast<std::uint32_t>(sizeof(fastfile::FxElemDefDisk32));
    for (std::uint32_t index = 0; index < effect.elementCount; ++index)
    {
        const fastfile::FxElemDefDisk32 &elem = effect.elements[index];
        if (!elem.velSamples.token.isNull())
        {
            bytes = AlignSize(
                bytes, alignof(fastfile::FxElemVelStateSampleDisk32));
            bytes += (static_cast<std::uint32_t>(elem.velIntervalCount) + 1u)
                * static_cast<std::uint32_t>(
                    sizeof(fastfile::FxElemVelStateSampleDisk32));
        }
        if (!elem.visSamples.token.isNull())
        {
            bytes = AlignSize(
                bytes, alignof(fastfile::FxElemVisStateSampleDisk32));
            bytes +=
                (static_cast<std::uint32_t>(elem.visStateIntervalCount) + 1u)
                * static_cast<std::uint32_t>(
                    sizeof(fastfile::FxElemVisStateSampleDisk32));
        }
        if (elem.elemType == ElemType::Decal && !elem.visuals.token.isNull())
        {
            bytes = AlignSize(
                bytes, alignof(fastfile::FxElemMarkVisualsDisk32));
            bytes += static_cast<std::uint32_t>(elem.visualCount)
                * static_cast<std::uint32_t>(
                    sizeof(fastfile::FxElemMarkVisualsDisk32));
        }
        else if (elem.visualCount > 1u && !elem.visuals.token.isNull())
        {
            bytes = AlignSize(bytes, alignof(fastfile::FxElemVisualsDisk32));
            bytes += static_cast<std::uint32_t>(elem.visualCount)
                * static_cast<std::uint32_t>(
                    sizeof(fastfile::FxElemVisualsDisk32));
        }
        if (!elem.trailDef.token.isNull())
        {
            const fastfile::FxTrailDefDisk32 *const trail =
                effect.built[index].trail;
            bytes = AlignSize(bytes, alignof(fastfile::FxTrailDefDisk32));
            bytes += sizeof(fastfile::FxTrailDefDisk32);
            bytes = AlignSize(bytes, alignof(fastfile::FxTrailVertexDisk32));
            bytes += static_cast<std::uint32_t>(trail->indCount)
                * static_cast<std::uint32_t>(
                    sizeof(fastfile::FxTrailVertexDisk32));
            bytes = AlignSize(bytes, alignof(std::uint16_t));
            bytes += static_cast<std::uint32_t>(trail->indCount)
                * static_cast<std::uint32_t>(sizeof(std::uint16_t));
        }
    }
    bytes += static_cast<std::uint32_t>(effect.nameBytes);
    return bytes;
}

void FinalizeEffect(BuiltEffect *const effect)
{
    effect->header->totalSize =
        static_cast<std::int32_t>(ComputeLegacyTotalSize(*effect));

    std::int64_t maximumLoopingLife = 0;
    bool hasInfiniteLoop = false;
    for (std::int32_t index = 0;
         index < effect->header->elemDefCountLooping;
         ++index)
    {
        const fastfile::FxSpawnDefDisk32 &spawn =
            effect->elements[index].spawn;
        if (spawn.loopCountOrCountAmplitude
            == (std::numeric_limits<std::int32_t>::max)())
        {
            hasInfiniteLoop = true;
            continue;
        }
        if (spawn.intervalMsecOrCountBase > 0
            && spawn.loopCountOrCountAmplitude > 1)
        {
            const std::int64_t lastSpawn =
                static_cast<std::int64_t>(spawn.intervalMsecOrCountBase)
                * (spawn.loopCountOrCountAmplitude - 1);
            if (lastSpawn > maximumLoopingLife)
                maximumLoopingLife = lastSpawn;
        }
    }
    effect->header->msecLoopingLife = hasInfiniteLoop
        ? (std::numeric_limits<std::int32_t>::max)()
        : static_cast<std::int32_t>(maximumLoopingLife);
}

bool UsesMaterialVisuals(const ElemType type)
{
    return type == ElemType::SpriteBillboard
        || type == ElemType::SpriteOriented || type == ElemType::Tail
        || type == ElemType::Trail || type == ElemType::Cloud
        || type == ElemType::Decal;
}

bool IsLight(const ElemType type)
{
    return type == ElemType::OmniLight || type == ElemType::SpotLight;
}

BuiltEffect BuildEffect(
    WireImage *const image,
    const char *const name,
    const std::int32_t looping,
    const std::int32_t oneShot,
    const std::int32_t emission,
    const std::vector<ElemSpec> &specs)
{
    BuiltEffect effect;
    CHECK(specs.size() <= kMaxBuiltElements);
    effect.elementCount = static_cast<std::uint32_t>(specs.size());

    // Header first, exactly like the legacy stream.
    fastfile::FxEffectDefDisk32 header{};
    header.name.token = NextToken();
    header.flags = 0x12345678;
    header.elemDefCountLooping = looping;
    header.elemDefCountOneShot = oneShot;
    header.elemDefCountEmission = emission;
    if (!specs.empty())
        header.elemDefs.token = NextToken();
    effect.header = image->Append(header);

    effect.name = image->AppendString(name);
    effect.nameBytes = std::strlen(name) + 1u;

    if (specs.empty())
    {
        FinalizeEffect(&effect);
        return effect;
    }

    std::vector<fastfile::FxElemDefDisk32> elems(specs.size());
    for (std::size_t index = 0; index < specs.size(); ++index)
    {
        const ElemSpec &spec = specs[index];
        fastfile::FxElemDefDisk32 &elem = elems[index];
        elem = {};
        elem.elemType = spec.type;
        elem.spawn.intervalMsecOrCountBase = 1;
        elem.lifeSpanMsec.base = 1000;
        if (static_cast<std::int32_t>(index) < looping)
        {
            elem.spawn.loopCountOrCountAmplitude =
                (std::numeric_limits<std::int32_t>::max)();
        }
        elem.velIntervalCount = spec.velIntervals;
        elem.velSamples.token = NextToken();
        // Runner elements carry no visibility payload; a zero interval count
        // has no payload at all in the canonical compiler representation.
        const std::uint8_t visIntervals =
            spec.type == ElemType::Runner ? 0 : spec.visIntervals;
        elem.visStateIntervalCount = visIntervals;
        if (visIntervals != 0)
            elem.visSamples.token = NextToken();
        if (IsLight(spec.type))
        {
            elem.visualCount = 1;
        }
        else
        {
            elem.visualCount = spec.visualCount;
            if (spec.visualCount != 0)
                elem.visuals.token = NextToken();
            if (UsesMaterialVisuals(spec.type) && spec.visualCount != 0)
            {
                // FX_ConvertAtlas canonicalizes an active non-atlased
                // material to a one-entry, zero-bit atlas.
                elem.atlas.entryCount = 1;
            }
        }
        if (spec.effectRefs)
        {
            elem.effectOnImpact.token = NextToken();
            elem.effectOnDeath.token = NextToken();
            elem.effectEmitted.token = NextToken();
        }
        if (spec.trail)
            elem.trailDef.token = NextToken();
    }
    effect.elements = image->AppendArray(elems.data(), elems.size());

    for (std::size_t index = 0; index < specs.size(); ++index)
    {
        const ElemSpec &spec = specs[index];
        fastfile::FxElemDefDisk32 &elem = effect.elements[index];
        BuiltElement &built = effect.built[index];

        std::vector<fastfile::FxElemVelStateSampleDisk32> velocity(
            static_cast<std::size_t>(elem.velIntervalCount) + 1u);
        for (std::size_t sample = 0; sample < velocity.size(); ++sample)
        {
            velocity[sample].local.velocity.base[0] =
                static_cast<float>(sample + 1u);
        }
        built.velocity = image->AppendArray(velocity.data(), velocity.size());
        built.velocityCount = static_cast<std::uint32_t>(velocity.size());

        if (!elem.visSamples.token.isNull())
        {
            std::vector<fastfile::FxElemVisStateSampleDisk32> visibility(
                static_cast<std::size_t>(elem.visStateIntervalCount) + 1u);
            for (std::size_t sample = 0; sample < visibility.size(); ++sample)
            {
                visibility[sample].base.scale =
                    static_cast<float>(sample + 2u);
            }
            built.visibility =
                image->AppendArray(visibility.data(), visibility.size());
            built.visibilityCount =
                static_cast<std::uint32_t>(visibility.size());
        }

        if (elem.elemType == ElemType::Decal)
        {
            std::vector<fastfile::FxElemMarkVisualsDisk32> marks(
                elem.visualCount);
            for (fastfile::FxElemMarkVisualsDisk32 &mark : marks)
            {
                mark.materials[0].token = NextToken();
                mark.materials[1].token = NextToken();
            }
            built.marks = image->AppendArray(marks.data(), marks.size());
        }
        else if (elem.visualCount > 1)
        {
            std::vector<fastfile::FxElemVisualsDisk32> visuals(
                elem.visualCount);
            for (fastfile::FxElemVisualsDisk32 &visual : visuals)
                visual.token = NextToken();
            built.visualsArray =
                image->AppendArray(visuals.data(), visuals.size());
        }
        if (elem.elemType == ElemType::Sound)
        {
            for (std::uint32_t slot = 0; slot < elem.visualCount; ++slot)
            {
                char soundName[64];
                std::snprintf(
                    soundName,
                    sizeof(soundName),
                    "snd/alias_%u_%u",
                    static_cast<unsigned>(index),
                    slot);
                built.strings[slot] = image->AppendString(soundName);
            }
        }

        if (spec.trail)
        {
            fastfile::FxTrailDefDisk32 trail{};
            trail.scrollTimeMsec = 100;
            trail.repeatDist = 4;
            trail.splitDist = 8;
            trail.vertCount = spec.vertCount;
            trail.verts.token = NextToken();
            trail.indCount = spec.indCount;
            trail.inds.token = NextToken();
            built.trail = image->Append(trail);

            std::vector<fastfile::FxTrailVertexDisk32> vertices(
                static_cast<std::size_t>(spec.vertCount));
            for (std::size_t vertex = 0; vertex < vertices.size(); ++vertex)
                vertices[vertex].pos[0] = static_cast<float>(vertex);
            built.trailVertices =
                image->AppendArray(vertices.data(), vertices.size());

            std::vector<std::uint16_t> indices(
                static_cast<std::size_t>(spec.indCount));
            for (std::size_t element = 0; element < indices.size(); ++element)
            {
                indices[element] = static_cast<std::uint16_t>(
                    element % vertices.size());
            }
            built.trailIndices =
                image->AppendArray(indices.data(), indices.size());
        }
    }

    FinalizeEffect(&effect);
    return effect;
}

// ---------------------------------------------------------------------------
// Resolution pool and publication sink.

struct alignas(16) OpaqueAsset final
{
    std::uint8_t bytes[64];
};

struct ResolutionPool final
{
    OpaqueAsset assets[64] = {};
    FxEffectDef identities[8] = {};
    std::uint32_t nextAsset = 0;
    std::uint32_t nextIdentity = 0;

    Resolution NextAssetResolution()
    {
        Resolution resolution;
        resolution.pointer = &assets[nextAsset++ % 64];
        resolution.retainedByteCount = sizeof(OpaqueAsset);
        resolution.retainedAlignment = alignof(OpaqueAsset);
        return resolution;
    }

    Resolution NextEffectResolution()
    {
        Resolution resolution;
        resolution.pointer = &identities[nextIdentity++ % 8];
        resolution.retainedByteCount = sizeof(FxEffectDef);
        resolution.retainedAlignment = alignof(FxEffectDef);
        return resolution;
    }
};

struct PublicationSink final
{
    FxEffectDef *effects[8] = {};
    std::uint32_t effectCount = 0;
    FxImpactTable *tables[4] = {};
    std::uint32_t tableCount = 0;
    bool rejectEffect = false;
    bool rejectImpact = false;
    Adapter *reenterTarget = nullptr;
    Status reentryStatus = Status::Success;
};

bool PublishEffect(void *const context, FxEffectDef *const effect) noexcept
{
    PublicationSink &sink = *static_cast<PublicationSink *>(context);
    if (sink.reenterTarget)
    {
        sink.reentryStatus =
            fastfile::TrySealFxEffectDefZoneDisk32(sink.reenterTarget);
    }
    if (sink.rejectEffect)
        return false;
    if (sink.effectCount < 8)
        sink.effects[sink.effectCount++] = effect;
    return true;
}

bool PublishImpact(void *const context, FxImpactTable *const table) noexcept
{
    PublicationSink &sink = *static_cast<PublicationSink *>(context);
    if (sink.rejectImpact)
        return false;
    if (sink.tableCount < 4)
        sink.tables[sink.tableCount++] = table;
    return true;
}

// ---------------------------------------------------------------------------
// Test environment: heap adapter workspace, aligned arena storage, wire image.

constexpr std::size_t kArenaBytes = 64 * 1024;

struct alignas(fastfile::kFxFastFileNativeArenaStorageAlignment)
    ArenaStorage final
{
    std::uint8_t bytes[kArenaBytes];
};

struct Environment final
{
    std::unique_ptr<Adapter> adapter = std::make_unique<Adapter>();
    std::unique_ptr<ArenaStorage> storage = std::make_unique<ArenaStorage>();
    Arena arena{};
    WireImage image{};
    ResolutionPool pool{};
    PublicationSink sink{};

    Environment()
    {
        CHECK(arena.TryBind(storage->bytes, kArenaBytes, 21)
              == fastfile::FxFastFileNativeArenaStatus::Success);
    }

    [[nodiscard]] Cursor cursor() noexcept
    {
        return {&image, &WireOracle};
    }

    [[nodiscard]] Publication publication() noexcept
    {
        return {&sink, &PublishEffect, &PublishImpact};
    }

    [[nodiscard]] bool InArena(const void *const pointer) const noexcept
    {
        const std::uint8_t *const probe =
            static_cast<const std::uint8_t *>(pointer);
        return probe >= storage->bytes && probe < storage->bytes + kArenaBytes;
    }
};

// ---------------------------------------------------------------------------
// Driver: replays the legacy wire-walk report order.  A step budget lets a
// test stop the replay mid-walk and take over with a deviation; each adapter
// call consumes one step.

struct StepBudget final
{
    std::int64_t remaining = -1;

    bool Take()
    {
        if (remaining < 0)
            return true;
        if (remaining == 0)
            return false;
        --remaining;
        return true;
    }
};

#define DRIVE(call)                                                           \
    do                                                                        \
    {                                                                         \
        if (budget && !budget->Take())                                        \
            return Status::Success;                                           \
        const Status driveStatus = (call);                                    \
        if (driveStatus != Status::Success)                                   \
            return driveStatus;                                               \
    } while (false)

Status DriveEffectReports(
    Environment &env,
    const BuiltEffect &effect,
    StepBudget *const budget)
{
    Adapter *const ws = env.adapter.get();
    for (std::uint32_t index = 0; index < effect.elementCount; ++index)
    {
        const fastfile::FxElemDefDisk32 &elem = effect.elements[index];
        const BuiltElement &built = effect.built[index];
        DRIVE(fastfile::TryRecordFxElemSpanZoneDisk32(
            ws, SpanKind::VelocitySamples, built.velocity,
            built.velocityCount));
        if (built.visibility)
        {
            DRIVE(fastfile::TryRecordFxElemSpanZoneDisk32(
                ws, SpanKind::VisibilitySamples, built.visibility,
                built.visibilityCount));
        }
        if (elem.elemType == ElemType::Decal)
        {
            DRIVE(fastfile::TryRecordFxElemSpanZoneDisk32(
                ws, SpanKind::MarkVisuals, built.marks, elem.visualCount));
            for (std::uint32_t slot = 0;
                 slot < 2u * elem.visualCount;
                 ++slot)
            {
                DRIVE(fastfile::TryRecordFxReferenceZoneDisk32(
                    ws,
                    &built.marks[slot / 2u].materials[slot % 2u].token,
                    env.pool.NextAssetResolution()));
            }
        }
        else if (!IsLight(elem.elemType) && elem.visualCount != 0)
        {
            if (elem.visualCount > 1)
            {
                DRIVE(fastfile::TryRecordFxElemSpanZoneDisk32(
                    ws, SpanKind::Visuals, built.visualsArray,
                    elem.visualCount));
            }
            for (std::uint32_t slot = 0; slot < elem.visualCount; ++slot)
            {
                const disk32::PointerToken *const field =
                    elem.visualCount == 1
                        ? &elem.visuals.token
                        : &built.visualsArray[slot].token;
                if (elem.elemType == ElemType::Sound)
                {
                    DRIVE(fastfile::TryRecordFxSoundNameZoneDisk32(
                        ws,
                        field,
                        built.strings[slot],
                        std::strlen(built.strings[slot]) + 1u));
                }
                else if (elem.elemType == ElemType::Runner)
                {
                    DRIVE(fastfile::TryRecordFxReferenceZoneDisk32(
                        ws, field, env.pool.NextEffectResolution()));
                }
                else
                {
                    DRIVE(fastfile::TryRecordFxReferenceZoneDisk32(
                        ws, field, env.pool.NextAssetResolution()));
                }
            }
        }
        const disk32::PointerToken *const referenceFields[3] = {
            &elem.effectOnImpact.token,
            &elem.effectOnDeath.token,
            &elem.effectEmitted.token,
        };
        for (const disk32::PointerToken *const field : referenceFields)
        {
            if (field->isNull())
                continue;
            DRIVE(fastfile::TryRecordFxReferenceZoneDisk32(
                ws, field, env.pool.NextEffectResolution()));
        }
        if (built.trail)
        {
            DRIVE(fastfile::TryRecordFxElemSpanZoneDisk32(
                ws, SpanKind::TrailDefinition, built.trail, 1));
            DRIVE(fastfile::TryRecordFxElemSpanZoneDisk32(
                ws, SpanKind::TrailVertices, built.trailVertices,
                static_cast<std::uint32_t>(built.trail->vertCount)));
            DRIVE(fastfile::TryRecordFxElemSpanZoneDisk32(
                ws, SpanKind::TrailIndices, built.trailIndices,
                static_cast<std::uint32_t>(built.trail->indCount)));
        }
    }
    return Status::Success;
}

Status DriveEffect(
    Environment &env,
    const BuiltEffect &effect,
    FxEffectDef **const outEffect,
    StepBudget *const budget = nullptr)
{
    Adapter *const ws = env.adapter.get();
    DRIVE(fastfile::TryBeginFxEffectDefZoneDisk32(
        ws, &env.arena, env.cursor(), effect.header));
    DRIVE(fastfile::TryRecordFxEffectDefNameZoneDisk32(
        ws, effect.name, effect.nameBytes));
    if (effect.elementCount != 0)
    {
        DRIVE(fastfile::TryRecordFxElemDefArrayZoneDisk32(
            ws, effect.elements, effect.elementCount));
        const Status reportStatus = DriveEffectReports(env, effect, budget);
        if (reportStatus != Status::Success)
            return reportStatus;
        if (budget && budget->remaining == 0)
            return Status::Success;
    }
    DRIVE(fastfile::TrySealFxEffectDefZoneDisk32(ws));
    DRIVE(fastfile::TryPublishFxEffectDefZoneDisk32(
        ws, env.publication(), outEffect));
    return Status::Success;
}

// ---------------------------------------------------------------------------
// Impact fixtures.

enum class SlotPlan : std::uint8_t
{
    Null,
    Alias,
    Inline,
    SharedInline,
};

struct BuiltImpact final
{
    fastfile::FxImpactTableDisk32 *header = nullptr;
    const char *name = nullptr;
    std::uint64_t nameBytes = 0;
    fastfile::FxImpactEntryDisk32 *entries = nullptr;
    SlotPlan plans[fastfile::kFxFastFileImpactDisk32HandleCount] = {};
    BuiltEffect inlineEffects[4] = {};
    std::uint32_t inlineCount = 0;
};

constexpr std::uint32_t kHandlesPerEntry = static_cast<std::uint32_t>(
    fastfile::kImpactNonFleshEffectCount
    + fastfile::kImpactFleshEffectCount);

disk32::PointerToken *SlotField(
    fastfile::FxImpactEntryDisk32 *const entries,
    const std::uint32_t slot)
{
    const std::uint32_t entry = slot / kHandlesPerEntry;
    const std::uint32_t within = slot % kHandlesPerEntry;
    if (within < fastfile::kImpactNonFleshEffectCount)
        return &entries[entry].nonflesh[within].token;
    return &entries[entry]
                .flesh[within - fastfile::kImpactNonFleshEffectCount].token;
}

BuiltImpact BuildImpact(
    WireImage *const image,
    const char *const name,
    const std::vector<std::pair<std::uint32_t, SlotPlan>> &slots)
{
    BuiltImpact impact;
    fastfile::FxImpactTableDisk32 header{};
    header.name.token = NextToken();
    header.table.token = NextToken();
    impact.header = image->Append(header);
    impact.name = image->AppendString(name);
    impact.nameBytes = std::strlen(name) + 1u;

    fastfile::FxImpactEntryDisk32
        entries[fastfile::kImpactSurfaceCount] = {};
    impact.entries = image->AppendArray(
        entries, fastfile::kImpactSurfaceCount);

    for (const auto &[slot, plan] : slots)
    {
        impact.plans[slot] = plan;
        disk32::PointerToken *const field = SlotField(impact.entries, slot);
        switch (plan)
        {
        case SlotPlan::Null:
            *field = {};
            break;
        case SlotPlan::Alias:
            *field = NextToken();
            break;
        case SlotPlan::Inline:
            *field = {disk32::kInline};
            break;
        case SlotPlan::SharedInline:
            *field = {disk32::kSharedInline};
            break;
        }
        if (plan == SlotPlan::Inline || plan == SlotPlan::SharedInline)
        {
            CHECK(impact.inlineCount < 4);
            char inlineName[32];
            std::snprintf(
                inlineName,
                sizeof(inlineName),
                "fx/inline_%u",
                slot);
            impact.inlineEffects[impact.inlineCount++] = BuildEffect(
                image, inlineName, 0, 1, 0, {ElemSpec{}});
        }
    }
    return impact;
}

Status DriveImpact(
    Environment &env,
    const BuiltImpact &impact,
    FxImpactTable **const outTable,
    StepBudget *const budget = nullptr)
{
    Adapter *const ws = env.adapter.get();
    DRIVE(fastfile::TryBeginFxImpactTableZoneDisk32(
        ws, &env.arena, env.cursor(), impact.header));
    DRIVE(fastfile::TryRecordFxImpactTableNameZoneDisk32(
        ws, impact.name, impact.nameBytes));
    DRIVE(fastfile::TryRecordFxImpactEntryArrayZoneDisk32(
        ws, impact.entries,
        static_cast<std::uint32_t>(fastfile::kImpactSurfaceCount)));
    std::uint32_t inlineIndex = 0;
    for (std::uint32_t slot = 0;
         slot < fastfile::kFxFastFileImpactDisk32HandleCount;
         ++slot)
    {
        switch (impact.plans[slot])
        {
        case SlotPlan::Null:
            break;
        case SlotPlan::Alias:
            DRIVE(fastfile::TryRecordFxImpactHandleZoneDisk32(
                ws,
                SlotField(impact.entries, slot),
                env.pool.NextEffectResolution()));
            break;
        case SlotPlan::Inline:
        case SlotPlan::SharedInline:
        {
            FxEffectDef *inlineEffect = nullptr;
            const Status inlineStatus = DriveEffect(
                env,
                impact.inlineEffects[inlineIndex++],
                &inlineEffect,
                budget);
            if (inlineStatus != Status::Success)
                return inlineStatus;
            break;
        }
        }
    }
    DRIVE(fastfile::TrySealFxImpactTableZoneDisk32(ws));
    DRIVE(fastfile::TryPublishFxImpactTableZoneDisk32(
        ws, env.publication(), outTable));
    return Status::Success;
}

// ---------------------------------------------------------------------------
// Effect tests.

void TestZeroElementEffect()
{
    Environment env;
    const BuiltEffect effect =
        BuildEffect(&env.image, "fx/zero_elements", 0, 0, 0, {});
    FxEffectDef *published = nullptr;
    CHECK(DriveEffect(env, effect, &published) == Status::Success);
    CHECK(published != nullptr);
    CHECK(env.sink.effectCount == 1);
    CHECK(env.sink.effects[0] == published);
    CHECK(env.InArena(published));
    CHECK(std::strcmp(published->name, "fx/zero_elements") == 0);
    CHECK(published->elemDefCountLooping == 0);
    CHECK(published->elemDefs == nullptr);
    CHECK(env.adapter->phase() == Phase::Idle);
    CHECK(env.adapter->frameDepth() == 0);
    CHECK(env.arena.openTransactionDepth() == 0);
    CHECK(env.arena.committedBytes() == env.arena.usedBytes());
    CHECK(env.arena.committedBytes() != 0);
}

void TestAllVisualKindsEffect()
{
    Environment env;
    const BuiltEffect effect = BuildEffect(
        &env.image,
        "fx/all_kinds",
        4,
        3,
        4,
        {
            ElemSpec{ElemType::SpriteBillboard},
            ElemSpec{ElemType::SpriteOriented, 2, 2, 3},
            ElemSpec{ElemType::Tail},
            ElemSpec{
                ElemType::Trail, 1, 1, 1, true, 3, 4},
            ElemSpec{ElemType::Cloud},
            ElemSpec{ElemType::Model, 1, 1, 2},
            ElemSpec{ElemType::OmniLight},
            ElemSpec{ElemType::SpotLight},
            ElemSpec{ElemType::Sound, 1, 1, 2},
            ElemSpec{ElemType::Decal, 1, 1, 2},
            ElemSpec{ElemType::Runner, 1, 0, 2},
        });
    effect.elements[0].effectOnImpact.token = NextToken();
    effect.elements[0].effectOnDeath.token = NextToken();
    effect.elements[0].effectEmitted.token = NextToken();

    FxEffectDef *published = nullptr;
    CHECK(DriveEffect(env, effect, &published) == Status::Success);
    CHECK(published != nullptr);
    CHECK(env.InArena(published));
    CHECK(std::strcmp(published->name, "fx/all_kinds") == 0);
    CHECK(published->elemDefCountLooping == 4);
    CHECK(published->elemDefCountOneShot == 3);
    CHECK(published->elemDefCountEmission == 4);
    CHECK(published->elemDefs != nullptr);
    CHECK(env.InArena(published->elemDefs));
    CHECK(env.adapter->phase() == Phase::Idle);
    CHECK(env.arena.committedBytes() == env.arena.usedBytes());
    CHECK(env.adapter->lastConverterStatus()
          == fastfile::FxFastFileNativeDisk32Status::Success);

    // The sound visual names are retained, so they must have been copied
    // into arena-owned storage rather than pointing at the wire.
    const FxElemDef &soundElem = published->elemDefs[8];
    CHECK(soundElem.visualCount == 2);
    const FxElemVisuals *const soundVisuals = soundElem.visuals.array;
    CHECK(soundVisuals != nullptr);
    if (soundVisuals)
    {
        for (std::uint32_t slot = 0; slot < 2; ++slot)
        {
            const char *const soundName = soundVisuals[slot].soundName;
            CHECK(soundName != nullptr);
            CHECK(env.InArena(soundName));
            CHECK(std::strcmp(
                      soundName,
                      effect.built[8].strings[slot]) == 0);
        }
    }
}

void TestSequentialEffectsShareArena()
{
    Environment env;
    const BuiltEffect first =
        BuildEffect(&env.image, "fx/first", 0, 1, 0, {ElemSpec{}});
    const BuiltEffect second =
        BuildEffect(&env.image, "fx/second", 0, 1, 0, {ElemSpec{}});

    FxEffectDef *firstPublished = nullptr;
    CHECK(DriveEffect(env, first, &firstPublished) == Status::Success);
    const std::uint64_t committedAfterFirst = env.arena.committedBytes();

    FxEffectDef *secondPublished = nullptr;
    CHECK(DriveEffect(env, second, &secondPublished) == Status::Success);
    CHECK(env.arena.committedBytes() > committedAfterFirst);
    CHECK(firstPublished != secondPublished);
    CHECK(env.sink.effectCount == 2);
    CHECK(std::strcmp(firstPublished->name, "fx/first") == 0);
    CHECK(std::strcmp(secondPublished->name, "fx/second") == 0);
}

void TestPublicationRejectionStrandsCommittedStorage()
{
    Environment env;
    env.sink.rejectEffect = true;
    const BuiltEffect effect =
        BuildEffect(&env.image, "fx/rejected", 0, 1, 0, {ElemSpec{}});
    FxEffectDef *published = nullptr;
    CHECK(DriveEffect(env, effect, &published)
          == Status::PublicationFailed);
    CHECK(published == nullptr);
    CHECK(env.sink.effectCount == 0);
    CHECK(env.adapter->phase() == Phase::Idle);
    CHECK(env.arena.openTransactionDepth() == 0);
    // The materialized output was committed before publication, so rejection
    // strands it as retired storage rather than reclaiming published bytes.
    CHECK(env.arena.committedBytes() != 0);

    // The adapter is reusable immediately.
    env.sink.rejectEffect = false;
    const BuiltEffect retry =
        BuildEffect(&env.image, "fx/retry", 0, 1, 0, {ElemSpec{}});
    CHECK(DriveEffect(env, retry, &published) == Status::Success);
    CHECK(published != nullptr);
}

void TestConverterRejectionTearsDown()
{
    Environment env;
    BuiltEffect effect =
        BuildEffect(&env.image, "fx/bad_semantics", 0, 1, 0, {ElemSpec{}});
    // Corrupt the validated legacy accounting: the plan must fail.
    effect.header->totalSize += 4;
    FxEffectDef *published = nullptr;
    CHECK(DriveEffect(env, effect, &published) == Status::ConversionFailed);
    CHECK(published == nullptr);
    CHECK(env.adapter->lastConverterStatus()
          != fastfile::FxFastFileNativeDisk32Status::Success);
    CHECK(env.adapter->phase() == Phase::Idle);
    CHECK(env.arena.openTransactionDepth() == 0);
    CHECK(env.arena.usedBytes() == 0);

    // Immediately reusable after structural reset.
    const BuiltEffect retry =
        BuildEffect(&env.image, "fx/retry_after_semantics", 0, 1, 0,
                    {ElemSpec{}});
    CHECK(DriveEffect(env, retry, &published) == Status::Success);
    CHECK(published != nullptr);
}

void TestArenaExhaustionFailsClosed()
{
    Environment env;
    // Rebind a tiny arena that cannot hold the widened output.
    CHECK(env.arena.TryUnbind()
          == fastfile::FxFastFileNativeArenaStatus::Success);
    CHECK(env.arena.TryBind(env.storage->bytes, 64, 23)
          == fastfile::FxFastFileNativeArenaStatus::Success);
    const BuiltEffect effect =
        BuildEffect(&env.image, "fx/too_big", 0, 1, 0, {ElemSpec{}});
    FxEffectDef *published = nullptr;
    CHECK(DriveEffect(env, effect, &published) == Status::ArenaFailed);
    CHECK(published == nullptr);
    CHECK(env.adapter->lastArenaStatus()
          == fastfile::FxFastFileNativeArenaStatus::InsufficientCapacity);
    CHECK(env.adapter->phase() == Phase::Idle);
    CHECK(env.arena.usedBytes() == 0);
}

void TestBeginValidation()
{
    Environment env;
    BuiltEffect effect =
        BuildEffect(&env.image, "fx/begin", 0, 1, 0, {ElemSpec{}});
    Adapter *const ws = env.adapter.get();
    const Cursor cursor = env.cursor();

    CHECK(fastfile::TryBeginFxEffectDefZoneDisk32(
              nullptr, &env.arena, cursor, effect.header)
          == Status::InvalidArgument);
    CHECK(fastfile::TryBeginFxEffectDefZoneDisk32(
              ws, nullptr, cursor, effect.header)
          == Status::InvalidArgument);
    CHECK(fastfile::TryBeginFxEffectDefZoneDisk32(
              ws, &env.arena, Cursor{}, effect.header)
          == Status::InvalidArgument);
    CHECK(fastfile::TryBeginFxEffectDefZoneDisk32(
              ws, &env.arena, cursor, nullptr)
          == Status::InvalidArgument);

    Arena unbound;
    CHECK(fastfile::TryBeginFxEffectDefZoneDisk32(
              ws, &unbound, cursor, effect.header)
          == Status::InvalidArgument);

    const Cursor rejecting{&env.image, &RejectingOracle};
    CHECK(fastfile::TryBeginFxEffectDefZoneDisk32(
              ws, &env.arena, rejecting, effect.header)
          == Status::InvalidSpan);

    // Header records not materialized by the cursor are rejected.
    fastfile::FxEffectDefDisk32 outside = *effect.header;
    CHECK(fastfile::TryBeginFxEffectDefZoneDisk32(
              ws, &env.arena, cursor, &outside)
          == Status::InvalidSpan);

    // Count/token contradictions fail before any transaction opens.
    effect.header->elemDefCountLooping = -1;
    CHECK(fastfile::TryBeginFxEffectDefZoneDisk32(
              ws, &env.arena, cursor, effect.header)
          == Status::InvalidCount);
    effect.header->elemDefCountLooping = 0;
    effect.header->elemDefCountOneShot = 257;
    CHECK(fastfile::TryBeginFxEffectDefZoneDisk32(
              ws, &env.arena, cursor, effect.header)
          == Status::InvalidCount);
    effect.header->elemDefCountOneShot = 0;
    CHECK(fastfile::TryBeginFxEffectDefZoneDisk32(
              ws, &env.arena, cursor, effect.header)
          == Status::InvalidToken);
    effect.header->elemDefCountOneShot = 1;
    const disk32::PointerToken savedName = effect.header->name.token;
    effect.header->name.token = {};
    CHECK(fastfile::TryBeginFxEffectDefZoneDisk32(
              ws, &env.arena, cursor, effect.header)
          == Status::InvalidToken);
    effect.header->name.token = savedName;

    CHECK(ws->phase() == Phase::Idle);
    CHECK(env.arena.openTransactionDepth() == 0);

    // The pristine header still drives to publication.
    FxEffectDef *published = nullptr;
    CHECK(DriveEffect(env, effect, &published) == Status::Success);
    CHECK(published != nullptr);
}

void TestNameValidation()
{
    Environment env;
    const BuiltEffect effect =
        BuildEffect(&env.image, "fx/name", 0, 1, 0, {ElemSpec{}});
    Adapter *const ws = env.adapter.get();

    // Reporting anything but the name after begin is a sequence violation.
    CHECK(fastfile::TryBeginFxEffectDefZoneDisk32(
              ws, &env.arena, env.cursor(), effect.header)
          == Status::Success);
    CHECK(fastfile::TryRecordFxElemDefArrayZoneDisk32(
              ws, effect.elements, effect.elementCount)
          == Status::InvalidSequence);
    CHECK(ws->phase() == Phase::Idle);
    CHECK(env.arena.openTransactionDepth() == 0);

    struct NameCase final
    {
        const char *name;
        std::uint64_t bytes;
        Status expected;
    };
    const char *const wireName = effect.name;
    const NameCase cases[] = {
        {wireName, 1, Status::InvalidString},
        {wireName, effect.nameBytes - 1, Status::InvalidString},
        {wireName, effect.nameBytes + 4, Status::InvalidString},
        {wireName,
         fastfile::kFxFastFileZoneAdapterMaxNameBytes + 1,
         Status::InvalidString},
    };
    for (const NameCase &nameCase : cases)
    {
        CHECK(fastfile::TryBeginFxEffectDefZoneDisk32(
                  ws, &env.arena, env.cursor(), effect.header)
              == Status::Success);
        CHECK(fastfile::TryRecordFxEffectDefNameZoneDisk32(
                  ws, nameCase.name, nameCase.bytes)
              == nameCase.expected);
        CHECK(ws->phase() == Phase::Idle);
        CHECK(env.arena.openTransactionDepth() == 0);
    }

    // A name outside the cursor extent is rejected as provenance.
    static const char outsideName[] = "fx/outside";
    CHECK(fastfile::TryBeginFxEffectDefZoneDisk32(
              ws, &env.arena, env.cursor(), effect.header)
          == Status::Success);
    CHECK(fastfile::TryRecordFxEffectDefNameZoneDisk32(
              ws, outsideName, sizeof(outsideName))
          == Status::InvalidSpan);
    CHECK(ws->phase() == Phase::Idle);
}

void TestElementSequenceGuards()
{
    Environment env;
    const BuiltEffect effect = BuildEffect(
        &env.image, "fx/sequence", 0, 1, 0,
        {ElemSpec{ElemType::SpriteBillboard, 1, 1, 1}});
    Adapter *const ws = env.adapter.get();
    const BuiltElement &built = effect.built[0];

    // Wrong span kind first: visibility before velocity.
    StepBudget toElements{3};
    CHECK(DriveEffect(env, effect, nullptr, &toElements) == Status::Success);
    CHECK(ws->phase() == Phase::EffectElements);
    CHECK(fastfile::TryRecordFxElemSpanZoneDisk32(
              ws, SpanKind::VisibilitySamples, built.visibility,
              built.visibilityCount)
          == Status::InvalidSequence);
    CHECK(ws->phase() == Phase::Idle);
    CHECK(env.arena.usedBytes() == 0);

    // Wrong count.
    StepBudget again{3};
    CHECK(DriveEffect(env, effect, nullptr, &again) == Status::Success);
    CHECK(fastfile::TryRecordFxElemSpanZoneDisk32(
              ws, SpanKind::VelocitySamples, built.velocity,
              built.velocityCount + 1)
          == Status::InvalidCount);
    CHECK(ws->phase() == Phase::Idle);

    // Span outside the cursor extent.
    static const fastfile::FxElemVelStateSampleDisk32 outside[2] = {};
    StepBudget third{3};
    CHECK(DriveEffect(env, effect, nullptr, &third) == Status::Success);
    CHECK(fastfile::TryRecordFxElemSpanZoneDisk32(
              ws, SpanKind::VelocitySamples, outside, 2)
          == Status::InvalidSpan);
    CHECK(ws->phase() == Phase::Idle);

    // Duplicate span after the expectation advanced.
    StepBudget fourth{4};
    CHECK(DriveEffect(env, effect, nullptr, &fourth) == Status::Success);
    CHECK(fastfile::TryRecordFxElemSpanZoneDisk32(
              ws, SpanKind::VelocitySamples, built.velocity,
              built.velocityCount)
          == Status::InvalidSequence);
    CHECK(ws->phase() == Phase::Idle);

    // Premature seal mid-walk.
    StepBudget fifth{4};
    CHECK(DriveEffect(env, effect, nullptr, &fifth) == Status::Success);
    CHECK(fastfile::TrySealFxEffectDefZoneDisk32(ws)
          == Status::InvalidSequence);
    CHECK(ws->phase() == Phase::Idle);

    // Publish without seal.
    StepBudget sixth{4};
    CHECK(DriveEffect(env, effect, nullptr, &sixth) == Status::Success);
    FxEffectDef *published = nullptr;
    CHECK(fastfile::TryPublishFxEffectDefZoneDisk32(
              ws, env.publication(), &published)
          == Status::InvalidSequence);
    CHECK(published == nullptr);
    CHECK(ws->phase() == Phase::Idle);
}

void TestReferenceGuards()
{
    Environment env;
    const BuiltEffect effect = BuildEffect(
        &env.image, "fx/references", 0, 1, 0,
        {ElemSpec{ElemType::Sound, 1, 1, 2}});
    Adapter *const ws = env.adapter.get();
    const BuiltElement &built = effect.built[0];

    // A sound slot must use the sound-name entry point.
    StepBudget toVisualRefs{6};
    CHECK(DriveEffect(env, effect, nullptr, &toVisualRefs)
          == Status::Success);
    CHECK(ws->phase() == Phase::EffectElements);
    CHECK(fastfile::TryRecordFxReferenceZoneDisk32(
              ws,
              &built.visualsArray[0].token,
              env.pool.NextAssetResolution())
          == Status::InvalidSequence);
    CHECK(ws->phase() == Phase::Idle);

    // The wrong slot field is rejected even with the right kind.
    StepBudget second{6};
    CHECK(DriveEffect(env, effect, nullptr, &second) == Status::Success);
    CHECK(fastfile::TryRecordFxSoundNameZoneDisk32(
              ws,
              &built.visualsArray[1].token,
              built.strings[0],
              std::strlen(built.strings[0]) + 1u)
          == Status::InvalidSequence);
    CHECK(ws->phase() == Phase::Idle);

    // A malformed wire sound name fails as a string.
    StepBudget third{6};
    CHECK(DriveEffect(env, effect, nullptr, &third) == Status::Success);
    CHECK(fastfile::TryRecordFxSoundNameZoneDisk32(
              ws,
              &built.visualsArray[0].token,
              built.strings[0],
              std::strlen(built.strings[0]))
          == Status::InvalidString);
    CHECK(ws->phase() == Phase::Idle);

    // Sound names land in the arena even before publication: a mid-walk
    // abort must reclaim them.
    StepBudget fourth{6};
    CHECK(DriveEffect(env, effect, nullptr, &fourth) == Status::Success);
    CHECK(fastfile::TryRecordFxSoundNameZoneDisk32(
              ws,
              &built.visualsArray[0].token,
              built.strings[0],
              std::strlen(built.strings[0]) + 1u)
          == Status::Success);
    CHECK(env.arena.usedBytes() != 0);
    CHECK(fastfile::AbortFxFastFileZoneAdapterDisk32(ws) == Status::Success);
    CHECK(env.arena.usedBytes() == 0);
    CHECK(ws->phase() == Phase::Idle);
    CHECK(fastfile::AbortFxFastFileZoneAdapterDisk32(ws)
          == Status::InvalidPhase);
}

void TestStructuralPrechecksRejectContradictoryWire()
{
    Environment env;
    Adapter *const ws = env.adapter.get();

    // Null velocity token: the walk cannot even start for the element.
    {
        BuiltEffect effect = BuildEffect(
            &env.image, "fx/no_velocity", 0, 1, 0, {ElemSpec{}});
        effect.elements[0].velSamples.token = {};
        CHECK(fastfile::TryBeginFxEffectDefZoneDisk32(
                  ws, &env.arena, env.cursor(), effect.header)
              == Status::Success);
        CHECK(fastfile::TryRecordFxEffectDefNameZoneDisk32(
                  ws, effect.name, effect.nameBytes)
              == Status::Success);
        CHECK(fastfile::TryRecordFxElemDefArrayZoneDisk32(
                  ws, effect.elements, effect.elementCount)
              == Status::InvalidToken);
        CHECK(ws->phase() == Phase::Idle);
    }

    // A light element with a visuals token contradicts the grammar.
    {
        BuiltEffect effect = BuildEffect(
            &env.image, "fx/light_visual", 0, 1, 0,
            {ElemSpec{ElemType::OmniLight}});
        effect.elements[0].visuals.token = NextToken();
        CHECK(fastfile::TryBeginFxEffectDefZoneDisk32(
                  ws, &env.arena, env.cursor(), effect.header)
              == Status::Success);
        CHECK(fastfile::TryRecordFxEffectDefNameZoneDisk32(
                  ws, effect.name, effect.nameBytes)
              == Status::Success);
        CHECK(fastfile::TryRecordFxElemDefArrayZoneDisk32(
                  ws, effect.elements, effect.elementCount)
              == Status::Success);
        CHECK(fastfile::TryRecordFxElemSpanZoneDisk32(
                  ws,
                  SpanKind::VelocitySamples,
                  effect.built[0].velocity,
                  effect.built[0].velocityCount)
              == Status::Success);
        CHECK(fastfile::TryRecordFxElemSpanZoneDisk32(
                  ws,
                  SpanKind::VisibilitySamples,
                  effect.built[0].visibility,
                  effect.built[0].visibilityCount)
              == Status::InvalidToken);
        CHECK(ws->phase() == Phase::Idle);
    }

    // A runner with zero visuals is invalid.
    {
        BuiltEffect effect = BuildEffect(
            &env.image, "fx/runner_none", 0, 1, 0,
            {ElemSpec{ElemType::Runner, 1, 0, 0}});
        CHECK(fastfile::TryBeginFxEffectDefZoneDisk32(
                  ws, &env.arena, env.cursor(), effect.header)
              == Status::Success);
        CHECK(fastfile::TryRecordFxEffectDefNameZoneDisk32(
                  ws, effect.name, effect.nameBytes)
              == Status::Success);
        CHECK(fastfile::TryRecordFxElemDefArrayZoneDisk32(
                  ws, effect.elements, effect.elementCount)
              == Status::Success);
        CHECK(fastfile::TryRecordFxElemSpanZoneDisk32(
                  ws,
                  SpanKind::VelocitySamples,
                  effect.built[0].velocity,
                  effect.built[0].velocityCount)
              == Status::InvalidCount);
        CHECK(ws->phase() == Phase::Idle);
    }

    // A non-trail element with a trail token contradicts the grammar.
    {
        BuiltEffect effect = BuildEffect(
            &env.image, "fx/false_trail", 0, 1, 0, {ElemSpec{}});
        effect.elements[0].trailDef.token = NextToken();
        StepBudget budget{5};
        CHECK(DriveEffect(env, effect, nullptr, &budget)
              == Status::Success);
        // The single material reference is the last expected item; recording
        // it advances the walk into the contradictory trail expectation.
        CHECK(fastfile::TryRecordFxReferenceZoneDisk32(
                  ws,
                  &effect.elements[0].visuals.token,
                  env.pool.NextAssetResolution())
              == Status::InvalidToken);
        CHECK(ws->phase() == Phase::Idle);
    }
}

void TestPublicationReentrancyIsRejected()
{
    Environment env;
    env.sink.reenterTarget = env.adapter.get();
    const BuiltEffect effect =
        BuildEffect(&env.image, "fx/reentry", 0, 1, 0, {ElemSpec{}});
    FxEffectDef *published = nullptr;
    CHECK(DriveEffect(env, effect, &published) == Status::Success);
    CHECK(published != nullptr);
    CHECK(env.sink.reentryStatus == Status::Busy);
}

// ---------------------------------------------------------------------------
// Impact tests.

void TestImpactAliasHandles()
{
    Environment env;
    const BuiltImpact impact = BuildImpact(
        &env.image,
        "impact/alias_only",
        {{0, SlotPlan::Alias},
         {33, SlotPlan::Alias},
         {395, SlotPlan::Alias}});
    FxImpactTable *published = nullptr;
    CHECK(DriveImpact(env, impact, &published) == Status::Success);
    CHECK(published != nullptr);
    CHECK(env.sink.tableCount == 1);
    CHECK(env.InArena(published));
    CHECK(std::strcmp(published->name, "impact/alias_only") == 0);
    CHECK(published->table != nullptr);
    CHECK(env.InArena(published->table));
    CHECK(published->table[0].nonflesh[0] == &env.pool.identities[0]);
    CHECK(published->table[1].nonflesh[0] == &env.pool.identities[1]);
    CHECK(published->table[11].flesh[3] == &env.pool.identities[2]);
    CHECK(published->table[0].nonflesh[1] == nullptr);
    CHECK(env.adapter->phase() == Phase::Idle);
    CHECK(env.arena.committedBytes() == env.arena.usedBytes());
}

void TestImpactWithNestedInlineEffects()
{
    Environment env;
    const BuiltImpact impact = BuildImpact(
        &env.image,
        "impact/nested",
        {{2, SlotPlan::Inline},
         {40, SlotPlan::Alias},
         {70, SlotPlan::SharedInline}});
    FxImpactTable *published = nullptr;
    CHECK(DriveImpact(env, impact, &published) == Status::Success);
    CHECK(published != nullptr);

    // Both inline effects published before the impact table, in wire order.
    CHECK(env.sink.effectCount == 2);
    CHECK(env.sink.tableCount == 1);
    CHECK(std::strcmp(env.sink.effects[0]->name, "fx/inline_2") == 0);
    CHECK(std::strcmp(env.sink.effects[1]->name, "fx/inline_70") == 0);

    // The nested publications were auto-bound to their impact slots.
    CHECK(published->table[0].nonflesh[2] == env.sink.effects[0]);
    CHECK(published->table[2].nonflesh[4] == env.sink.effects[1]);
    CHECK(published->table[1].nonflesh[7] == &env.pool.identities[0]);
    CHECK(env.adapter->phase() == Phase::Idle);
    CHECK(env.arena.committedBytes() == env.arena.usedBytes());
    CHECK(env.arena.openTransactionDepth() == 0);
}

void TestNestedEffectFailureTearsDownWholeTransaction()
{
    Environment env;
    BuiltImpact impact = BuildImpact(
        &env.image,
        "impact/nested_failure",
        {{5, SlotPlan::Inline}});
    // Corrupt the nested effect's accounting so its plan fails.
    impact.inlineEffects[0].header->totalSize += 4;
    FxImpactTable *published = nullptr;
    CHECK(DriveImpact(env, impact, &published) == Status::ConversionFailed);
    CHECK(published == nullptr);
    CHECK(env.sink.effectCount == 0);
    CHECK(env.sink.tableCount == 0);
    CHECK(env.adapter->phase() == Phase::Idle);
    CHECK(env.adapter->frameDepth() == 0);
    CHECK(env.arena.openTransactionDepth() == 0);
    CHECK(env.arena.usedBytes() == 0);
}

void TestNestedCommitSurvivesOuterFailure()
{
    Environment env;
    const BuiltImpact impact = BuildImpact(
        &env.image,
        "impact/outer_failure",
        {{3, SlotPlan::Inline}});
    // Drive through the nested effect's publication, then abort the impact.
    StepBudget budget{3 + 8};
    CHECK(DriveImpact(env, impact, nullptr, &budget) == Status::Success);
    CHECK(env.sink.effectCount == 1);
    CHECK(env.adapter->frameDepth() == 1);
    const std::uint64_t committed = env.arena.committedBytes();
    CHECK(committed != 0);
    CHECK(fastfile::AbortFxFastFileZoneAdapterDisk32(env.adapter.get())
          == Status::Success);
    // The published nested effect keeps its committed storage.
    CHECK(env.arena.committedBytes() == committed);
    CHECK(env.arena.usedBytes() == committed);
    CHECK(env.adapter->phase() == Phase::Idle);
}

void TestImpactSequenceGuards()
{
    Environment env;
    Adapter *const ws = env.adapter.get();
    const BuiltImpact impact = BuildImpact(
        &env.image,
        "impact/sequence",
        {{0, SlotPlan::Alias}, {1, SlotPlan::Inline}});

    // Null header tokens are rejected before any transaction opens.
    {
        fastfile::FxImpactTableDisk32 broken = *impact.header;
        broken.name.token = {};
        const fastfile::FxImpactTableDisk32 *const wireBroken =
            env.image.Append(broken);
        CHECK(fastfile::TryBeginFxImpactTableZoneDisk32(
                  ws, &env.arena, env.cursor(), wireBroken)
              == Status::InvalidToken);
        CHECK(ws->phase() == Phase::Idle);
    }

    // Wrong entry count.
    CHECK(fastfile::TryBeginFxImpactTableZoneDisk32(
              ws, &env.arena, env.cursor(), impact.header)
          == Status::Success);
    CHECK(fastfile::TryRecordFxImpactTableNameZoneDisk32(
              ws, impact.name, impact.nameBytes)
          == Status::Success);
    CHECK(fastfile::TryRecordFxImpactEntryArrayZoneDisk32(
              ws, impact.entries, 11)
          == Status::InvalidCount);
    CHECK(ws->phase() == Phase::Idle);

    // The pending slot is an alias: starting a nested effect is a sequence
    // violation.
    {
        StepBudget budget{3};
        CHECK(DriveImpact(env, impact, nullptr, &budget) == Status::Success);
        CHECK(ws->phase() == Phase::ImpactEntries);
        const BuiltEffect stray = BuildEffect(
            &env.image, "fx/stray", 0, 1, 0, {ElemSpec{}});
        CHECK(fastfile::TryBeginFxEffectDefZoneDisk32(
                  ws, &env.arena, env.cursor(), stray.header)
              == Status::InvalidSequence);
        CHECK(ws->phase() == Phase::Idle);
    }

    // The pending slot is an inline sentinel: an alias report is a token
    // violation.
    {
        StepBudget budget{4};
        CHECK(DriveImpact(env, impact, nullptr, &budget) == Status::Success);
        CHECK(fastfile::TryRecordFxImpactHandleZoneDisk32(
                  ws,
                  SlotField(impact.entries, 1),
                  env.pool.NextEffectResolution())
              == Status::InvalidToken);
        CHECK(ws->phase() == Phase::Idle);
    }

    // Out-of-order slot reports are rejected.
    {
        StepBudget budget{3};
        CHECK(DriveImpact(env, impact, nullptr, &budget) == Status::Success);
        CHECK(fastfile::TryRecordFxImpactHandleZoneDisk32(
                  ws,
                  SlotField(impact.entries, 1),
                  env.pool.NextEffectResolution())
              == Status::InvalidSequence);
        CHECK(ws->phase() == Phase::Idle);
    }

    // Premature seal.
    {
        StepBudget budget{3};
        CHECK(DriveImpact(env, impact, nullptr, &budget) == Status::Success);
        CHECK(fastfile::TrySealFxImpactTableZoneDisk32(ws)
              == Status::InvalidSequence);
        CHECK(ws->phase() == Phase::Idle);
    }

    // Impact under impact is rejected (and tears down).
    {
        StepBudget budget{3};
        CHECK(DriveImpact(env, impact, nullptr, &budget) == Status::Success);
        CHECK(fastfile::TryBeginFxImpactTableZoneDisk32(
                  ws, &env.arena, env.cursor(), impact.header)
              == Status::InvalidSequence);
        CHECK(ws->phase() == Phase::Idle);
    }
}

void TestImpactPublicationRejected()
{
    Environment env;
    env.sink.rejectImpact = true;
    const BuiltImpact impact = BuildImpact(
        &env.image, "impact/rejected", {{7, SlotPlan::Alias}});
    FxImpactTable *published = nullptr;
    CHECK(DriveImpact(env, impact, &published)
          == Status::PublicationFailed);
    CHECK(published == nullptr);
    CHECK(env.sink.tableCount == 0);
    CHECK(env.adapter->phase() == Phase::Idle);
    CHECK(env.arena.openTransactionDepth() == 0);
}

void TestWorkspaceStartsIdle()
{
    Environment env;
    CHECK(env.adapter->phase() == Phase::Idle);
    CHECK(env.adapter->frameDepth() == 0);
    CHECK(env.adapter->lastConverterStatus()
          == fastfile::FxFastFileNativeDisk32Status::Success);
    CHECK(env.adapter->lastArenaStatus()
          == fastfile::FxFastFileNativeArenaStatus::Success);

    // Record calls without an open transaction fail without side effects.
    CHECK(fastfile::TryRecordFxEffectDefNameZoneDisk32(
              env.adapter.get(), "x", 2)
          == Status::InvalidPhase);
    CHECK(fastfile::TrySealFxEffectDefZoneDisk32(env.adapter.get())
          == Status::InvalidPhase);
    FxEffectDef *effect = nullptr;
    CHECK(fastfile::TryPublishFxEffectDefZoneDisk32(
              env.adapter.get(), env.publication(), &effect)
          == Status::InvalidPhase);
    CHECK(effect == nullptr);
}
} // namespace

int main()
{
    TestWorkspaceStartsIdle();
    TestZeroElementEffect();
    TestAllVisualKindsEffect();
    TestSequentialEffectsShareArena();
    TestPublicationRejectionStrandsCommittedStorage();
    TestConverterRejectionTearsDown();
    TestArenaExhaustionFailsClosed();
    TestBeginValidation();
    TestNameValidation();
    TestElementSequenceGuards();
    TestReferenceGuards();
    TestStructuralPrechecksRejectContradictoryWire();
    TestPublicationReentrancyIsRejected();
    TestImpactAliasHandles();
    TestImpactWithNestedInlineEffects();
    TestNestedEffectFailureTearsDownWholeTransaction();
    TestNestedCommitSurvivesOuterFailure();
    TestImpactSequenceGuards();
    TestImpactPublicationRejected();

    if (failures != 0)
    {
        std::fprintf(stderr, "%d adapter check(s) failed\n", failures);
        return 1;
    }
    std::puts("fx fast-file zone adapter tests passed");
    return 0;
}
