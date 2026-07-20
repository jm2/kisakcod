#pragma once

#include <EffectsCore/fx_fastfile_impact_native_disk32.h>
#include <EffectsCore/fx_fastfile_native_arena.h>
#include <EffectsCore/fx_fastfile_native_disk32.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

struct FxEffectDef;
struct FxImpactTable;

namespace fx::fastfile
{
// Guarded stateful zone adapter between the production fast-file XBlock
// cursor walk and the pure transactional FX Disk32 converters.
//
// The stateful wire walk (db_load.cpp) keeps ownership of stream order and
// materialization; it reports each Disk32 extent and each externally resolved
// reference to this adapter in exact legacy wire order.  The adapter derives
// the expected report sequence from the Disk32 records themselves (tokens and
// counts), validates every reported extent against the caller's cursor
// oracle, assembles the provenance-bounded converter views, replays the
// recorded resolutions and extents through the reviewed pure
// planner/materializer callbacks, reserves widened output from the zone-owned
// native arena, and invokes the caller's publication sink only after
// materialization has fully succeeded.  The arena reservation is committed
// immediately after successful materialization and before publication, so a
// rejected publication strands only unreferenced retired zone storage and a
// completed effect/impact XAsset is never observable half-built.  Once a wire
// transaction is open, sequence, validation, conversion, and publication
// failures tear the complete adapter transaction down: open arena
// reservations are abandoned, both converter workspaces are structurally
// reset, and the adapter returns to Idle while committed sibling
// publications keep their storage.  Busy and invalid-argument precondition
// failures preserve an active transaction so its trusted caller may retry.
//
// One impact-table transaction may nest inline effect-definition transactions
// (legacy sentinel handles); an effect transaction never nests another
// effect.  The legacy x86 in-place fixup path in db_load.cpp remains the
// compatibility boundary and is unchanged by this adapter.

inline constexpr std::size_t kFxFastFileZoneAdapterMaxReferences =
    kFxFastFileDisk32MaxResolvedReferences
    + kFxFastFileImpactDisk32JournalCount;
inline constexpr std::size_t kFxFastFileZoneAdapterMaxSpans =
    kFxFastFileDisk32MaxProvenanceRequests + 2u;
inline constexpr std::uint64_t kFxFastFileZoneAdapterMaxNameBytes = 4096;

// Span-provenance oracle implemented over the production XBlock cursor
// (or a test fake).  It must return true only when the complete extent lies
// within storage the cursor has materialized for the current zone.  The
// cursor-owned wire storage, adapter workspace, arena backing, and publication
// output locations must be mutually disjoint.  Cursor callbacks must not
// mutate recorded wire bytes or manipulate the shared arena.
using FxFastFileZoneAdapterValidateWireSpanCallback = bool (*)(
    void *context,
    const void *address,
    std::uint64_t byteCount) noexcept;

struct FxFastFileZoneAdapterCursor
{
    void *context = nullptr;
    FxFastFileZoneAdapterValidateWireSpanCallback validateWireSpan = nullptr;
};

// Publication sink.  Each callback is invoked exactly once after complete
// conversion and after the arena reservation has been committed.  A
// successful callback must return the canonical registered asset identity
// through outPublished; this may differ from the materialized arena root
// because DB_AddXAsset shallow-copies roots into its asset pools.  Returning
// false must leave external publication state unchanged.  Rejection strands
// the committed arena reservation as retired zone storage.  Publication
// callbacks must not mutate recorded wire bytes, manipulate the shared arena,
// or call adapter APIs recursively.
using FxFastFileZoneAdapterPublishEffectCallback = bool (*)(
    void *context,
    FxEffectDef *materialized,
    FxEffectDef **outPublished) noexcept;
using FxFastFileZoneAdapterPublishImpactCallback = bool (*)(
    void *context,
    FxImpactTable *materialized,
    FxImpactTable **outPublished) noexcept;

struct FxFastFileZoneAdapterPublication
{
    void *context = nullptr;
    FxFastFileZoneAdapterPublishEffectCallback publishEffect = nullptr;
    FxFastFileZoneAdapterPublishImpactCallback publishImpact = nullptr;
};

enum class FxFastFileZoneAdapterDisk32Status : std::uint8_t
{
    Success,
    Busy,
    InvalidArgument,
    InvalidPhase,
    InvalidSequence,
    InvalidSpan,
    InvalidCount,
    InvalidToken,
    InvalidString,
    InvalidReference,
    CapacityExceeded,
    ArenaFailed,
    ConversionFailed,
    PublicationFailed,
};

enum class FxFastFileZoneAdapterDisk32Phase : std::uint8_t
{
    Idle,
    EffectHeader,
    EffectName,
    EffectElements,
    EffectSealed,
    ImpactHeader,
    ImpactName,
    ImpactEntries,
    ImpactSealed,
};

class alignas(8) FxFastFileZoneAdapterDisk32Workspace;

#ifdef KISAK_FX_FASTFILE_ZONE_ADAPTER_TESTING
struct FxFastFileZoneAdapterDisk32WorkspaceTestAccess;
#endif

[[nodiscard]] FxFastFileZoneAdapterDisk32Status TryBeginFxEffectDefZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *workspace,
    FxFastFileNativeArena *arena,
    const FxFastFileZoneAdapterCursor &cursor,
    const FxEffectDefDisk32 *header) noexcept;

[[nodiscard]] FxFastFileZoneAdapterDisk32Status
TryRecordFxEffectDefNameZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *workspace,
    const char *name,
    std::uint64_t nameBytes) noexcept;

[[nodiscard]] FxFastFileZoneAdapterDisk32Status
TryRecordFxElemDefArrayZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *workspace,
    const FxElemDefDisk32 *elements,
    std::uint32_t count) noexcept;

// Reports one element-owned Disk32 extent (velocity/visibility samples,
// visuals or mark visuals array, trail definition/vertices/indices) for the
// element the adapter currently expects, in exact legacy wire order.  count
// is the record count (1 for TrailDefinition).
[[nodiscard]] FxFastFileZoneAdapterDisk32Status TryRecordFxElemSpanZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *workspace,
    FxFastFileDisk32SourceSpanKind kind,
    const void *address,
    std::uint32_t count) noexcept;

// Reports one externally resolved reference (material, model, or referenced
// effect identity) for the token field the adapter currently expects.  The
// resolution descriptor must satisfy the converter's retained-extent
// contract; the adapter records it verbatim for the exactly-once resolver
// replay.
[[nodiscard]] FxFastFileZoneAdapterDisk32Status TryRecordFxReferenceZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *workspace,
    const disk32::PointerToken *sourceField,
    const FxFastFileDisk32ResolvedReference &resolution) noexcept;

// Reports the wire sound-alias name for the sound visual slot the adapter
// currently expects.  The adapter validates the exact C-string extent against
// the cursor oracle and copies it into the zone-owned arena inside the open
// transaction, because the converter retains (does not copy) sound names.
[[nodiscard]] FxFastFileZoneAdapterDisk32Status TryRecordFxSoundNameZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *workspace,
    const disk32::PointerToken *sourceField,
    const char *name,
    std::uint64_t nameBytes) noexcept;

[[nodiscard]] FxFastFileZoneAdapterDisk32Status TrySealFxEffectDefZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *workspace) noexcept;

// outEffect storage is part of the caller-owned publication output and must
// satisfy the disjointness contract above.
[[nodiscard]] FxFastFileZoneAdapterDisk32Status TryPublishFxEffectDefZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *workspace,
    const FxFastFileZoneAdapterPublication &publication,
    FxEffectDef **outEffect) noexcept;

[[nodiscard]] FxFastFileZoneAdapterDisk32Status TryBeginFxImpactTableZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *workspace,
    FxFastFileNativeArena *arena,
    const FxFastFileZoneAdapterCursor &cursor,
    const FxImpactTableDisk32 *header) noexcept;

[[nodiscard]] FxFastFileZoneAdapterDisk32Status
TryRecordFxImpactTableNameZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *workspace,
    const char *name,
    std::uint64_t nameBytes) noexcept;

[[nodiscard]] FxFastFileZoneAdapterDisk32Status
TryRecordFxImpactEntryArrayZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *workspace,
    const FxImpactEntryDisk32 *entries,
    std::uint32_t count) noexcept;

// Reports one externally resolved (alias) effect handle for the impact slot
// the adapter currently expects, in entry/nonflesh/flesh order.  Slots whose
// wire token is null are skipped automatically and must not be reported;
// slots whose wire token is a legacy inline sentinel must instead run a
// complete nested effect-definition transaction.
[[nodiscard]] FxFastFileZoneAdapterDisk32Status
TryRecordFxImpactHandleZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *workspace,
    const disk32::PointerToken *sourceField,
    const FxFastFileDisk32ResolvedReference &resolution) noexcept;

[[nodiscard]] FxFastFileZoneAdapterDisk32Status TrySealFxImpactTableZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *workspace) noexcept;

// outTable storage is part of the caller-owned publication output and must
// satisfy the disjointness contract above.
[[nodiscard]] FxFastFileZoneAdapterDisk32Status
TryPublishFxImpactTableZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *workspace,
    const FxFastFileZoneAdapterPublication &publication,
    FxImpactTable **outTable) noexcept;

// Abandons every open wire transaction: open arena reservations are
// abandoned innermost-first, both converter workspaces are structurally
// reset, and the adapter returns to Idle.  Fails InvalidPhase when the
// adapter is already Idle.
[[nodiscard]] FxFastFileZoneAdapterDisk32Status
AbortFxFastFileZoneAdapterDisk32(
    FxFastFileZoneAdapterDisk32Workspace *workspace) noexcept;

// Heap-only scratch.  Owns both pure converter workspaces, the assembled
// element views, and the fixed recording journals sized for one impact-table
// transaction plus one nested maximum-size effect transaction.
class alignas(8) FxFastFileZoneAdapterDisk32Workspace final
{
public:
    FxFastFileZoneAdapterDisk32Workspace() noexcept = default;
    ~FxFastFileZoneAdapterDisk32Workspace() noexcept = default;

    FxFastFileZoneAdapterDisk32Workspace(
        const FxFastFileZoneAdapterDisk32Workspace &) = delete;
    FxFastFileZoneAdapterDisk32Workspace &operator=(
        const FxFastFileZoneAdapterDisk32Workspace &) = delete;
    FxFastFileZoneAdapterDisk32Workspace(
        FxFastFileZoneAdapterDisk32Workspace &&) = delete;
    FxFastFileZoneAdapterDisk32Workspace &operator=(
        FxFastFileZoneAdapterDisk32Workspace &&) = delete;

    [[nodiscard]] FxFastFileZoneAdapterDisk32Phase phase() const noexcept;

    [[nodiscard]] std::uint32_t frameDepth() const noexcept
    {
        return frameDepth_;
    }

    // Outer placement-storage teardown must reject both published frames and
    // the pre-publication interval while an adapter entry point/callback is
    // operating on this workspace.
    [[nodiscard]] bool readyForDestruction() const noexcept
    {
        return !operating_ && frameDepth_ == 0;
    }

    // Exact read-only gate for an externally serialized composition check.
    // A depth-zero workspace is reusable only after the complete recording
    // topology has returned to ResetRecordingState's canonical boundary;
    // stale counts or frame witnesses would otherwise become base indices for
    // the next top-level transaction.
    [[nodiscard]] bool readyForCompositionAuthentication() const noexcept;

    [[nodiscard]] FxFastFileNativeDisk32Status
    lastConverterStatus() const noexcept
    {
        return lastConverterStatus_;
    }

    [[nodiscard]] FxFastFileNativeArenaStatus lastArenaStatus() const noexcept
    {
        return lastArenaStatus_;
    }

private:
    friend FxFastFileZoneAdapterDisk32Status TryBeginFxEffectDefZoneDisk32(
        FxFastFileZoneAdapterDisk32Workspace *,
        FxFastFileNativeArena *,
        const FxFastFileZoneAdapterCursor &,
        const FxEffectDefDisk32 *) noexcept;
    friend FxFastFileZoneAdapterDisk32Status
    TryRecordFxEffectDefNameZoneDisk32(
        FxFastFileZoneAdapterDisk32Workspace *,
        const char *,
        std::uint64_t) noexcept;
    friend FxFastFileZoneAdapterDisk32Status
    TryRecordFxElemDefArrayZoneDisk32(
        FxFastFileZoneAdapterDisk32Workspace *,
        const FxElemDefDisk32 *,
        std::uint32_t) noexcept;
    friend FxFastFileZoneAdapterDisk32Status TryRecordFxElemSpanZoneDisk32(
        FxFastFileZoneAdapterDisk32Workspace *,
        FxFastFileDisk32SourceSpanKind,
        const void *,
        std::uint32_t) noexcept;
    friend FxFastFileZoneAdapterDisk32Status TryRecordFxReferenceZoneDisk32(
        FxFastFileZoneAdapterDisk32Workspace *,
        const disk32::PointerToken *,
        const FxFastFileDisk32ResolvedReference &) noexcept;
    friend FxFastFileZoneAdapterDisk32Status TryRecordFxSoundNameZoneDisk32(
        FxFastFileZoneAdapterDisk32Workspace *,
        const disk32::PointerToken *,
        const char *,
        std::uint64_t) noexcept;
    friend FxFastFileZoneAdapterDisk32Status TrySealFxEffectDefZoneDisk32(
        FxFastFileZoneAdapterDisk32Workspace *) noexcept;
    friend FxFastFileZoneAdapterDisk32Status TryPublishFxEffectDefZoneDisk32(
        FxFastFileZoneAdapterDisk32Workspace *,
        const FxFastFileZoneAdapterPublication &,
        FxEffectDef **) noexcept;
    friend FxFastFileZoneAdapterDisk32Status TryBeginFxImpactTableZoneDisk32(
        FxFastFileZoneAdapterDisk32Workspace *,
        FxFastFileNativeArena *,
        const FxFastFileZoneAdapterCursor &,
        const FxImpactTableDisk32 *) noexcept;
    friend FxFastFileZoneAdapterDisk32Status
    TryRecordFxImpactTableNameZoneDisk32(
        FxFastFileZoneAdapterDisk32Workspace *,
        const char *,
        std::uint64_t) noexcept;
    friend FxFastFileZoneAdapterDisk32Status
    TryRecordFxImpactEntryArrayZoneDisk32(
        FxFastFileZoneAdapterDisk32Workspace *,
        const FxImpactEntryDisk32 *,
        std::uint32_t) noexcept;
    friend FxFastFileZoneAdapterDisk32Status
    TryRecordFxImpactHandleZoneDisk32(
        FxFastFileZoneAdapterDisk32Workspace *,
        const disk32::PointerToken *,
        const FxFastFileDisk32ResolvedReference &) noexcept;
    friend FxFastFileZoneAdapterDisk32Status TrySealFxImpactTableZoneDisk32(
        FxFastFileZoneAdapterDisk32Workspace *) noexcept;
    friend FxFastFileZoneAdapterDisk32Status
    TryPublishFxImpactTableZoneDisk32(
        FxFastFileZoneAdapterDisk32Workspace *,
        const FxFastFileZoneAdapterPublication &,
        FxImpactTable **) noexcept;
    friend FxFastFileZoneAdapterDisk32Status
    AbortFxFastFileZoneAdapterDisk32(
        FxFastFileZoneAdapterDisk32Workspace *) noexcept;
#ifdef KISAK_FX_FASTFILE_ZONE_ADAPTER_TESTING
    friend struct FxFastFileZoneAdapterDisk32WorkspaceTestAccess;
#endif

    enum class FrameKind : std::uint8_t
    {
        None,
        Effect,
        Impact,
    };

    enum class ElementStage : std::uint8_t
    {
        Velocity,
        Visibility,
        VisualsSpan,
        VisualReferences,
        EffectReferences,
        TrailDefinition,
        TrailVertices,
        TrailIndices,
        Complete,
    };

    struct RecordedReference
    {
        const disk32::PointerToken *sourceField = nullptr;
        FxFastFileDisk32ResolvedReference resolution{};
        disk32::PointerToken token{};
        FxFastFileDisk32ReferenceKind kind =
            FxFastFileDisk32ReferenceKind::EffectName;
        bool isString = false;
        bool consumed = false;
    };

    // alignas keeps the ILP32 MSVC and GCC layouts identical.
    struct alignas(8) RecordedSpan
    {
        const void *address = nullptr;
        const disk32::PointerToken *sourceField = nullptr;
        std::uint64_t byteCount = 0;
        disk32::PointerToken token{};
        std::uint32_t count = 0;
        FxFastFileDisk32SourceSpanKind kind =
            FxFastFileDisk32SourceSpanKind::EffectHeader;
    };

    struct Frame
    {
        FrameKind kind = FrameKind::None;
        FxFastFileZoneAdapterDisk32Phase state =
            FxFastFileZoneAdapterDisk32Phase::Idle;
        ElementStage stage = ElementStage::Velocity;
        const FxEffectDefDisk32 *effectHeader = nullptr;
        const FxImpactTableDisk32 *impactHeader = nullptr;
        const FxElemDefDisk32 *elements = nullptr;
        const FxImpactEntryDisk32 *entries = nullptr;
        std::uint32_t elementCount = 0;
        std::uint32_t elementIndex = 0;
        std::uint32_t visualSlot = 0;
        std::uint32_t effectRefSlot = 0;
        std::uint32_t impactSlot = 0;
        std::uint32_t referenceBase = 0;
        std::uint32_t spanBase = 0;
        FxFastFileNativeArenaTransaction arenaTransaction{};
    };

    static void ResetRecordingState(
        FxFastFileZoneAdapterDisk32Workspace &workspace) noexcept;
    static void TeardownTransaction(
        FxFastFileZoneAdapterDisk32Workspace &workspace) noexcept;
    [[nodiscard]] static FxFastFileZoneAdapterDisk32Status FailTransaction(
        FxFastFileZoneAdapterDisk32Workspace &workspace,
        FxFastFileZoneAdapterDisk32Status status) noexcept;
    [[nodiscard]] static FxFastFileZoneAdapterDisk32Status NormalizeEffectWalk(
        FxFastFileZoneAdapterDisk32Workspace &workspace,
        Frame &frame) noexcept;
    static void NormalizeImpactSlots(Frame &frame) noexcept;
    [[nodiscard]] static FxFastFileZoneAdapterDisk32Status AppendReference(
        FxFastFileZoneAdapterDisk32Workspace &workspace,
        const RecordedReference &reference) noexcept;
    [[nodiscard]] static FxFastFileZoneAdapterDisk32Status AppendSpan(
        FxFastFileZoneAdapterDisk32Workspace &workspace,
        const RecordedSpan &span) noexcept;
    [[nodiscard]] static const disk32::PointerToken *ImpactSlotField(
        const Frame &frame,
        std::uint32_t slot) noexcept;

    [[nodiscard]] static bool ResolveReferenceThunk(
        void *context,
        FxFastFileDisk32ReferenceKind kind,
        const disk32::PointerToken *sourceField,
        disk32::PointerToken token,
        FxFastFileDisk32ResolvedReference *outReference) noexcept;
    [[nodiscard]] static bool ValidateSpanThunk(
        void *context,
        FxFastFileDisk32SourceSpanKind kind,
        const disk32::PointerToken *sourceField,
        disk32::PointerToken token,
        const void *address,
        std::uint64_t byteCount,
        std::size_t alignment) noexcept;

    FxFastFileNativeDisk32Workspace effectConverter_{};
    FxFastFileImpactNativeDisk32Workspace impactConverter_{};
    FxFastFileElemDefDisk32View elementViews_[
        kFxFastFileDisk32MaxEffectElements]{};
    RecordedReference references_[kFxFastFileZoneAdapterMaxReferences]{};
    RecordedSpan spans_[kFxFastFileZoneAdapterMaxSpans]{};
    Frame frames_[2]{};
    FxFastFileNativeArena *arena_ = nullptr;
#if !KISAK_ARCH_64BIT
    // Keep the pointer-bearing members densely packed on ILP32 targets.
    std::uint32_t arenaPadding_ = 0;
#endif
    FxFastFileZoneAdapterCursor cursor_{};
    std::uint32_t referenceCount_ = 0;
    std::uint32_t spanCount_ = 0;
    std::uint32_t frameDepth_ = 0;
    std::uint32_t resolveHint_ = 0;
    std::uint32_t spanHint_ = 0;
    FxFastFileNativeDisk32Status lastConverterStatus_ =
        FxFastFileNativeDisk32Status::Success;
    FxFastFileNativeArenaStatus lastArenaStatus_ =
        FxFastFileNativeArenaStatus::Success;
    bool operating_ = false;
};

static_assert(
    std::is_nothrow_default_constructible_v<
        FxFastFileZoneAdapterDisk32Workspace>);
static_assert(
    std::is_nothrow_destructible_v<FxFastFileZoneAdapterDisk32Workspace>);
RUNTIME_SIZE(FxFastFileZoneAdapterDisk32Workspace, 0xBD048, 0xC34C8);

inline bool FxFastFileZoneAdapterDisk32Workspace::
readyForCompositionAuthentication() const noexcept
{
    if (operating_ || frameDepth_ != 0 || arena_ != nullptr
        || cursor_.context != nullptr || cursor_.validateWireSpan != nullptr
        || referenceCount_ != 0 || spanCount_ != 0 || resolveHint_ != 0
        || spanHint_ != 0
        || effectConverter_.phase() != FxFastFileNativeDisk32Phase::Empty
        || impactConverter_.phase() != FxFastFileNativeDisk32Phase::Empty)
    {
        return false;
    }

    for (const Frame &frame : frames_)
    {
        if (frame.kind != FrameKind::None
            || frame.state != FxFastFileZoneAdapterDisk32Phase::Idle
            || frame.stage != ElementStage::Velocity
            || frame.effectHeader != nullptr || frame.impactHeader != nullptr
            || frame.elements != nullptr || frame.entries != nullptr
            || frame.elementCount != 0 || frame.elementIndex != 0
            || frame.visualSlot != 0 || frame.effectRefSlot != 0
            || frame.impactSlot != 0 || frame.referenceBase != 0
            || frame.spanBase != 0
            || frame.arenaTransaction
                != FxFastFileNativeArenaTransaction{})
        {
            return false;
        }
    }
    return true;
}

#ifdef KISAK_FX_FASTFILE_ZONE_ADAPTER_TESTING
// Tests opt in before including this header. Production callers have no
// mutation escape hatch around the checked adapter state machine.
struct FxFastFileZoneAdapterDisk32WorkspaceTestAccess final
{
    static void SetArena(
        FxFastFileZoneAdapterDisk32Workspace *const workspace,
        FxFastFileNativeArena *const arena) noexcept
    {
        if (workspace)
            workspace->arena_ = arena;
    }

    static void SetCursor(
        FxFastFileZoneAdapterDisk32Workspace *const workspace,
        const FxFastFileZoneAdapterCursor &cursor) noexcept
    {
        if (workspace)
            workspace->cursor_ = cursor;
    }

    static void SetRecordingCounts(
        FxFastFileZoneAdapterDisk32Workspace *const workspace,
        const std::uint32_t referenceCount,
        const std::uint32_t spanCount) noexcept
    {
        if (workspace)
        {
            workspace->referenceCount_ = referenceCount;
            workspace->spanCount_ = spanCount;
        }
    }

    static void SetHints(
        FxFastFileZoneAdapterDisk32Workspace *const workspace,
        const std::uint32_t resolveHint,
        const std::uint32_t spanHint) noexcept
    {
        if (workspace)
        {
            workspace->resolveHint_ = resolveHint;
            workspace->spanHint_ = spanHint;
        }
    }

    static void CorruptDormantFrame(
        FxFastFileZoneAdapterDisk32Workspace *const workspace) noexcept
    {
        if (workspace)
            workspace->frames_[0].kind =
                FxFastFileZoneAdapterDisk32Workspace::FrameKind::Effect;
    }

    static void SetOperating(
        FxFastFileZoneAdapterDisk32Workspace *const workspace,
        const bool operating) noexcept
    {
        if (workspace)
            workspace->operating_ = operating;
    }

    static void RestoreStableState(
        FxFastFileZoneAdapterDisk32Workspace *const workspace) noexcept
    {
        if (!workspace)
            return;
        workspace->operating_ = false;
        FxFastFileZoneAdapterDisk32Workspace::ResetRecordingState(*workspace);
    }
};
#endif
} // namespace fx::fastfile
