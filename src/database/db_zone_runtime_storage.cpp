#include <database/db_zone_runtime_storage.h>

#include <EffectsCore/fx_fastfile_native_arena.h>
#include <EffectsCore/fx_fastfile_zone_adapter_disk32.h>
#include <database/db_script_string_journal.h>

#include <limits>
#include <new>
#include <type_traits>

namespace db::zone_runtime_storage
{
namespace
{
using Arena = fx::fastfile::FxFastFileNativeArena;
using ArenaStatus = fx::fastfile::FxFastFileNativeArenaStatus;
using Journal = script_string_journal::ScriptStringJournal;
using JournalEntry = script_string_journal::ScriptStringJournalEntry;
using Phase = script_string_journal::ScriptStringJournalPhase;
using Status = ZoneRuntimeStorageStatus;
using Workspace = fx::fastfile::FxFastFileZoneAdapterDisk32Workspace;

static_assert(std::is_nothrow_default_constructible_v<Journal>);
static_assert(std::is_nothrow_destructible_v<Journal>);
static_assert(std::is_nothrow_default_constructible_v<JournalEntry>);
static_assert(std::is_nothrow_destructible_v<JournalEntry>);
static_assert(std::is_nothrow_default_constructible_v<Arena>);
static_assert(std::is_nothrow_destructible_v<Arena>);
static_assert(std::is_nothrow_default_constructible_v<Workspace>);
static_assert(std::is_nothrow_destructible_v<Workspace>);

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

[[nodiscard]] bool JournalCanBeDestroyed(
    const Journal &journal) noexcept
{
    if (journal.callbackActive() || journal.poisoned()
        || journal.storage() != nullptr || journal.capacity() != 0)
    {
        return false;
    }

    if (journal.initialized())
    {
        return journal.phase() == Phase::Committed
            || journal.phase() == Phase::RolledBack;
    }

    return journal.phase() == Phase::Staging
        && journal.key() == zone_load::ZoneLoadContextKey{}
        && journal.expectedCount() == 0 && journal.entryCount() == 0
        && journal.transferCursor() == 0 && journal.rollbackCursor() == 0;
}

[[nodiscard]] bool BindingPointersMatch(
    const ZoneRuntimeStorageBinding &binding) noexcept
{
    const ZoneRuntimeStoragePlan *const plan = binding.plan();
    void *const slab = binding.slab();
    return plan && slab
        && binding.scriptStringJournal()
            == ObjectAt<Journal>(slab, plan->scriptStringJournal)
        && binding.scriptStringEntries()
            == (plan->expectedStringCount == 0
                    ? nullptr
                    : ObjectAt<JournalEntry>(
                          slab, plan->scriptStringEntries))
        && binding.fxNativeArena()
            == ObjectAt<Arena>(slab, plan->fxNativeArena)
        && binding.fxZoneAdapterWorkspace()
            == ObjectAt<Workspace>(
                slab, plan->fxZoneAdapterWorkspace)
        && binding.fxArenaBacking()
            == AddressAt(slab, plan->fxArenaBacking);
}
} // namespace

bool ZoneRuntimeStorageBinding::isPristine() const noexcept
{
    return self_ == nullptr && slab_ == nullptr && slabCapacity_ == 0
        && plan_ == ZoneRuntimeStoragePlan{} && journal_ == nullptr
        && entries_ == nullptr && arena_ == nullptr && workspace_ == nullptr
        && arenaBacking_ == nullptr && state_ == State::Pristine;
}

bool ZoneRuntimeStorageBinding::isSelfAuthenticating(
    const State state) const noexcept
{
    return self_ == this && state_ == state;
}

bool ZoneRuntimeStorageBinding::bound() const noexcept
{
    return isSelfAuthenticating(State::Bound);
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
                            sizeof(Arena),
                            alignof(Arena),
                            &plan.fxNativeArena)
        || !TryAppendExtent(&cursor,
                            sizeof(Workspace),
                            alignof(Workspace),
                            &plan.fxZoneAdapterWorkspace)
        || !TryAppendExtent(
            &cursor,
            arenaBudget,
            fx::fastfile::kFxFastFileNativeArenaStorageAlignment,
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
    if (!PlanIsCanonical(planSnapshot))
        return Status::InvalidPlan;
    if (!IsAligned(
            slab, fx::fastfile::kFxFastFileNativeArenaStorageAlignment))
    {
        return slab ? Status::MisalignedStorage : Status::InvalidArgument;
    }
    if (!IsRangeRepresentable(slab, slabCapacity))
        return Status::SizeOverflow;
    if (slabCapacity < planSnapshot.totalBytes)
        return Status::InsufficientCapacity;
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
    Arena *const arena = ::new (ObjectAt<Arena>(
        slab, planSnapshot.fxNativeArena)) Arena{};
    Workspace *const workspace = ::new (ObjectAt<Workspace>(
        slab, planSnapshot.fxZoneAdapterWorkspace)) Workspace{};
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
    if (binding->destroyed())
        return Status::AlreadyComplete;
    if (!binding->isSelfAuthenticating(
            ZoneRuntimeStorageBinding::State::Bound))
    {
        return Status::InvalidBinding;
    }
    if (!PlanIsCanonical(binding->plan_)
        || !IsAligned(binding->slab_,
                      fx::fastfile::kFxFastFileNativeArenaStorageAlignment)
        || !IsRangeRepresentable(binding->slab_, binding->slabCapacity_)
        || binding->slabCapacity_ < binding->plan_.totalBytes
        || !IsRangeRepresentable(binding, sizeof(*binding))
        || RangesOverlap(binding->slab_,
                         binding->slabCapacity_,
                         binding,
                         sizeof(*binding))
        || !BindingPointersMatch(*binding))
    {
        return Status::InvalidBinding;
    }
    if (!JournalCanBeDestroyed(*binding->journal_))
        return binding->journal_->callbackActive()
            ? Status::Busy
            : Status::InvalidPhase;
    if (binding->workspace_->frameDepth() != 0
        || binding->workspace_->phase()
            != fx::fastfile::FxFastFileZoneAdapterDisk32Phase::Idle
        || binding->arena_->openTransactionDepth() != 0)
    {
        return Status::InvalidPhase;
    }

    if (binding->arena_->bound())
    {
        if (binding->arena_->zoneIdentity() == 0
            || binding->arena_->capacity() != binding->plan_.arenaBudget
            || binding->arena_->usedBytes() > binding->arena_->capacity()
            || binding->arena_->committedBytes()
                > binding->arena_->usedBytes())
        {
            return Status::InvalidBinding;
        }
        const ArenaStatus arenaStatus = binding->arena_->TryUnbind();
        if (arenaStatus == ArenaStatus::Busy)
            return Status::Busy;
        if (arenaStatus != ArenaStatus::Success)
            return Status::ArenaFailed;
    }
    else if (binding->arena_->zoneIdentity() != 0
             || binding->arena_->capacity() != 0
             || binding->arena_->usedBytes() != 0
             || binding->arena_->committedBytes() != 0)
    {
        return Status::InvalidBinding;
    }

    // Reverse of construction: workspace, arena, entries, journal.
    binding->workspace_->~FxFastFileZoneAdapterDisk32Workspace();
    binding->arena_->~FxFastFileNativeArena();
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
