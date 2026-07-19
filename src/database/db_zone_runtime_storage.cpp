#include <database/db_zone_runtime_storage.h>

#include <database/db_script_string_journal.h>
#include <database/db_zone_runtime_storage_fx_bridge.h>

#include <limits>
#include <new>
#include <type_traits>

namespace db::zone_runtime_storage
{
namespace
{
using Arena = fx::fastfile::FxFastFileNativeArena;
using Journal = script_string_journal::ScriptStringJournal;
using JournalEntry = script_string_journal::ScriptStringJournalEntry;
using Status = ZoneRuntimeStorageStatus;
using Workspace = fx::fastfile::FxFastFileZoneAdapterDisk32Workspace;

static_assert(std::is_nothrow_default_constructible_v<Journal>);
static_assert(std::is_nothrow_destructible_v<Journal>);
static_assert(std::is_nothrow_default_constructible_v<JournalEntry>);
static_assert(std::is_nothrow_destructible_v<JournalEntry>);

[[nodiscard]] constexpr bool IsPowerOfTwo(
    const std::uint32_t value) noexcept
{
    return value != 0 && (value & (value - 1u)) == 0;
}

[[nodiscard]] bool TryAppendExtent(
    std::uint32_t *const cursor,
    const std::uint64_t byteCount,
    const std::uint32_t alignment,
    ZoneRuntimeStorageExtent *const outExtent) noexcept
{
    if (!cursor || !outExtent || !IsPowerOfTwo(alignment)
        || byteCount > UINT32_MAX)
    {
        return false;
    }

    const std::uint32_t mask = alignment - 1u;
    if (*cursor > UINT32_MAX - mask)
        return false;
    const std::uint32_t aligned = (*cursor + mask) & ~mask;
    if (byteCount > static_cast<std::uint64_t>(UINT32_MAX - aligned))
        return false;

    outExtent->offset = aligned;
    outExtent->byteCount = static_cast<std::uint32_t>(byteCount);
    *cursor = aligned + outExtent->byteCount;
    return true;
}

[[nodiscard]] bool IsAligned(
    const void *const value,
    const std::size_t alignment) noexcept
{
    return value
        && reinterpret_cast<std::uintptr_t>(value) % alignment == 0;
}

[[nodiscard]] bool IsRangeRepresentable(
    const void *const value,
    const std::size_t byteCount) noexcept
{
    if (!value)
        return byteCount == 0;
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(value);
    return byteCount
        <= (std::numeric_limits<std::uintptr_t>::max)() - begin;
}

[[nodiscard]] bool RangesOverlap(
    const void *const first,
    const std::size_t firstBytes,
    const void *const second,
    const std::size_t secondBytes) noexcept
{
    if (firstBytes == 0 || secondBytes == 0)
        return false;
    const std::uintptr_t firstBegin =
        reinterpret_cast<std::uintptr_t>(first);
    const std::uintptr_t secondBegin =
        reinterpret_cast<std::uintptr_t>(second);
    return firstBegin < secondBegin + secondBytes
        && secondBegin < firstBegin + firstBytes;
}

[[nodiscard]] void *AddressAt(
    void *const slab,
    const ZoneRuntimeStorageExtent &extent) noexcept
{
    return static_cast<void *>(
        static_cast<std::uint8_t *>(slab) + extent.offset);
}

template <typename T>
[[nodiscard]] T *ObjectAt(
    void *const slab,
    const ZoneRuntimeStorageExtent &extent) noexcept
{
    return static_cast<T *>(AddressAt(slab, extent));
}

[[nodiscard]] bool PlanIsCanonical(
    const ZoneRuntimeStoragePlan &plan) noexcept
{
    ZoneRuntimeStoragePlan canonical{};
    return TryPlanZoneRuntimeStorage(
               plan.expectedStringCount, plan.arenaBudget, &canonical)
            == Status::Success
        && canonical == plan;
}

[[nodiscard]] bool LayoutAddressesAreAligned(
    void *const slab,
    const ZoneRuntimeStoragePlan &plan,
    const detail::FxRuntimeStorageLayout &fxLayout) noexcept
{
    return IsAligned(
               AddressAt(slab, plan.scriptStringJournal), alignof(Journal))
        && IsAligned(AddressAt(slab, plan.scriptStringEntries),
                     alignof(JournalEntry))
        && IsAligned(AddressAt(slab, plan.fxNativeArena),
                     fxLayout.arenaAlignment)
        && IsAligned(AddressAt(slab, plan.fxZoneAdapterWorkspace),
                     fxLayout.workspaceAlignment)
        && IsAligned(AddressAt(slab, plan.fxArenaBacking),
                     fxLayout.backingAlignment);
}

[[nodiscard]] bool JournalCanBeDestroyed(
    const Journal &journal) noexcept
{
    return journal.readyForDestruction();
}

} // namespace

bool ZoneRuntimeStorageBinding::isPristine() const noexcept
{
    return self_ == nullptr && slab_ == nullptr && slabCapacity_ == 0
        && plan_ == ZoneRuntimeStoragePlan{} && journal_ == nullptr
        && entries_ == nullptr && arena_ == nullptr && workspace_ == nullptr
        && arenaBacking_ == nullptr && state_ == State::Pristine;
}

ZoneRuntimeStorageBinding::~ZoneRuntimeStorageBinding() noexcept
{
}

bool ZoneRuntimeStorageBinding::isSelfAuthenticating(
    const State state) const noexcept
{
    return self_ == this && state_ == state;
}

bool ZoneRuntimeStorageBinding::hasCanonicalBoundMetadata() const noexcept
{
    if (!isSelfAuthenticating(State::Bound)
        || !IsRangeRepresentable(this, sizeof(*this)))
    {
        return false;
    }
    const detail::FxRuntimeStorageLayout fxLayout =
        detail::GetFxRuntimeStorageLayout();
    if (!PlanIsCanonical(plan_)
        || !IsAligned(slab_, fxLayout.backingAlignment)
        || !IsRangeRepresentable(slab_, slabCapacity_)
        || slabCapacity_ < plan_.totalBytes
        || !LayoutAddressesAreAligned(slab_, plan_, fxLayout)
        || RangesOverlap(slab_, slabCapacity_, this, sizeof(*this)))
    {
        return false;
    }
    return journal_ == ObjectAt<Journal>(slab_, plan_.scriptStringJournal)
        && entries_
            == (plan_.expectedStringCount == 0
                    ? nullptr
                    : ObjectAt<JournalEntry>(
                          slab_, plan_.scriptStringEntries))
        && arena_ == ObjectAt<Arena>(slab_, plan_.fxNativeArena)
        && workspace_
            == ObjectAt<Workspace>(slab_, plan_.fxZoneAdapterWorkspace)
        && arenaBacking_ == AddressAt(slab_, plan_.fxArenaBacking);
}

bool ZoneRuntimeStorageBinding::bound() const noexcept
{
    return hasCanonicalBoundMetadata();
}

bool ZoneRuntimeStorageBinding::destroyed() const noexcept
{
    return isSelfAuthenticating(State::Destroyed) && slab_ == nullptr
        && slabCapacity_ == 0 && plan_ == ZoneRuntimeStoragePlan{}
        && journal_ == nullptr && entries_ == nullptr && arena_ == nullptr
        && workspace_ == nullptr && arenaBacking_ == nullptr;
}

void *ZoneRuntimeStorageBinding::slab() const noexcept
{
    return bound() ? slab_ : nullptr;
}

std::size_t ZoneRuntimeStorageBinding::slabCapacity() const noexcept
{
    return bound() ? slabCapacity_ : 0;
}

const ZoneRuntimeStoragePlan *ZoneRuntimeStorageBinding::plan() const noexcept
{
    return bound() ? &plan_ : nullptr;
}

script_string_journal::ScriptStringJournal *
ZoneRuntimeStorageBinding::scriptStringJournal() const noexcept
{
    return bound() ? journal_ : nullptr;
}

script_string_journal::ScriptStringJournalEntry *
ZoneRuntimeStorageBinding::scriptStringEntries() const noexcept
{
    return bound() ? entries_ : nullptr;
}

fx::fastfile::FxFastFileNativeArena *
ZoneRuntimeStorageBinding::fxNativeArena() const noexcept
{
    return bound() ? arena_ : nullptr;
}

fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *
ZoneRuntimeStorageBinding::fxZoneAdapterWorkspace() const noexcept
{
    return bound() ? workspace_ : nullptr;
}

void *ZoneRuntimeStorageBinding::fxArenaBacking() const noexcept
{
    return bound() ? arenaBacking_ : nullptr;
}

ZoneRuntimeStorageStatus TryPlanZoneRuntimeStorage(
    const std::uint32_t expectedStringCount,
    const std::uint64_t arenaBudget,
    ZoneRuntimeStoragePlan *const outPlan) noexcept
{
    if (!IsAligned(outPlan, alignof(ZoneRuntimeStoragePlan)))
        return Status::InvalidArgument;
    if (!IsRangeRepresentable(outPlan, sizeof(*outPlan)))
        return Status::SizeOverflow;
    if (expectedStringCount
        > script_string_journal::kMaxScriptStringJournalEntries)
    {
        return Status::InvalidCount;
    }
    if (arenaBudget == 0)
        return Status::InvalidArgument;
    if (arenaBudget > UINT32_MAX)
        return Status::SizeOverflow;

    ZoneRuntimeStoragePlan plan{};
    const detail::FxRuntimeStorageLayout fxLayout =
        detail::GetFxRuntimeStorageLayout();
    plan.expectedStringCount = expectedStringCount;
    plan.arenaBudget = static_cast<std::uint32_t>(arenaBudget);
    std::uint32_t cursor = 0;
    const std::uint64_t entryBytes =
        static_cast<std::uint64_t>(expectedStringCount)
        * sizeof(JournalEntry);

    if (!TryAppendExtent(&cursor,
                         sizeof(Journal),
                         alignof(Journal),
                         &plan.scriptStringJournal)
        || !TryAppendExtent(&cursor,
                            entryBytes,
                            alignof(JournalEntry),
                            &plan.scriptStringEntries)
        || !TryAppendExtent(&cursor,
                            fxLayout.arenaBytes,
                            fxLayout.arenaAlignment,
                            &plan.fxNativeArena)
        || !TryAppendExtent(&cursor,
                            fxLayout.workspaceBytes,
                            fxLayout.workspaceAlignment,
                            &plan.fxZoneAdapterWorkspace)
        || !TryAppendExtent(
            &cursor,
            arenaBudget,
            fxLayout.backingAlignment,
            &plan.fxArenaBacking))
    {
        return Status::SizeOverflow;
    }

    plan.totalBytes = cursor;
    *outPlan = plan;
    return Status::Success;
}

ZoneRuntimeStorageStatus TryBindZoneRuntimeStorage(
    void *const slab,
    const std::size_t slabCapacity,
    const ZoneRuntimeStoragePlan *const plan,
    ZoneRuntimeStorageBinding *const outBinding) noexcept
{
    if (!IsAligned(plan, alignof(ZoneRuntimeStoragePlan))
        || !IsAligned(outBinding, alignof(ZoneRuntimeStorageBinding)))
    {
        return Status::InvalidArgument;
    }
    if (!IsRangeRepresentable(plan, sizeof(*plan))
        || !IsRangeRepresentable(outBinding, sizeof(*outBinding)))
    {
        return Status::SizeOverflow;
    }

    // Snapshot before any placement: a caller may keep its plan in the slab.
    const ZoneRuntimeStoragePlan planSnapshot = *plan;
    const detail::FxRuntimeStorageLayout fxLayout =
        detail::GetFxRuntimeStorageLayout();
    if (!PlanIsCanonical(planSnapshot))
        return Status::InvalidPlan;
    if (!IsAligned(slab, fxLayout.backingAlignment))
    {
        return slab ? Status::MisalignedStorage : Status::InvalidArgument;
    }
    if (!IsRangeRepresentable(slab, slabCapacity))
        return Status::SizeOverflow;
    if (slabCapacity < planSnapshot.totalBytes)
        return Status::InsufficientCapacity;
    if (!LayoutAddressesAreAligned(slab, planSnapshot, fxLayout))
        return Status::MisalignedStorage;
    if (RangesOverlap(
            slab, slabCapacity, outBinding, sizeof(*outBinding))
        || RangesOverlap(
            plan, sizeof(*plan), outBinding, sizeof(*outBinding)))
    {
        return Status::OverlappingStorage;
    }
    if (!outBinding->isPristine())
        return Status::InvalidPhase;

    Journal *const journal = ::new (ObjectAt<Journal>(
        slab, planSnapshot.scriptStringJournal)) Journal{};
    JournalEntry *const entries = planSnapshot.expectedStringCount == 0
        ? nullptr
        : ObjectAt<JournalEntry>(
              slab, planSnapshot.scriptStringEntries);
    for (std::uint32_t index = 0;
         index < planSnapshot.expectedStringCount;
         ++index)
    {
        ::new (entries + index) JournalEntry{};
    }
    Arena *const arena = detail::ConstructFxRuntimeArena(
        AddressAt(slab, planSnapshot.fxNativeArena));
    Workspace *const workspace = detail::ConstructFxRuntimeWorkspace(
        AddressAt(slab, planSnapshot.fxZoneAdapterWorkspace));
    void *const arenaBacking =
        AddressAt(slab, planSnapshot.fxArenaBacking);

    // Publish only after every no-throw construction has completed.
    outBinding->slab_ = slab;
    outBinding->slabCapacity_ = slabCapacity;
    outBinding->plan_ = planSnapshot;
    outBinding->journal_ = journal;
    outBinding->entries_ = entries;
    outBinding->arena_ = arena;
    outBinding->workspace_ = workspace;
    outBinding->arenaBacking_ = arenaBacking;
    outBinding->self_ = outBinding;
    outBinding->state_ = ZoneRuntimeStorageBinding::State::Bound;
    return Status::Success;
}

ZoneRuntimeStorageStatus TryDestroyZoneRuntimeStorage(
    ZoneRuntimeStorageBinding *const binding) noexcept
{
    if (!IsAligned(binding, alignof(ZoneRuntimeStorageBinding)))
        return Status::InvalidArgument;
    if (!IsRangeRepresentable(binding, sizeof(*binding)))
        return Status::SizeOverflow;
    if (binding->destroyed())
        return Status::AlreadyComplete;
    if (!binding->bound())
    {
        return Status::InvalidBinding;
    }
    if (!JournalCanBeDestroyed(*binding->journal_))
    {
        const bool canonicalCallbackActive =
            binding->journal_->initialized()
            && binding->journal_->callbackActive();
        return canonicalCallbackActive
            ? Status::Busy
            : Status::InvalidPhase;
    }
    const detail::FxRuntimeStorageDestroyStatus fxStatus =
        detail::TryPrepareFxRuntimeStorageDestroy(
            binding->arena_,
            binding->workspace_,
            binding->arenaBacking_,
            binding->plan_.arenaBudget);
    switch (fxStatus)
    {
    case detail::FxRuntimeStorageDestroyStatus::Success:
        break;
    case detail::FxRuntimeStorageDestroyStatus::Busy:
        return Status::Busy;
    case detail::FxRuntimeStorageDestroyStatus::InvalidBinding:
        return Status::InvalidBinding;
    case detail::FxRuntimeStorageDestroyStatus::InvalidPhase:
        return Status::InvalidPhase;
    case detail::FxRuntimeStorageDestroyStatus::ArenaFailed:
    default:
        return Status::ArenaFailed;
    }

    // Reverse of construction: workspace, arena, entries, journal.
    detail::DestroyFxRuntimeWorkspace(binding->workspace_);
    detail::DestroyFxRuntimeArena(binding->arena_);
    for (std::uint32_t index = binding->plan_.expectedStringCount;
         index > 0;
         --index)
    {
        binding->entries_[index - 1].~ScriptStringJournalEntry();
    }
    binding->journal_->~ScriptStringJournal();

    binding->slab_ = nullptr;
    binding->slabCapacity_ = 0;
    binding->plan_ = {};
    binding->journal_ = nullptr;
    binding->entries_ = nullptr;
    binding->arena_ = nullptr;
    binding->workspace_ = nullptr;
    binding->arenaBacking_ = nullptr;
    binding->state_ = ZoneRuntimeStorageBinding::State::Destroyed;
    return Status::Success;
}
} // namespace db::zone_runtime_storage
