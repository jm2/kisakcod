#include <EffectsCore/fx_fastfile_zone_adapter_disk32.h>

#include <EffectsCore/fx_effect_def.h>
#include <EffectsCore/fx_runtime.h>

#include <cstring>
#include <limits>
#include <new>

namespace fx::fastfile
{
namespace
{
using Status = FxFastFileZoneAdapterDisk32Status;
using Phase = FxFastFileZoneAdapterDisk32Phase;
using SpanKind = FxFastFileDisk32SourceSpanKind;
using ReferenceKind = FxFastFileDisk32ReferenceKind;

class OperationGate final
{
public:
    explicit OperationGate(bool *flag) noexcept : flag_(flag)
    {
        if (*flag_)
        {
            flag_ = nullptr;
            return;
        }
        *flag_ = true;
    }

    ~OperationGate() noexcept
    {
        if (flag_)
            *flag_ = false;
    }

    OperationGate(const OperationGate &) = delete;
    OperationGate &operator=(const OperationGate &) = delete;

    [[nodiscard]] bool acquired() const noexcept
    {
        return flag_ != nullptr;
    }

private:
    bool *flag_;
};

[[nodiscard]] bool IsAlignedAddress(
    const void *const address,
    const std::size_t alignment) noexcept
{
    // Callers pass compile-time alignof constants or converter-validated
    // power-of-two alignments, so the mask form is exact.
    return (reinterpret_cast<std::uintptr_t>(address)
            & (static_cast<std::uintptr_t>(alignment) - 1u)) == 0;
}

[[nodiscard]] bool OracleValidates(
    const FxFastFileZoneAdapterCursor &cursor,
    const void *const address,
    const std::uint64_t byteCount) noexcept
{
    return cursor.validateWireSpan
        && cursor.validateWireSpan(cursor.context, address, byteCount);
}

[[nodiscard]] bool IsExactWireCString(
    const char *const name,
    const std::uint64_t nameBytes) noexcept
{
    if (!name || nameBytes == 0
        || nameBytes > kFxFastFileZoneAdapterMaxNameBytes)
    {
        return false;
    }
    const std::size_t bytes = static_cast<std::size_t>(nameBytes);
    if (name[bytes - 1] != '\0')
        return false;
    return bytes == 1 || std::memchr(name, '\0', bytes - 1) == nullptr;
}

[[nodiscard]] bool IsLightElement(const FxElemTypeDisk32 type) noexcept
{
    return type == FxElemTypeDisk32::OmniLight
        || type == FxElemTypeDisk32::SpotLight;
}

// Mirrors the converter's visual-reference grammar selection.
[[nodiscard]] ReferenceKind VisualReferenceKind(
    const FxElemTypeDisk32 type) noexcept
{
    if (type == FxElemTypeDisk32::Model)
        return ReferenceKind::Model;
    if (type == FxElemTypeDisk32::Sound)
        return ReferenceKind::SoundName;
    if (type == FxElemTypeDisk32::Runner)
        return ReferenceKind::EffectNameReference;
    return ReferenceKind::Material;
}

[[nodiscard]] const disk32::PointerToken *ElementEffectReferenceField(
    const FxElemDefDisk32 &element,
    const std::uint32_t slot) noexcept
{
    switch (slot)
    {
    case 0:
        return &element.effectOnImpact.token;
    case 1:
        return &element.effectOnDeath.token;
    default:
        return &element.effectEmitted.token;
    }
}
} // namespace

FxFastFileZoneAdapterDisk32Phase
FxFastFileZoneAdapterDisk32Workspace::phase() const noexcept
{
    return frameDepth_ == 0 ? Phase::Idle : frames_[frameDepth_ - 1].state;
}

void FxFastFileZoneAdapterDisk32Workspace::ResetRecordingState(
    FxFastFileZoneAdapterDisk32Workspace &workspace) noexcept
{
    workspace.referenceCount_ = 0;
    workspace.spanCount_ = 0;
    workspace.frameDepth_ = 0;
    workspace.resolveHint_ = 0;
    workspace.spanHint_ = 0;
    workspace.frames_[0] = Frame{};
    workspace.frames_[1] = Frame{};
    workspace.arena_ = nullptr;
    workspace.cursor_ = FxFastFileZoneAdapterCursor{};
}

void FxFastFileZoneAdapterDisk32Workspace::TeardownTransaction(
    FxFastFileZoneAdapterDisk32Workspace &workspace) noexcept
{
    for (std::uint32_t depth = workspace.frameDepth_; depth > 0; --depth)
    {
        Frame &frame = workspace.frames_[depth - 1];
        if (workspace.arena_ && frame.arenaTransaction)
            (void)workspace.arena_->TryAbandon(frame.arenaTransaction);
        frame = Frame{};
    }
    workspace.effectConverter_.~FxFastFileNativeDisk32Workspace();
    ::new (static_cast<void *>(&workspace.effectConverter_))
        FxFastFileNativeDisk32Workspace();
    workspace.impactConverter_.~FxFastFileImpactNativeDisk32Workspace();
    ::new (static_cast<void *>(&workspace.impactConverter_))
        FxFastFileImpactNativeDisk32Workspace();
    ResetRecordingState(workspace);
}

FxFastFileZoneAdapterDisk32Status
FxFastFileZoneAdapterDisk32Workspace::FailTransaction(
    FxFastFileZoneAdapterDisk32Workspace &workspace,
    const FxFastFileZoneAdapterDisk32Status status) noexcept
{
    TeardownTransaction(workspace);
    return status;
}

FxFastFileZoneAdapterDisk32Status
FxFastFileZoneAdapterDisk32Workspace::AppendReference(
    FxFastFileZoneAdapterDisk32Workspace &workspace,
    const RecordedReference &reference) noexcept
{
    if (workspace.referenceCount_ >= kFxFastFileZoneAdapterMaxReferences)
        return Status::CapacityExceeded;
    workspace.references_[workspace.referenceCount_++] = reference;
    return Status::Success;
}

FxFastFileZoneAdapterDisk32Status
FxFastFileZoneAdapterDisk32Workspace::AppendSpan(
    FxFastFileZoneAdapterDisk32Workspace &workspace,
    const RecordedSpan &span) noexcept
{
    if (workspace.spanCount_ >= kFxFastFileZoneAdapterMaxSpans)
        return Status::CapacityExceeded;
    workspace.spans_[workspace.spanCount_++] = span;
    return Status::Success;
}

const disk32::PointerToken *
FxFastFileZoneAdapterDisk32Workspace::ImpactSlotField(
    const Frame &frame,
    const std::uint32_t slot) noexcept
{
    const std::uint32_t handlesPerEntry = static_cast<std::uint32_t>(
        kImpactNonFleshEffectCount + kImpactFleshEffectCount);
    const std::uint32_t entry = slot / handlesPerEntry;
    const std::uint32_t within = slot % handlesPerEntry;
    if (!frame.entries
        || entry >= static_cast<std::uint32_t>(kImpactSurfaceCount))
    {
        return nullptr;
    }
    if (within < kImpactNonFleshEffectCount)
        return &frame.entries[entry].nonflesh[within].token;
    return &frame.entries[entry]
                .flesh[within - kImpactNonFleshEffectCount].token;
}

void FxFastFileZoneAdapterDisk32Workspace::NormalizeImpactSlots(
    Frame &frame) noexcept
{
    const std::uint32_t slotCount =
        static_cast<std::uint32_t>(kFxFastFileImpactDisk32HandleCount);
    while (frame.impactSlot < slotCount)
    {
        const disk32::PointerToken *const field =
            ImpactSlotField(frame, frame.impactSlot);
        if (!field || !field->isNull())
            return;
        ++frame.impactSlot;
    }
}

// Moves the effect walk cursor to the next required report, performing the
// structural token/count prechecks that decide which reports exist.  The
// converter remains the semantic authority; these prechecks only reject wire
// records whose expectation cannot even be derived.
FxFastFileZoneAdapterDisk32Status
FxFastFileZoneAdapterDisk32Workspace::NormalizeEffectWalk(
    FxFastFileZoneAdapterDisk32Workspace &workspace,
    Frame &frame) noexcept
{
    while (frame.elementIndex < frame.elementCount)
    {
        const FxElemDefDisk32 &element = frame.elements[frame.elementIndex];
        switch (frame.stage)
        {
        case ElementStage::Velocity:
            if (element.elemType >= FxElemTypeDisk32::Count)
                return Status::InvalidToken;
            if (element.velSamples.token.isNull()
                || element.velIntervalCount == 0)
            {
                return Status::InvalidToken;
            }
            return Status::Success;

        case ElementStage::Visibility:
            if (element.visSamples.token.isNull())
            {
                if (element.visStateIntervalCount != 0)
                    return Status::InvalidToken;
                frame.stage = ElementStage::VisualsSpan;
                continue;
            }
            return Status::Success;

        case ElementStage::VisualsSpan:
            if (IsLightElement(element.elemType))
            {
                if (element.visualCount != 1
                    || !element.visuals.token.isNull())
                {
                    return Status::InvalidToken;
                }
                frame.stage = ElementStage::EffectReferences;
                frame.effectRefSlot = 0;
                continue;
            }
            if (element.elemType == FxElemTypeDisk32::Decal)
            {
                if (element.visualCount == 0
                    || element.visualCount > kFxFastFileDisk32MaxDecalVisuals)
                {
                    return Status::InvalidCount;
                }
                if (element.visuals.token.isNull())
                    return Status::InvalidToken;
                return Status::Success;
            }
            if (element.visualCount > kFxFastFileDisk32MaxVisuals)
                return Status::InvalidCount;
            if (element.visualCount == 0)
            {
                if (element.elemType == FxElemTypeDisk32::Runner)
                    return Status::InvalidCount;
                if (!element.visuals.token.isNull())
                    return Status::InvalidToken;
                frame.stage = ElementStage::EffectReferences;
                frame.effectRefSlot = 0;
                continue;
            }
            if (element.visualCount == 1)
            {
                if (element.visuals.token.isNull())
                    return Status::InvalidToken;
                frame.stage = ElementStage::VisualReferences;
                frame.visualSlot = 0;
                continue;
            }
            if (element.visuals.token.isNull())
                return Status::InvalidToken;
            return Status::Success;

        case ElementStage::VisualReferences:
        {
            const std::uint32_t total =
                element.elemType == FxElemTypeDisk32::Decal
                    ? 2u * element.visualCount
                    : element.visualCount;
            if (frame.visualSlot >= total)
            {
                frame.stage = ElementStage::EffectReferences;
                frame.effectRefSlot = 0;
                continue;
            }
            return Status::Success;
        }

        case ElementStage::EffectReferences:
            while (frame.effectRefSlot < 3
                   && ElementEffectReferenceField(
                          element, frame.effectRefSlot)->isNull())
            {
                ++frame.effectRefSlot;
            }
            if (frame.effectRefSlot >= 3)
            {
                frame.stage = ElementStage::TrailDefinition;
                continue;
            }
            return Status::Success;

        case ElementStage::TrailDefinition:
            if (element.elemType != FxElemTypeDisk32::Trail)
            {
                if (!element.trailDef.token.isNull())
                    return Status::InvalidToken;
                frame.stage = ElementStage::Complete;
                continue;
            }
            if (element.trailDef.token.isNull())
                return Status::InvalidToken;
            return Status::Success;

        case ElementStage::TrailVertices:
        {
            const FxTrailDefDisk32 *const trail =
                workspace.elementViews_[frame.elementIndex].trail;
            if (!trail)
                return Status::InvalidSequence;
            if (trail->vertCount <= 0 || trail->indCount <= 0)
                return Status::InvalidCount;
            if (trail->verts.token.isNull() || trail->inds.token.isNull())
                return Status::InvalidToken;
            return Status::Success;
        }

        case ElementStage::TrailIndices:
            return Status::Success;

        case ElementStage::Complete:
            ++frame.elementIndex;
            frame.stage = ElementStage::Velocity;
            frame.visualSlot = 0;
            frame.effectRefSlot = 0;
            continue;
        }
        return Status::InvalidSequence;
    }
    return Status::Success;
}

bool FxFastFileZoneAdapterDisk32Workspace::ResolveReferenceThunk(
    void *const context,
    const FxFastFileDisk32ReferenceKind kind,
    const disk32::PointerToken *const sourceField,
    const disk32::PointerToken token,
    FxFastFileDisk32ResolvedReference *const outReference) noexcept
{
    if (!context || !outReference)
        return false;
    FxFastFileZoneAdapterDisk32Workspace &workspace =
        *static_cast<FxFastFileZoneAdapterDisk32Workspace *>(context);
    if (workspace.frameDepth_ == 0)
        return false;
    const Frame &frame = workspace.frames_[workspace.frameDepth_ - 1];
    const std::uint32_t begin = frame.referenceBase;
    const std::uint32_t end = workspace.referenceCount_;
    if (begin >= end)
        return false;

    std::uint32_t probe = workspace.resolveHint_;
    if (probe < begin || probe >= end)
        probe = begin;
    for (std::uint32_t step = 0; step < end - begin; ++step)
    {
        RecordedReference &candidate = workspace.references_[probe];
        if (candidate.sourceField == sourceField
            && candidate.kind == kind
            && candidate.token.value == token.value
            && !candidate.consumed)
        {
            candidate.consumed = true;
            *outReference = candidate.resolution;
            workspace.resolveHint_ = probe + 1;
            return true;
        }
        ++probe;
        if (probe >= end)
            probe = begin;
    }
    return false;
}

bool FxFastFileZoneAdapterDisk32Workspace::ValidateSpanThunk(
    void *const context,
    const FxFastFileDisk32SourceSpanKind kind,
    const disk32::PointerToken *const sourceField,
    const disk32::PointerToken token,
    const void *const address,
    const std::uint64_t byteCount,
    const std::size_t alignment) noexcept
{
    if (!context)
        return false;
    FxFastFileZoneAdapterDisk32Workspace &workspace =
        *static_cast<FxFastFileZoneAdapterDisk32Workspace *>(context);
    if (workspace.frameDepth_ == 0)
        return false;
    const Frame &frame = workspace.frames_[workspace.frameDepth_ - 1];

    if (kind == SpanKind::String)
    {
        // Resolver-returned string attestation: the extent must be exactly a
        // string this transaction recorded (wire name or arena-owned copy).
        for (std::uint32_t index = frame.referenceBase;
             index < workspace.referenceCount_;
             ++index)
        {
            const RecordedReference &candidate = workspace.references_[index];
            if (candidate.isString
                && candidate.sourceField == sourceField
                && candidate.token.value == token.value
                && candidate.resolution.pointer == address
                && candidate.resolution.retainedByteCount == byteCount
                && alignment == 1)
            {
                return true;
            }
        }
        return false;
    }

    const std::uint32_t begin = frame.spanBase;
    const std::uint32_t end = workspace.spanCount_;
    if (begin >= end)
        return false;
    std::uint32_t probe = workspace.spanHint_;
    if (probe < begin || probe >= end)
        probe = begin;
    for (std::uint32_t step = 0; step < end - begin; ++step)
    {
        const RecordedSpan &candidate = workspace.spans_[probe];
        if (candidate.kind == kind
            && candidate.sourceField == sourceField
            && candidate.token.value == token.value
            && candidate.address == address
            && candidate.byteCount == byteCount
            && IsAlignedAddress(address, alignment))
        {
            workspace.spanHint_ = probe + 1;
            return true;
        }
        ++probe;
        if (probe >= end)
            probe = begin;
    }
    return false;
}

FxFastFileZoneAdapterDisk32Status TryBeginFxEffectDefZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *const workspace,
    FxFastFileNativeArena *const arena,
    const FxFastFileZoneAdapterCursor &cursor,
    const FxEffectDefDisk32 *const header) noexcept
{
    using Workspace = FxFastFileZoneAdapterDisk32Workspace;
    if (!workspace)
        return Status::InvalidArgument;
    OperationGate gate(&workspace->operating_);
    if (!gate.acquired())
        return Status::Busy;
    if (!arena || !cursor.validateWireSpan || !header)
        return Status::InvalidArgument;

    const bool nested = workspace->frameDepth_ != 0;
    if (nested)
    {
        if (workspace->frameDepth_ != 1)
            return Workspace::FailTransaction(
                *workspace, Status::InvalidSequence);
        Workspace::Frame &parent = workspace->frames_[0];
        if (parent.kind != Workspace::FrameKind::Impact
            || parent.state != Phase::ImpactEntries)
        {
            return Workspace::FailTransaction(
                *workspace, Status::InvalidSequence);
        }
        if (arena != workspace->arena_
            || cursor.context != workspace->cursor_.context
            || cursor.validateWireSpan != workspace->cursor_.validateWireSpan)
        {
            return Status::InvalidArgument;
        }
        const disk32::PointerToken *const pendingField =
            Workspace::ImpactSlotField(parent, parent.impactSlot);
        if (parent.impactSlot
                >= static_cast<std::uint32_t>(
                    kFxFastFileImpactDisk32HandleCount)
            || !pendingField
            || !(pendingField->isInline() || pendingField->isSharedInline()))
        {
            return Workspace::FailTransaction(
                *workspace, Status::InvalidSequence);
        }
    }
    else if (!arena->bound())
    {
        return Status::InvalidArgument;
    }

    if (!IsAlignedAddress(header, alignof(FxEffectDefDisk32))
        || !OracleValidates(cursor, header, sizeof(*header)))
    {
        return nested
            ? Workspace::FailTransaction(*workspace, Status::InvalidSpan)
            : Status::InvalidSpan;
    }
    if (header->elemDefCountLooping < 0 || header->elemDefCountOneShot < 0
        || header->elemDefCountEmission < 0)
    {
        return nested
            ? Workspace::FailTransaction(*workspace, Status::InvalidCount)
            : Status::InvalidCount;
    }
    const std::uint64_t elementCount =
        static_cast<std::uint64_t>(header->elemDefCountLooping)
        + static_cast<std::uint64_t>(header->elemDefCountOneShot)
        + static_cast<std::uint64_t>(header->elemDefCountEmission);
    if (elementCount > kFxFastFileDisk32MaxEffectElements)
    {
        return nested
            ? Workspace::FailTransaction(*workspace, Status::InvalidCount)
            : Status::InvalidCount;
    }
    if ((elementCount == 0) != header->elemDefs.token.isNull()
        || header->name.token.isNull())
    {
        return nested
            ? Workspace::FailTransaction(*workspace, Status::InvalidToken)
            : Status::InvalidToken;
    }

    FxFastFileNativeArenaTransaction transaction;
    const FxFastFileNativeArenaStatus arenaStatus =
        arena->TryBeginTransaction(&transaction);
    if (arenaStatus != FxFastFileNativeArenaStatus::Success)
    {
        workspace->lastArenaStatus_ = arenaStatus;
        return nested
            ? Workspace::FailTransaction(*workspace, Status::ArenaFailed)
            : Status::ArenaFailed;
    }

    if (!nested)
    {
        workspace->arena_ = arena;
        workspace->cursor_ = cursor;
    }

    Workspace::Frame &frame = workspace->frames_[workspace->frameDepth_];
    frame = Workspace::Frame{};
    frame.kind = Workspace::FrameKind::Effect;
    frame.state = Phase::EffectHeader;
    frame.effectHeader = header;
    frame.referenceBase = workspace->referenceCount_;
    frame.spanBase = workspace->spanCount_;
    frame.arenaTransaction = transaction;
    ++workspace->frameDepth_;

    for (std::uint32_t index = 0;
         index < kFxFastFileDisk32MaxEffectElements;
         ++index)
    {
        workspace->elementViews_[index] = FxFastFileElemDefDisk32View{};
    }

    Workspace::RecordedSpan span;
    span.address = header;
    span.sourceField = nullptr;
    span.byteCount = sizeof(*header);
    span.token = disk32::PointerToken{};
    span.count = 1;
    span.kind = SpanKind::EffectHeader;
    const Status appendStatus = Workspace::AppendSpan(*workspace, span);
    if (appendStatus != Status::Success)
        return Workspace::FailTransaction(*workspace, appendStatus);
    return Status::Success;
}

FxFastFileZoneAdapterDisk32Status TryRecordFxEffectDefNameZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *const workspace,
    const char *const name,
    const std::uint64_t nameBytes) noexcept
{
    using Workspace = FxFastFileZoneAdapterDisk32Workspace;
    if (!workspace)
        return Status::InvalidArgument;
    OperationGate gate(&workspace->operating_);
    if (!gate.acquired())
        return Status::Busy;
    if (!name)
        return Status::InvalidArgument;
    if (workspace->frameDepth_ == 0)
        return Status::InvalidPhase;
    Workspace::Frame &frame = workspace->frames_[workspace->frameDepth_ - 1];
    if (frame.kind != Workspace::FrameKind::Effect
        || frame.state != Phase::EffectHeader)
    {
        return Workspace::FailTransaction(*workspace, Status::InvalidSequence);
    }
    // The materializer copies the effect's own name into its output and
    // requires at least a one-character name.  Bound the reported extent and
    // validate it against the cursor oracle before inspecting any byte.
    if (nameBytes < 2 || nameBytes > kFxFastFileZoneAdapterMaxNameBytes)
        return Workspace::FailTransaction(*workspace, Status::InvalidString);
    if (!OracleValidates(workspace->cursor_, name, nameBytes))
        return Workspace::FailTransaction(*workspace, Status::InvalidSpan);
    if (!IsExactWireCString(name, nameBytes))
        return Workspace::FailTransaction(*workspace, Status::InvalidString);

    Workspace::RecordedReference reference;
    reference.sourceField = &frame.effectHeader->name.token;
    reference.resolution.pointer = name;
    reference.resolution.retainedByteCount = nameBytes;
    reference.resolution.retainedAlignment = 1;
    reference.token = frame.effectHeader->name.token;
    reference.kind = ReferenceKind::EffectName;
    reference.isString = true;
    const Status appendStatus =
        Workspace::AppendReference(*workspace, reference);
    if (appendStatus != Status::Success)
        return Workspace::FailTransaction(*workspace, appendStatus);
    frame.state = Phase::EffectName;
    return Status::Success;
}

FxFastFileZoneAdapterDisk32Status TryRecordFxElemDefArrayZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *const workspace,
    const FxElemDefDisk32 *const elements,
    const std::uint32_t count) noexcept
{
    using Workspace = FxFastFileZoneAdapterDisk32Workspace;
    if (!workspace)
        return Status::InvalidArgument;
    OperationGate gate(&workspace->operating_);
    if (!gate.acquired())
        return Status::Busy;
    if (!elements || count == 0)
        return Status::InvalidArgument;
    if (workspace->frameDepth_ == 0)
        return Status::InvalidPhase;
    Workspace::Frame &frame = workspace->frames_[workspace->frameDepth_ - 1];
    if (frame.kind != Workspace::FrameKind::Effect
        || frame.state != Phase::EffectName)
    {
        return Workspace::FailTransaction(*workspace, Status::InvalidSequence);
    }
    const FxEffectDefDisk32 &header = *frame.effectHeader;
    if (header.elemDefs.token.isNull())
        return Workspace::FailTransaction(*workspace, Status::InvalidSequence);
    const std::uint64_t expectedCount =
        static_cast<std::uint64_t>(header.elemDefCountLooping)
        + static_cast<std::uint64_t>(header.elemDefCountOneShot)
        + static_cast<std::uint64_t>(header.elemDefCountEmission);
    if (count != expectedCount)
        return Workspace::FailTransaction(*workspace, Status::InvalidCount);
    if (!IsAlignedAddress(elements, alignof(FxElemDefDisk32)))
        return Workspace::FailTransaction(*workspace, Status::InvalidSpan);
    const std::uint64_t byteCount =
        static_cast<std::uint64_t>(count) * sizeof(FxElemDefDisk32);
    if (!OracleValidates(workspace->cursor_, elements, byteCount))
        return Workspace::FailTransaction(*workspace, Status::InvalidSpan);

    Workspace::RecordedSpan span;
    span.address = elements;
    span.sourceField = &header.elemDefs.token;
    span.byteCount = byteCount;
    span.token = header.elemDefs.token;
    span.count = count;
    span.kind = SpanKind::ElementDefinitions;
    const Status appendStatus = Workspace::AppendSpan(*workspace, span);
    if (appendStatus != Status::Success)
        return Workspace::FailTransaction(*workspace, appendStatus);

    frame.elements = elements;
    frame.elementCount = count;
    frame.elementIndex = 0;
    frame.stage = Workspace::ElementStage::Velocity;
    frame.visualSlot = 0;
    frame.effectRefSlot = 0;
    frame.state = Phase::EffectElements;
    const Status walkStatus =
        Workspace::NormalizeEffectWalk(*workspace, frame);
    if (walkStatus != Status::Success)
        return Workspace::FailTransaction(*workspace, walkStatus);
    return Status::Success;
}

FxFastFileZoneAdapterDisk32Status TryRecordFxElemSpanZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *const workspace,
    const FxFastFileDisk32SourceSpanKind kind,
    const void *const address,
    const std::uint32_t count) noexcept
{
    using Workspace = FxFastFileZoneAdapterDisk32Workspace;
    if (!workspace)
        return Status::InvalidArgument;
    OperationGate gate(&workspace->operating_);
    if (!gate.acquired())
        return Status::Busy;
    if (!address || count == 0)
        return Status::InvalidArgument;
    if (workspace->frameDepth_ == 0)
        return Status::InvalidPhase;
    Workspace::Frame &frame = workspace->frames_[workspace->frameDepth_ - 1];
    if (frame.kind != Workspace::FrameKind::Effect
        || frame.state != Phase::EffectElements
        || frame.elementIndex >= frame.elementCount)
    {
        return Workspace::FailTransaction(*workspace, Status::InvalidSequence);
    }

    const FxElemDefDisk32 &element = frame.elements[frame.elementIndex];
    FxFastFileElemDefDisk32View &view =
        workspace->elementViews_[frame.elementIndex];

    SpanKind expectedKind{};
    const disk32::PointerToken *sourceField = nullptr;
    std::uint32_t expectedCount = 0;
    std::uint64_t recordBytes = 0;
    std::size_t requiredAlignment = 0;
    Workspace::ElementStage nextStage{};

    switch (frame.stage)
    {
    case Workspace::ElementStage::Velocity:
        expectedKind = SpanKind::VelocitySamples;
        sourceField = &element.velSamples.token;
        expectedCount =
            static_cast<std::uint32_t>(element.velIntervalCount) + 1u;
        recordBytes = sizeof(FxElemVelStateSampleDisk32);
        requiredAlignment = alignof(FxElemVelStateSampleDisk32);
        nextStage = Workspace::ElementStage::Visibility;
        break;
    case Workspace::ElementStage::Visibility:
        expectedKind = SpanKind::VisibilitySamples;
        sourceField = &element.visSamples.token;
        expectedCount =
            static_cast<std::uint32_t>(element.visStateIntervalCount) + 1u;
        recordBytes = sizeof(FxElemVisStateSampleDisk32);
        requiredAlignment = alignof(FxElemVisStateSampleDisk32);
        nextStage = Workspace::ElementStage::VisualsSpan;
        break;
    case Workspace::ElementStage::VisualsSpan:
        if (element.elemType == FxElemTypeDisk32::Decal)
        {
            expectedKind = SpanKind::MarkVisuals;
            recordBytes = sizeof(FxElemMarkVisualsDisk32);
            requiredAlignment = alignof(FxElemMarkVisualsDisk32);
        }
        else
        {
            expectedKind = SpanKind::Visuals;
            recordBytes = sizeof(FxElemVisualsDisk32);
            requiredAlignment = alignof(FxElemVisualsDisk32);
        }
        sourceField = &element.visuals.token;
        expectedCount = element.visualCount;
        nextStage = Workspace::ElementStage::VisualReferences;
        break;
    case Workspace::ElementStage::TrailDefinition:
        expectedKind = SpanKind::TrailDefinition;
        sourceField = &element.trailDef.token;
        expectedCount = 1;
        recordBytes = sizeof(FxTrailDefDisk32);
        requiredAlignment = alignof(FxTrailDefDisk32);
        nextStage = Workspace::ElementStage::TrailVertices;
        break;
    case Workspace::ElementStage::TrailVertices:
        expectedKind = SpanKind::TrailVertices;
        sourceField = &view.trail->verts.token;
        expectedCount = static_cast<std::uint32_t>(view.trail->vertCount);
        recordBytes = sizeof(FxTrailVertexDisk32);
        requiredAlignment = alignof(FxTrailVertexDisk32);
        nextStage = Workspace::ElementStage::TrailIndices;
        break;
    case Workspace::ElementStage::TrailIndices:
        expectedKind = SpanKind::TrailIndices;
        sourceField = &view.trail->inds.token;
        expectedCount = static_cast<std::uint32_t>(view.trail->indCount);
        recordBytes = sizeof(std::uint16_t);
        requiredAlignment = alignof(std::uint16_t);
        nextStage = Workspace::ElementStage::Complete;
        break;
    default:
        return Workspace::FailTransaction(*workspace, Status::InvalidSequence);
    }

    if (kind != expectedKind)
        return Workspace::FailTransaction(*workspace, Status::InvalidSequence);
    if (count != expectedCount)
        return Workspace::FailTransaction(*workspace, Status::InvalidCount);
    if (!IsAlignedAddress(address, requiredAlignment))
        return Workspace::FailTransaction(*workspace, Status::InvalidSpan);
    const std::uint64_t byteCount =
        static_cast<std::uint64_t>(count) * recordBytes;
    if (!OracleValidates(workspace->cursor_, address, byteCount))
        return Workspace::FailTransaction(*workspace, Status::InvalidSpan);

    Workspace::RecordedSpan span;
    span.address = address;
    span.sourceField = sourceField;
    span.byteCount = byteCount;
    span.token = *sourceField;
    span.count = count;
    span.kind = expectedKind;
    const Status appendStatus = Workspace::AppendSpan(*workspace, span);
    if (appendStatus != Status::Success)
        return Workspace::FailTransaction(*workspace, appendStatus);

    switch (expectedKind)
    {
    case SpanKind::VelocitySamples:
        view.velocitySamples = {
            static_cast<const FxElemVelStateSampleDisk32 *>(address), count};
        break;
    case SpanKind::VisibilitySamples:
        view.visibilitySamples = {
            static_cast<const FxElemVisStateSampleDisk32 *>(address), count};
        break;
    case SpanKind::Visuals:
        view.visuals = {
            static_cast<const FxElemVisualsDisk32 *>(address), count};
        break;
    case SpanKind::MarkVisuals:
        view.markVisuals = {
            static_cast<const FxElemMarkVisualsDisk32 *>(address), count};
        break;
    case SpanKind::TrailDefinition:
        view.trail = static_cast<const FxTrailDefDisk32 *>(address);
        break;
    case SpanKind::TrailVertices:
        view.trailVertices = {
            static_cast<const FxTrailVertexDisk32 *>(address), count};
        break;
    case SpanKind::TrailIndices:
        view.trailIndices = {
            static_cast<const std::uint16_t *>(address), count};
        break;
    default:
        return Workspace::FailTransaction(*workspace, Status::InvalidSequence);
    }

    frame.stage = nextStage;
    if (nextStage == Workspace::ElementStage::VisualReferences)
        frame.visualSlot = 0;
    const Status walkStatus =
        Workspace::NormalizeEffectWalk(*workspace, frame);
    if (walkStatus != Status::Success)
        return Workspace::FailTransaction(*workspace, walkStatus);
    return Status::Success;
}

namespace
{
// Computes the wire token field for the visual-reference slot the effect walk
// currently expects: decal mark materials, multi-visual array entries, or the
// single in-record visual union.
[[nodiscard]] const disk32::PointerToken *ExpectedVisualField(
    const FxElemDefDisk32 &element,
    const FxFastFileElemDefDisk32View &view,
    const std::uint32_t slot) noexcept
{
    if (element.elemType == FxElemTypeDisk32::Decal)
    {
        if (!view.markVisuals.data || slot >= 2u * element.visualCount)
            return nullptr;
        return &view.markVisuals.data[slot / 2u].materials[slot % 2u].token;
    }
    if (element.visualCount == 1)
        return slot == 0 ? &element.visuals.token : nullptr;
    if (!view.visuals.data || slot >= element.visualCount)
        return nullptr;
    return &view.visuals.data[slot].token;
}

[[nodiscard]] FxFastFileDisk32ReferenceKind ExpectedVisualKind(
    const FxElemDefDisk32 &element) noexcept
{
    if (element.elemType == FxElemTypeDisk32::Decal)
        return FxFastFileDisk32ReferenceKind::Material;
    return VisualReferenceKind(element.elemType);
}
} // namespace

FxFastFileZoneAdapterDisk32Status TryRecordFxReferenceZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *const workspace,
    const disk32::PointerToken *const sourceField,
    const FxFastFileDisk32ResolvedReference &resolution) noexcept
{
    using Workspace = FxFastFileZoneAdapterDisk32Workspace;
    if (!workspace)
        return Status::InvalidArgument;
    OperationGate gate(&workspace->operating_);
    if (!gate.acquired())
        return Status::Busy;
    if (!sourceField)
        return Status::InvalidArgument;
    if (workspace->frameDepth_ == 0)
        return Status::InvalidPhase;
    Workspace::Frame &frame = workspace->frames_[workspace->frameDepth_ - 1];
    if (frame.kind != Workspace::FrameKind::Effect
        || frame.state != Phase::EffectElements
        || frame.elementIndex >= frame.elementCount)
    {
        return Workspace::FailTransaction(*workspace, Status::InvalidSequence);
    }

    const FxElemDefDisk32 &element = frame.elements[frame.elementIndex];
    const FxFastFileElemDefDisk32View &view =
        workspace->elementViews_[frame.elementIndex];

    const disk32::PointerToken *expectedField = nullptr;
    ReferenceKind expectedKind{};
    if (frame.stage == Workspace::ElementStage::VisualReferences)
    {
        expectedField = ExpectedVisualField(element, view, frame.visualSlot);
        expectedKind = ExpectedVisualKind(element);
        if (expectedKind == ReferenceKind::SoundName)
        {
            return Workspace::FailTransaction(
                *workspace, Status::InvalidSequence);
        }
    }
    else if (frame.stage == Workspace::ElementStage::EffectReferences)
    {
        expectedField =
            ElementEffectReferenceField(element, frame.effectRefSlot);
        expectedKind = ReferenceKind::EffectNameReference;
    }
    else
    {
        return Workspace::FailTransaction(*workspace, Status::InvalidSequence);
    }

    if (sourceField != expectedField)
        return Workspace::FailTransaction(*workspace, Status::InvalidSequence);
    if (!resolution.pointer || resolution.retainedByteCount == 0)
        return Workspace::FailTransaction(*workspace, Status::InvalidReference);

    Workspace::RecordedReference reference;
    reference.sourceField = sourceField;
    reference.resolution = resolution;
    reference.token = *sourceField;
    reference.kind = expectedKind;
    reference.isString = false;
    const Status appendStatus =
        Workspace::AppendReference(*workspace, reference);
    if (appendStatus != Status::Success)
        return Workspace::FailTransaction(*workspace, appendStatus);

    if (frame.stage == Workspace::ElementStage::VisualReferences)
        ++frame.visualSlot;
    else
        ++frame.effectRefSlot;
    const Status walkStatus =
        Workspace::NormalizeEffectWalk(*workspace, frame);
    if (walkStatus != Status::Success)
        return Workspace::FailTransaction(*workspace, walkStatus);
    return Status::Success;
}

FxFastFileZoneAdapterDisk32Status TryRecordFxSoundNameZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *const workspace,
    const disk32::PointerToken *const sourceField,
    const char *const name,
    const std::uint64_t nameBytes) noexcept
{
    using Workspace = FxFastFileZoneAdapterDisk32Workspace;
    if (!workspace)
        return Status::InvalidArgument;
    OperationGate gate(&workspace->operating_);
    if (!gate.acquired())
        return Status::Busy;
    if (!sourceField || !name)
        return Status::InvalidArgument;
    if (workspace->frameDepth_ == 0)
        return Status::InvalidPhase;
    Workspace::Frame &frame = workspace->frames_[workspace->frameDepth_ - 1];
    if (frame.kind != Workspace::FrameKind::Effect
        || frame.state != Phase::EffectElements
        || frame.elementIndex >= frame.elementCount
        || frame.stage != Workspace::ElementStage::VisualReferences)
    {
        return Workspace::FailTransaction(*workspace, Status::InvalidSequence);
    }

    const FxElemDefDisk32 &element = frame.elements[frame.elementIndex];
    const FxFastFileElemDefDisk32View &view =
        workspace->elementViews_[frame.elementIndex];
    if (ExpectedVisualKind(element) != ReferenceKind::SoundName
        || sourceField != ExpectedVisualField(element, view, frame.visualSlot))
    {
        return Workspace::FailTransaction(*workspace, Status::InvalidSequence);
    }
    // Bound the reported extent and validate it against the cursor oracle
    // before inspecting any byte.
    if (nameBytes == 0 || nameBytes > kFxFastFileZoneAdapterMaxNameBytes)
        return Workspace::FailTransaction(*workspace, Status::InvalidString);
    if (!OracleValidates(workspace->cursor_, name, nameBytes))
        return Workspace::FailTransaction(*workspace, Status::InvalidSpan);
    if (!IsExactWireCString(name, nameBytes))
        return Workspace::FailTransaction(*workspace, Status::InvalidString);

    // The converter retains sound names instead of copying them, so the name
    // must live as long as the zone: copy it into the arena inside this
    // transaction.
    void *copy = nullptr;
    const FxFastFileNativeArenaStatus arenaStatus =
        workspace->arena_->TryReserve(
            frame.arenaTransaction,
            static_cast<std::size_t>(nameBytes),
            1,
            &copy);
    if (arenaStatus != FxFastFileNativeArenaStatus::Success)
    {
        workspace->lastArenaStatus_ = arenaStatus;
        return Workspace::FailTransaction(*workspace, Status::ArenaFailed);
    }
    std::memcpy(copy, name, static_cast<std::size_t>(nameBytes));

    Workspace::RecordedReference reference;
    reference.sourceField = sourceField;
    reference.resolution.pointer = copy;
    reference.resolution.retainedByteCount = nameBytes;
    reference.resolution.retainedAlignment = 1;
    reference.token = *sourceField;
    reference.kind = ReferenceKind::SoundName;
    reference.isString = true;
    const Status appendStatus =
        Workspace::AppendReference(*workspace, reference);
    if (appendStatus != Status::Success)
        return Workspace::FailTransaction(*workspace, appendStatus);

    ++frame.visualSlot;
    const Status walkStatus =
        Workspace::NormalizeEffectWalk(*workspace, frame);
    if (walkStatus != Status::Success)
        return Workspace::FailTransaction(*workspace, walkStatus);
    return Status::Success;
}

FxFastFileZoneAdapterDisk32Status TrySealFxEffectDefZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *const workspace) noexcept
{
    using Workspace = FxFastFileZoneAdapterDisk32Workspace;
    if (!workspace)
        return Status::InvalidArgument;
    OperationGate gate(&workspace->operating_);
    if (!gate.acquired())
        return Status::Busy;
    if (workspace->frameDepth_ == 0)
        return Status::InvalidPhase;
    Workspace::Frame &frame = workspace->frames_[workspace->frameDepth_ - 1];
    if (frame.kind != Workspace::FrameKind::Effect)
        return Workspace::FailTransaction(*workspace, Status::InvalidSequence);

    const bool zeroElements = frame.effectHeader->elemDefs.token.isNull();
    const bool sealable =
        (frame.state == Phase::EffectName && zeroElements)
        || (frame.state == Phase::EffectElements
            && frame.elementIndex == frame.elementCount);
    if (!sealable)
        return Workspace::FailTransaction(*workspace, Status::InvalidSequence);
    frame.state = Phase::EffectSealed;
    return Status::Success;
}

FxFastFileZoneAdapterDisk32Status TryPublishFxEffectDefZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *const workspace,
    const FxFastFileZoneAdapterPublication &publication,
    FxEffectDef **const outEffect) noexcept
{
    using Workspace = FxFastFileZoneAdapterDisk32Workspace;
    if (!workspace)
        return Status::InvalidArgument;
    OperationGate gate(&workspace->operating_);
    if (!gate.acquired())
        return Status::Busy;
    if (!publication.publishEffect || !outEffect)
        return Status::InvalidArgument;
    if (workspace->frameDepth_ == 0)
        return Status::InvalidPhase;
    Workspace::Frame &frame = workspace->frames_[workspace->frameDepth_ - 1];
    if (frame.kind != Workspace::FrameKind::Effect
        || frame.state != Phase::EffectSealed)
    {
        return Workspace::FailTransaction(*workspace, Status::InvalidSequence);
    }

    FxFastFileEffectDefDisk32View source;
    source.effect = frame.effectHeader;
    source.elements = {frame.elements, frame.elementCount};
    source.elementViews = {
        frame.elementCount != 0 ? workspace->elementViews_ : nullptr,
        frame.elementCount};
    source.provenance = {workspace, &Workspace::ValidateSpanThunk};
    const FxFastFileDisk32Resolvers resolvers{
        workspace, &Workspace::ResolveReferenceThunk};

    workspace->resolveHint_ = frame.referenceBase;
    workspace->spanHint_ = frame.spanBase;

    FxFastFileNativeDisk32Plan plan;
    FxFastFileNativeDisk32Status converterStatus = TryPlanFxEffectDefDisk32(
        &workspace->effectConverter_, source, resolvers, &plan);
    workspace->lastConverterStatus_ = converterStatus;
    if (converterStatus != FxFastFileNativeDisk32Status::Success)
        return Workspace::FailTransaction(*workspace, Status::ConversionFailed);

    // Every recorded resolution must have been consumed exactly once; a
    // leftover means the derived expectation walk and the converter's frozen
    // schedule disagree, which must fail closed.
    for (std::uint32_t index = frame.referenceBase;
         index < workspace->referenceCount_;
         ++index)
    {
        if (!workspace->references_[index].consumed)
        {
            return Workspace::FailTransaction(
                *workspace, Status::InvalidReference);
        }
    }

    void *storage = nullptr;
    FxFastFileNativeArenaStatus arenaStatus = workspace->arena_->TryReserve(
        frame.arenaTransaction,
        plan.outputBytes(),
        plan.outputAlignment(),
        &storage);
    if (arenaStatus != FxFastFileNativeArenaStatus::Success)
    {
        workspace->lastArenaStatus_ = arenaStatus;
        return Workspace::FailTransaction(*workspace, Status::ArenaFailed);
    }

    FxEffectDef *effect = nullptr;
    converterStatus = TryMaterializeFxEffectDefDisk32(
        &workspace->effectConverter_,
        plan,
        storage,
        plan.outputBytes(),
        &effect);
    workspace->lastConverterStatus_ = converterStatus;
    if (converterStatus != FxFastFileNativeDisk32Status::Success)
        return Workspace::FailTransaction(*workspace, Status::ConversionFailed);

    // Commit before publication: a rejected publication must strand only
    // unreferenced retired storage, never reclaim bytes an accepted
    // publication could already reference.
    arenaStatus = workspace->arena_->TryCommit(frame.arenaTransaction);
    if (arenaStatus != FxFastFileNativeArenaStatus::Success)
    {
        workspace->lastArenaStatus_ = arenaStatus;
        return Workspace::FailTransaction(*workspace, Status::ArenaFailed);
    }
    frame.arenaTransaction = FxFastFileNativeArenaTransaction{};

    if (!publication.publishEffect(publication.context, effect))
        return Workspace::FailTransaction(*workspace, Status::PublicationFailed);

    // Pop the effect frame and roll its recording slices back.
    workspace->referenceCount_ = frame.referenceBase;
    workspace->spanCount_ = frame.spanBase;
    frame = Workspace::Frame{};
    --workspace->frameDepth_;

    if (workspace->frameDepth_ != 0)
    {
        Workspace::Frame &parent = workspace->frames_[0];
        const disk32::PointerToken *const pendingField =
            Workspace::ImpactSlotField(parent, parent.impactSlot);
        if (parent.kind != Workspace::FrameKind::Impact
            || parent.state != Phase::ImpactEntries
            || !pendingField)
        {
            return Workspace::FailTransaction(
                *workspace, Status::InvalidSequence);
        }
        Workspace::RecordedReference handle;
        handle.sourceField = pendingField;
        handle.resolution.pointer = effect;
        handle.resolution.retainedByteCount = sizeof(FxEffectDef);
        handle.resolution.retainedAlignment = alignof(FxEffectDef);
        handle.token = *pendingField;
        handle.kind = ReferenceKind::EffectAssetHandle;
        handle.isString = false;
        const Status appendStatus =
            Workspace::AppendReference(*workspace, handle);
        if (appendStatus != Status::Success)
            return Workspace::FailTransaction(*workspace, appendStatus);
        ++parent.impactSlot;
        Workspace::NormalizeImpactSlots(parent);
    }
    else
    {
        Workspace::ResetRecordingState(*workspace);
    }

    *outEffect = effect;
    return Status::Success;
}

FxFastFileZoneAdapterDisk32Status TryBeginFxImpactTableZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *const workspace,
    FxFastFileNativeArena *const arena,
    const FxFastFileZoneAdapterCursor &cursor,
    const FxImpactTableDisk32 *const header) noexcept
{
    using Workspace = FxFastFileZoneAdapterDisk32Workspace;
    if (!workspace)
        return Status::InvalidArgument;
    OperationGate gate(&workspace->operating_);
    if (!gate.acquired())
        return Status::Busy;
    if (!arena || !cursor.validateWireSpan || !header)
        return Status::InvalidArgument;
    if (workspace->frameDepth_ != 0)
        return Workspace::FailTransaction(*workspace, Status::InvalidSequence);
    if (!arena->bound())
        return Status::InvalidArgument;
    if (!IsAlignedAddress(header, alignof(FxImpactTableDisk32))
        || !OracleValidates(cursor, header, sizeof(*header)))
    {
        return Status::InvalidSpan;
    }
    if (header->name.token.isNull() || header->table.token.isNull())
        return Status::InvalidToken;

    FxFastFileNativeArenaTransaction transaction;
    const FxFastFileNativeArenaStatus arenaStatus =
        arena->TryBeginTransaction(&transaction);
    if (arenaStatus != FxFastFileNativeArenaStatus::Success)
    {
        workspace->lastArenaStatus_ = arenaStatus;
        return Status::ArenaFailed;
    }

    workspace->arena_ = arena;
    workspace->cursor_ = cursor;

    Workspace::Frame &frame = workspace->frames_[0];
    frame = Workspace::Frame{};
    frame.kind = Workspace::FrameKind::Impact;
    frame.state = Phase::ImpactHeader;
    frame.impactHeader = header;
    frame.referenceBase = workspace->referenceCount_;
    frame.spanBase = workspace->spanCount_;
    frame.arenaTransaction = transaction;
    workspace->frameDepth_ = 1;

    Workspace::RecordedSpan span;
    span.address = header;
    span.sourceField = nullptr;
    span.byteCount = sizeof(*header);
    span.token = disk32::PointerToken{};
    span.count = 1;
    span.kind = SpanKind::ImpactTableHeader;
    const Status appendStatus = Workspace::AppendSpan(*workspace, span);
    if (appendStatus != Status::Success)
        return Workspace::FailTransaction(*workspace, appendStatus);
    return Status::Success;
}

FxFastFileZoneAdapterDisk32Status TryRecordFxImpactTableNameZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *const workspace,
    const char *const name,
    const std::uint64_t nameBytes) noexcept
{
    using Workspace = FxFastFileZoneAdapterDisk32Workspace;
    if (!workspace)
        return Status::InvalidArgument;
    OperationGate gate(&workspace->operating_);
    if (!gate.acquired())
        return Status::Busy;
    if (!name)
        return Status::InvalidArgument;
    if (workspace->frameDepth_ == 0)
        return Status::InvalidPhase;
    Workspace::Frame &frame = workspace->frames_[workspace->frameDepth_ - 1];
    if (frame.kind != Workspace::FrameKind::Impact
        || frame.state != Phase::ImpactHeader)
    {
        return Workspace::FailTransaction(*workspace, Status::InvalidSequence);
    }
    // Bound the reported extent and validate it against the cursor oracle
    // before inspecting any byte.
    if (nameBytes < 2 || nameBytes > kFxFastFileZoneAdapterMaxNameBytes)
        return Workspace::FailTransaction(*workspace, Status::InvalidString);
    if (!OracleValidates(workspace->cursor_, name, nameBytes))
        return Workspace::FailTransaction(*workspace, Status::InvalidSpan);
    if (!IsExactWireCString(name, nameBytes))
        return Workspace::FailTransaction(*workspace, Status::InvalidString);

    Workspace::RecordedReference reference;
    reference.sourceField = &frame.impactHeader->name.token;
    reference.resolution.pointer = name;
    reference.resolution.retainedByteCount = nameBytes;
    reference.resolution.retainedAlignment = 1;
    reference.token = frame.impactHeader->name.token;
    reference.kind = ReferenceKind::EffectName;
    reference.isString = true;
    const Status appendStatus =
        Workspace::AppendReference(*workspace, reference);
    if (appendStatus != Status::Success)
        return Workspace::FailTransaction(*workspace, appendStatus);
    frame.state = Phase::ImpactName;
    return Status::Success;
}

FxFastFileZoneAdapterDisk32Status TryRecordFxImpactEntryArrayZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *const workspace,
    const FxImpactEntryDisk32 *const entries,
    const std::uint32_t count) noexcept
{
    using Workspace = FxFastFileZoneAdapterDisk32Workspace;
    if (!workspace)
        return Status::InvalidArgument;
    OperationGate gate(&workspace->operating_);
    if (!gate.acquired())
        return Status::Busy;
    if (!entries || count == 0)
        return Status::InvalidArgument;
    if (workspace->frameDepth_ == 0)
        return Status::InvalidPhase;
    Workspace::Frame &frame = workspace->frames_[workspace->frameDepth_ - 1];
    if (frame.kind != Workspace::FrameKind::Impact
        || frame.state != Phase::ImpactName)
    {
        return Workspace::FailTransaction(*workspace, Status::InvalidSequence);
    }
    if (count != static_cast<std::uint32_t>(kImpactSurfaceCount))
        return Workspace::FailTransaction(*workspace, Status::InvalidCount);
    if (!IsAlignedAddress(entries, alignof(FxImpactEntryDisk32)))
        return Workspace::FailTransaction(*workspace, Status::InvalidSpan);
    const std::uint64_t byteCount =
        static_cast<std::uint64_t>(count) * sizeof(FxImpactEntryDisk32);
    if (!OracleValidates(workspace->cursor_, entries, byteCount))
        return Workspace::FailTransaction(*workspace, Status::InvalidSpan);

    Workspace::RecordedSpan span;
    span.address = entries;
    span.sourceField = &frame.impactHeader->table.token;
    span.byteCount = byteCount;
    span.token = frame.impactHeader->table.token;
    span.count = count;
    span.kind = SpanKind::ImpactEntries;
    const Status appendStatus = Workspace::AppendSpan(*workspace, span);
    if (appendStatus != Status::Success)
        return Workspace::FailTransaction(*workspace, appendStatus);

    frame.entries = entries;
    frame.impactSlot = 0;
    frame.state = Phase::ImpactEntries;
    Workspace::NormalizeImpactSlots(frame);
    return Status::Success;
}

FxFastFileZoneAdapterDisk32Status TryRecordFxImpactHandleZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *const workspace,
    const disk32::PointerToken *const sourceField,
    const FxFastFileDisk32ResolvedReference &resolution) noexcept
{
    using Workspace = FxFastFileZoneAdapterDisk32Workspace;
    if (!workspace)
        return Status::InvalidArgument;
    OperationGate gate(&workspace->operating_);
    if (!gate.acquired())
        return Status::Busy;
    if (!sourceField)
        return Status::InvalidArgument;
    if (workspace->frameDepth_ == 0)
        return Status::InvalidPhase;
    Workspace::Frame &frame = workspace->frames_[workspace->frameDepth_ - 1];
    if (frame.kind != Workspace::FrameKind::Impact
        || frame.state != Phase::ImpactEntries)
    {
        return Workspace::FailTransaction(*workspace, Status::InvalidSequence);
    }
    const std::uint32_t slotCount =
        static_cast<std::uint32_t>(kFxFastFileImpactDisk32HandleCount);
    const disk32::PointerToken *const pendingField =
        Workspace::ImpactSlotField(frame, frame.impactSlot);
    if (frame.impactSlot >= slotCount || !pendingField
        || sourceField != pendingField)
    {
        return Workspace::FailTransaction(*workspace, Status::InvalidSequence);
    }
    // Null slots are skipped automatically and inline sentinels must run a
    // nested effect transaction; only real alias tokens may be reported here.
    if (!sourceField->isOffset())
        return Workspace::FailTransaction(*workspace, Status::InvalidToken);
    if (!resolution.pointer || resolution.retainedByteCount == 0)
        return Workspace::FailTransaction(*workspace, Status::InvalidReference);

    Workspace::RecordedReference reference;
    reference.sourceField = sourceField;
    reference.resolution = resolution;
    reference.token = *sourceField;
    reference.kind = ReferenceKind::EffectAssetHandle;
    reference.isString = false;
    const Status appendStatus =
        Workspace::AppendReference(*workspace, reference);
    if (appendStatus != Status::Success)
        return Workspace::FailTransaction(*workspace, appendStatus);

    ++frame.impactSlot;
    Workspace::NormalizeImpactSlots(frame);
    return Status::Success;
}

FxFastFileZoneAdapterDisk32Status TrySealFxImpactTableZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *const workspace) noexcept
{
    using Workspace = FxFastFileZoneAdapterDisk32Workspace;
    if (!workspace)
        return Status::InvalidArgument;
    OperationGate gate(&workspace->operating_);
    if (!gate.acquired())
        return Status::Busy;
    if (workspace->frameDepth_ == 0)
        return Status::InvalidPhase;
    Workspace::Frame &frame = workspace->frames_[workspace->frameDepth_ - 1];
    if (frame.kind != Workspace::FrameKind::Impact
        || frame.state != Phase::ImpactEntries
        || frame.impactSlot
            != static_cast<std::uint32_t>(kFxFastFileImpactDisk32HandleCount))
    {
        return Workspace::FailTransaction(*workspace, Status::InvalidSequence);
    }
    frame.state = Phase::ImpactSealed;
    return Status::Success;
}

FxFastFileZoneAdapterDisk32Status TryPublishFxImpactTableZoneDisk32(
    FxFastFileZoneAdapterDisk32Workspace *const workspace,
    const FxFastFileZoneAdapterPublication &publication,
    FxImpactTable **const outTable) noexcept
{
    using Workspace = FxFastFileZoneAdapterDisk32Workspace;
    if (!workspace)
        return Status::InvalidArgument;
    OperationGate gate(&workspace->operating_);
    if (!gate.acquired())
        return Status::Busy;
    if (!publication.publishImpact || !outTable)
        return Status::InvalidArgument;
    if (workspace->frameDepth_ == 0)
        return Status::InvalidPhase;
    Workspace::Frame &frame = workspace->frames_[workspace->frameDepth_ - 1];
    if (frame.kind != Workspace::FrameKind::Impact
        || frame.state != Phase::ImpactSealed)
    {
        return Workspace::FailTransaction(*workspace, Status::InvalidSequence);
    }

    FxFastFileImpactTableDisk32View source;
    source.impactTable = frame.impactHeader;
    source.entries = {
        frame.entries, static_cast<std::uint32_t>(kImpactSurfaceCount)};
    source.provenance = {workspace, &Workspace::ValidateSpanThunk};
    const FxFastFileDisk32Resolvers resolvers{
        workspace, &Workspace::ResolveReferenceThunk};

    workspace->resolveHint_ = frame.referenceBase;
    workspace->spanHint_ = frame.spanBase;

    FxFastFileImpactNativeDisk32Plan plan;
    FxFastFileNativeDisk32Status converterStatus = TryPlanFxImpactTableDisk32(
        &workspace->impactConverter_, source, resolvers, &plan);
    workspace->lastConverterStatus_ = converterStatus;
    if (converterStatus != FxFastFileNativeDisk32Status::Success)
        return Workspace::FailTransaction(*workspace, Status::ConversionFailed);

    for (std::uint32_t index = frame.referenceBase;
         index < workspace->referenceCount_;
         ++index)
    {
        if (!workspace->references_[index].consumed)
        {
            return Workspace::FailTransaction(
                *workspace, Status::InvalidReference);
        }
    }

    void *storage = nullptr;
    FxFastFileNativeArenaStatus arenaStatus = workspace->arena_->TryReserve(
        frame.arenaTransaction,
        plan.outputBytes(),
        plan.outputAlignment(),
        &storage);
    if (arenaStatus != FxFastFileNativeArenaStatus::Success)
    {
        workspace->lastArenaStatus_ = arenaStatus;
        return Workspace::FailTransaction(*workspace, Status::ArenaFailed);
    }

    FxImpactTable *table = nullptr;
    converterStatus = TryMaterializeFxImpactTableDisk32(
        &workspace->impactConverter_,
        plan,
        storage,
        plan.outputBytes(),
        &table);
    workspace->lastConverterStatus_ = converterStatus;
    if (converterStatus != FxFastFileNativeDisk32Status::Success)
        return Workspace::FailTransaction(*workspace, Status::ConversionFailed);

    arenaStatus = workspace->arena_->TryCommit(frame.arenaTransaction);
    if (arenaStatus != FxFastFileNativeArenaStatus::Success)
    {
        workspace->lastArenaStatus_ = arenaStatus;
        return Workspace::FailTransaction(*workspace, Status::ArenaFailed);
    }
    frame.arenaTransaction = FxFastFileNativeArenaTransaction{};

    if (!publication.publishImpact(publication.context, table))
        return Workspace::FailTransaction(*workspace, Status::PublicationFailed);

    Workspace::ResetRecordingState(*workspace);
    *outTable = table;
    return Status::Success;
}

FxFastFileZoneAdapterDisk32Status AbortFxFastFileZoneAdapterDisk32(
    FxFastFileZoneAdapterDisk32Workspace *const workspace) noexcept
{
    using Workspace = FxFastFileZoneAdapterDisk32Workspace;
    if (!workspace)
        return Status::InvalidArgument;
    OperationGate gate(&workspace->operating_);
    if (!gate.acquired())
        return Status::Busy;
    if (workspace->frameDepth_ == 0)
        return Status::InvalidPhase;
    Workspace::TeardownTransaction(*workspace);
    return Status::Success;
}
} // namespace fx::fastfile
