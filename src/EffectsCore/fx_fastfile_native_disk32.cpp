#include <EffectsCore/fx_fastfile_native_disk32.h>

#include <EffectsCore/fx_runtime.h>
#include <EffectsCore/fx_runtime_blob.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <type_traits>

namespace fx::fastfile
{
namespace
{
using Status = FxFastFileNativeDisk32Status;

constexpr std::int32_t kMaxTrailVertices = 64;
constexpr std::int32_t kMaxTrailIndices = 128;
// Keep static definition timing inside the same bounded domain used by the
// archive semantic validator.  Besides limiting hostile fast-file work, this
// leaves headroom for the runtime's signed additions of delay, lifespan and
// last-looping-spawn time.
constexpr std::int64_t kDurationLimitMsec =
    INT64_C(24) * 60 * 60 * 1000;
// A sampled integer range evaluates `(amplitude + 1) * random16` in signed
// 32-bit arithmetic.  32767 is the largest amplitude for which that product
// cannot overflow.
constexpr std::int32_t kRandomRangeAmplitudeMax = 32767;
constexpr std::uint64_t kFnvOffset = UINT64_C(14695981039346656037);
constexpr std::uint64_t kFnvPrime = UINT64_C(1099511628211);

static_assert(alignof(FxElemVelStateSampleDisk32)
              == alignof(FxElemVelStateSample));
static_assert(alignof(FxElemVisStateSampleDisk32)
              == alignof(FxElemVisStateSample));
static_assert(alignof(FxTrailVertexDisk32)
              == alignof(FxTrailVertex));

template <typename T>
[[nodiscard]] bool IsAligned(const T *const pointer) noexcept
{
    return pointer
        && reinterpret_cast<std::uintptr_t>(pointer) % alignof(T) == 0;
}

template <typename T>
[[nodiscard]] bool IsEmpty(const FxFastFileDisk32Span<T> span) noexcept
{
    return !span.data && span.count == 0;
}

template <typename T>
[[nodiscard]] bool SpanBytes(
    const FxFastFileDisk32Span<T> span,
    std::uint64_t *const bytes) noexcept
{
    if (!bytes)
        return false;
    *bytes = static_cast<std::uint64_t>(span.count) * sizeof(T);
    return span.count == 0
        || *bytes / sizeof(T) == static_cast<std::uint64_t>(span.count);
}

[[nodiscard]] bool ComputeContentFingerprint(
    const void *const address,
    const std::uint64_t byteCount,
    std::uint64_t *const outFingerprint) noexcept
{
    if (!address || !outFingerprint
        || byteCount > (std::numeric_limits<std::size_t>::max)())
    {
        return false;
    }
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(address);
    const std::size_t bytes = static_cast<std::size_t>(byteCount);
    if (bytes > (std::numeric_limits<std::uintptr_t>::max)() - begin)
        return false;

    std::uint64_t fingerprint = kFnvOffset;
    const auto *const data = static_cast<const std::uint8_t *>(address);
    for (std::size_t index = 0; index < bytes; ++index)
    {
        fingerprint ^= data[index];
        fingerprint *= kFnvPrime;
    }
    *outFingerprint = fingerprint;
    return true;
}

struct ProvenanceScheduleState final
{
    FxFastFileDisk32FrozenProvenanceRequest *recorded = nullptr;
    std::uint32_t count = 0;
};

[[nodiscard]] Status AppendProvenanceRequest(
    ProvenanceScheduleState *const schedule,
    const FxFastFileDisk32SourceSpanKind kind,
    const disk32::PointerToken *const sourceField,
    const disk32::PointerToken token,
    const void *const addressField,
    const std::uint32_t *const countField,
    const std::uint32_t countValue,
    const void *const address,
    const std::uint64_t byteCount,
    const std::size_t alignment) noexcept
{
    if (!schedule)
        return Status::Success;
    if (!addressField || alignment == 0
        || alignment > (std::numeric_limits<std::uint32_t>::max)()
        || schedule->count >= kFxFastFileDisk32MaxProvenanceRequests)
    {
        return Status::SizeOverflow;
    }

    if (!schedule->recorded)
        return Status::InvalidArgument;

    FxFastFileDisk32FrozenProvenanceRequest request{};
    request.kind = kind;
    request.sourceField = sourceField;
    request.token = token;
    request.addressField = addressField;
    request.countField = countField;
    request.countValue = countValue;
    request.address = address;
    request.byteCount = byteCount;
    request.alignment = static_cast<std::uint32_t>(alignment);
    if (!ComputeContentFingerprint(
            address, byteCount, &request.contentFingerprint))
    {
        return Status::SizeOverflow;
    }
    schedule->recorded[schedule->count++] = request;
    return Status::Success;
}

template <typename T>
[[nodiscard]] Status ValidateOwnedSpan(
    const FxFastFileEffectDefDisk32View &,
    const FxFastFileDisk32SourceSpanKind kind,
    const disk32::PointerToken *const sourceField,
    const disk32::PointerToken token,
    const FxFastFileDisk32Span<T> &span,
    const std::uint32_t expectedCount,
    ProvenanceScheduleState *const schedule) noexcept
{
    if (token.isNull())
    {
        return expectedCount == 0 && IsEmpty(span)
            ? Status::Success
            : Status::InvalidPointerCount;
    }
    if (!sourceField || expectedCount == 0 || span.count != expectedCount)
    {
        return Status::InvalidPointerCount;
    }
    if (!span.data)
        return Status::InvalidSourceLayout;
    if (!IsAligned(span.data))
        return Status::InvalidSourceLayout;

    std::uint64_t byteCount = 0;
    if (!SpanBytes(span, &byteCount))
        return Status::SizeOverflow;
    return AppendProvenanceRequest(
        schedule,
        kind,
        sourceField,
        token,
        &span.data,
        &span.count,
        span.count,
        span.data,
        byteCount,
        alignof(T));
}

[[nodiscard]] Status ValidateUnownedSpan(
    const FxFastFileEffectDefDisk32View &,
    const FxFastFileDisk32SourceSpanKind kind,
    const disk32::PointerToken *const sourceField,
    const disk32::PointerToken token,
    const void *const addressField,
    const void *const address,
    const std::uint64_t byteCount,
    const std::size_t alignment,
    ProvenanceScheduleState *const schedule) noexcept
{
    if (!address
        || reinterpret_cast<std::uintptr_t>(address) % alignment != 0)
    {
        return Status::InvalidSourceLayout;
    }
    return AppendProvenanceRequest(
        schedule,
        kind,
        sourceField,
        token,
        addressField,
        nullptr,
        0,
        address,
        byteCount,
        alignment);
}

[[nodiscard]] bool IsLight(const FxElemTypeDisk32 type) noexcept
{
    return type == FxElemTypeDisk32::OmniLight
        || type == FxElemTypeDisk32::SpotLight;
}

[[nodiscard]] bool UsesMaterial(const FxElemTypeDisk32 type) noexcept
{
    return type == FxElemTypeDisk32::SpriteBillboard
        || type == FxElemTypeDisk32::SpriteOriented
        || type == FxElemTypeDisk32::Tail
        || type == FxElemTypeDisk32::Trail
        || type == FxElemTypeDisk32::Cloud
        || type == FxElemTypeDisk32::Decal;
}

[[nodiscard]] bool ValidateTimeRange(
    const FxIntRangeDisk32 &range,
    const bool requirePositiveMinimum) noexcept
{
    if (range.amplitude < 0
        || range.amplitude > kRandomRangeAmplitudeMax)
    {
        return false;
    }
    const std::int64_t minimum = range.base;
    const std::int64_t maximum =
        static_cast<std::int64_t>(range.base) + range.amplitude;
    return (!requirePositiveMinimum || minimum > 0)
        && minimum >= -kDurationLimitMsec
        && minimum <= kDurationLimitMsec
        && maximum >= -kDurationLimitMsec
        && maximum <= kDurationLimitMsec;
}

[[nodiscard]] bool ValidateOneShotCount(
    const FxSpawnDefDisk32 &spawn) noexcept
{
    if (spawn.intervalMsecOrCountBase < 0
        || spawn.loopCountOrCountAmplitude < 0
        || spawn.loopCountOrCountAmplitude > kRandomRangeAmplitudeMax)
    {
        return false;
    }
    const std::int64_t maximum =
        static_cast<std::int64_t>(spawn.intervalMsecOrCountBase)
        + spawn.loopCountOrCountAmplitude;
    return maximum <= static_cast<std::int64_t>(MAX_ELEMS);
}

[[nodiscard]] bool IsCanonicalZeroAtlas(
    const FxElemAtlasDisk32 &atlas) noexcept
{
    return atlas.behavior == 0 && atlas.index == 0 && atlas.fps == 0
        && atlas.loopCount == 0 && atlas.colIndexBits == 0
        && atlas.rowIndexBits == 0 && atlas.entryCount == 0;
}

[[nodiscard]] bool ValidateAtlas(
    const FxElemDefDisk32 &elem) noexcept
{
    // FX_ConvertAtlas zeroes the complete record for non-material elements and
    // material elements without visuals.  Preserve that canonical inactive
    // representation so untrusted shift counts cannot become live later.
    if (!UsesMaterial(elem.elemType) || elem.visualCount == 0)
        return IsCanonicalZeroAtlas(elem.atlas);

    const std::int32_t entryCount = elem.atlas.entryCount;
    const std::uint32_t colBits = elem.atlas.colIndexBits;
    const std::uint32_t rowBits = elem.atlas.rowIndexBits;
    const std::uint32_t totalBits = colBits + rowBits;
    if (entryCount <= 0 || entryCount > 256 || colBits > 8
        || rowBits > 8 || totalBits > 8
        || static_cast<std::uint32_t>(entryCount) != (1u << totalBits)
        || (elem.atlas.behavior & 3u) == 3u)
    {
        return false;
    }

    // FX_GetSpriteTexCoords multiplies fps by the signed millisecond age
    // before dividing by 1000.  Bound the product for every possible sampled
    // lifespan when time-based atlas animation is active.
    const std::int64_t maximumLife =
        static_cast<std::int64_t>(elem.lifeSpanMsec.base)
        + elem.lifeSpanMsec.amplitude;
    return (elem.atlas.behavior & 4u) != 0 || elem.atlas.fps == 0
        || maximumLife
            <= (std::numeric_limits<std::int32_t>::max)()
                / elem.atlas.fps;
}

[[nodiscard]] Status ValidateElementSemantics(
    const FxEffectDefDisk32 &effect,
    const std::uint32_t index,
    const FxElemDefDisk32 &elem,
    std::int64_t *const maximumLoopingLife,
    bool *const hasInfiniteLoop) noexcept
{
    if (!maximumLoopingLife || !hasInfiniteLoop
        || !ValidateTimeRange(elem.spawnDelayMsec, false)
        || !ValidateTimeRange(elem.lifeSpanMsec, true))
    {
        return Status::InvalidCount;
    }

    if (index < static_cast<std::uint32_t>(effect.elemDefCountLooping))
    {
        const std::int32_t interval = elem.spawn.intervalMsecOrCountBase;
        const std::int32_t count = elem.spawn.loopCountOrCountAmplitude;
        if (interval <= 0 || interval > kDurationLimitMsec || count <= 0)
        {
            return Status::InvalidCount;
        }
        if (count == (std::numeric_limits<std::int32_t>::max)())
        {
            *hasInfiniteLoop = true;
        }
        else
        {
            const std::int64_t lastSpawn = count > 1
                ? static_cast<std::int64_t>(interval) * (count - 1)
                : 0;
            if (lastSpawn > kDurationLimitMsec)
                return Status::InvalidCount;
            if (*maximumLoopingLife < lastSpawn)
                *maximumLoopingLife = lastSpawn;
        }
    }
    else if (!ValidateOneShotCount(elem.spawn))
    {
        return Status::InvalidCount;
    }

    // Every compiler routing except Runner has at least one visual channel;
    // FX_SetupVisualState then unconditionally dereferences two samples.
    const bool requiresVisibilitySamples =
        elem.elemType != FxElemTypeDisk32::Runner;
    if ((requiresVisibilitySamples
            && (elem.visStateIntervalCount == 0
                || elem.visSamples.token.isNull()))
        || (!requiresVisibilitySamples
            && (elem.visStateIntervalCount != 0
                || !elem.visSamples.token.isNull()))
        || !ValidateAtlas(elem))
    {
        return Status::InvalidVisual;
    }
    return Status::Success;
}

[[nodiscard]] FxFastFileDisk32ReferenceKind VisualReferenceKind(
    const FxElemTypeDisk32 type) noexcept
{
    if (type == FxElemTypeDisk32::Model)
        return FxFastFileDisk32ReferenceKind::Model;
    if (type == FxElemTypeDisk32::Sound)
        return FxFastFileDisk32ReferenceKind::SoundName;
    if (type == FxElemTypeDisk32::Runner)
        return FxFastFileDisk32ReferenceKind::EffectNameReference;
    return FxFastFileDisk32ReferenceKind::Material;
}

[[nodiscard]] Status ValidateGraph(
    const FxFastFileEffectDefDisk32View &source,
    ProvenanceScheduleState *const schedule) noexcept
{
    if (!source.effect || !source.provenance.validateSpan)
        return Status::InvalidArgument;

    Status status = ValidateUnownedSpan(
        source,
        FxFastFileDisk32SourceSpanKind::EffectHeader,
        nullptr,
        {},
        &source.effect,
        source.effect,
        sizeof(*source.effect),
        alignof(FxEffectDefDisk32),
        schedule);
    if (status != Status::Success)
        return status;

    const FxEffectDefDisk32 &effect = *source.effect;
    if (effect.elemDefCountLooping < 0 || effect.elemDefCountOneShot < 0
        || effect.elemDefCountEmission < 0)
    {
        return Status::InvalidCount;
    }
    const std::uint64_t elementCount =
        static_cast<std::uint64_t>(effect.elemDefCountLooping)
        + static_cast<std::uint64_t>(effect.elemDefCountOneShot)
        + static_cast<std::uint64_t>(effect.elemDefCountEmission);
    if (elementCount > kFxFastFileDisk32MaxEffectElements
        || source.elements.count != elementCount
        || source.elementViews.count != elementCount)
    {
        return Status::InvalidCount;
    }
    if ((elementCount == 0)
        != (effect.elemDefs.token.isNull()))
    {
        return Status::InvalidPointerCount;
    }
    if (elementCount == 0)
    {
        if (!IsEmpty(source.elements) || !IsEmpty(source.elementViews))
            return Status::InvalidPointerCount;
    }
    else
    {
        status = ValidateOwnedSpan(
            source,
            FxFastFileDisk32SourceSpanKind::ElementDefinitions,
            &effect.elemDefs.token,
            effect.elemDefs.token,
            source.elements,
            static_cast<std::uint32_t>(elementCount),
            schedule);
        if (status != Status::Success)
            return status;
        if (!source.elementViews.data
            || !IsAligned(source.elementViews.data))
        {
            return Status::InvalidSourceLayout;
        }
    }
    if (effect.name.token.isNull())
        return Status::InvalidPointerCount;

    std::int64_t maximumLoopingLife = 0;
    bool hasInfiniteLoop = false;
    for (std::uint32_t index = 0; index < elementCount; ++index)
    {
        const FxElemDefDisk32 &elem = source.elements.data[index];
        const FxFastFileElemDefDisk32View &view =
            source.elementViews.data[index];
        if (elem.elemType >= FxElemTypeDisk32::Count)
            return Status::InvalidVisual;

        status = ValidateElementSemantics(
            effect,
            index,
            elem,
            &maximumLoopingLife,
            &hasInfiniteLoop);
        if (status != Status::Success)
            return status;

        if (elem.velSamples.token.isNull() || elem.velIntervalCount == 0)
            return Status::InvalidPointerCount;
        status = ValidateOwnedSpan(
            source,
            FxFastFileDisk32SourceSpanKind::VelocitySamples,
            &elem.velSamples.token,
            elem.velSamples.token,
            view.velocitySamples,
            static_cast<std::uint32_t>(elem.velIntervalCount) + 1u,
            schedule);
        if (status != Status::Success)
            return status;

        if (elem.visSamples.token.isNull())
        {
            if (elem.visStateIntervalCount != 0
                || !IsEmpty(view.visibilitySamples))
            {
                return Status::InvalidPointerCount;
            }
        }
        else
        {
            status = ValidateOwnedSpan(
                source,
                FxFastFileDisk32SourceSpanKind::VisibilitySamples,
                &elem.visSamples.token,
                elem.visSamples.token,
                view.visibilitySamples,
                static_cast<std::uint32_t>(elem.visStateIntervalCount) + 1u,
                schedule);
            if (status != Status::Success)
                return status;
        }

        if (IsLight(elem.elemType))
        {
            if (elem.visualCount != 1 || !elem.visuals.token.isNull()
                || !IsEmpty(view.visuals) || !IsEmpty(view.markVisuals))
            {
                return Status::InvalidVisual;
            }
        }
        else if (elem.elemType == FxElemTypeDisk32::Decal)
        {
            if (elem.visualCount == 0
                || elem.visualCount > kFxFastFileDisk32MaxDecalVisuals
                || !IsEmpty(view.visuals))
            {
                return Status::InvalidVisual;
            }
            status = ValidateOwnedSpan(
                source,
                FxFastFileDisk32SourceSpanKind::MarkVisuals,
                &elem.visuals.token,
                elem.visuals.token,
                view.markVisuals,
                elem.visualCount,
                schedule);
            if (status != Status::Success)
                return status;
        }
        else
        {
            if (elem.visualCount > kFxFastFileDisk32MaxVisuals
                || !IsEmpty(view.markVisuals)
                || (elem.elemType == FxElemTypeDisk32::Runner
                    && elem.visualCount == 0))
            {
                return Status::InvalidVisual;
            }
            if (elem.visualCount == 0)
            {
                if (!elem.visuals.token.isNull() || !IsEmpty(view.visuals))
                    return Status::InvalidVisual;
            }
            else if (elem.visualCount == 1)
            {
                if (elem.visuals.token.isNull() || !IsEmpty(view.visuals))
                    return Status::InvalidVisual;
            }
            else
            {
                status = ValidateOwnedSpan(
                    source,
                    FxFastFileDisk32SourceSpanKind::Visuals,
                    &elem.visuals.token,
                    elem.visuals.token,
                    view.visuals,
                    elem.visualCount,
                    schedule);
                if (status != Status::Success)
                    return status;
            }
        }

        if (elem.elemType != FxElemTypeDisk32::Trail)
        {
            if (!elem.trailDef.token.isNull() || view.trail
                || !IsEmpty(view.trailVertices)
                || !IsEmpty(view.trailIndices))
            {
                return Status::InvalidTrail;
            }
            continue;
        }
        if (elem.trailDef.token.isNull() || !view.trail)
            return Status::InvalidTrail;
        status = ValidateUnownedSpan(
            source,
            FxFastFileDisk32SourceSpanKind::TrailDefinition,
            &elem.trailDef.token,
            elem.trailDef.token,
            &view.trail,
            view.trail,
            sizeof(*view.trail),
            alignof(FxTrailDefDisk32),
            schedule);
        if (status != Status::Success)
            return status;

        const FxTrailDefDisk32 &trail = *view.trail;
        if (trail.repeatDist <= 0 || trail.splitDist <= 0
            || trail.vertCount <= 0 || trail.vertCount > kMaxTrailVertices
            || trail.indCount <= 0 || trail.indCount > kMaxTrailIndices
            || trail.vertCount > trail.indCount
            || (trail.indCount & 1) != 0)
        {
            return Status::InvalidTrail;
        }
        status = ValidateOwnedSpan(
            source,
            FxFastFileDisk32SourceSpanKind::TrailVertices,
            &trail.verts.token,
            trail.verts.token,
            view.trailVertices,
            static_cast<std::uint32_t>(trail.vertCount),
            schedule);
        if (status != Status::Success)
            return status == Status::InvalidProvenance
                ? status
                : Status::InvalidTrail;
        status = ValidateOwnedSpan(
            source,
            FxFastFileDisk32SourceSpanKind::TrailIndices,
            &trail.inds.token,
            trail.inds.token,
            view.trailIndices,
            static_cast<std::uint32_t>(trail.indCount),
            schedule);
        if (status != Status::Success)
            return status == Status::InvalidProvenance
                ? status
                : Status::InvalidTrail;
        for (std::uint32_t trailIndex = 0;
             trailIndex < view.trailIndices.count;
             ++trailIndex)
        {
            if (view.trailIndices.data[trailIndex] >= trail.vertCount)
                return Status::InvalidTrail;
        }

        // The runtime's trail updater only accepts trail definitions from the
        // looping range.  Validate the complete serialized trail first so a
        // malformed span still reports its structural failure deterministically.
        if (index
            >= static_cast<std::uint32_t>(effect.elemDefCountLooping))
        {
            return Status::InvalidTrail;
        }
    }
    const std::int32_t expectedLoopingLife = hasInfiniteLoop
        ? (std::numeric_limits<std::int32_t>::max)()
        : static_cast<std::int32_t>(maximumLoopingLife);
    return effect.msecLoopingLife == expectedLoopingLife
        ? Status::Success
        : Status::InvalidCount;
}

[[nodiscard]] Status PlanLayout(
    const FxFastFileEffectDefDisk32View &source,
    const std::uint32_t effectNameBytes,
    FxRuntimeBlobCursor *const cursor) noexcept
{
    if (!cursor || !cursor->ReserveArray<FxEffectDef>(1)
        || !cursor->ReserveArray<FxElemDef>(
            source.elements.count))
    {
        return Status::SizeOverflow;
    }
    for (std::uint32_t index = 0; index < source.elements.count; ++index)
    {
        const FxElemDefDisk32 &elem = source.elements.data[index];
        const FxFastFileElemDefDisk32View &view =
            source.elementViews.data[index];
        if (!elem.velSamples.token.isNull()
            && !cursor->ReserveArray<FxElemVelStateSample>(
                view.velocitySamples.count))
        {
            return Status::SizeOverflow;
        }
        if (!elem.visSamples.token.isNull()
            && !cursor->ReserveArray<FxElemVisStateSample>(
                view.visibilitySamples.count))
        {
            return Status::SizeOverflow;
        }
        if (elem.elemType == FxElemTypeDisk32::Decal)
        {
            if (!cursor->ReserveArray<FxElemMarkVisuals>(
                    elem.visualCount))
                return Status::SizeOverflow;
        }
        else if (!IsLight(elem.elemType) && elem.visualCount > 1
                 && !cursor->ReserveArray<FxElemVisuals>(
                     elem.visualCount))
        {
            return Status::SizeOverflow;
        }
        if (view.trail)
        {
            if (!cursor->ReserveArray<FxTrailDef>(1)
                || !cursor->ReserveArray<FxTrailVertex>(
                    static_cast<std::uint32_t>(view.trail->indCount))
                || !cursor->ReserveArray<std::uint16_t>(
                    static_cast<std::uint32_t>(view.trail->indCount)))
            {
                return Status::SizeOverflow;
            }
        }
    }
    return cursor->ReserveArray<char>(effectNameBytes)
        ? Status::Success
        : Status::SizeOverflow;
}

[[nodiscard]] Status PlanDisk32Layout(
    const FxFastFileEffectDefDisk32View &source,
    const std::uint32_t effectNameBytes,
    FxRuntimeBlobCursor *const cursor) noexcept
{
    if (!cursor || !cursor->ReserveArray<FxEffectDefDisk32>(1)
        || !cursor->ReserveArray<FxElemDefDisk32>(source.elements.count))
    {
        return Status::SizeOverflow;
    }
    for (std::uint32_t index = 0; index < source.elements.count; ++index)
    {
        const FxElemDefDisk32 &elem = source.elements.data[index];
        const FxFastFileElemDefDisk32View &view =
            source.elementViews.data[index];
        if (!cursor->ReserveArray<FxElemVelStateSampleDisk32>(
                view.velocitySamples.count))
        {
            return Status::SizeOverflow;
        }
        if (!elem.visSamples.token.isNull()
            && !cursor->ReserveArray<FxElemVisStateSampleDisk32>(
                view.visibilitySamples.count))
        {
            return Status::SizeOverflow;
        }
        if (elem.elemType == FxElemTypeDisk32::Decal)
        {
            if (!cursor->ReserveArray<FxElemMarkVisualsDisk32>(
                    elem.visualCount))
            {
                return Status::SizeOverflow;
            }
        }
        else if (!IsLight(elem.elemType) && elem.visualCount > 1
                 && !cursor->ReserveArray<FxElemVisualsDisk32>(
                     elem.visualCount))
        {
            return Status::SizeOverflow;
        }
        if (view.trail)
        {
            if (!cursor->ReserveArray<FxTrailDefDisk32>(1)
                || !cursor->ReserveArray<FxTrailVertexDisk32>(
                    static_cast<std::uint32_t>(view.trail->indCount))
                || !cursor->ReserveArray<std::uint16_t>(
                    static_cast<std::uint32_t>(view.trail->indCount)))
            {
                return Status::SizeOverflow;
            }
        }
    }
    return cursor->ReserveArray<char>(effectNameBytes)
        ? Status::Success
        : Status::SizeOverflow;
}

class Fingerprint final
{
public:
    void Bytes(const void *const data, const std::size_t size) noexcept
    {
        const auto *bytes = static_cast<const std::uint8_t *>(data);
        for (std::size_t index = 0; index < size; ++index)
        {
            value_ ^= bytes[index];
            value_ *= kFnvPrime;
        }
    }

    template <typename T>
    void Scalar(const T value) noexcept
    {
        static_assert(std::is_trivially_copyable_v<T>);
        Bytes(&value, sizeof(value));
    }

    [[nodiscard]] std::uint64_t Value() const noexcept { return value_; }

private:
    std::uint64_t value_ = kFnvOffset;
};

void HashProvenanceRequest(
    Fingerprint *const hash,
    const FxFastFileDisk32FrozenProvenanceRequest &request) noexcept
{
    hash->Scalar(reinterpret_cast<std::uintptr_t>(request.sourceField));
#if !KISAK_ARCH_64BIT
    hash->Scalar(request.sourceFieldPadding_);
#endif
    hash->Scalar(reinterpret_cast<std::uintptr_t>(request.addressField));
#if !KISAK_ARCH_64BIT
    hash->Scalar(request.addressFieldPadding_);
#endif
    hash->Scalar(reinterpret_cast<std::uintptr_t>(request.countField));
#if !KISAK_ARCH_64BIT
    hash->Scalar(request.countFieldPadding_);
#endif
    hash->Scalar(reinterpret_cast<std::uintptr_t>(request.address));
#if !KISAK_ARCH_64BIT
    hash->Scalar(request.addressPadding_);
#endif
    hash->Scalar(request.byteCount);
    hash->Scalar(request.alignment);
    hash->Scalar(request.contentFingerprint);
    hash->Scalar(request.token.value);
    hash->Scalar(request.countValue);
    hash->Scalar(request.kind);
    hash->Bytes(request.reserved_, sizeof(request.reserved_));
}

[[nodiscard]] std::uint64_t ComputeProvenanceRequestChecksum(
    const FxFastFileDisk32FrozenProvenanceRequest &request) noexcept
{
    Fingerprint hash;
    HashProvenanceRequest(&hash, request);
    return hash.Value();
}

[[nodiscard]] std::uint64_t ComputeProvenanceJournalFingerprint(
    const FxFastFileDisk32FrozenProvenanceRequest *const requests,
    const std::uint64_t *const checksums,
    const std::uint32_t requestCount) noexcept
{
    Fingerprint hash;
    hash.Scalar(requestCount);
    for (std::uint32_t index = 0; index < requestCount; ++index)
    {
        HashProvenanceRequest(&hash, requests[index]);
        hash.Scalar(checksums[index]);
    }
    return hash.Value();
}

[[nodiscard]] bool IsCanonicalProvenanceRequest(
    const FxFastFileDisk32FrozenProvenanceRequest &request) noexcept
{
#if !KISAK_ARCH_64BIT
    if (request.sourceFieldPadding_ != 0
        || request.addressFieldPadding_ != 0
        || request.countFieldPadding_ != 0
        || request.addressPadding_ != 0)
        return false;
#endif
    return request.addressField && request.address
        && request.byteCount != 0
        && request.alignment != 0
        && (request.countField || request.countValue == 0)
        && request.reserved_[0] == 0 && request.reserved_[1] == 0
        && request.reserved_[2] == 0;
}

[[nodiscard]] bool FrozenProvenanceJournalMatches(
    const FxFastFileDisk32FrozenProvenanceRequest *const requests,
    const std::uint64_t *const checksums,
    const std::uint32_t requestCount,
    const std::uint64_t journalFingerprint) noexcept
{
    if (!requests || !checksums || requestCount == 0
        || requestCount > kFxFastFileDisk32MaxProvenanceRequests
        || ComputeProvenanceJournalFingerprint(
               requests, checksums, requestCount)
            != journalFingerprint)
    {
        return false;
    }
    for (std::uint32_t index = 0; index < requestCount; ++index)
    {
        if (checksums[index]
            != ComputeProvenanceRequestChecksum(requests[index]))
            return false;
    }
    return true;
}

[[nodiscard]] bool FrozenProvenanceBindingMatches(
    const FxFastFileDisk32FrozenProvenanceRequest &request,
    const std::uint64_t checksum) noexcept
{
    if (!IsCanonicalProvenanceRequest(request)
        || checksum != ComputeProvenanceRequestChecksum(request))
    {
        return false;
    }
    const void *liveAddress = nullptr;
    std::memcpy(&liveAddress, request.addressField, sizeof(liveAddress));
    return liveAddress == request.address
        && (!request.countField
            || *request.countField == request.countValue)
        && (!request.sourceField
            ? request.token.isNull()
            : request.sourceField->value == request.token.value);
}

[[nodiscard]] bool FrozenProvenanceContentMatches(
    const FxFastFileDisk32FrozenProvenanceRequest &request) noexcept
{
    std::uint64_t contentFingerprint = 0;
    return ComputeContentFingerprint(
               request.address,
               request.byteCount,
               &contentFingerprint)
        && contentFingerprint == request.contentFingerprint;
}

[[nodiscard]] Status InvokeFrozenProvenanceRequests(
    const FxFastFileDisk32Provenance &provenance,
    FxFastFileDisk32FrozenProvenanceRequest *const requests,
    std::uint64_t *const checksums,
    const std::uint32_t requestCount,
    const std::uint64_t journalFingerprint) noexcept
{
    if (!provenance.validateSpan || !requests || !checksums
        || requestCount == 0
        || requestCount > kFxFastFileDisk32MaxProvenanceRequests)
    {
        return Status::InvalidArgument;
    }
    if (!FrozenProvenanceJournalMatches(
            requests, checksums, requestCount, journalFingerprint))
    {
        return Status::SourceChanged;
    }

    for (std::uint32_t index = 0; index < requestCount; ++index)
    {
        if (!FrozenProvenanceBindingMatches(
                requests[index], checksums[index])
            || !FrozenProvenanceContentMatches(requests[index]))
        {
            return Status::SourceChanged;
        }
        const FxFastFileDisk32FrozenProvenanceRequest request =
            requests[index];

        const bool accepted = provenance.validateSpan(
            provenance.context,
            request.kind,
            request.sourceField,
            request.token,
            request.address,
            request.byteCount,
            static_cast<std::size_t>(request.alignment));

        if (!FrozenProvenanceBindingMatches(
                requests[index], checksums[index])
            || !FrozenProvenanceContentMatches(requests[index]))
        {
            return Status::SourceChanged;
        }
        if (!accepted)
            return Status::InvalidProvenance;
    }
    return FrozenProvenanceJournalMatches(
               requests, checksums, requestCount, journalFingerprint)
        ? Status::Success
        : Status::SourceChanged;
}

template <typename T>
void HashSpan(Fingerprint *const hash, const FxFastFileDisk32Span<T> span)
    noexcept
{
    hash->Scalar(reinterpret_cast<std::uintptr_t>(span.data));
    hash->Scalar(span.count);
    if (span.data && span.count)
    {
        hash->Bytes(
            span.data, static_cast<std::size_t>(span.count) * sizeof(T));
    }
}

[[nodiscard]] std::uint64_t ComputeSourceFingerprint(
    const FxFastFileEffectDefDisk32View &source) noexcept
{
    Fingerprint hash;
    hash.Scalar(reinterpret_cast<std::uintptr_t>(source.effect));
    if (!source.effect
        || (source.elementViews.count && !source.elementViews.data))
    {
        return hash.Value();
    }
    hash.Bytes(source.effect, sizeof(*source.effect));
    HashSpan(&hash, source.elements);
    hash.Scalar(reinterpret_cast<std::uintptr_t>(source.elementViews.data));
    hash.Scalar(source.elementViews.count);
    for (std::uint32_t index = 0; index < source.elementViews.count; ++index)
    {
        const FxFastFileElemDefDisk32View &view =
            source.elementViews.data[index];
        HashSpan(&hash, view.velocitySamples);
        HashSpan(&hash, view.visibilitySamples);
        HashSpan(&hash, view.visuals);
        HashSpan(&hash, view.markVisuals);
        hash.Scalar(reinterpret_cast<std::uintptr_t>(view.trail));
        if (view.trail)
            hash.Bytes(view.trail, sizeof(*view.trail));
        HashSpan(&hash, view.trailVertices);
        HashSpan(&hash, view.trailIndices);
    }
    return hash.Value();
}

[[nodiscard]] std::uint64_t ComputeBoundFingerprint(
    const FxFastFileEffectDefDisk32View &source,
    const FxFastFileDisk32ResolvedReference *const resolved,
    const std::uint32_t resolvedCount) noexcept
{
    Fingerprint hash;
    const std::uint64_t sourceHash = ComputeSourceFingerprint(source);
    hash.Scalar(sourceHash);
    hash.Scalar(resolvedCount);
    for (std::uint32_t index = 0; index < resolvedCount; ++index)
    {
        hash.Scalar(reinterpret_cast<std::uintptr_t>(resolved[index].pointer));
        hash.Scalar(resolved[index].retainedByteCount);
        hash.Scalar(resolved[index].retainedAlignment);
        hash.Bytes(
            resolved[index].pointer,
            static_cast<std::size_t>(resolved[index].retainedByteCount));
    }
    return hash.Value();
}

[[nodiscard]] bool IsExactCString(
    const FxFastFileDisk32ResolvedReference &reference) noexcept
{
    if (!reference.pointer || reference.retainedByteCount < 2
        || reference.retainedByteCount
            > (std::numeric_limits<std::size_t>::max)())
    {
        return false;
    }
    const std::size_t bytes =
        static_cast<std::size_t>(reference.retainedByteCount);
    const auto *text = static_cast<const char *>(reference.pointer);
    return text[bytes - 1] == '\0'
        && !std::memchr(text, '\0', bytes - 1);
}

[[nodiscard]] std::uint64_t ComputeResolvedReferenceFingerprint(
    const FxFastFileDisk32ResolvedReference &reference) noexcept
{
    Fingerprint hash;
    hash.Scalar(reinterpret_cast<std::uintptr_t>(reference.pointer));
    hash.Scalar(reference.retainedByteCount);
    hash.Scalar(reference.retainedAlignment);
    hash.Bytes(
        reference.pointer,
        static_cast<std::size_t>(reference.retainedByteCount));
    return hash.Value();
}

void HashResolvedReference(
    Fingerprint *const hash,
    const FxFastFileDisk32ResolvedReference &reference,
    const std::uint32_t index) noexcept
{
    hash->Scalar(index);
    hash->Scalar(reinterpret_cast<std::uintptr_t>(reference.pointer));
    hash->Scalar(reference.retainedByteCount);
    hash->Scalar(reference.retainedAlignment);
    hash->Bytes(
        reference.pointer,
        static_cast<std::size_t>(reference.retainedByteCount));
}

[[nodiscard]] std::uint64_t ComputeResolvedReferencesFingerprint(
    const FxFastFileDisk32ResolvedReference *const resolved,
    const std::uint32_t resolvedCount) noexcept
{
    Fingerprint hash;
    for (std::uint32_t index = 0; index < resolvedCount; ++index)
        HashResolvedReference(&hash, resolved[index], index);
    hash.Scalar(resolvedCount);
    return hash.Value();
}

[[nodiscard]] bool IsPowerOfTwo(const std::uint64_t value) noexcept
{
    return value != 0 && (value & (value - 1u)) == 0;
}

[[nodiscard]] Status ValidateRetainedDescriptor(
    const FxFastFileDisk32ReferenceKind kind,
    const FxFastFileDisk32ResolvedReference &reference,
    std::size_t *const outBytes) noexcept
{
    const bool isString = kind == FxFastFileDisk32ReferenceKind::EffectName
        || kind == FxFastFileDisk32ReferenceKind::SoundName;
    const Status invalid = isString ? Status::InvalidString
                                    : Status::InvalidArgument;
#if !KISAK_ARCH_64BIT
    if (reference.pointerPadding_ != 0)
        return invalid;
#endif
    if (!reference.pointer || !outBytes || reference.retainedByteCount == 0
        || reference.retainedByteCount
            > (std::numeric_limits<std::size_t>::max)()
        || !IsPowerOfTwo(reference.retainedAlignment)
        || reference.retainedAlignment
            > (std::numeric_limits<std::size_t>::max)())
    {
        return invalid;
    }

    const std::size_t bytes =
        static_cast<std::size_t>(reference.retainedByteCount);
    const std::size_t alignment =
        static_cast<std::size_t>(reference.retainedAlignment);
    const std::uintptr_t begin =
        reinterpret_cast<std::uintptr_t>(reference.pointer);
    if (begin % alignment != 0
        || bytes > (std::numeric_limits<std::uintptr_t>::max)() - begin)
    {
        return invalid;
    }
    *outBytes = bytes;
    return Status::Success;
}

[[nodiscard]] Status ValidateResolvedReference(
    const FxFastFileDisk32ReferenceKind kind,
    const FxFastFileDisk32ResolvedReference &reference,
    std::size_t *const outBytes) noexcept
{
    Status status = ValidateRetainedDescriptor(kind, reference, outBytes);
    if (status != Status::Success)
        return status;

    switch (kind)
    {
    case FxFastFileDisk32ReferenceKind::EffectName:
    case FxFastFileDisk32ReferenceKind::SoundName:
        return reference.retainedAlignment == alignof(char)
                && IsExactCString(reference)
            ? Status::Success
            : Status::InvalidString;

    case FxFastFileDisk32ReferenceKind::EffectNameReference:
    case FxFastFileDisk32ReferenceKind::EffectAssetHandle:
        return reference.retainedByteCount == sizeof(FxEffectDef)
                && reference.retainedAlignment == alignof(FxEffectDef)
            ? Status::Success
            : Status::InvalidArgument;

    case FxFastFileDisk32ReferenceKind::Material:
    case FxFastFileDisk32ReferenceKind::Model:
        return reference.retainedAlignment >= alignof(void *)
            ? Status::Success
            : Status::InvalidArgument;
    }
    return Status::InvalidArgument;
}

[[nodiscard]] bool ValidateRetainedJournalRanges(
    const FxFastFileDisk32ResolvedReference *const resolved,
    const std::uint32_t resolvedCount) noexcept
{
    if (!resolved || resolvedCount == 0
        || resolvedCount > kFxFastFileDisk32MaxResolvedReferences)
    {
        return false;
    }
    for (std::uint32_t index = 0; index < resolvedCount; ++index)
    {
        std::size_t bytes = 0;
        if (ValidateRetainedDescriptor(
                FxFastFileDisk32ReferenceKind::Material,
                resolved[index],
                &bytes)
            != Status::Success)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool RangesOverlap(
    const void *left,
    std::size_t leftBytes,
    const void *right,
    std::size_t rightBytes) noexcept;

[[nodiscard]] bool OverlapsSource(
    const void *storage,
    std::size_t storageBytes,
    const FxFastFileEffectDefDisk32View &source,
    const FxFastFileDisk32ResolvedReference *resolved,
    std::uint32_t resolvedCount) noexcept;

[[nodiscard]] Status ResolveOne(
    const FxFastFileNativeDisk32Workspace *const workspace,
    const FxFastFileEffectDefDisk32View &source,
    const FxFastFileEffectDefDisk32View *const sourceArgument,
    const FxFastFileDisk32Resolvers &resolvers,
    const FxFastFileDisk32Resolvers *const resolverArgument,
    const FxFastFileNativeDisk32Plan *const outPlan,
    const FxFastFileDisk32ReferenceKind kind,
    const disk32::PointerToken *const sourceField,
    const disk32::PointerToken token,
    FxFastFileDisk32ResolvedReference *const journal,
    std::uint32_t *const journalCount,
    Fingerprint *const expectedReferences) noexcept
{
    if (!workspace || !sourceArgument || !resolverArgument || !outPlan
        || !sourceField || token.isNull()
        || !journal || !journalCount || !expectedReferences
        || *journalCount >= kFxFastFileDisk32MaxResolvedReferences)
    {
        return Status::InvalidPointerCount;
    }
    FxFastFileDisk32ResolvedReference resolved{};
    const bool wasResolved = resolvers.resolve(
        resolvers.context, kind, sourceField, token, &resolved);
    if (!wasResolved || !resolved.pointer)
    {
        return Status::UnresolvedReference;
    }
    std::size_t retainedBytes = 0;
    Status status = ValidateRetainedDescriptor(kind, resolved, &retainedBytes);
    if (status != Status::Success)
        return status;
    if (RangesOverlap(
            resolved.pointer,
            retainedBytes,
            &resolved,
            sizeof(resolved)))
    {
        return Status::InvalidArgument;
    }
    if (RangesOverlap(
            resolved.pointer, retainedBytes, workspace, sizeof(*workspace))
        || RangesOverlap(
            resolved.pointer, retainedBytes, &source, sizeof(source))
        || RangesOverlap(
            resolved.pointer,
            retainedBytes,
            sourceArgument,
            sizeof(*sourceArgument))
        || RangesOverlap(
            resolved.pointer, retainedBytes, &resolvers, sizeof(resolvers))
        || RangesOverlap(
            resolved.pointer,
            retainedBytes,
            resolverArgument,
            sizeof(*resolverArgument))
        || RangesOverlap(
            resolved.pointer, retainedBytes, outPlan, sizeof(*outPlan))
        || OverlapsSource(
            resolved.pointer, retainedBytes, source, nullptr, 0))
    {
        return Status::InvalidArgument;
    }
    status = ValidateResolvedReference(kind, resolved, &retainedBytes);
    if (status != Status::Success)
        return status;

    const bool isString = kind == FxFastFileDisk32ReferenceKind::EffectName
        || kind == FxFastFileDisk32ReferenceKind::SoundName;
    const void *const retainedAddress = resolved.pointer;
    const std::uint64_t retainedByteCount = resolved.retainedByteCount;
    const std::uint64_t retainedAlignment = resolved.retainedAlignment;
    const std::uint64_t retainedFingerprint =
        ComputeResolvedReferenceFingerprint(resolved);
    if (isString)
    {
        // The resolver contract keeps this returned span readable for the
        // transaction. Bind it before invoking provenance so that callback
        // cannot silently replace the bytes it was asked to attest.
        const bool provenanceIsValid = source.provenance.validateSpan(
            source.provenance.context,
            FxFastFileDisk32SourceSpanKind::String,
            sourceField,
            token,
            resolved.pointer,
            resolved.retainedByteCount,
            static_cast<std::size_t>(resolved.retainedAlignment));
        std::size_t verifiedBytes = 0;
        if (resolved.pointer != retainedAddress
            || resolved.retainedByteCount != retainedByteCount
            || resolved.retainedAlignment != retainedAlignment
#if !KISAK_ARCH_64BIT
            || resolved.pointerPadding_ != 0
#endif
            || ValidateResolvedReference(kind, resolved, &verifiedBytes)
                != Status::Success
            || verifiedBytes != retainedBytes
            || ComputeResolvedReferenceFingerprint(resolved)
                != retainedFingerprint)
        {
            return Status::SourceChanged;
        }
        if (!provenanceIsValid)
            return Status::InvalidProvenance;
    }
    const std::uint32_t resolvedIndex = *journalCount;
    HashResolvedReference(expectedReferences, resolved, resolvedIndex);
    journal[resolvedIndex] = resolved;
    ++*journalCount;
    return Status::Success;
}

constexpr std::uint32_t kMaxElementResolveRequests =
    kFxFastFileDisk32MaxVisuals + 3u;

struct FrozenResolveRequest final
{
    FxFastFileDisk32ReferenceKind kind{};
    const disk32::PointerToken *sourceField = nullptr;
    disk32::PointerToken token{};
};

void HashResolveRequest(
    Fingerprint *const hash,
    const FxFastFileDisk32ReferenceKind kind,
    const disk32::PointerToken *const sourceField,
    const disk32::PointerToken token) noexcept
{
    hash->Scalar(kind);
    hash->Scalar(reinterpret_cast<std::uintptr_t>(sourceField));
    hash->Scalar(token.value);
}

[[nodiscard]] bool AppendFrozenRequest(
    FrozenResolveRequest *const requests,
    std::uint32_t *const requestCount,
    Fingerprint *const schedule,
    const FxFastFileDisk32ReferenceKind kind,
    const disk32::PointerToken *const sourceField,
    const disk32::PointerToken token,
    const bool optional) noexcept
{
    if (!requests || !requestCount || !sourceField)
        return false;
    if (schedule)
        HashResolveRequest(schedule, kind, sourceField, token);
    if (token.isNull() && optional)
        return true;
    if (*requestCount >= kMaxElementResolveRequests)
        return false;
    requests[(*requestCount)++] = {kind, sourceField, token};
    return true;
}

[[nodiscard]] bool FreezeElementResolveRequests(
    const FxFastFileEffectDefDisk32View &source,
    const std::uint32_t index,
    FrozenResolveRequest *const requests,
    std::uint32_t *const requestCount,
    Fingerprint *const schedule) noexcept
{
    if (!requests || !requestCount || index >= source.elements.count
        || index >= source.elementViews.count || !source.elements.data
        || !source.elementViews.data)
    {
        return false;
    }
    *requestCount = 0;
    const FxElemDefDisk32 &elem = source.elements.data[index];
    const FxFastFileElemDefDisk32View &view =
        source.elementViews.data[index];
    if (schedule)
    {
        schedule->Scalar(index);
        schedule->Scalar(reinterpret_cast<std::uintptr_t>(&elem));
        schedule->Scalar(reinterpret_cast<std::uintptr_t>(&view));
        schedule->Scalar(elem.elemType);
        schedule->Scalar(elem.visualCount);
    }
    if (elem.elemType >= FxElemTypeDisk32::Count
        || elem.visualCount > kFxFastFileDisk32MaxVisuals)
    {
        return false;
    }

    if (elem.elemType == FxElemTypeDisk32::Decal)
    {
        if (elem.visualCount == 0
            || elem.visualCount > kFxFastFileDisk32MaxDecalVisuals
            || !view.markVisuals.data)
        {
            return false;
        }
        for (std::uint32_t visual = 0; visual < elem.visualCount; ++visual)
        {
            for (std::uint32_t material = 0; material < 2; ++material)
            {
                const disk32::PointerToken *const field =
                    &view.markVisuals.data[visual]
                         .materials[material].token;
                if (!AppendFrozenRequest(
                        requests,
                        requestCount,
                        schedule,
                        FxFastFileDisk32ReferenceKind::Material,
                        field,
                        *field,
                        false))
                {
                    return false;
                }
            }
        }
    }
    else if (!IsLight(elem.elemType) && elem.visualCount)
    {
        const FxFastFileDisk32ReferenceKind kind =
            VisualReferenceKind(elem.elemType);
        if (elem.visualCount == 1)
        {
            if (!AppendFrozenRequest(
                    requests,
                    requestCount,
                    schedule,
                    kind,
                    &elem.visuals.token,
                    elem.visuals.token,
                    false))
            {
                return false;
            }
        }
        else
        {
            if (!view.visuals.data)
                return false;
            for (std::uint32_t visual = 0; visual < elem.visualCount;
                 ++visual)
            {
                const disk32::PointerToken *const field =
                    &view.visuals.data[visual].token;
                if (!AppendFrozenRequest(
                        requests,
                        requestCount,
                        schedule,
                        kind,
                        field,
                        *field,
                        false))
                {
                    return false;
                }
            }
        }
    }

    const disk32::PointerToken *const effectReferences[] = {
        &elem.effectOnImpact.token,
        &elem.effectOnDeath.token,
        &elem.effectEmitted.token,
    };
    for (const disk32::PointerToken *const reference : effectReferences)
    {
        if (!AppendFrozenRequest(
                requests,
                requestCount,
                schedule,
                FxFastFileDisk32ReferenceKind::EffectNameReference,
                reference,
                *reference,
                true))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ComputeResolutionScheduleFingerprint(
    const FxFastFileEffectDefDisk32View &source,
    std::uint64_t *const outFingerprint) noexcept
{
    if (!outFingerprint || !source.effect
        || source.elements.count > kFxFastFileDisk32MaxEffectElements
        || source.elements.count != source.elementViews.count
        || (source.elements.count
            && (!source.elements.data || !source.elementViews.data)))
    {
        return false;
    }

    Fingerprint schedule;
    schedule.Scalar(reinterpret_cast<std::uintptr_t>(source.effect));
    schedule.Scalar(reinterpret_cast<std::uintptr_t>(source.elements.data));
    schedule.Scalar(source.elements.count);
    schedule.Scalar(
        reinterpret_cast<std::uintptr_t>(source.elementViews.data));
    schedule.Scalar(source.elementViews.count);
    schedule.Scalar(source.effect->elemDefCountLooping);
    schedule.Scalar(source.effect->elemDefCountOneShot);
    schedule.Scalar(source.effect->elemDefCountEmission);
    HashResolveRequest(
        &schedule,
        FxFastFileDisk32ReferenceKind::EffectName,
        &source.effect->name.token,
        source.effect->name.token);
    if (source.effect->name.token.isNull())
        return false;

    std::uint32_t totalRequests = 1;
    for (std::uint32_t index = 0; index < source.elements.count; ++index)
    {
        FrozenResolveRequest requests[kMaxElementResolveRequests]{};
        std::uint32_t requestCount = 0;
        if (!FreezeElementResolveRequests(
                source, index, requests, &requestCount, &schedule))
        {
            return false;
        }
        totalRequests += requestCount;
    }
    schedule.Scalar(totalRequests);
    *outFingerprint = schedule.Value();
    return true;
}

[[nodiscard]] bool ResolutionScheduleMatches(
    const FxFastFileEffectDefDisk32View &source,
    const std::uint64_t expectedFingerprint) noexcept
{
    std::uint64_t fingerprint = 0;
    return ComputeResolutionScheduleFingerprint(source, &fingerprint)
        && fingerprint == expectedFingerprint;
}

[[nodiscard]] Status ResolveFrozenGroup(
    const FxFastFileNativeDisk32Workspace *const workspace,
    const FxFastFileEffectDefDisk32View &source,
    const FxFastFileEffectDefDisk32View *const sourceArgument,
    const FxFastFileDisk32Resolvers &resolvers,
    const FxFastFileDisk32Resolvers *const resolverArgument,
    const FxFastFileNativeDisk32Plan *const outPlan,
    const std::uint64_t scheduleFingerprint,
    const FrozenResolveRequest *const requests,
    const std::uint32_t requestCount,
    FxFastFileDisk32ResolvedReference *const journal,
    std::uint32_t *const journalCount,
    Fingerprint *const expectedReferences) noexcept
{
    for (std::uint32_t index = 0; index < requestCount; ++index)
    {
        const FrozenResolveRequest &request = requests[index];
        const Status status = ResolveOne(
            workspace,
            source,
            sourceArgument,
            resolvers,
            resolverArgument,
            outPlan,
            request.kind,
            request.sourceField,
            request.token,
            journal,
            journalCount,
            expectedReferences);
        if (status != Status::Success)
        {
            return ResolutionScheduleMatches(source, scheduleFingerprint)
                ? status
                : Status::SourceChanged;
        }
    }
    return ResolutionScheduleMatches(source, scheduleFingerprint)
        ? Status::Success
        : Status::SourceChanged;
}

[[nodiscard]] Status ResolveGraph(
    const FxFastFileNativeDisk32Workspace *const workspace,
    const FxFastFileEffectDefDisk32View &source,
    const FxFastFileEffectDefDisk32View *const sourceArgument,
    const FxFastFileDisk32Resolvers &resolvers,
    const FxFastFileDisk32Resolvers *const resolverArgument,
    const FxFastFileNativeDisk32Plan *const outPlan,
    const std::uint64_t scheduleFingerprint,
    FxFastFileDisk32ResolvedReference *const journal,
    std::uint32_t *const journalCount) noexcept
{
    *journalCount = 0;
    Fingerprint expectedReferences;
    const FrozenResolveRequest rootRequest{
        FxFastFileDisk32ReferenceKind::EffectName,
        &source.effect->name.token,
        source.effect->name.token,
    };
    Status status = ResolveFrozenGroup(
        workspace,
        source,
        sourceArgument,
        resolvers,
        resolverArgument,
        outPlan,
        scheduleFingerprint,
        &rootRequest,
        1,
        journal,
        journalCount,
        &expectedReferences);
    if (status != Status::Success)
        return status;

    for (std::uint32_t index = 0; index < source.elements.count; ++index)
    {
        FrozenResolveRequest requests[kMaxElementResolveRequests]{};
        std::uint32_t requestCount = 0;
        if (!FreezeElementResolveRequests(
                source, index, requests, &requestCount, nullptr))
        {
            return Status::SourceChanged;
        }
        status = ResolveFrozenGroup(
            workspace,
            source,
            sourceArgument,
            resolvers,
            resolverArgument,
            outPlan,
            scheduleFingerprint,
            requests,
            requestCount,
            journal,
            journalCount,
            &expectedReferences);
        if (status != Status::Success)
            return status;
    }
    expectedReferences.Scalar(*journalCount);
    return expectedReferences.Value()
            == ComputeResolvedReferencesFingerprint(journal, *journalCount)
        ? Status::Success
        : Status::SourceChanged;
}

template <typename T>
void CopyFloatRange(T *const destination, const FxFloatRangeDisk32 &source)
    noexcept
{
    destination->base = source.base;
    destination->amplitude = source.amplitude;
}

void CopyIntRange(
    FxIntRange *const destination,
    const FxIntRangeDisk32 &source) noexcept
{
    destination->base = source.base;
    destination->amplitude = source.amplitude;
}

[[nodiscard]] bool RangesOverlap(
    const void *const left,
    const std::size_t leftBytes,
    const void *const right,
    const std::size_t rightBytes) noexcept
{
    if (!left || !right || leftBytes == 0 || rightBytes == 0)
        return false;
    const std::uintptr_t leftBegin = reinterpret_cast<std::uintptr_t>(left);
    const std::uintptr_t rightBegin = reinterpret_cast<std::uintptr_t>(right);
    if (leftBytes > (std::numeric_limits<std::uintptr_t>::max)() - leftBegin
        || rightBytes
            > (std::numeric_limits<std::uintptr_t>::max)() - rightBegin)
    {
        return true;
    }
    return leftBegin < rightBegin + rightBytes
        && rightBegin < leftBegin + leftBytes;
}

template <typename T>
[[nodiscard]] bool OverlapsSpan(
    const void *const storage,
    const std::size_t storageBytes,
    const FxFastFileDisk32Span<T> span) noexcept
{
    return RangesOverlap(
        storage,
        storageBytes,
        span.data,
        static_cast<std::size_t>(span.count) * sizeof(T));
}

[[nodiscard]] bool OverlapsSource(
    const void *const storage,
    const std::size_t storageBytes,
    const FxFastFileEffectDefDisk32View &source,
    const FxFastFileDisk32ResolvedReference *const resolved,
    const std::uint32_t resolvedCount) noexcept
{
    if (RangesOverlap(
            storage, storageBytes, source.effect, sizeof(*source.effect))
        || OverlapsSpan(storage, storageBytes, source.elements)
        || OverlapsSpan(storage, storageBytes, source.elementViews))
    {
        return true;
    }
    for (std::uint32_t index = 0; index < source.elementViews.count; ++index)
    {
        const FxFastFileElemDefDisk32View &view =
            source.elementViews.data[index];
        if (OverlapsSpan(storage, storageBytes, view.velocitySamples)
            || OverlapsSpan(storage, storageBytes, view.visibilitySamples)
            || OverlapsSpan(storage, storageBytes, view.visuals)
            || OverlapsSpan(storage, storageBytes, view.markVisuals)
            || RangesOverlap(
                storage, storageBytes, view.trail,
                view.trail ? sizeof(*view.trail) : 0)
            || OverlapsSpan(storage, storageBytes, view.trailVertices)
            || OverlapsSpan(storage, storageBytes, view.trailIndices))
        {
            return true;
        }
    }
    for (std::uint32_t index = 0; index < resolvedCount; ++index)
    {
        if (RangesOverlap(
                storage,
                storageBytes,
                resolved[index].pointer,
                static_cast<std::size_t>(
                    resolved[index].retainedByteCount)))
        {
            return true;
        }
    }
    return false;
}

void AssignVisual(
    FxElemVisuals *const destination,
    const FxElemTypeDisk32 type,
    const FxFastFileDisk32ResolvedReference &reference) noexcept
{
    if (type == FxElemTypeDisk32::Sound)
    {
        destination->soundName =
            static_cast<const char *>(reference.pointer);
    }
    else if (type == FxElemTypeDisk32::Runner)
    {
        destination->effectDef.handle =
            static_cast<const FxEffectDef *>(reference.pointer);
    }
    else if (type == FxElemTypeDisk32::Model)
    {
        destination->model = const_cast<XModel *>(
            static_cast<const XModel *>(reference.pointer));
    }
    else
    {
        destination->material = const_cast<Material *>(
            static_cast<const Material *>(reference.pointer));
    }
}
} // namespace

FxFastFileNativeDisk32Status TryPlanFxEffectDefDisk32(
    FxFastFileNativeDisk32Workspace *const workspace,
    const FxFastFileEffectDefDisk32View &sourceArgument,
    const FxFastFileDisk32Resolvers &resolvers,
    FxFastFileNativeDisk32Plan *const outPlan) noexcept
{
    if (!workspace)
        return Status::InvalidArgument;
    if (workspace->operating_)
        return Status::Busy;
    if (!outPlan || !resolvers.resolve
        || !sourceArgument.provenance.validateSpan
        || reinterpret_cast<std::uintptr_t>(outPlan)
                % alignof(FxFastFileNativeDisk32Plan)
            != 0)
    {
        return Status::InvalidArgument;
    }
    if (RangesOverlap(
            outPlan, sizeof(*outPlan), workspace, sizeof(*workspace)))
    {
        return Status::InvalidArgument;
    }
    if (workspace->phase_ != FxFastFileNativeDisk32Phase::Empty)
        return Status::InvalidPhase;

    // Reject aliases whose addresses can be checked without dereferencing an
    // unvalidated graph. The full reachable-span check follows validation.
    if (RangesOverlap(
            &sourceArgument,
            sizeof(sourceArgument),
            workspace,
            sizeof(*workspace))
        || RangesOverlap(
            &resolvers, sizeof(resolvers), workspace, sizeof(*workspace))
        || RangesOverlap(
            outPlan,
            sizeof(*outPlan),
            &sourceArgument,
            sizeof(sourceArgument))
        || RangesOverlap(
            outPlan, sizeof(*outPlan), &resolvers, sizeof(resolvers))
        || RangesOverlap(
            sourceArgument.effect,
            sourceArgument.effect ? sizeof(*sourceArgument.effect) : 0,
            workspace,
            sizeof(*workspace))
        || RangesOverlap(
            sourceArgument.elements.data,
            sourceArgument.elements.data ? 1 : 0,
            workspace,
            sizeof(*workspace))
        || RangesOverlap(
            sourceArgument.elementViews.data,
            sourceArgument.elementViews.data ? 1 : 0,
            workspace,
            sizeof(*workspace)))
    {
        return Status::InvalidArgument;
    }

    // Validate and bind the complete caller-owned graph before changing any
    // workspace byte. Otherwise a source view placed inside the workspace
    // could be destroyed by the operation gate or journal initialization.
    Status status = ValidateGraph(sourceArgument, nullptr);
    if (status != Status::Success)
        return status;
    if (OverlapsSource(
            workspace, sizeof(*workspace), sourceArgument, nullptr, 0)
        || OverlapsSource(
            outPlan, sizeof(*outPlan), sourceArgument, nullptr, 0))
    {
        return Status::InvalidArgument;
    }
    const FxFastFileEffectDefDisk32View source = sourceArgument;
    const FxFastFileDisk32Resolvers resolverSnapshot = resolvers;
    const std::uint64_t sourceFingerprint =
        ComputeSourceFingerprint(source);
    std::uint64_t scheduleFingerprint = 0;
    if (!ComputeResolutionScheduleFingerprint(
            source, &scheduleFingerprint))
    {
        return Status::InvalidSourceLayout;
    }

    workspace->operating_ = true;
    const auto finish = [workspace](const Status status) noexcept {
        workspace->operating_ = false;
        return status;
    };
    const auto clearPlan = [workspace]() noexcept {
        workspace->source_ = {};
        workspace->plan_ = {};
        workspace->resolvedCount_ = 0;
        workspace->provenanceRequestCount_ = 0;
        workspace->phase_ = FxFastFileNativeDisk32Phase::Empty;
    };
    clearPlan();

    // Freeze the complete source-provenance schedule before callback one.
    // Each callback is invoked only from this fixed heap-workspace journal.
    // A side checksum binds each current request in O(1); whole-journal
    // integrity is checked once at each callback-phase boundary.
    ProvenanceScheduleState provenanceSchedule{
        workspace->provenanceRequests_};
    status = ValidateGraph(source, &provenanceSchedule);
    workspace->provenanceRequestCount_ = provenanceSchedule.count;
    if (status != Status::Success)
    {
        clearPlan();
        return finish(status);
    }
    if (ComputeSourceFingerprint(source) != sourceFingerprint)
    {
        clearPlan();
        return finish(Status::SourceChanged);
    }
    const std::uint32_t provenanceRequestCount =
        workspace->provenanceRequestCount_;
    for (std::uint32_t index = 0; index < provenanceRequestCount; ++index)
    {
        workspace->provenanceRequestChecksums_[index] =
            ComputeProvenanceRequestChecksum(
                workspace->provenanceRequests_[index]);
    }
    const std::uint64_t provenanceJournalFingerprint =
        ComputeProvenanceJournalFingerprint(
            workspace->provenanceRequests_,
            workspace->provenanceRequestChecksums_,
            provenanceRequestCount);
    status = InvokeFrozenProvenanceRequests(
        source.provenance,
        workspace->provenanceRequests_,
        workspace->provenanceRequestChecksums_,
        provenanceRequestCount,
        provenanceJournalFingerprint);
    if (status != Status::Success)
    {
        clearPlan();
        return finish(status);
    }
    status = ValidateGraph(source, nullptr);
    if (workspace->provenanceRequestCount_ != provenanceRequestCount
        || status != Status::Success
        || ComputeSourceFingerprint(source) != sourceFingerprint)
    {
        clearPlan();
        return finish(Status::SourceChanged);
    }
    status = ResolveGraph(
        workspace,
        source,
        &sourceArgument,
        resolverSnapshot,
        &resolvers,
        outPlan,
        scheduleFingerprint,
        workspace->resolved_,
        &workspace->resolvedCount_);
    if (status != Status::Success)
    {
        clearPlan();
        return finish(status);
    }
    status = ValidateGraph(source, nullptr);
    if (status != Status::Success
        || ComputeSourceFingerprint(source) != sourceFingerprint)
    {
        clearPlan();
        return finish(Status::SourceChanged);
    }
    if (OverlapsSource(
            outPlan,
            sizeof(*outPlan),
            source,
            workspace->resolved_,
            workspace->resolvedCount_))
    {
        clearPlan();
        return finish(Status::InvalidArgument);
    }

    if (workspace->resolved_[0].retainedByteCount
        > (std::numeric_limits<std::uint32_t>::max)())
    {
        clearPlan();
        return finish(Status::SizeOverflow);
    }
    const std::uint32_t effectNameBytes = static_cast<std::uint32_t>(
        workspace->resolved_[0].retainedByteCount);
    FxRuntimeBlobCursor disk32Planner;
    status = PlanDisk32Layout(source, effectNameBytes, &disk32Planner);
    if (status != Status::Success)
    {
        clearPlan();
        return finish(status);
    }
    if (source.effect->totalSize < 0
        || static_cast<std::uint32_t>(source.effect->totalSize)
            != disk32Planner.Offset())
    {
        clearPlan();
        return finish(Status::InvalidSourceLayout);
    }
    FxRuntimeBlobCursor planner;
    status = PlanLayout(source, effectNameBytes, &planner);
    if (status != Status::Success)
    {
        clearPlan();
        return finish(status);
    }
    if (planner.Offset()
        > static_cast<std::uint32_t>(
            (std::numeric_limits<std::int32_t>::max)()))
    {
        clearPlan();
        return finish(Status::SizeOverflow);
    }

    FxFastFileNativeDisk32Plan candidate{};
    candidate.workspaceIdentity_ = workspace;
    candidate.serial_ = workspace->nextSerial_++;
    if (candidate.serial_ == 0)
        candidate.serial_ = workspace->nextSerial_++;
    if (workspace->nextSerial_ == 0)
        workspace->nextSerial_ = 1;
    candidate.sourceFingerprint_ = ComputeBoundFingerprint(
        source, workspace->resolved_, workspace->resolvedCount_);
    candidate.outputBytes_ = planner.Offset();
    candidate.outputAlignment_ = static_cast<std::uint32_t>(
        alignof(FxElemDef));
    candidate.elementCount_ = source.elements.count;
    candidate.resolvedReferenceCount_ = workspace->resolvedCount_;
    candidate.effectNameBytes_ = effectNameBytes;

    workspace->source_ = source;
    workspace->plan_ = candidate;
    workspace->phase_ = FxFastFileNativeDisk32Phase::Planned;
    *outPlan = candidate;
    return finish(Status::Success);
}

FxFastFileNativeDisk32Status TryMaterializeFxEffectDefDisk32(
    FxFastFileNativeDisk32Workspace *const workspace,
    const FxFastFileNativeDisk32Plan &plan,
    void *const storage,
    const std::size_t capacity,
    FxEffectDef **const outEffect) noexcept
{
    if (!workspace)
        return Status::InvalidArgument;
    if (workspace->operating_)
        return Status::Busy;
    if (!storage || !outEffect
        || reinterpret_cast<std::uintptr_t>(outEffect)
            % alignof(FxEffectDef *) != 0)
    {
        return Status::InvalidArgument;
    }

    workspace->operating_ = true;
    const auto finish = [workspace](const Status status) noexcept {
        workspace->operating_ = false;
        return status;
    };
    const auto invalidate = [workspace]() noexcept {
        workspace->source_ = {};
        workspace->plan_ = {};
        workspace->resolvedCount_ = 0;
        workspace->provenanceRequestCount_ = 0;
        workspace->phase_ = FxFastFileNativeDisk32Phase::Empty;
    };

    const FxFastFileNativeDisk32Plan &bound = workspace->plan_;
    if (plan.workspaceIdentity_ != workspace || plan.serial_ == 0)
        return finish(Status::InvalidPlan);
    if (workspace->phase_ != FxFastFileNativeDisk32Phase::Planned)
        return finish(Status::InvalidPhase);
    if (plan.workspaceIdentity_ != bound.workspaceIdentity_
        || plan.serial_ != bound.serial_
        || plan.sourceFingerprint_ != bound.sourceFingerprint_
        || plan.outputBytes_ != bound.outputBytes_
        || plan.outputAlignment_ != bound.outputAlignment_
        || plan.elementCount_ != bound.elementCount_
        || plan.resolvedReferenceCount_ != bound.resolvedReferenceCount_
        || plan.effectNameBytes_ != bound.effectNameBytes_)
    {
        return finish(Status::InvalidPlan);
    }
#if !KISAK_ARCH_64BIT
    if (plan.workspaceIdentityPadding_ != bound.workspaceIdentityPadding_)
        return finish(Status::InvalidPlan);
#endif
    if (plan.outputBytes_ == 0
        || plan.outputAlignment_ != alignof(FxElemDef)
        || plan.elementCount_ != workspace->source_.elements.count
        || plan.resolvedReferenceCount_ != workspace->resolvedCount_
        || plan.effectNameBytes_ < 2)
    {
        return finish(Status::InvalidPlan);
    }

    std::size_t rootNameBytes = 0;
    if (!ValidateRetainedJournalRanges(
            workspace->resolved_, workspace->resolvedCount_)
        || ValidateResolvedReference(
               FxFastFileDisk32ReferenceKind::EffectName,
               workspace->resolved_[0],
               &rootNameBytes)
            != Status::Success
        || rootNameBytes != plan.effectNameBytes_)
    {
        invalidate();
        return finish(Status::SourceChanged);
    }

    const std::uintptr_t storageAddress =
        reinterpret_cast<std::uintptr_t>(storage);
    if (storageAddress % plan.outputAlignment_ != 0)
        return finish(Status::MisalignedStorage);
    if (capacity < plan.outputBytes_)
        return finish(Status::InsufficientCapacity);
    if (plan.outputBytes_
        > (std::numeric_limits<std::uintptr_t>::max)() - storageAddress)
    {
        return finish(Status::SizeOverflow);
    }
    if (RangesOverlap(
            storage, plan.outputBytes_, workspace, sizeof(*workspace))
        || RangesOverlap(storage, plan.outputBytes_, &plan, sizeof(plan))
        || RangesOverlap(
            storage, plan.outputBytes_, outEffect, sizeof(*outEffect))
        || OverlapsSource(
            storage,
            plan.outputBytes_,
            workspace->source_,
            workspace->resolved_,
            workspace->resolvedCount_)
        || RangesOverlap(
            outEffect, sizeof(*outEffect), workspace, sizeof(*workspace))
        || RangesOverlap(outEffect, sizeof(*outEffect), &plan, sizeof(plan))
        || OverlapsSource(
            outEffect,
            sizeof(*outEffect),
            workspace->source_,
            workspace->resolved_,
            workspace->resolvedCount_))
    {
        return finish(Status::OverlappingStorage);
    }

    Status status = ValidateGraph(workspace->source_, nullptr);
    if (status != Status::Success
        || ComputeBoundFingerprint(
               workspace->source_,
               workspace->resolved_,
               workspace->resolvedCount_)
            != plan.sourceFingerprint_)
    {
        invalidate();
        return finish(Status::SourceChanged);
    }
    FxRuntimeBlobCursor verifier(nullptr, plan.outputBytes_);
    status = PlanLayout(
        workspace->source_, plan.effectNameBytes_, &verifier);
    if (status != Status::Success || verifier.Offset() != plan.outputBytes_)
    {
        invalidate();
        return finish(Status::SourceChanged);
    }

    // No validation or callback remains beyond this point. The dry cursor
    // above proved that every reservation below fits the exact bound plan.
    std::memset(storage, 0, plan.outputBytes_);
    FxRuntimeBlobCursor writer(
        static_cast<std::uint8_t *>(storage), plan.outputBytes_);
    FxEffectDef *nativeEffect = nullptr;
    FxElemDef *nativeElements = nullptr;
    if (!writer.ReserveArray(1, &nativeEffect)
        || !writer.ReserveArray(plan.elementCount_, &nativeElements))
    {
        invalidate();
        return finish(Status::InvalidPlan);
    }
    std::construct_at(nativeEffect);

    const FxEffectDefDisk32 &diskEffect = *workspace->source_.effect;
    std::uint32_t resolvedIndex = 1;
    for (std::uint32_t index = 0; index < plan.elementCount_; ++index)
    {
        const FxElemDefDisk32 &disk = workspace->source_.elements.data[index];
        const FxFastFileElemDefDisk32View &view =
            workspace->source_.elementViews.data[index];
        FxElemDef &native = nativeElements[index];
        std::construct_at(&native);

        native.flags = disk.flags;
        if (index
            < static_cast<std::uint32_t>(diskEffect.elemDefCountLooping))
        {
            native.spawn.looping.intervalMsec =
                disk.spawn.intervalMsecOrCountBase;
            native.spawn.looping.count = disk.spawn.loopCountOrCountAmplitude;
        }
        else
        {
            native.spawn.oneShot.count.base =
                disk.spawn.intervalMsecOrCountBase;
            native.spawn.oneShot.count.amplitude =
                disk.spawn.loopCountOrCountAmplitude;
        }
        CopyFloatRange(&native.spawnRange, disk.spawnRange);
        CopyFloatRange(&native.fadeInRange, disk.fadeInRange);
        CopyFloatRange(&native.fadeOutRange, disk.fadeOutRange);
        native.spawnFrustumCullRadius = disk.spawnFrustumCullRadius;
        CopyIntRange(&native.spawnDelayMsec, disk.spawnDelayMsec);
        CopyIntRange(&native.lifeSpanMsec, disk.lifeSpanMsec);
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            CopyFloatRange(&native.spawnOrigin[axis], disk.spawnOrigin[axis]);
            CopyFloatRange(&native.spawnAngles[axis], disk.spawnAngles[axis]);
            CopyFloatRange(
                &native.angularVelocity[axis], disk.angularVelocity[axis]);
            native.collMins[axis] = disk.collMins[axis];
            native.collMaxs[axis] = disk.collMaxs[axis];
        }
        CopyFloatRange(&native.spawnOffsetRadius, disk.spawnOffsetRadius);
        CopyFloatRange(&native.spawnOffsetHeight, disk.spawnOffsetHeight);
        CopyFloatRange(&native.initialRotation, disk.initialRotation);
        CopyFloatRange(&native.gravity, disk.gravity);
        CopyFloatRange(&native.reflectionFactor, disk.reflectionFactor);
        native.atlas.behavior = disk.atlas.behavior;
        native.atlas.index = disk.atlas.index;
        native.atlas.fps = disk.atlas.fps;
        native.atlas.loopCount = disk.atlas.loopCount;
        native.atlas.colIndexBits = disk.atlas.colIndexBits;
        native.atlas.rowIndexBits = disk.atlas.rowIndexBits;
        native.atlas.entryCount = disk.atlas.entryCount;
        native.elemType = static_cast<std::uint8_t>(disk.elemType);
        native.visualCount = disk.visualCount;
        native.velIntervalCount = disk.velIntervalCount;
        native.visStateIntervalCount = disk.visStateIntervalCount;

        if (!disk.velSamples.token.isNull())
        {
            if (!writer.ReserveArray(
                    view.velocitySamples.count, &native.velSamples))
            {
                invalidate();
                return finish(Status::InvalidPlan);
            }
            std::memcpy(
                native.velSamples,
                view.velocitySamples.data,
                static_cast<std::size_t>(view.velocitySamples.count)
                    * sizeof(*native.velSamples));
        }
        if (!disk.visSamples.token.isNull())
        {
            if (!writer.ReserveArray(
                    view.visibilitySamples.count, &native.visSamples))
            {
                invalidate();
                return finish(Status::InvalidPlan);
            }
            std::memcpy(
                native.visSamples,
                view.visibilitySamples.data,
                static_cast<std::size_t>(view.visibilitySamples.count)
                    * sizeof(*native.visSamples));
        }

        if (disk.elemType == FxElemTypeDisk32::Decal)
        {
            FxElemMarkVisuals *markVisuals = nullptr;
            if (!writer.ReserveArray(disk.visualCount, &markVisuals))
            {
                invalidate();
                return finish(Status::InvalidPlan);
            }
            native.visuals.markArray = markVisuals;
            for (std::uint32_t visual = 0; visual < disk.visualCount;
                 ++visual)
            {
                std::construct_at(&markVisuals[visual]);
                for (std::uint32_t material = 0; material < 2; ++material)
                {
                    markVisuals[visual].materials[material] =
                        const_cast<Material *>(static_cast<const Material *>(
                            workspace->resolved_[resolvedIndex++].pointer));
                }
            }
        }
        else if (!IsLight(disk.elemType) && disk.visualCount == 1)
        {
            std::construct_at(&native.visuals.instance);
            AssignVisual(
                &native.visuals.instance,
                disk.elemType,
                workspace->resolved_[resolvedIndex++]);
        }
        else if (!IsLight(disk.elemType) && disk.visualCount > 1)
        {
            FxElemVisuals *visuals = nullptr;
            if (!writer.ReserveArray(disk.visualCount, &visuals))
            {
                invalidate();
                return finish(Status::InvalidPlan);
            }
            native.visuals.array = visuals;
            for (std::uint32_t visual = 0; visual < disk.visualCount;
                 ++visual)
            {
                std::construct_at(&visuals[visual]);
                AssignVisual(
                    &visuals[visual],
                    disk.elemType,
                    workspace->resolved_[resolvedIndex++]);
            }
        }
        else
        {
            std::construct_at(&native.visuals.instance);
            native.visuals.instance.anonymous = nullptr;
        }

        native.effectOnImpact.handle = disk.effectOnImpact.token.isNull()
            ? nullptr
            : static_cast<const FxEffectDef *>(
                workspace->resolved_[resolvedIndex++].pointer);
        native.effectOnDeath.handle = disk.effectOnDeath.token.isNull()
            ? nullptr
            : static_cast<const FxEffectDef *>(
                workspace->resolved_[resolvedIndex++].pointer);
        native.effectEmitted.handle = disk.effectEmitted.token.isNull()
            ? nullptr
            : static_cast<const FxEffectDef *>(
                workspace->resolved_[resolvedIndex++].pointer);
        CopyFloatRange(&native.emitDist, disk.emitDist);
        CopyFloatRange(&native.emitDistVariance, disk.emitDistVariance);

        if (view.trail)
        {
            FxTrailDef *trail = nullptr;
            FxTrailVertex *vertices = nullptr;
            std::uint16_t *indices = nullptr;
            if (!writer.ReserveArray(1, &trail)
                || !writer.ReserveArray(
                    static_cast<std::uint32_t>(view.trail->indCount),
                    &vertices)
                || !writer.ReserveArray(
                    static_cast<std::uint32_t>(view.trail->indCount),
                    &indices))
            {
                invalidate();
                return finish(Status::InvalidPlan);
            }
            std::construct_at(trail);
            native.trailDef = trail;
            trail->scrollTimeMsec = view.trail->scrollTimeMsec;
            trail->repeatDist = view.trail->repeatDist;
            trail->splitDist = view.trail->splitDist;
            trail->vertCount = view.trail->vertCount;
            trail->verts = vertices;
            trail->indCount = view.trail->indCount;
            trail->inds = indices;
            std::memcpy(
                vertices,
                view.trailVertices.data,
                static_cast<std::size_t>(view.trail->vertCount)
                    * sizeof(*vertices));
            std::memcpy(
                indices,
                view.trailIndices.data,
                static_cast<std::size_t>(view.trail->indCount)
                    * sizeof(*indices));
        }
        native.sortOrder = disk.sortOrder;
        native.lightingFrac = disk.lightingFrac;
        native.useItemClip = disk.useItemClip;
        native.unused[0] = disk.unused[0];
    }

    char *effectName = nullptr;
    if (!writer.ReserveArray(plan.effectNameBytes_, &effectName))
    {
        invalidate();
        return finish(Status::InvalidPlan);
    }
    std::memcpy(
        effectName,
        workspace->resolved_[0].pointer,
        plan.effectNameBytes_);
    nativeEffect->name = effectName;
    nativeEffect->flags = diskEffect.flags;
    nativeEffect->totalSize = static_cast<std::int32_t>(plan.outputBytes_);
    nativeEffect->msecLoopingLife = diskEffect.msecLoopingLife;
    nativeEffect->elemDefCountLooping = diskEffect.elemDefCountLooping;
    nativeEffect->elemDefCountOneShot = diskEffect.elemDefCountOneShot;
    nativeEffect->elemDefCountEmission = diskEffect.elemDefCountEmission;
    nativeEffect->elemDefs = plan.elementCount_
        ? nativeElements
        : nullptr;

    if (resolvedIndex != workspace->resolvedCount_
        || writer.Offset() != plan.outputBytes_)
    {
        invalidate();
        return finish(Status::InvalidPlan);
    }
    *outEffect = nativeEffect;
    invalidate();
    return finish(Status::Success);
}
} // namespace fx::fastfile
