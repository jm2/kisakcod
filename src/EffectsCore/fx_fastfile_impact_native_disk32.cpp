#include <EffectsCore/fx_fastfile_impact_native_disk32.h>

#include <EffectsCore/fx_runtime.h>

#include <algorithm>
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
constexpr std::uint64_t kFnvOffset = UINT64_C(14695981039346656037);
constexpr std::uint64_t kFnvPrime = UINT64_C(1099511628211);

class OperationReset final
{
  public:
    explicit OperationReset(bool *const operating) noexcept
        : operating_(operating)
    {
    }

    ~OperationReset() noexcept
    {
        if (operating_)
            *operating_ = false;
    }

    OperationReset(const OperationReset &) = delete;
    OperationReset &operator=(const OperationReset &) = delete;

  private:
    bool *operating_;
};

class PlanningFailureReset final
{
  public:
    PlanningFailureReset(FxFastFileImpactTableDisk32View *const source,
                         FxFastFileImpactNativeDisk32Plan *const plan,
                         FxFastFileDisk32ResolvedReference *const resolved,
                         const std::size_t resolvedCapacity,
                         std::uint32_t *const resolvedCount,
                         FxFastFileNativeDisk32Phase *const phase,
                         bool *const committed) noexcept
        : source_(source), plan_(plan), resolved_(resolved),
          resolvedCapacity_(resolvedCapacity), resolvedCount_(resolvedCount),
          phase_(phase), committed_(committed)
    {
    }

    ~PlanningFailureReset() noexcept
    {
        if (!committed_ || *committed_)
            return;
        *source_ = {};
        *plan_ = {};
        for (std::size_t index = 0; index < resolvedCapacity_; ++index)
            resolved_[index] = {};
        *resolvedCount_ = 0;
        *phase_ = FxFastFileNativeDisk32Phase::Empty;
    }

    PlanningFailureReset(const PlanningFailureReset &) = delete;
    PlanningFailureReset &operator=(const PlanningFailureReset &) = delete;

  private:
    FxFastFileImpactTableDisk32View *source_;
    FxFastFileImpactNativeDisk32Plan *plan_;
    FxFastFileDisk32ResolvedReference *resolved_;
    std::size_t resolvedCapacity_;
    std::uint32_t *resolvedCount_;
    FxFastFileNativeDisk32Phase *phase_;
    bool *committed_;
};

struct AddressRange final
{
    std::uintptr_t begin;
    std::uintptr_t end;
};

bool TryMakeRange(const void *const address,
                  const std::size_t bytes,
                  AddressRange *const outRange) noexcept
{
    if (!address || !bytes || !outRange)
        return false;
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(address);
    if (bytes > (std::numeric_limits<std::uintptr_t>::max)() - begin)
        return false;
    *outRange = {begin, begin + bytes};
    return true;
}

bool RangesOverlap(const void *const left,
                   const std::size_t leftBytes,
                   const void *const right,
                   const std::size_t rightBytes) noexcept
{
    AddressRange leftRange{};
    AddressRange rightRange{};
    if (!TryMakeRange(left, leftBytes, &leftRange) ||
        !TryMakeRange(right, rightBytes, &rightRange))
    {
        return true;
    }
    return leftRange.begin < rightRange.end && rightRange.begin < leftRange.end;
}

bool IsAligned(const void *const address, const std::size_t alignment) noexcept
{
    return address && alignment && (alignment & (alignment - 1u)) == 0 &&
           reinterpret_cast<std::uintptr_t>(address) % alignment == 0;
}

void HashBytes(std::uint64_t *const hash,
               const void *const bytes,
               const std::size_t byteCount) noexcept
{
    const auto *const values = static_cast<const std::uint8_t *>(bytes);
    for (std::size_t index = 0; index < byteCount; ++index)
    {
        *hash ^= values[index];
        *hash *= kFnvPrime;
    }
}

template <typename VALUE>
void HashValue(std::uint64_t *const hash, const VALUE &value) noexcept
{
    static_assert(std::is_trivially_copyable_v<VALUE>);
    HashBytes(hash, &value, sizeof(value));
}

std::uint64_t
ComputeSourceFingerprint(const FxFastFileImpactTableDisk32View &source,
                         const FxFastFileDisk32ResolvedReference &name) noexcept
{
    std::uint64_t hash = kFnvOffset;
    const std::uintptr_t headerAddress =
        reinterpret_cast<std::uintptr_t>(source.impactTable);
    const std::uintptr_t entriesAddress =
        reinterpret_cast<std::uintptr_t>(source.entries.data);
    const std::uintptr_t nameAddress =
        reinterpret_cast<std::uintptr_t>(name.pointer);
    HashValue(&hash, headerAddress);
    HashValue(&hash, entriesAddress);
    HashValue(&hash, source.entries.count);
    HashBytes(&hash, source.impactTable, sizeof(*source.impactTable));
    HashBytes(&hash,
              source.entries.data,
              sizeof(FxImpactEntryDisk32) * kImpactSurfaceCount);
    HashValue(&hash, nameAddress);
    HashValue(&hash, name.stringByteCount);
    HashBytes(&hash, name.pointer, name.stringByteCount);
    return hash;
}

std::uint64_t
ComputeBoundFingerprint(const FxFastFileImpactTableDisk32View &source,
                        const FxFastFileDisk32ResolvedReference *const resolved,
                        const std::uint32_t resolvedCount) noexcept
{
    std::uint64_t hash = ComputeSourceFingerprint(source, resolved[0]);
    HashValue(&hash, resolvedCount);
    for (std::size_t index = 0; index < kFxFastFileImpactDisk32JournalCount;
         ++index)
    {
        const std::uintptr_t identity =
            reinterpret_cast<std::uintptr_t>(resolved[index].pointer);
        HashValue(&hash, identity);
        HashValue(&hash, resolved[index].stringByteCount);
    }
    return hash;
}

bool IsExactCString(const FxFastFileDisk32ResolvedReference &reference) noexcept
{
    if (!reference.pointer || reference.stringByteCount < 2u)
        return false;
    const auto *const string = static_cast<const char *>(reference.pointer);
    const std::size_t bytes = reference.stringByteCount;
    if (string[bytes - 1u] != '\0')
        return false;
    return std::memchr(string, '\0', bytes - 1u) == nullptr;
}

bool ComputeNativeLayout(const std::uint32_t nameBytes,
                         std::uint32_t *const outBytes,
                         std::uint32_t *const outAlignment,
                         std::uint32_t *const outEntriesOffset,
                         std::uint32_t *const outNameOffset) noexcept
{
    if (!nameBytes || !outBytes || !outAlignment || !outEntriesOffset ||
        !outNameOffset)
    {
        return false;
    }

    constexpr std::size_t alignment =
        alignof(FxImpactTable) > alignof(FxImpactEntry)
            ? alignof(FxImpactTable)
            : alignof(FxImpactEntry);
    static_assert((alignment & (alignment - 1u)) == 0);

    std::size_t offset = sizeof(FxImpactTable);
    const std::size_t entryAlignment = alignof(FxImpactEntry);
    if (offset >
        (std::numeric_limits<std::size_t>::max)() - (entryAlignment - 1u))
    {
        return false;
    }
    offset = (offset + entryAlignment - 1u) & ~(entryAlignment - 1u);
    const std::size_t entriesOffset = offset;
    constexpr std::size_t entriesBytes =
        sizeof(FxImpactEntry) * kImpactSurfaceCount;
    if (entriesBytes > (std::numeric_limits<std::size_t>::max)() - offset)
        return false;
    offset += entriesBytes;
    const std::size_t nameOffset = offset;
    if (nameBytes > (std::numeric_limits<std::size_t>::max)() - offset)
        return false;
    offset += nameBytes;
    if (offset > (std::numeric_limits<std::uint32_t>::max)())
        return false;

    *outBytes = static_cast<std::uint32_t>(offset);
    *outAlignment = static_cast<std::uint32_t>(alignment);
    *outEntriesOffset = static_cast<std::uint32_t>(entriesOffset);
    *outNameOffset = static_cast<std::uint32_t>(nameOffset);
    return true;
}

bool SourceMatchesSnapshots(
    const FxFastFileImpactTableDisk32View &source,
    const FxImpactTableDisk32 &headerSnapshot,
    const FxImpactEntryDisk32 *const entrySnapshots) noexcept
{
    return std::memcmp(source.impactTable,
                       &headerSnapshot,
                       sizeof(headerSnapshot)) == 0 &&
           std::memcmp(source.entries.data,
                       entrySnapshots,
                       sizeof(FxImpactEntryDisk32) * kImpactSurfaceCount) == 0;
}
} // namespace

FxFastFileNativeDisk32Status TryPlanFxImpactTableDisk32(
    FxFastFileImpactNativeDisk32Workspace *const workspace,
    const FxFastFileImpactTableDisk32View &sourceArgument,
    const FxFastFileDisk32Resolvers &resolvers,
    FxFastFileImpactNativeDisk32Plan *const outPlan) noexcept
{
    if (!workspace || !outPlan || !sourceArgument.impactTable ||
        !sourceArgument.provenance.validateSpan || !resolvers.resolve ||
        reinterpret_cast<std::uintptr_t>(outPlan) %
                alignof(FxFastFileImpactNativeDisk32Plan) !=
            0)
    {
        return FxFastFileNativeDisk32Status::InvalidArgument;
    }
    if (workspace->operating_)
        return FxFastFileNativeDisk32Status::Busy;
    if (workspace->phase_ != FxFastFileNativeDisk32Phase::Empty)
        return FxFastFileNativeDisk32Status::InvalidPhase;
    if (RangesOverlap(&sourceArgument,
                      sizeof(sourceArgument),
                      workspace,
                      sizeof(*workspace)) ||
        RangesOverlap(sourceArgument.impactTable,
                      sizeof(*sourceArgument.impactTable),
                      workspace,
                      sizeof(*workspace)) ||
        (sourceArgument.entries.data &&
         RangesOverlap(sourceArgument.entries.data,
                       sizeof(FxImpactEntryDisk32) * kImpactSurfaceCount,
                       workspace,
                       sizeof(*workspace))) ||
        RangesOverlap(outPlan, sizeof(*outPlan), workspace, sizeof(*workspace)))
    {
        return FxFastFileNativeDisk32Status::OverlappingStorage;
    }
    workspace->operating_ = true;
    const OperationReset operationReset(&workspace->operating_);
    const FxFastFileImpactTableDisk32View source = sourceArgument;
    bool committed = false;
    const PlanningFailureReset planningFailureReset(
        &workspace->source_,
        &workspace->plan_,
        workspace->resolved_,
        kFxFastFileImpactDisk32JournalCount,
        &workspace->resolvedCount_,
        &workspace->phase_,
        &committed);

    if (source.entries.count != kImpactSurfaceCount)
        return FxFastFileNativeDisk32Status::InvalidCount;
    if (!source.entries.data)
        return FxFastFileNativeDisk32Status::InvalidPointerCount;
    if (!IsAligned(source.impactTable, alignof(FxImpactTableDisk32)) ||
        !IsAligned(source.entries.data, alignof(FxImpactEntryDisk32)))
    {
        return FxFastFileNativeDisk32Status::InvalidSourceLayout;
    }
    if (RangesOverlap(source.impactTable,
                      sizeof(*source.impactTable),
                      source.entries.data,
                      sizeof(FxImpactEntryDisk32) * kImpactSurfaceCount))
    {
        return FxFastFileNativeDisk32Status::InvalidSourceLayout;
    }

    workspace->sourceHeaderSnapshot_ = *source.impactTable;
    std::memcpy(workspace->sourceEntrySnapshots_,
                source.entries.data,
                sizeof(workspace->sourceEntrySnapshots_));

    const disk32::PointerToken rootToken{};
    if (!source.provenance.validateSpan(
            source.provenance.context,
            FxFastFileDisk32SourceSpanKind::ImpactTableHeader,
            nullptr,
            rootToken,
            source.impactTable,
            sizeof(*source.impactTable),
            alignof(FxImpactTableDisk32)))
    {
        return FxFastFileNativeDisk32Status::InvalidProvenance;
    }
    if (!SourceMatchesSnapshots(source,
                                workspace->sourceHeaderSnapshot_,
                                workspace->sourceEntrySnapshots_))
    {
        return FxFastFileNativeDisk32Status::SourceChanged;
    }

    const FxImpactTableDisk32 &header = workspace->sourceHeaderSnapshot_;
    if (header.table.token.isNull())
        return FxFastFileNativeDisk32Status::InvalidPointerCount;
    if (header.name.token.isNull())
        return FxFastFileNativeDisk32Status::InvalidString;
    if (!source.provenance.validateSpan(
            source.provenance.context,
            FxFastFileDisk32SourceSpanKind::ImpactEntries,
            &source.impactTable->table.token,
            header.table.token,
            source.entries.data,
            sizeof(FxImpactEntryDisk32) * kImpactSurfaceCount,
            alignof(FxImpactEntryDisk32)))
    {
        return FxFastFileNativeDisk32Status::InvalidProvenance;
    }
    if (!SourceMatchesSnapshots(source,
                                workspace->sourceHeaderSnapshot_,
                                workspace->sourceEntrySnapshots_))
    {
        return FxFastFileNativeDisk32Status::SourceChanged;
    }
    if (header.table.token.isNull())
        return FxFastFileNativeDisk32Status::InvalidPointerCount;
    if (header.name.token.isNull())
        return FxFastFileNativeDisk32Status::InvalidString;

    if (RangesOverlap(
            outPlan, sizeof(*outPlan), workspace, sizeof(*workspace)) ||
        RangesOverlap(outPlan,
                      sizeof(*outPlan),
                      source.impactTable,
                      sizeof(*source.impactTable)) ||
        RangesOverlap(outPlan,
                      sizeof(*outPlan),
                      source.entries.data,
                      sizeof(FxImpactEntryDisk32) * kImpactSurfaceCount))
    {
        return FxFastFileNativeDisk32Status::OverlappingStorage;
    }

    for (FxFastFileDisk32ResolvedReference &resolved : workspace->resolved_)
        resolved = {};

    FxFastFileDisk32ResolvedReference name{};
    if (!resolvers.resolve(resolvers.context,
                           FxFastFileDisk32ReferenceKind::EffectName,
                           &source.impactTable->name.token,
                           header.name.token,
                           &name))
    {
        return FxFastFileNativeDisk32Status::UnresolvedReference;
    }
    if (!SourceMatchesSnapshots(source,
                                workspace->sourceHeaderSnapshot_,
                                workspace->sourceEntrySnapshots_))
    {
        return FxFastFileNativeDisk32Status::SourceChanged;
    }
    if (!name.pointer || name.stringByteCount == 0)
        return FxFastFileNativeDisk32Status::InvalidString;
    if (RangesOverlap(
            name.pointer, name.stringByteCount, workspace, sizeof(*workspace)))
    {
        return FxFastFileNativeDisk32Status::OverlappingStorage;
    }
    if (!IsExactCString(name))
        return FxFastFileNativeDisk32Status::InvalidString;
    const std::uint64_t nameBeforeProvenanceFingerprint =
        ComputeSourceFingerprint(source, name);
    if (!source.provenance.validateSpan(source.provenance.context,
                                        FxFastFileDisk32SourceSpanKind::String,
                                        &source.impactTable->name.token,
                                        header.name.token,
                                        name.pointer,
                                        name.stringByteCount,
                                        alignof(char)))
    {
        return FxFastFileNativeDisk32Status::InvalidProvenance;
    }
    if (!SourceMatchesSnapshots(source,
                                workspace->sourceHeaderSnapshot_,
                                workspace->sourceEntrySnapshots_))
    {
        return FxFastFileNativeDisk32Status::SourceChanged;
    }
    if (!IsExactCString(name))
        return FxFastFileNativeDisk32Status::InvalidString;
    const std::uint64_t initialSourceFingerprint =
        ComputeSourceFingerprint(source, name);
    if (initialSourceFingerprint != nameBeforeProvenanceFingerprint)
        return FxFastFileNativeDisk32Status::SourceChanged;
    if (RangesOverlap(name.pointer,
                      name.stringByteCount,
                      source.impactTable,
                      sizeof(*source.impactTable)) ||
        RangesOverlap(name.pointer,
                      name.stringByteCount,
                      source.entries.data,
                      sizeof(FxImpactEntryDisk32) * kImpactSurfaceCount))
    {
        return FxFastFileNativeDisk32Status::InvalidSourceLayout;
    }
    if (RangesOverlap(
            outPlan, sizeof(*outPlan), name.pointer, name.stringByteCount))
    {
        return FxFastFileNativeDisk32Status::OverlappingStorage;
    }
    workspace->resolved_[0] = name;
    const auto identityOverlapsOwnedInput =
        [workspace, &source, &name, outPlan](
            const void *const identity) noexcept
    {
        return RangesOverlap(identity, 1u, workspace, sizeof(*workspace)) ||
               RangesOverlap(identity,
                             1u,
                             source.impactTable,
                             sizeof(*source.impactTable)) ||
               RangesOverlap(identity,
                             1u,
                             source.entries.data,
                             sizeof(FxImpactEntryDisk32) *
                                 kImpactSurfaceCount) ||
               RangesOverlap(
                   identity, 1u, name.pointer, name.stringByteCount) ||
               RangesOverlap(identity, 1u, outPlan, sizeof(*outPlan));
    };

    std::uint32_t resolvedCount = 1;
    std::size_t journalIndex = 1;
    for (std::size_t entryIndex = 0; entryIndex < kImpactSurfaceCount;
         ++entryIndex)
    {
        const FxImpactEntryDisk32 &entry = source.entries.data[entryIndex];
        for (std::size_t effectIndex = 0;
             effectIndex < kImpactNonFleshEffectCount;
             ++effectIndex, ++journalIndex)
        {
            const disk32::PointerToken *const sourceField =
                &entry.nonflesh[effectIndex].token;
            if (sourceField->isNull())
                continue;
            FxFastFileDisk32ResolvedReference resolved{};
            if (!resolvers.resolve(
                    resolvers.context,
                    FxFastFileDisk32ReferenceKind::EffectAssetHandle,
                    sourceField,
                    *sourceField,
                    &resolved) ||
                !resolved.pointer)
            {
                return FxFastFileNativeDisk32Status::UnresolvedReference;
            }
            if (resolved.stringByteCount != 0)
                return FxFastFileNativeDisk32Status::InvalidString;
            if (identityOverlapsOwnedInput(resolved.pointer))
            {
                return FxFastFileNativeDisk32Status::OverlappingStorage;
            }
            workspace->resolved_[journalIndex] = resolved;
            ++resolvedCount;
        }
        for (std::size_t effectIndex = 0; effectIndex < kImpactFleshEffectCount;
             ++effectIndex, ++journalIndex)
        {
            const disk32::PointerToken *const sourceField =
                &entry.flesh[effectIndex].token;
            if (sourceField->isNull())
                continue;
            FxFastFileDisk32ResolvedReference resolved{};
            if (!resolvers.resolve(
                    resolvers.context,
                    FxFastFileDisk32ReferenceKind::EffectAssetHandle,
                    sourceField,
                    *sourceField,
                    &resolved) ||
                !resolved.pointer)
            {
                return FxFastFileNativeDisk32Status::UnresolvedReference;
            }
            if (resolved.stringByteCount != 0)
                return FxFastFileNativeDisk32Status::InvalidString;
            if (identityOverlapsOwnedInput(resolved.pointer))
            {
                return FxFastFileNativeDisk32Status::OverlappingStorage;
            }
            workspace->resolved_[journalIndex] = resolved;
            ++resolvedCount;
        }
    }
    if (journalIndex != kFxFastFileImpactDisk32JournalCount)
        return FxFastFileNativeDisk32Status::InvalidCount;
    for (std::size_t index = 1; index < kFxFastFileImpactDisk32JournalCount;
         ++index)
    {
        const void *const identity = workspace->resolved_[index].pointer;
        if (identity && RangesOverlap(outPlan, sizeof(*outPlan), identity, 1u))
            return FxFastFileNativeDisk32Status::OverlappingStorage;
    }
    if (std::memcmp(source.impactTable,
                    &workspace->sourceHeaderSnapshot_,
                    sizeof(workspace->sourceHeaderSnapshot_)) != 0 ||
        std::memcmp(source.entries.data,
                    workspace->sourceEntrySnapshots_,
                    sizeof(workspace->sourceEntrySnapshots_)) != 0)
    {
        return FxFastFileNativeDisk32Status::SourceChanged;
    }
    const std::uint64_t finalSourceFingerprint =
        ComputeSourceFingerprint(source, name);
    if (!IsExactCString(name) ||
        finalSourceFingerprint != initialSourceFingerprint)
        return FxFastFileNativeDisk32Status::SourceChanged;

    FxFastFileImpactNativeDisk32Plan planned{};
    if (!ComputeNativeLayout(name.stringByteCount,
                             &planned.outputBytes_,
                             &planned.outputAlignment_,
                             &planned.entriesOffset_,
                             &planned.nameOffset_))
    {
        return FxFastFileNativeDisk32Status::SizeOverflow;
    }
    planned.workspaceIdentity_ = workspace;
    planned.serial_ = workspace->nextSerial_;
    if (!planned.serial_)
        planned.serial_ = 1;
    workspace->nextSerial_ = planned.serial_ + 1u;
    if (!workspace->nextSerial_)
        workspace->nextSerial_ = 1;
    planned.sourceFingerprint_ =
        ComputeBoundFingerprint(source, workspace->resolved_, resolvedCount);
    planned.entryCount_ = static_cast<std::uint32_t>(kImpactSurfaceCount);
    planned.resolvedReferenceCount_ = resolvedCount;
    planned.nameBytes_ = name.stringByteCount;

    workspace->source_ = source;
    workspace->resolvedCount_ = resolvedCount;
    workspace->plan_ = planned;
    workspace->phase_ = FxFastFileNativeDisk32Phase::Planned;
    *outPlan = planned;
    committed = true;
    return FxFastFileNativeDisk32Status::Success;
}

FxFastFileNativeDisk32Status TryMaterializeFxImpactTableDisk32(
    FxFastFileImpactNativeDisk32Workspace *const workspace,
    const FxFastFileImpactNativeDisk32Plan &plan,
    void *const storage,
    const std::size_t capacity,
    FxImpactTable **const outTable) noexcept
{
    if (!workspace || !storage || !outTable ||
        reinterpret_cast<std::uintptr_t>(outTable) % alignof(FxImpactTable *) !=
            0)
    {
        return FxFastFileNativeDisk32Status::InvalidArgument;
    }
    if (workspace->operating_)
        return FxFastFileNativeDisk32Status::Busy;
    workspace->operating_ = true;
    const OperationReset operationReset(&workspace->operating_);

    const FxFastFileImpactNativeDisk32Plan &expected = workspace->plan_;
    if (plan.workspaceIdentity_ != workspace || plan.serial_ == 0)
        return FxFastFileNativeDisk32Status::InvalidPlan;
    if (workspace->phase_ != FxFastFileNativeDisk32Phase::Planned)
        return FxFastFileNativeDisk32Status::InvalidPhase;
    if (plan.workspaceIdentity_ != expected.workspaceIdentity_ ||
        plan.serial_ != expected.serial_ ||
        plan.sourceFingerprint_ != expected.sourceFingerprint_ ||
        plan.outputBytes_ != expected.outputBytes_ ||
        plan.outputAlignment_ != expected.outputAlignment_ ||
        plan.entryCount_ != expected.entryCount_ ||
        plan.resolvedReferenceCount_ != expected.resolvedReferenceCount_ ||
        plan.entriesOffset_ != expected.entriesOffset_ ||
        plan.nameOffset_ != expected.nameOffset_ ||
        plan.nameBytes_ != expected.nameBytes_)
    {
        return FxFastFileNativeDisk32Status::InvalidPlan;
    }
#if !KISAK_ARCH_64BIT
    if (plan.workspaceIdentityPadding_ != expected.workspaceIdentityPadding_)
        return FxFastFileNativeDisk32Status::InvalidPlan;
#endif
    std::uint32_t verifiedBytes = 0;
    std::uint32_t verifiedAlignment = 0;
    std::uint32_t verifiedEntriesOffset = 0;
    std::uint32_t verifiedNameOffset = 0;
    if (!ComputeNativeLayout(plan.nameBytes_,
                             &verifiedBytes,
                             &verifiedAlignment,
                             &verifiedEntriesOffset,
                             &verifiedNameOffset) ||
        plan.outputBytes_ != verifiedBytes ||
        plan.outputAlignment_ != verifiedAlignment ||
        plan.entriesOffset_ != verifiedEntriesOffset ||
        plan.nameOffset_ != verifiedNameOffset ||
        plan.entryCount_ != kImpactSurfaceCount ||
        plan.resolvedReferenceCount_ != workspace->resolvedCount_)
    {
        return FxFastFileNativeDisk32Status::InvalidPlan;
    }
    if (!IsAligned(storage, plan.outputAlignment_))
        return FxFastFileNativeDisk32Status::MisalignedStorage;
    if (capacity < plan.outputBytes_)
        return FxFastFileNativeDisk32Status::InsufficientCapacity;
    const std::uintptr_t storageAddress =
        reinterpret_cast<std::uintptr_t>(storage);
    if (plan.outputBytes_ >
        (std::numeric_limits<std::uintptr_t>::max)() - storageAddress)
    {
        return FxFastFileNativeDisk32Status::SizeOverflow;
    }

    if (RangesOverlap(
            storage, plan.outputBytes_, workspace, sizeof(*workspace)) ||
        RangesOverlap(storage, plan.outputBytes_, &plan, sizeof(plan)) ||
        RangesOverlap(
            storage, plan.outputBytes_, outTable, sizeof(*outTable)) ||
        RangesOverlap(storage,
                      plan.outputBytes_,
                      workspace->source_.impactTable,
                      sizeof(*workspace->source_.impactTable)) ||
        RangesOverlap(storage,
                      plan.outputBytes_,
                      workspace->source_.entries.data,
                      sizeof(FxImpactEntryDisk32) * kImpactSurfaceCount) ||
        RangesOverlap(storage,
                      plan.outputBytes_,
                      workspace->resolved_[0].pointer,
                      workspace->resolved_[0].stringByteCount))
    {
        return FxFastFileNativeDisk32Status::OverlappingStorage;
    }
    if (RangesOverlap(
            outTable, sizeof(*outTable), workspace, sizeof(*workspace)) ||
        RangesOverlap(outTable, sizeof(*outTable), &plan, sizeof(plan)) ||
        RangesOverlap(outTable,
                      sizeof(*outTable),
                      workspace->source_.impactTable,
                      sizeof(*workspace->source_.impactTable)) ||
        RangesOverlap(outTable,
                      sizeof(*outTable),
                      workspace->source_.entries.data,
                      sizeof(FxImpactEntryDisk32) * kImpactSurfaceCount) ||
        RangesOverlap(outTable,
                      sizeof(*outTable),
                      workspace->resolved_[0].pointer,
                      workspace->resolved_[0].stringByteCount))
    {
        return FxFastFileNativeDisk32Status::OverlappingStorage;
    }
    for (std::size_t index = 1; index < kFxFastFileImpactDisk32JournalCount;
         ++index)
    {
        const void *const identity = workspace->resolved_[index].pointer;
        if (identity && RangesOverlap(storage, plan.outputBytes_, identity, 1u))
            return FxFastFileNativeDisk32Status::OverlappingStorage;
        if (identity &&
            RangesOverlap(outTable, sizeof(*outTable), identity, 1u))
        {
            return FxFastFileNativeDisk32Status::OverlappingStorage;
        }
    }

    if (!SourceMatchesSnapshots(workspace->source_,
                                workspace->sourceHeaderSnapshot_,
                                workspace->sourceEntrySnapshots_) ||
        !IsExactCString(workspace->resolved_[0]) ||
        ComputeBoundFingerprint(workspace->source_,
                                workspace->resolved_,
                                workspace->resolvedCount_) !=
            plan.sourceFingerprint_)
    {
        workspace->source_ = {};
        workspace->plan_ = {};
        for (FxFastFileDisk32ResolvedReference &resolved : workspace->resolved_)
            resolved = {};
        workspace->resolvedCount_ = 0;
        workspace->phase_ = FxFastFileNativeDisk32Phase::Empty;
        return FxFastFileNativeDisk32Status::SourceChanged;
    }

    auto *const bytes = static_cast<std::uint8_t *>(storage);
    FxImpactTable *const table =
        std::construct_at(reinterpret_cast<FxImpactTable *>(bytes));
    auto *const entries =
        reinterpret_cast<FxImpactEntry *>(bytes + plan.entriesOffset_);
    for (std::size_t index = 0; index < kImpactSurfaceCount; ++index)
        std::construct_at(&entries[index]);
    auto *const name = reinterpret_cast<char *>(bytes + plan.nameOffset_);
    std::memcpy(name,
                workspace->resolved_[0].pointer,
                workspace->resolved_[0].stringByteCount);

    table->name = name;
    table->table = entries;
    std::size_t journalIndex = 1;
    for (std::size_t entryIndex = 0; entryIndex < kImpactSurfaceCount;
         ++entryIndex)
    {
        for (std::size_t effectIndex = 0;
             effectIndex < kImpactNonFleshEffectCount;
             ++effectIndex, ++journalIndex)
        {
            entries[entryIndex].nonflesh[effectIndex] =
                static_cast<const FxEffectDef *>(
                    workspace->resolved_[journalIndex].pointer);
        }
        for (std::size_t effectIndex = 0; effectIndex < kImpactFleshEffectCount;
             ++effectIndex, ++journalIndex)
        {
            entries[entryIndex].flesh[effectIndex] =
                static_cast<const FxEffectDef *>(
                    workspace->resolved_[journalIndex].pointer);
        }
    }

    *outTable = table;
    workspace->source_ = {};
    workspace->plan_ = {};
    for (FxFastFileDisk32ResolvedReference &resolved : workspace->resolved_)
        resolved = {};
    workspace->resolvedCount_ = 0;
    workspace->phase_ = FxFastFileNativeDisk32Phase::Empty;
    return FxFastFileNativeDisk32Status::Success;
}
} // namespace fx::fastfile
