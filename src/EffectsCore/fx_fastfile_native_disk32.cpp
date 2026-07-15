#include <EffectsCore/fx_fastfile_native_disk32.h>

#include <EffectsCore/fx_runtime.h>
#include <EffectsCore/fx_runtime_blob.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <type_traits>

namespace fx::fastfile::native_detail
{
struct FloatRange final
{
    float base;
    float amplitude;
};

struct IntRange final
{
    std::int32_t base;
    std::int32_t amplitude;
};

union SpawnDef final
{
    struct
    {
        std::int32_t intervalMsec;
        std::int32_t count;
    } looping;
    struct
    {
        IntRange count;
    } oneShot;
};

struct ElemAtlas final
{
    std::uint8_t behavior;
    std::uint8_t index;
    std::uint8_t fps;
    std::uint8_t loopCount;
    std::uint8_t colIndexBits;
    std::uint8_t rowIndexBits;
    std::int16_t entryCount;
};

struct ElemVec3Range final
{
    float base[3];
    float amplitude[3];
};

struct ElemVisualState final
{
    std::uint8_t color[4];
    float rotationDelta;
    float rotationTotal;
    float size[2];
    float scale;
};

struct ElemVisStateSample final
{
    ElemVisualState base;
    ElemVisualState amplitude;
};

struct ElemVelStateInFrame final
{
    ElemVec3Range velocity;
    ElemVec3Range totalDelta;
};

struct ElemVelStateSample final
{
    ElemVelStateInFrame local;
    ElemVelStateInFrame world;
};

union EffectDefRef final
{
    const FxEffectDef *handle;
    const char *name;
};

union ElemVisuals final
{
    const void *anonymous;
    void *asset;
    EffectDefRef effectDef;
    const char *soundName;
};

struct ElemMarkVisuals final
{
    const void *materials[2];
};

union ElemDefVisuals final
{
    ElemMarkVisuals *markArray;
    ElemVisuals *array;
    ElemVisuals instance;
};

struct TrailVertex final
{
    float pos[2];
    float normal[2];
    float texCoord;
};

struct TrailDef final
{
    std::int32_t scrollTimeMsec;
    std::int32_t repeatDist;
    std::int32_t splitDist;
    std::int32_t vertCount;
    TrailVertex *verts;
    std::int32_t indCount;
    std::uint16_t *inds;
};

struct ElemDef final
{
    std::int32_t flags;
    SpawnDef spawn;
    FloatRange spawnRange;
    FloatRange fadeInRange;
    FloatRange fadeOutRange;
    float spawnFrustumCullRadius;
    IntRange spawnDelayMsec;
    IntRange lifeSpanMsec;
    FloatRange spawnOrigin[3];
    FloatRange spawnOffsetRadius;
    FloatRange spawnOffsetHeight;
    FloatRange spawnAngles[3];
    FloatRange angularVelocity[3];
    FloatRange initialRotation;
    FloatRange gravity;
    FloatRange reflectionFactor;
    ElemAtlas atlas;
    std::uint8_t elemType;
    std::uint8_t visualCount;
    std::uint8_t velIntervalCount;
    std::uint8_t visStateIntervalCount;
    ElemVelStateSample *velSamples;
    ElemVisStateSample *visSamples;
    ElemDefVisuals visuals;
    float collMins[3];
    float collMaxs[3];
    EffectDefRef effectOnImpact;
    EffectDefRef effectOnDeath;
    EffectDefRef effectEmitted;
    FloatRange emitDist;
    FloatRange emitDistVariance;
    TrailDef *trailDef;
    std::uint8_t sortOrder;
    std::uint8_t lightingFrac;
    std::uint8_t useItemClip;
    std::uint8_t unused[1];
};

static_assert(sizeof(FloatRange) == 0x08);
static_assert(sizeof(IntRange) == 0x08);
static_assert(sizeof(SpawnDef) == 0x08);
static_assert(sizeof(ElemAtlas) == 0x08);
static_assert(sizeof(ElemVisStateSample) == 0x30);
static_assert(sizeof(ElemVelStateSample) == 0x60);
static_assert(sizeof(ElemVisuals) == sizeof(void *));
static_assert(sizeof(ElemMarkVisuals) == 2 * sizeof(void *));
static_assert(sizeof(TrailVertex) == 0x14);
static_assert(sizeof(TrailDef) == (KISAK_ARCH_64BIT ? 0x28u : 0x1Cu));
static_assert(sizeof(ElemDef) == FX_ELEM_DEF_RUNTIME_SIZE);
static_assert(alignof(ElemDef) == FX_ELEM_DEF_RUNTIME_ALIGNMENT);
static_assert(offsetof(ElemDef, elemType) == 0xB0);
static_assert(
    offsetof(ElemDef, velSamples)
    == (KISAK_ARCH_64BIT ? 0xB8u : 0xB4u));
static_assert(
    offsetof(ElemDef, visSamples)
    == (KISAK_ARCH_64BIT ? 0xC0u : 0xB8u));
static_assert(
    offsetof(ElemDef, visuals)
    == (KISAK_ARCH_64BIT ? 0xC8u : 0xBCu));
static_assert(
    offsetof(ElemDef, effectOnImpact)
    == (KISAK_ARCH_64BIT ? 0xE8u : 0xD8u));
static_assert(
    offsetof(ElemDef, effectOnDeath)
    == (KISAK_ARCH_64BIT ? 0xF0u : 0xDCu));
static_assert(
    offsetof(ElemDef, effectEmitted)
    == (KISAK_ARCH_64BIT ? 0xF8u : 0xE0u));
static_assert(
    offsetof(ElemDef, trailDef)
    == (KISAK_ARCH_64BIT ? 0x110u : 0xF4u));
} // namespace fx::fastfile::native_detail

namespace fx::fastfile
{
namespace
{
using Status = FxFastFileNativeDisk32Status;

constexpr std::int32_t kMaxTrailVertices = 64;
constexpr std::int32_t kMaxTrailIndices = 128;
constexpr std::uint64_t kFnvOffset = UINT64_C(14695981039346656037);
constexpr std::uint64_t kFnvPrime = UINT64_C(1099511628211);

static_assert(sizeof(FxElemVelStateSampleDisk32)
              == sizeof(native_detail::ElemVelStateSample));
static_assert(alignof(FxElemVelStateSampleDisk32)
              == alignof(native_detail::ElemVelStateSample));
static_assert(sizeof(FxElemVisStateSampleDisk32)
              == sizeof(native_detail::ElemVisStateSample));
static_assert(alignof(FxElemVisStateSampleDisk32)
              == alignof(native_detail::ElemVisStateSample));
static_assert(sizeof(FxTrailVertexDisk32)
              == sizeof(native_detail::TrailVertex));
static_assert(alignof(FxTrailVertexDisk32)
              == alignof(native_detail::TrailVertex));
static_assert(
    std::is_trivially_copyable_v<native_detail::ElemVelStateSample>);
static_assert(
    std::is_trivially_copyable_v<native_detail::ElemVisStateSample>);
static_assert(std::is_trivially_copyable_v<native_detail::TrailVertex>);

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

template <typename T>
[[nodiscard]] Status ValidateOwnedSpan(
    const FxFastFileEffectDefDisk32View &source,
    const FxFastFileDisk32SourceSpanKind kind,
    const disk32::PointerToken *const sourceField,
    const disk32::PointerToken token,
    const FxFastFileDisk32Span<T> span,
    const std::uint32_t expectedCount,
    const bool callProvenance) noexcept
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
    if (callProvenance
        && !source.provenance.validateSpan(
            source.provenance.context,
            kind,
            sourceField,
            token,
            span.data,
            byteCount,
            alignof(T)))
    {
        return Status::InvalidProvenance;
    }
    return Status::Success;
}

[[nodiscard]] Status ValidateUnownedSpan(
    const FxFastFileEffectDefDisk32View &source,
    const FxFastFileDisk32SourceSpanKind kind,
    const void *const address,
    const std::uint64_t byteCount,
    const std::size_t alignment,
    const bool callProvenance) noexcept
{
    if (!address
        || reinterpret_cast<std::uintptr_t>(address) % alignment != 0)
    {
        return Status::InvalidSourceLayout;
    }
    if (callProvenance
        && !source.provenance.validateSpan(
            source.provenance.context,
            kind,
            nullptr,
            {},
            address,
            byteCount,
            alignment))
    {
        return Status::InvalidProvenance;
    }
    return Status::Success;
}

[[nodiscard]] bool IsLight(const FxElemTypeDisk32 type) noexcept
{
    return type == FxElemTypeDisk32::OmniLight
        || type == FxElemTypeDisk32::SpotLight;
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
    const bool callProvenance) noexcept
{
    if (!source.effect || !source.provenance.validateSpan)
        return Status::InvalidArgument;

    Status status = ValidateUnownedSpan(
        source,
        FxFastFileDisk32SourceSpanKind::EffectHeader,
        source.effect,
        sizeof(*source.effect),
        alignof(FxEffectDefDisk32),
        callProvenance);
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
            callProvenance);
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

    for (std::uint32_t index = 0; index < elementCount; ++index)
    {
        const FxElemDefDisk32 &elem = source.elements.data[index];
        const FxFastFileElemDefDisk32View &view =
            source.elementViews.data[index];
        if (elem.elemType >= FxElemTypeDisk32::Count)
            return Status::InvalidVisual;

        if (elem.velSamples.token.isNull() || elem.velIntervalCount == 0)
            return Status::InvalidPointerCount;
        status = ValidateOwnedSpan(
            source,
            FxFastFileDisk32SourceSpanKind::VelocitySamples,
            &elem.velSamples.token,
            elem.velSamples.token,
            view.velocitySamples,
            static_cast<std::uint32_t>(elem.velIntervalCount) + 1u,
            callProvenance);
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
                callProvenance);
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
                callProvenance);
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
                    callProvenance);
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
            view.trail,
            sizeof(*view.trail),
            alignof(FxTrailDefDisk32),
            false);
        if (status != Status::Success)
            return status;
        if (callProvenance
            && !source.provenance.validateSpan(
                source.provenance.context,
                FxFastFileDisk32SourceSpanKind::TrailDefinition,
                &elem.trailDef.token,
                elem.trailDef.token,
                view.trail,
                sizeof(*view.trail),
                alignof(FxTrailDefDisk32)))
        {
            return Status::InvalidProvenance;
        }

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
            callProvenance);
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
            callProvenance);
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
    }
    return Status::Success;
}

[[nodiscard]] Status PlanLayout(
    const FxFastFileEffectDefDisk32View &source,
    const std::uint32_t effectNameBytes,
    FxRuntimeBlobCursor *const cursor) noexcept
{
    if (!cursor || !cursor->ReserveArray<FxEffectDef>(1)
        || !cursor->ReserveArray<native_detail::ElemDef>(
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
            && !cursor->ReserveArray<native_detail::ElemVelStateSample>(
                view.velocitySamples.count))
        {
            return Status::SizeOverflow;
        }
        if (!elem.visSamples.token.isNull()
            && !cursor->ReserveArray<native_detail::ElemVisStateSample>(
                view.visibilitySamples.count))
        {
            return Status::SizeOverflow;
        }
        if (elem.elemType == FxElemTypeDisk32::Decal)
        {
            if (!cursor->ReserveArray<native_detail::ElemMarkVisuals>(
                    elem.visualCount))
                return Status::SizeOverflow;
        }
        else if (!IsLight(elem.elemType) && elem.visualCount > 1
                 && !cursor->ReserveArray<native_detail::ElemVisuals>(
                     elem.visualCount))
        {
            return Status::SizeOverflow;
        }
        if (view.trail)
        {
            if (!cursor->ReserveArray<native_detail::TrailDef>(1)
                || !cursor->ReserveArray<native_detail::TrailVertex>(
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

template <typename T>
void HashSpan(Fingerprint *const hash, const FxFastFileDisk32Span<T> span)
    noexcept
{
    hash->Scalar(reinterpret_cast<std::uintptr_t>(span.data));
    hash->Scalar(span.count);
    hash->Bytes(span.data, static_cast<std::size_t>(span.count) * sizeof(T));
}

[[nodiscard]] std::uint64_t ComputeSourceFingerprint(
    const FxFastFileEffectDefDisk32View &source) noexcept
{
    Fingerprint hash;
    hash.Scalar(reinterpret_cast<std::uintptr_t>(source.effect));
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
        hash.Scalar(resolved[index].stringByteCount);
        if (resolved[index].stringByteCount)
        {
            hash.Bytes(
                resolved[index].pointer,
                resolved[index].stringByteCount);
        }
    }
    return hash.Value();
}

[[nodiscard]] bool IsExactCString(
    const FxFastFileDisk32ResolvedReference &reference) noexcept
{
    if (!reference.pointer || reference.stringByteCount < 2)
        return false;
    const auto *text = static_cast<const char *>(reference.pointer);
    return text[reference.stringByteCount - 1] == '\0'
        && !std::memchr(text, '\0', reference.stringByteCount - 1);
}

[[nodiscard]] Status ResolveOne(
    const FxFastFileEffectDefDisk32View &source,
    const FxFastFileDisk32Resolvers &resolvers,
    const FxFastFileDisk32ReferenceKind kind,
    const disk32::PointerToken *const sourceField,
    FxFastFileDisk32ResolvedReference *const journal,
    std::uint32_t *const journalCount) noexcept
{
    if (!sourceField || sourceField->isNull() || !journal || !journalCount
        || *journalCount >= kFxFastFileDisk32MaxResolvedReferences)
    {
        return Status::InvalidPointerCount;
    }
    FxFastFileDisk32ResolvedReference resolved{};
    if (!resolvers.resolve(
            resolvers.context, kind, sourceField, *sourceField, &resolved)
        || !resolved.pointer)
    {
        return Status::UnresolvedReference;
    }
    const bool isString = kind == FxFastFileDisk32ReferenceKind::EffectName
        || kind == FxFastFileDisk32ReferenceKind::SoundName;
    if (isString)
    {
        if (resolved.stringByteCount < 2)
            return Status::InvalidString;
        if (!source.provenance.validateSpan(
                source.provenance.context,
                FxFastFileDisk32SourceSpanKind::String,
                sourceField,
                *sourceField,
                resolved.pointer,
                resolved.stringByteCount,
                alignof(char)))
        {
            return Status::InvalidProvenance;
        }
        if (!IsExactCString(resolved))
            return Status::InvalidString;
    }
    else if (resolved.stringByteCount != 0)
    {
        return Status::InvalidString;
    }
    journal[(*journalCount)++] = resolved;
    return Status::Success;
}

[[nodiscard]] Status ResolveGraph(
    const FxFastFileEffectDefDisk32View &source,
    const FxFastFileDisk32Resolvers &resolvers,
    FxFastFileDisk32ResolvedReference *const journal,
    std::uint32_t *const journalCount) noexcept
{
    *journalCount = 0;
    Status status = ResolveOne(
        source,
        resolvers,
        FxFastFileDisk32ReferenceKind::EffectName,
        &source.effect->name.token,
        journal,
        journalCount);
    if (status != Status::Success)
        return status;

    for (std::uint32_t index = 0; index < source.elements.count; ++index)
    {
        const FxElemDefDisk32 &elem = source.elements.data[index];
        const FxFastFileElemDefDisk32View &view =
            source.elementViews.data[index];
        if (elem.elemType == FxElemTypeDisk32::Decal)
        {
            for (std::uint32_t visual = 0; visual < elem.visualCount;
                 ++visual)
            {
                for (std::uint32_t material = 0; material < 2; ++material)
                {
                    status = ResolveOne(
                        source,
                        resolvers,
                        FxFastFileDisk32ReferenceKind::Material,
                        &view.markVisuals.data[visual]
                             .materials[material].token,
                        journal,
                        journalCount);
                    if (status != Status::Success)
                        return status;
                }
            }
        }
        else if (!IsLight(elem.elemType) && elem.visualCount)
        {
            const FxFastFileDisk32ReferenceKind kind =
                VisualReferenceKind(elem.elemType);
            if (elem.visualCount == 1)
            {
                status = ResolveOne(
                    source,
                    resolvers,
                    kind,
                    &elem.visuals.token,
                    journal,
                    journalCount);
                if (status != Status::Success)
                    return status;
            }
            else
            {
                for (std::uint32_t visual = 0; visual < elem.visualCount;
                     ++visual)
                {
                    status = ResolveOne(
                        source,
                        resolvers,
                        kind,
                        &view.visuals.data[visual].token,
                        journal,
                        journalCount);
                    if (status != Status::Success)
                        return status;
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
            if (reference->isNull())
                continue;
            status = ResolveOne(
                source,
                resolvers,
                FxFastFileDisk32ReferenceKind::EffectNameReference,
                reference,
                journal,
                journalCount);
            if (status != Status::Success)
                return status;
        }
    }
    return Status::Success;
}

template <typename T>
void CopyFloatRange(T *const destination, const FxFloatRangeDisk32 &source)
    noexcept
{
    destination->base = source.base;
    destination->amplitude = source.amplitude;
}

void CopyIntRange(
    native_detail::IntRange *const destination,
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
        const std::size_t retainedBytes =
            resolved[index].stringByteCount
            ? resolved[index].stringByteCount
            : 1u;
        if (RangesOverlap(
                storage,
                storageBytes,
                resolved[index].pointer,
                retainedBytes))
        {
            return true;
        }
    }
    return false;
}

void AssignVisual(
    native_detail::ElemVisuals *const destination,
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
    else
    {
        destination->anonymous = reference.pointer;
    }
}
} // namespace

FxFastFileNativeDisk32Status TryPlanFxEffectDefDisk32(
    FxFastFileNativeDisk32Workspace *const workspace,
    const FxFastFileEffectDefDisk32View &source,
    const FxFastFileDisk32Resolvers &resolvers,
    FxFastFileNativeDisk32Plan *const outPlan) noexcept
{
    if (!workspace || !outPlan || !resolvers.resolve
        || !source.provenance.validateSpan)
    {
        return Status::InvalidArgument;
    }
    if (workspace->operating_)
        return Status::Busy;
    if (RangesOverlap(
            outPlan, sizeof(*outPlan), workspace, sizeof(*workspace)))
    {
        return Status::InvalidArgument;
    }
    if (workspace->phase_ != FxFastFileNativeDisk32Phase::Empty)
        return Status::InvalidPhase;

    workspace->operating_ = true;
    const auto finish = [workspace](const Status status) noexcept {
        workspace->operating_ = false;
        return status;
    };
    const auto clearPlan = [workspace]() noexcept {
        workspace->source_ = {};
        workspace->plan_ = {};
        workspace->resolvedCount_ = 0;
        workspace->phase_ = FxFastFileNativeDisk32Phase::Empty;
    };
    clearPlan();

    Status status = ValidateGraph(source, true);
    if (status != Status::Success)
        return finish(status);
    // A provenance callback is external code and may have mutated an earlier
    // record. Re-run every pure graph invariant after the last callback.
    status = ValidateGraph(source, false);
    if (status != Status::Success)
        return finish(Status::SourceChanged);
    if (OverlapsSource(
            outPlan, sizeof(*outPlan), source, nullptr, 0))
    {
        return finish(Status::InvalidArgument);
    }

    const std::uint64_t sourceFingerprint =
        ComputeSourceFingerprint(source);
    status = ResolveGraph(
        source,
        resolvers,
        workspace->resolved_,
        &workspace->resolvedCount_);
    if (status != Status::Success)
    {
        clearPlan();
        return finish(status);
    }
    status = ValidateGraph(source, false);
    if (status != Status::Success
        || ComputeSourceFingerprint(source) != sourceFingerprint)
    {
        clearPlan();
        return finish(Status::SourceChanged);
    }
    for (std::uint32_t index = 0; index < workspace->resolvedCount_; ++index)
    {
        if (workspace->resolved_[index].stringByteCount
            && !IsExactCString(workspace->resolved_[index]))
        {
            clearPlan();
            return finish(Status::SourceChanged);
        }
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

    const std::uint32_t effectNameBytes =
        workspace->resolved_[0].stringByteCount;
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
        alignof(native_detail::ElemDef));
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
    if (!workspace || !storage || !outEffect
        || reinterpret_cast<std::uintptr_t>(outEffect)
            % alignof(FxEffectDef *) != 0)
    {
        return Status::InvalidArgument;
    }
    if (workspace->operating_)
        return Status::Busy;

    workspace->operating_ = true;
    const auto finish = [workspace](const Status status) noexcept {
        workspace->operating_ = false;
        return status;
    };
    const auto invalidate = [workspace]() noexcept {
        workspace->source_ = {};
        workspace->plan_ = {};
        workspace->resolvedCount_ = 0;
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
        || plan.outputAlignment_ != alignof(native_detail::ElemDef)
        || plan.elementCount_ != workspace->source_.elements.count
        || plan.resolvedReferenceCount_ != workspace->resolvedCount_
        || plan.effectNameBytes_ < 2)
    {
        return finish(Status::InvalidPlan);
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

    Status status = ValidateGraph(workspace->source_, false);
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
    for (std::uint32_t index = 0; index < workspace->resolvedCount_; ++index)
    {
        if (workspace->resolved_[index].stringByteCount
            && !IsExactCString(workspace->resolved_[index]))
        {
            invalidate();
            return finish(Status::SourceChanged);
        }
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
    native_detail::ElemDef *nativeElements = nullptr;
    if (!writer.ReserveArray(1, &nativeEffect)
        || !writer.ReserveArray(plan.elementCount_, &nativeElements))
    {
        invalidate();
        return finish(Status::InvalidPlan);
    }

    const FxEffectDefDisk32 &diskEffect = *workspace->source_.effect;
    std::uint32_t resolvedIndex = 1;
    for (std::uint32_t index = 0; index < plan.elementCount_; ++index)
    {
        const FxElemDefDisk32 &disk = workspace->source_.elements.data[index];
        const FxFastFileElemDefDisk32View &view =
            workspace->source_.elementViews.data[index];
        native_detail::ElemDef &native = nativeElements[index];

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
            if (!writer.ReserveArray(
                    disk.visualCount, &native.visuals.markArray))
            {
                invalidate();
                return finish(Status::InvalidPlan);
            }
            for (std::uint32_t visual = 0; visual < disk.visualCount;
                 ++visual)
            {
                for (std::uint32_t material = 0; material < 2; ++material)
                {
                    native.visuals.markArray[visual].materials[material] =
                        workspace->resolved_[resolvedIndex++].pointer;
                }
            }
        }
        else if (!IsLight(disk.elemType) && disk.visualCount == 1)
        {
            AssignVisual(
                &native.visuals.instance,
                disk.elemType,
                workspace->resolved_[resolvedIndex++]);
        }
        else if (!IsLight(disk.elemType) && disk.visualCount > 1)
        {
            if (!writer.ReserveArray(
                    disk.visualCount, &native.visuals.array))
            {
                invalidate();
                return finish(Status::InvalidPlan);
            }
            for (std::uint32_t visual = 0; visual < disk.visualCount;
                 ++visual)
            {
                AssignVisual(
                    &native.visuals.array[visual],
                    disk.elemType,
                    workspace->resolved_[resolvedIndex++]);
            }
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
            native_detail::TrailVertex *vertices = nullptr;
            if (!writer.ReserveArray(1, &native.trailDef)
                || !writer.ReserveArray(
                    static_cast<std::uint32_t>(view.trail->indCount),
                    &vertices)
                || !writer.ReserveArray(
                    static_cast<std::uint32_t>(view.trail->indCount),
                    &native.trailDef->inds))
            {
                invalidate();
                return finish(Status::InvalidPlan);
            }
            native.trailDef->scrollTimeMsec = view.trail->scrollTimeMsec;
            native.trailDef->repeatDist = view.trail->repeatDist;
            native.trailDef->splitDist = view.trail->splitDist;
            native.trailDef->vertCount = view.trail->vertCount;
            native.trailDef->verts = vertices;
            native.trailDef->indCount = view.trail->indCount;
            std::memcpy(
                vertices,
                view.trailVertices.data,
                static_cast<std::size_t>(view.trail->vertCount)
                    * sizeof(*vertices));
            std::memcpy(
                native.trailDef->inds,
                view.trailIndices.data,
                static_cast<std::size_t>(view.trail->indCount)
                    * sizeof(*native.trailDef->inds));
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
        ? reinterpret_cast<const FxElemDef *>(nativeElements)
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
