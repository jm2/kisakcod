#pragma once

#include <EffectsCore/fx_fastfile_disk32.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

struct FxEffectDef;

namespace fx::fastfile
{
inline constexpr std::uint32_t kFxFastFileDisk32MaxEffectElements = 256;
inline constexpr std::uint32_t kFxFastFileDisk32MaxVisuals = 32;
inline constexpr std::uint32_t kFxFastFileDisk32MaxDecalVisuals = 16;
inline constexpr std::size_t kFxFastFileDisk32MaxResolvedReferences =
    1u + static_cast<std::size_t>(kFxFastFileDisk32MaxEffectElements)
        * (3u + kFxFastFileDisk32MaxVisuals);
inline constexpr std::size_t kFxFastFileDisk32MaxProvenanceRequests =
    2u + static_cast<std::size_t>(kFxFastFileDisk32MaxEffectElements) * 6u;

template <typename T>
struct FxFastFileDisk32Span
{
    const T *data = nullptr;
    std::uint32_t count = 0;
};

enum class FxFastFileDisk32SourceSpanKind : std::uint8_t
{
    EffectHeader,
    ElementDefinitions,
    VelocitySamples,
    VisibilitySamples,
    Visuals,
    MarkVisuals,
    TrailDefinition,
    TrailVertices,
    TrailIndices,
    String,
    ImpactTableHeader,
    ImpactEntries,
};

using FxFastFileDisk32ValidateSourceSpanCallback = bool (*)(
    void *context,
    FxFastFileDisk32SourceSpanKind kind,
    const disk32::PointerToken *sourceField,
    disk32::PointerToken token,
    const void *address,
    std::uint64_t byteCount,
    std::size_t alignment) noexcept;

struct FxFastFileDisk32Provenance
{
    void *context = nullptr;
    FxFastFileDisk32ValidateSourceSpanCallback validateSpan = nullptr;
};

struct FxFastFileElemDefDisk32View
{
    FxFastFileDisk32Span<FxElemVelStateSampleDisk32> velocitySamples{};
    FxFastFileDisk32Span<FxElemVisStateSampleDisk32> visibilitySamples{};
    FxFastFileDisk32Span<FxElemVisualsDisk32> visuals{};
    FxFastFileDisk32Span<FxElemMarkVisualsDisk32> markVisuals{};
    const FxTrailDefDisk32 *trail = nullptr;
    FxFastFileDisk32Span<FxTrailVertexDisk32> trailVertices{};
    FxFastFileDisk32Span<std::uint16_t> trailIndices{};
};

struct FxFastFileEffectDefDisk32View
{
    const FxEffectDefDisk32 *effect = nullptr;
    FxFastFileDisk32Span<FxElemDefDisk32> elements{};
    FxFastFileDisk32Span<FxFastFileElemDefDisk32View> elementViews{};
    FxFastFileDisk32Provenance provenance{};
};

enum class FxFastFileDisk32ReferenceKind : std::uint8_t
{
    EffectName,
    Material,
    Model,
    SoundName,
    EffectNameReference,
    EffectAssetHandle,
};

struct alignas(8) FxFastFileDisk32ResolvedReference
{
    const void *pointer = nullptr;
#if !KISAK_ARCH_64BIT
    // Keep the retained extent fields at their LP64 offsets on ILP32 targets.
    // Resolvers must leave this reserved field zero.
    std::uint32_t pointerPadding_ = 0;
#endif
    // The complete externally retained object range.  Names include their
    // terminating NUL and use alignment one.  Effect identities describe one
    // complete FxEffectDef.  As a hard resolver precondition, an opaque asset
    // count must describe its complete readable, immutable retained range; the
    // converter cannot independently infer that type-erased extent.  Its
    // reported alignment must be a power of two of at least alignof(void *).
    std::uint64_t retainedByteCount = 0;
    std::uint64_t retainedAlignment = 0;
};

// One immutable caller-owned source-graph provenance request.  Effect planning
// fills the complete fixed-capacity source journal before its first provenance
// callback, then invokes only these frozen descriptors while checking each
// source span immediately before and after its callback.  Resolver-returned
// strings receive a separate immediate resolution-time attestation because
// their ranges do not exist until their resolver callback returns.
struct alignas(8) FxFastFileDisk32FrozenProvenanceRequest
{
    const disk32::PointerToken *sourceField = nullptr;
#if !KISAK_ARCH_64BIT
    std::uint32_t sourceFieldPadding_ = 0;
#endif
    // Addresses of the live pointer/count descriptor fields from which this
    // request was frozen.  countField is null for fixed-size records.
    const void *addressField = nullptr;
#if !KISAK_ARCH_64BIT
    std::uint32_t addressFieldPadding_ = 0;
#endif
    const std::uint32_t *countField = nullptr;
#if !KISAK_ARCH_64BIT
    std::uint32_t countFieldPadding_ = 0;
#endif
    const void *address = nullptr;
#if !KISAK_ARCH_64BIT
    std::uint32_t addressPadding_ = 0;
#endif
    std::uint64_t byteCount = 0;
    std::uint64_t contentFingerprint = 0;
    disk32::PointerToken token{};
    std::uint32_t countValue = 0;
    std::uint32_t alignment = 0;
    FxFastFileDisk32SourceSpanKind kind =
        FxFastFileDisk32SourceSpanKind::EffectHeader;
    std::uint8_t reserved_[3]{};
};

using FxFastFileDisk32ResolveReferenceCallback = bool (*)(
    void *context,
    FxFastFileDisk32ReferenceKind kind,
    const disk32::PointerToken *sourceField,
    disk32::PointerToken token,
    FxFastFileDisk32ResolvedReference *outReference) noexcept;

struct FxFastFileDisk32Resolvers
{
    void *context = nullptr;
    FxFastFileDisk32ResolveReferenceCallback resolve = nullptr;
};

enum class FxFastFileNativeDisk32Status : std::uint8_t
{
    Success,
    Busy,
    InvalidArgument,
    InvalidPhase,
    InvalidPlan,
    InvalidCount,
    InvalidSourceLayout,
    InvalidPointerCount,
    InvalidProvenance,
    UnresolvedReference,
    InvalidString,
    InvalidVisual,
    InvalidTrail,
    SizeOverflow,
    SourceChanged,
    MisalignedStorage,
    InsufficientCapacity,
    OverlappingStorage,
};

enum class FxFastFileNativeDisk32Phase : std::uint8_t
{
    Empty,
    Planned,
};

class FxFastFileNativeDisk32Workspace;

class alignas(8) FxFastFileNativeDisk32Plan final
{
public:
    constexpr FxFastFileNativeDisk32Plan() noexcept = default;

    [[nodiscard]] constexpr std::uint32_t outputBytes() const noexcept
    {
        return outputBytes_;
    }

    [[nodiscard]] constexpr std::uint32_t outputAlignment() const noexcept
    {
        return outputAlignment_;
    }

    [[nodiscard]] constexpr std::uint32_t elementCount() const noexcept
    {
        return elementCount_;
    }

    [[nodiscard]] constexpr std::uint32_t resolvedReferenceCount() const noexcept
    {
        return resolvedReferenceCount_;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return workspaceIdentity_ != nullptr && serial_ != 0;
    }

private:
    friend FxFastFileNativeDisk32Status TryPlanFxEffectDefDisk32(
        FxFastFileNativeDisk32Workspace *,
        const FxFastFileEffectDefDisk32View &,
        const FxFastFileDisk32Resolvers &,
        FxFastFileNativeDisk32Plan *) noexcept;
    friend FxFastFileNativeDisk32Status TryMaterializeFxEffectDefDisk32(
        FxFastFileNativeDisk32Workspace *,
        const FxFastFileNativeDisk32Plan &,
        void *,
        std::size_t,
        FxEffectDef **) noexcept;
    friend class FxFastFileNativeDisk32Workspace;

    const FxFastFileNativeDisk32Workspace *workspaceIdentity_ = nullptr;
#if !KISAK_ARCH_64BIT
    // Keep the following uint64_t members at the MSVC x86 offset on every
    // ILP32 compiler; alignas controls class alignment, not member placement.
    std::uint32_t workspaceIdentityPadding_ = 0;
#endif
    std::uint64_t serial_ = 0;
    std::uint64_t sourceFingerprint_ = 0;
    std::uint32_t outputBytes_ = 0;
    std::uint32_t outputAlignment_ = 0;
    std::uint32_t elementCount_ = 0;
    std::uint32_t resolvedReferenceCount_ = 0;
    std::uint32_t effectNameBytes_ = 0;
};

// Heap-only scratch.  The fixed journals hold every source-provenance request
// and the maximum number of resolved identities representable by 256 elements
// with 32 visual references apiece (including 16 two-material decals).
// Planning freezes the complete caller-owned source request schedule before
// callback one, calls each resolver exactly once, immediately binds and
// attests returned strings, and publishes Planned as its final mutation;
// materialization consumes only these journals.
class alignas(8) FxFastFileNativeDisk32Workspace final
{
public:
    FxFastFileNativeDisk32Workspace() noexcept = default;
    ~FxFastFileNativeDisk32Workspace() noexcept = default;

    FxFastFileNativeDisk32Workspace(
        const FxFastFileNativeDisk32Workspace &) = delete;
    FxFastFileNativeDisk32Workspace &operator=(
        const FxFastFileNativeDisk32Workspace &) = delete;
    FxFastFileNativeDisk32Workspace(
        FxFastFileNativeDisk32Workspace &&) = delete;
    FxFastFileNativeDisk32Workspace &operator=(
        FxFastFileNativeDisk32Workspace &&) = delete;

    [[nodiscard]] FxFastFileNativeDisk32Phase phase() const noexcept
    {
        return phase_;
    }

private:
    friend FxFastFileNativeDisk32Status TryPlanFxEffectDefDisk32(
        FxFastFileNativeDisk32Workspace *,
        const FxFastFileEffectDefDisk32View &,
        const FxFastFileDisk32Resolvers &,
        FxFastFileNativeDisk32Plan *) noexcept;
    friend FxFastFileNativeDisk32Status TryMaterializeFxEffectDefDisk32(
        FxFastFileNativeDisk32Workspace *,
        const FxFastFileNativeDisk32Plan &,
        void *,
        std::size_t,
        FxEffectDef **) noexcept;

    FxFastFileEffectDefDisk32View source_{};
    FxFastFileNativeDisk32Plan plan_{};
    FxFastFileDisk32ResolvedReference
        resolved_[kFxFastFileDisk32MaxResolvedReferences]{};
    FxFastFileDisk32FrozenProvenanceRequest
        provenanceRequests_[kFxFastFileDisk32MaxProvenanceRequests]{};
    std::uint64_t provenanceRequestChecksums_[
        kFxFastFileDisk32MaxProvenanceRequests]{};
    std::uint64_t nextSerial_ = 1;
    std::uint32_t resolvedCount_ = 0;
    std::uint32_t provenanceRequestCount_ = 0;
    FxFastFileNativeDisk32Phase phase_ =
        FxFastFileNativeDisk32Phase::Empty;
    bool operating_ = false;
};

// The source view, every reachable source byte, and every complete
// resolver-returned retained range must remain readable and byte-for-byte
// immutable for as long as the workspace remains Planned, including through
// every matching materialization attempt.  Resolvers must also keep each
// returned pointer/count/alignment descriptor valid through callback return.
// Planning resolves each retained native identity exactly once under the
// operation gate and commits outPlan only after the full graph, journals, and
// native layout validate.
[[nodiscard]] FxFastFileNativeDisk32Status TryPlanFxEffectDefDisk32(
    FxFastFileNativeDisk32Workspace *workspace,
    const FxFastFileEffectDefDisk32View &source,
    const FxFastFileDisk32Resolvers &resolvers,
    FxFastFileNativeDisk32Plan *outPlan) noexcept;

// Materializes into aligned caller-owned storage using the exact unconsumed
// plan/journal binding.  It performs no resolver callbacks.  Failure never
// changes outEffect; storage is changed only after plan, source fingerprint,
// capacity, alignment, and overlap checks complete.  Successful output owns
// all copied records and its effect name.  Resolver-retained sound names,
// materials, models, and effect identities are not copied and must remain
// readable and valid for every use of the successful output.
[[nodiscard]] FxFastFileNativeDisk32Status TryMaterializeFxEffectDefDisk32(
    FxFastFileNativeDisk32Workspace *workspace,
    const FxFastFileNativeDisk32Plan &plan,
    void *storage,
    std::size_t capacity,
    FxEffectDef **outEffect) noexcept;

static_assert(
    std::is_nothrow_default_constructible_v<
        FxFastFileNativeDisk32Workspace>);
static_assert(
    std::is_nothrow_destructible_v<FxFastFileNativeDisk32Workspace>);
static_assert(alignof(FxFastFileDisk32ResolvedReference) == 8);
static_assert(alignof(FxFastFileDisk32FrozenProvenanceRequest) == 8);
RUNTIME_SIZE(FxFastFileDisk32ResolvedReference, 0x18, 0x18);
RUNTIME_SIZE(FxFastFileDisk32FrozenProvenanceRequest, 0x40, 0x40);
RUNTIME_SIZE(FxFastFileNativeDisk32Plan, 0x30, 0x30);
RUNTIME_SIZE(FxFastFileNativeDisk32Workspace, 0x4F910, 0x4F928);
} // namespace fx::fastfile
