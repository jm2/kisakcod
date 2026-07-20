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
    return reinterpret_cast<void *>(
        reinterpret_cast<std::uintptr_t>(slab) + extent.offset);
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

[[nodiscard]] bool IsNullKey(
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    return key == zone_load::ZoneLoadContextKey{};
}

[[nodiscard]] bool JournalIsPristine(const Journal &journal) noexcept
{
    return journal.readyForDestruction() && !journal.initialized()
        && IsNullKey(journal.key())
        && journal.phase()
            == script_string_journal::ScriptStringJournalPhase::Staging
        && journal.storage() == nullptr && journal.capacity() == 0
        && journal.expectedCount() == 0 && journal.entryCount() == 0
        && journal.transferCursor() == 0 && journal.rollbackCursor() == 0
        && !journal.callbackActive() && !journal.poisoned();
}

[[nodiscard]] bool JournalIsStableActive(
    const Journal &journal,
    const JournalEntry *const expectedEntries,
    const std::uint32_t expectedCount,
    const zone_load::ZoneLoadContextKey &expectedKey) noexcept
{
    if (!journal.initialized() || journal.callbackActive()
        || journal.poisoned() || journal.key() != expectedKey
        || journal.storage() != expectedEntries
        || journal.capacity() != expectedCount
        || journal.expectedCount() != expectedCount)
    {
        return false;
    }

    using Phase = script_string_journal::ScriptStringJournalPhase;
    switch (journal.phase())
    {
    case Phase::Staging:
    case Phase::Sealed:
    case Phase::Transferring:
    case Phase::Transferred:
    case Phase::CommitReady:
    case Phase::RollingBack:
        return true;
    case Phase::Committed:
    case Phase::RolledBack:
    default:
        return false;
    }
}

[[nodiscard]] bool JournalIsDetached(
    const Journal &journal,
    const std::uint32_t expectedCount,
    const zone_load::ZoneLoadContextKey &expectedKey) noexcept
{
    if (!journal.readyForDestruction() || !journal.initialized()
        || journal.key() != expectedKey || journal.storage() != nullptr
        || journal.capacity() != 0
        || journal.expectedCount() != expectedCount)
    {
        return false;
    }
    using Phase = script_string_journal::ScriptStringJournalPhase;
    return journal.phase() == Phase::Committed
        || journal.phase() == Phase::RolledBack;
}

} // namespace

bool ZoneRuntimeStorageBinding::isSelfAuthenticating(
    const State state) const noexcept
{
    return self_ == this && state_ == state;
}

bool ZoneRuntimeStorageBinding::hasCanonicalPlacementMetadata(
    const State state) const noexcept
{
    if (!isSelfAuthenticating(state)
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

bool ZoneRuntimeStorageBinding::hasCanonicalBoundMetadata() const noexcept
{
    return hasCanonicalPlacementMetadata(State::Bound);
}

bool ZoneRuntimeStorageBinding::bound() const noexcept
{
    return hasCanonicalBoundMetadata();
}

bool ZoneRuntimeStorageBinding::destroyed() const noexcept
{
    return hasCanonicalPlacementMetadata(State::Destroyed);
}

bool AuthenticateZoneRuntimeStorageBinding(
    const ZoneRuntimeStorageBinding &binding,
    const ZoneRuntimeStorageBindingPhase expectedPhase) noexcept
{
    switch (expectedPhase)
    {
    case ZoneRuntimeStorageBindingPhase::Pristine:
        return binding.isPristine();
    case ZoneRuntimeStorageBindingPhase::Bound:
        return binding.hasCanonicalBoundMetadata();
    case ZoneRuntimeStorageBindingPhase::Destroyed:
        return binding.destroyed();
    default:
        return false;
    }
}

bool AuthenticateZoneRuntimeStorageComposition(
    const ZoneRuntimeStorageBinding &binding,
    const ZoneRuntimeStorageCompositionExpectation &expectation,
    const ZoneRuntimeStorageCompositionMode mode) noexcept
{
    const bool expectationIsEmpty = IsNullKey(expectation.key)
        && expectation.arenaZoneIdentity == 0
        && expectation.journal == nullptr && expectation.entries == nullptr
        && expectation.capacity == 0 && expectation.expectedCount == 0;
    if (mode == ZoneRuntimeStorageCompositionMode::Pristine)
        return expectationIsEmpty && binding.isPristine();

    if (mode == ZoneRuntimeStorageCompositionMode::Destroyed)
    {
        return IsNullKey(expectation.key)
            && expectation.arenaZoneIdentity == 0 && binding.destroyed()
            && expectation.journal == binding.journal_
            && expectation.entries == binding.entries_
            && expectation.capacity == binding.plan_.expectedStringCount
            && expectation.expectedCount
                == binding.plan_.expectedStringCount;
    }

    if (!binding.hasCanonicalBoundMetadata()
        || !static_cast<bool>(expectation.key)
        || expectation.arenaZoneIdentity == 0
        || expectation.arenaZoneIdentity != expectation.key.generation
        || expectation.journal != binding.journal_
        || expectation.entries != binding.entries_
        || expectation.capacity != binding.plan_.expectedStringCount
        || expectation.expectedCount != binding.plan_.expectedStringCount
        || !detail::AuthenticateStableFxRuntimeStorage(
            binding.arena_,
            binding.workspace_,
            binding.arenaBacking_,
            binding.plan_.arenaBudget,
            expectation.arenaZoneIdentity))
    {
        return false;
    }

    switch (mode)
    {
    case ZoneRuntimeStorageCompositionMode::Placed:
        return JournalIsPristine(*binding.journal_);
    case ZoneRuntimeStorageCompositionMode::Active:
        return JournalIsStableActive(
            *binding.journal_,
            binding.entries_,
            binding.plan_.expectedStringCount,
            expectation.key);
    case ZoneRuntimeStorageCompositionMode::Detached:
        return JournalIsDetached(
            *binding.journal_,
            binding.plan_.expectedStringCount,
            expectation.key);
    case ZoneRuntimeStorageCompositionMode::Pristine:
    case ZoneRuntimeStorageCompositionMode::Destroyed:
    default:
        return false;
    }
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
    if (RangesOverlap(
            plan, sizeof(*plan), outBinding, sizeof(*outBinding)))
    {
        return Status::OverlappingStorage;
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
            slab, slabCapacity, outBinding, sizeof(*outBinding)))
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

    // Retain the complete immutable placement identity. The component object
    // lifetimes have ended, so ordinary public accessors remain Bound-only;
    // terminal composition authentication compares addresses and plan fields
    // without dereferencing the destroyed objects.
    binding->state_ = ZoneRuntimeStorageBinding::State::Destroyed;
    return Status::Success;
}
} // namespace db::zone_runtime_storage
