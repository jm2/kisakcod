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

struct FxFastFileDisk32ResolvedReference
{
    const void *pointer = nullptr;
    // Required for EffectName and SoundName, including the terminating NUL;
    // must be zero for native asset identities.
    std::uint32_t stringByteCount = 0;
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

// Heap-only scratch.  The fixed journal holds the maximum number of resolved
// identities representable by 256 elements with 32 visual references apiece
// (including 16 two-material decals). Planning calls each resolver exactly
// once and publishes Planned as its final mutation; materialization consumes
// only this journal.
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
    std::uint64_t nextSerial_ = 1;
    std::uint32_t resolvedCount_ = 0;
    FxFastFileNativeDisk32Phase phase_ =
        FxFastFileNativeDisk32Phase::Empty;
    bool operating_ = false;
};

// The source view and every reachable source byte must remain immutable and
// readable between planning and materialization. Planning resolves each
// retained native identity exactly once under the operation gate and commits
// outPlan only after the full graph, journal, and native layout validate.
[[nodiscard]] FxFastFileNativeDisk32Status TryPlanFxEffectDefDisk32(
    FxFastFileNativeDisk32Workspace *workspace,
    const FxFastFileEffectDefDisk32View &source,
    const FxFastFileDisk32Resolvers &resolvers,
    FxFastFileNativeDisk32Plan *outPlan) noexcept;

// Materializes into aligned caller-owned storage using the exact unconsumed
// plan/journal binding.  It performs no resolver callbacks.  Failure never
// changes outEffect; storage is changed only after plan, source fingerprint,
// capacity, alignment, and overlap checks complete.  Successful output owns
// all copied records and its effect name; resolved assets/sound names retain
// the resolver's external lifetime.
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
RUNTIME_SIZE(FxFastFileNativeDisk32Plan, 0x30, 0x30);
RUNTIME_SIZE(FxFastFileNativeDisk32Workspace, 0x11868, 0x23088);
} // namespace fx::fastfile
