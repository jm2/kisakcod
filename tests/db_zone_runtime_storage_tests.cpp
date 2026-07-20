#define KISAK_FX_FASTFILE_ZONE_ADAPTER_TESTING 1

#include <database/db_zone_runtime_storage.h>

#include <EffectsCore/fx_fastfile_native_arena.h>
#include <EffectsCore/fx_fastfile_zone_adapter_disk32.h>
#include <database/db_script_string_journal.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
namespace runtime_storage = db::zone_runtime_storage;
namespace journal = db::script_string_journal;
namespace fastfile = fx::fastfile;
using Binding = runtime_storage::ZoneRuntimeStorageBinding;
using BindingPhase = runtime_storage::ZoneRuntimeStorageBindingPhase;
using CompositionExpectation =
    runtime_storage::ZoneRuntimeStorageCompositionExpectation;
using CompositionMode = runtime_storage::ZoneRuntimeStorageCompositionMode;
using Extent = runtime_storage::ZoneRuntimeStorageExtent;
using Plan = runtime_storage::ZoneRuntimeStoragePlan;
using Status = runtime_storage::ZoneRuntimeStorageStatus;

#define CHECK(condition)                                                       \
    do                                                                         \
    {                                                                          \
        if (!(condition))                                                      \
        {                                                                      \
            std::cerr << "check failed at line " << __LINE__ << ": "          \
                      << #condition << '\n';                                   \
            return false;                                                      \
        }                                                                      \
    } while (false)

class AlignedSlab final
{
public:
    explicit AlignedSlab(const std::size_t bytes) noexcept
        : bytes_(bytes),
          storage_(::operator new(
              bytes,
              std::align_val_t{
                  fastfile::kFxFastFileNativeArenaStorageAlignment},
              std::nothrow))
    {
    }
    ~AlignedSlab() noexcept
    {
        ::operator delete(
            storage_,
            std::align_val_t{
                fastfile::kFxFastFileNativeArenaStorageAlignment});
    }
    AlignedSlab(const AlignedSlab &) = delete;
    AlignedSlab &operator=(const AlignedSlab &) = delete;
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return storage_ != nullptr;
    }
    [[nodiscard]] void *data() const noexcept { return storage_; }
    [[nodiscard]] std::size_t size() const noexcept { return bytes_; }
    void Fill(const std::uint8_t value) noexcept
    {
        std::memset(storage_, value, bytes_);
    }
    [[nodiscard]] std::vector<std::uint8_t> Snapshot() const
    {
        const auto *const begin = static_cast<const std::uint8_t *>(storage_);
        return {begin, begin + bytes_};
    }

private:
    std::size_t bytes_ = 0;
    void *storage_ = nullptr;
};

[[nodiscard]] constexpr std::uint64_t AlignUp(
    const std::uint64_t value,
    const std::uint64_t alignment) noexcept
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}
[[nodiscard]] constexpr std::uint64_t End(const Extent &extent) noexcept
{
    return static_cast<std::uint64_t>(extent.offset) + extent.byteCount;
}
[[nodiscard]] bool Disjoint(const Extent &a, const Extent &b) noexcept
{
    return a.byteCount == 0 || b.byteCount == 0 || End(a) <= b.offset
        || End(b) <= a.offset;
}
[[nodiscard]] Plan SentinelPlan() noexcept
{
    Plan value{};
    value.scriptStringCapacity = UINT32_C(0x11111111);
    value.arenaBudget = UINT32_C(0x22222222);
    value.scriptStringJournal = {3, 4};
    value.scriptStringEntries = {5, 6};
    value.fxNativeArena = {7, 8};
    value.fxZoneAdapterWorkspace = {9, 10};
    value.fxArenaBacking = {11, 12};
    value.totalBytes = UINT32_C(0x33333333);
    return value;
}
[[nodiscard]] bool PlanStorage(
    const std::uint32_t strings,
    const std::uint64_t budget,
    Plan *const out) noexcept
{
    return runtime_storage::TryPlanZoneRuntimeStorage(strings, budget, out)
        == Status::Success;
}

bool TestPlanning()
{
    static_assert(std::is_standard_layout_v<Extent> && sizeof(Extent) == 8);
    static_assert(std::is_standard_layout_v<Plan> && sizeof(Plan) == 52);
    static_assert(!std::is_copy_constructible_v<Binding>);
    static_assert(!std::is_move_constructible_v<Binding>);
    static_assert(!std::is_trivially_copyable_v<Binding>);
    static_assert(sizeof(Binding) == (sizeof(void *) == 4 ? 0x58u : 0x80u));
    static_assert(
        sizeof(CompositionExpectation)
        == (sizeof(void *) == 4 ? 0x28u : 0x30u));
    static_assert(static_cast<std::uint8_t>(CompositionMode::Pristine) == 0);
    static_assert(static_cast<std::uint8_t>(CompositionMode::Placed) == 1);
    static_assert(static_cast<std::uint8_t>(CompositionMode::Active) == 2);
    static_assert(static_cast<std::uint8_t>(CompositionMode::Detached) == 3);
    static_assert(static_cast<std::uint8_t>(CompositionMode::Destroyed) == 4);
    static_assert(noexcept(
        runtime_storage::AuthenticateZoneRuntimeStorageBinding(
            std::declval<const Binding &>(), BindingPhase::Pristine)));
    using BindingAuthenticator = bool (*)(
        const Binding &, BindingPhase) noexcept;
    static_assert(std::is_same_v<
        decltype(&runtime_storage::AuthenticateZoneRuntimeStorageBinding),
        BindingAuthenticator>);
    static_assert(noexcept(
        runtime_storage::AuthenticateZoneRuntimeStorageComposition(
            std::declval<const Binding &>(),
            std::declval<const CompositionExpectation &>(),
            CompositionMode::Pristine)));
    using CompositionAuthenticator = bool (*)(
        const Binding &,
        const CompositionExpectation &,
        CompositionMode) noexcept;
    static_assert(std::is_same_v<
        decltype(&runtime_storage::AuthenticateZoneRuntimeStorageComposition),
        CompositionAuthenticator>);
    static_assert(
        sizeof(fastfile::FxFastFileZoneAdapterDisk32Workspace)
        == (sizeof(void *) == 4 ? 0xBD048u : 0xC34C8u));
    using WorkspaceAuthenticator = bool (
        fastfile::FxFastFileZoneAdapterDisk32Workspace::*)() const noexcept;
    static_assert(std::is_same_v<
        decltype(&fastfile::FxFastFileZoneAdapterDisk32Workspace::
                     readyForCompositionAuthentication),
        WorkspaceAuthenticator>);

    Plan zero{};
    CHECK(PlanStorage(0, 1, &zero));
    CHECK(zero.scriptStringCapacity == 0);
    CHECK(zero.scriptStringJournal.offset == 0);
    CHECK(zero.scriptStringJournal.byteCount
          == sizeof(journal::ScriptStringJournal));
    CHECK(zero.scriptStringEntries.byteCount == 0);
    CHECK(zero.fxNativeArena.byteCount
          == sizeof(fastfile::FxFastFileNativeArena));
    CHECK(zero.fxZoneAdapterWorkspace.byteCount
          == sizeof(fastfile::FxFastFileZoneAdapterDisk32Workspace));
    CHECK(zero.fxArenaBacking.byteCount == 1);
    CHECK(zero.totalBytes == End(zero.fxArenaBacking));

    Plan maximum{};
    CHECK(PlanStorage(journal::kMaxScriptStringJournalEntries, 257, &maximum));
    CHECK(maximum.scriptStringCapacity
          == journal::kMaxScriptStringJournalEntries);
    CHECK(maximum.scriptStringEntries.byteCount
          == journal::kMaxScriptStringJournalEntries
              * sizeof(journal::ScriptStringJournalEntry));
    for (const Plan *const plan : {&zero, &maximum})
    {
        const Extent extents[] = {
            plan->scriptStringJournal,
            plan->scriptStringEntries,
            plan->fxNativeArena,
            plan->fxZoneAdapterWorkspace,
            plan->fxArenaBacking};
        const std::size_t alignments[] = {
            alignof(journal::ScriptStringJournal),
            alignof(journal::ScriptStringJournalEntry),
            alignof(fastfile::FxFastFileNativeArena),
            alignof(fastfile::FxFastFileZoneAdapterDisk32Workspace),
            fastfile::kFxFastFileNativeArenaStorageAlignment};
        for (std::size_t i = 0; i < std::size(extents); ++i)
        {
            CHECK(extents[i].offset % alignments[i] == 0);
            CHECK(End(extents[i]) <= plan->totalBytes);
            for (std::size_t j = i + 1; j < std::size(extents); ++j)
                CHECK(Disjoint(extents[i], extents[j]));
        }
    }
    CHECK(zero.fxNativeArena.offset
          == AlignUp(zero.scriptStringEntries.offset,
                     alignof(fastfile::FxFastFileNativeArena)));
    CHECK(zero.fxZoneAdapterWorkspace.offset
          == AlignUp(End(zero.fxNativeArena),
                     alignof(fastfile::FxFastFileZoneAdapterDisk32Workspace)));
    CHECK(zero.fxArenaBacking.offset
          == AlignUp(End(zero.fxZoneAdapterWorkspace),
                     fastfile::kFxFastFileNativeArenaStorageAlignment));

    const std::uint64_t maxBudget = UINT32_MAX - zero.fxArenaBacking.offset;
    Plan largest{};
    CHECK(PlanStorage(0, maxBudget, &largest));
    CHECK(largest.totalBytes == UINT32_MAX);
    Plan output = SentinelPlan();
    const Plan sentinel = output;
    CHECK(runtime_storage::TryPlanZoneRuntimeStorage(
              0, maxBudget + 1u, &output)
          == Status::SizeOverflow);
    CHECK(output == sentinel);
    CHECK(runtime_storage::TryPlanZoneRuntimeStorage(
              0, static_cast<std::uint64_t>(UINT32_MAX) + 1u, &output)
          == Status::SizeOverflow);
    CHECK(output == sentinel);
    CHECK(runtime_storage::TryPlanZoneRuntimeStorage(0, 0, &output)
          == Status::InvalidArgument);
    CHECK(output == sentinel);
    CHECK(runtime_storage::TryPlanZoneRuntimeStorage(
              journal::kMaxScriptStringJournalEntries + 1u, 1, &output)
          == Status::InvalidCount);
    CHECK(output == sentinel);
    CHECK(runtime_storage::TryPlanZoneRuntimeStorage(0, 1, nullptr)
          == Status::InvalidArgument);
    const std::uintptr_t nearMaximumPlan =
        (std::numeric_limits<std::uintptr_t>::max)()
        - (alignof(Plan) - 1u);
    CHECK(runtime_storage::TryPlanZoneRuntimeStorage(
              0, 1, reinterpret_cast<Plan *>(nearMaximumPlan))
          == Status::SizeOverflow);
    alignas(Plan) std::uint8_t bytes[sizeof(Plan) + alignof(Plan)]{};
    CHECK(runtime_storage::TryPlanZoneRuntimeStorage(
              0, 1, reinterpret_cast<Plan *>(bytes + 1))
          == Status::InvalidArgument);
    CHECK(std::all_of(std::begin(bytes), std::end(bytes),
                      [](const std::uint8_t byte) { return byte == 0; }));
    return true;
}

bool TestBinding()
{
    Plan plan{};
    CHECK(PlanStorage(3, 257, &plan));
    AlignedSlab slab(plan.totalBytes);
    CHECK(slab);
    slab.Fill(0xA5);
    Binding binding;
    CHECK(runtime_storage::AuthenticateZoneRuntimeStorageBinding(
        binding, BindingPhase::Pristine));
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageBinding(
        binding, BindingPhase::Bound));
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageBinding(
        binding, BindingPhase::Destroyed));
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageBinding(
        binding, static_cast<BindingPhase>(UINT8_C(0xFF))));
    CHECK(runtime_storage::TryBindZoneRuntimeStorage(
              slab.data(), slab.size(), &plan, &binding)
          == Status::Success);
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageBinding(
        binding, BindingPhase::Pristine));
    CHECK(runtime_storage::AuthenticateZoneRuntimeStorageBinding(
        binding, BindingPhase::Bound));
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageBinding(
        binding, BindingPhase::Destroyed));
    CHECK(binding.bound() && !binding.destroyed());
    CHECK(binding.slab() == slab.data());
    CHECK(binding.slabCapacity() == slab.size());
    CHECK(binding.plan() && *binding.plan() == plan);
    CHECK(!binding.scriptStringJournal()->initialized());
    CHECK(binding.scriptStringJournal()->storage() == nullptr);
    CHECK(binding.scriptStringEntries() != nullptr);
    for (std::uint32_t i = 0; i < plan.scriptStringCapacity; ++i)
    {
        CHECK(binding.scriptStringEntries()[i].stringId == 0);
        CHECK(binding.scriptStringEntries()[i].state
              == journal::ScriptStringJournalEntryState::Released);
    }
    CHECK(!binding.fxNativeArena()->bound());
    CHECK(binding.fxZoneAdapterWorkspace()->frameDepth() == 0);
    CHECK(binding.fxZoneAdapterWorkspace()->phase()
          == fastfile::FxFastFileZoneAdapterDisk32Phase::Idle);
    CHECK(binding.fxArenaBacking()
          == static_cast<std::uint8_t *>(slab.data())
              + plan.fxArenaBacking.offset);
    CHECK(reinterpret_cast<std::uintptr_t>(binding.fxArenaBacking())
              % fastfile::kFxFastFileNativeArenaStorageAlignment
          == 0);

    CHECK(runtime_storage::TryDestroyZoneRuntimeStorage(&binding)
          == Status::Success);
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageBinding(
        binding, BindingPhase::Pristine));
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageBinding(
        binding, BindingPhase::Bound));
    CHECK(runtime_storage::AuthenticateZoneRuntimeStorageBinding(
        binding, BindingPhase::Destroyed));
    CHECK(!binding.bound() && binding.destroyed());
    CHECK(binding.slab() == nullptr && binding.plan() == nullptr);
    const auto afterDestroy = slab.Snapshot();
    CHECK(runtime_storage::TryDestroyZoneRuntimeStorage(&binding)
          == Status::AlreadyComplete);
    CHECK(slab.Snapshot() == afterDestroy);
    CHECK(runtime_storage::TryBindZoneRuntimeStorage(
              slab.data(), slab.size(), &plan, &binding)
          == Status::InvalidPhase);
    CHECK(slab.Snapshot() == afterDestroy);
    return true;
}

bool TestBindFailuresAndAliasing()
{
    Plan plan{};
    CHECK(PlanStorage(2, 64, &plan));
    {
        AlignedSlab slab(plan.totalBytes);
        CHECK(slab);
        Binding output;
        CHECK(runtime_storage::TryBindZoneRuntimeStorage(
                  slab.data(),
                  slab.size(),
                  reinterpret_cast<const Plan *>(&output),
                  &output)
              == Status::OverlappingStorage);
        CHECK(!output.bound() && !output.destroyed());

        const auto *const partial = reinterpret_cast<const Plan *>(
            reinterpret_cast<const std::uint8_t *>(&output)
            + alignof(Plan));
        CHECK(runtime_storage::TryBindZoneRuntimeStorage(
                  slab.data(), slab.size(), partial, &output)
              == Status::OverlappingStorage);
        CHECK(!output.bound() && !output.destroyed());
    }
    {
        AlignedSlab slab(plan.totalBytes);
        CHECK(slab);
        slab.Fill(0x31);
        const auto before = slab.Snapshot();
        Binding output;
        CHECK(runtime_storage::TryBindZoneRuntimeStorage(
                  slab.data(), slab.size() - 1u, &plan, &output)
              == Status::InsufficientCapacity);
        CHECK(!output.bound() && slab.Snapshot() == before);
    }
    {
        AlignedSlab slab(plan.totalBytes + 16u);
        CHECK(slab);
        slab.Fill(0x42);
        auto *const misaligned = static_cast<std::uint8_t *>(slab.data()) + 1;
        const std::vector<std::uint8_t> before(
            misaligned, misaligned + plan.totalBytes);
        Binding output;
        CHECK(runtime_storage::TryBindZoneRuntimeStorage(
                  misaligned, plan.totalBytes, &plan, &output)
              == Status::MisalignedStorage);
        CHECK(std::equal(before.begin(), before.end(), misaligned));
        CHECK(!output.bound());
    }
    {
        Binding output;
        const std::uintptr_t nearMaximum =
            (std::numeric_limits<std::uintptr_t>::max)() - 15u;
        CHECK(runtime_storage::TryBindZoneRuntimeStorage(
                  reinterpret_cast<void *>(nearMaximum),
                  32,
                  &plan,
                  &output)
              == Status::SizeOverflow);
        CHECK(!output.bound());
    }
    {
        const std::uintptr_t nearMaximumBinding =
            (std::numeric_limits<std::uintptr_t>::max)()
            - (alignof(Binding) - 1u);
        CHECK(runtime_storage::TryDestroyZoneRuntimeStorage(
                  reinterpret_cast<Binding *>(nearMaximumBinding))
              == Status::SizeOverflow);
    }
    {
        AlignedSlab first(plan.totalBytes);
        AlignedSlab second(plan.totalBytes);
        CHECK(first && second);
        second.Fill(0x53);
        Binding output;
        CHECK(runtime_storage::TryBindZoneRuntimeStorage(
                  first.data(), first.size(), &plan, &output)
              == Status::Success);
        const auto before = second.Snapshot();
        CHECK(runtime_storage::TryBindZoneRuntimeStorage(
                  second.data(), second.size(), &plan, &output)
              == Status::InvalidPhase);
        CHECK(second.Snapshot() == before && output.slab() == first.data());
        CHECK(runtime_storage::TryDestroyZoneRuntimeStorage(&output)
              == Status::Success);
    }
    {
        const std::size_t outputOffset = static_cast<std::size_t>(
            AlignUp(plan.totalBytes, alignof(Binding)));
        AlignedSlab slab(outputOffset + sizeof(Binding));
        CHECK(slab);
        slab.Fill(0x64);
        Binding *const output = ::new (
            static_cast<std::uint8_t *>(slab.data()) + outputOffset) Binding{};
        const auto before = slab.Snapshot();
        CHECK(runtime_storage::TryBindZoneRuntimeStorage(
                  slab.data(), slab.size(), &plan, output)
              == Status::OverlappingStorage);
        CHECK(slab.Snapshot() == before && !output->bound());
        output->~ZoneRuntimeStorageBinding();
    }
    {
        AlignedSlab slab(plan.totalBytes);
        CHECK(slab);
        Plan *const aliased = ::new (slab.data()) Plan(plan);
        Binding output;
        CHECK(runtime_storage::TryBindZoneRuntimeStorage(
                  slab.data(), slab.size(), aliased, &output)
              == Status::Success);
        CHECK(output.plan() && *output.plan() == plan);
        CHECK(runtime_storage::TryDestroyZoneRuntimeStorage(&output)
              == Status::Success);
    }
    {
        AlignedSlab slab(plan.totalBytes);
        CHECK(slab);
        Plan corrupt = plan;
        --corrupt.totalBytes;
        Plan *const aliased = ::new (slab.data()) Plan(corrupt);
        const auto before = slab.Snapshot();
        Binding output;
        CHECK(runtime_storage::TryBindZoneRuntimeStorage(
                  slab.data(), slab.size(), aliased, &output)
              == Status::InvalidPlan);
        CHECK(slab.Snapshot() == before && !output.bound());
        aliased->~ZoneRuntimeStoragePlan();
    }
    return true;
}

struct DestroyReentry final
{
    Binding *binding = nullptr;
    Status status = Status::Success;
};

bool ReenterDestroyFromSpanOracle(
    void *const context,
    const void *,
    const std::uint64_t) noexcept
{
    auto *const reentry = static_cast<DestroyReentry *>(context);
    reentry->status =
        runtime_storage::TryDestroyZoneRuntimeStorage(reentry->binding);
    return true;
}

bool TestLifetimeGates()
{
    {
        Plan plan{};
        CHECK(PlanStorage(1, 256, &plan));
        AlignedSlab slab(plan.totalBytes);
        CHECK(slab);
        Binding binding;
        CHECK(runtime_storage::TryBindZoneRuntimeStorage(
                  slab.data(), slab.size(), &plan, &binding)
              == Status::Success);
        auto *const arena = binding.fxNativeArena();
        CHECK(arena->TryBind(
                  binding.fxArenaBacking(), plan.arenaBudget, 71)
              == fastfile::FxFastFileNativeArenaStatus::Success);
        fastfile::FxFastFileNativeArenaTransaction transaction{};
        CHECK(arena->TryBeginTransaction(&transaction)
              == fastfile::FxFastFileNativeArenaStatus::Success);
        void *reserved = nullptr;
        CHECK(arena->TryReserve(transaction, 17, 16, &reserved)
              == fastfile::FxFastFileNativeArenaStatus::Success);
        CHECK(runtime_storage::TryDestroyZoneRuntimeStorage(&binding)
              == Status::InvalidPhase);
        CHECK(binding.bound() && arena->openTransactionDepth() == 1);
        CHECK(arena->TryCommit(transaction)
              == fastfile::FxFastFileNativeArenaStatus::Success);
        CHECK(runtime_storage::TryDestroyZoneRuntimeStorage(&binding)
              == Status::Success);
    }
    {
        Plan plan{};
        CHECK(PlanStorage(0, 256, &plan));
        AlignedSlab slab(plan.totalBytes);
        AlignedSlab foreign(plan.arenaBudget);
        CHECK(slab && foreign);
        Binding binding;
        CHECK(runtime_storage::TryBindZoneRuntimeStorage(
                  slab.data(), slab.size(), &plan, &binding)
              == Status::Success);
        auto *const arena = binding.fxNativeArena();
        CHECK(arena->TryBind(foreign.data(), foreign.size(), 72)
              == fastfile::FxFastFileNativeArenaStatus::Success);
        CHECK(runtime_storage::TryDestroyZoneRuntimeStorage(&binding)
              == Status::InvalidBinding);
        CHECK(binding.bound() && arena->bound());
        CHECK(arena->TryUnbind()
              == fastfile::FxFastFileNativeArenaStatus::Success);
        CHECK(runtime_storage::TryDestroyZoneRuntimeStorage(&binding)
              == Status::Success);
    }
    {
        Plan plan{};
        CHECK(PlanStorage(0, 256, &plan));
        AlignedSlab slab(plan.totalBytes);
        CHECK(slab);
        Binding binding;
        CHECK(runtime_storage::TryBindZoneRuntimeStorage(
                  slab.data(), slab.size(), &plan, &binding)
              == Status::Success);
        CHECK(binding.fxNativeArena()->TryBind(
                  binding.fxArenaBacking(), plan.arenaBudget, 73)
              == fastfile::FxFastFileNativeArenaStatus::Success);
        DestroyReentry reentry{&binding, Status::Success};
        fastfile::FxFastFileZoneAdapterCursor cursor{
            &reentry, ReenterDestroyFromSpanOracle};
        fastfile::FxEffectDefDisk32 header{};
        header.name.token.value = 1;
        CHECK(fastfile::TryBeginFxEffectDefZoneDisk32(
                  binding.fxZoneAdapterWorkspace(),
                  binding.fxNativeArena(),
                  cursor,
                  &header)
              == fastfile::FxFastFileZoneAdapterDisk32Status::Success);
        CHECK(reentry.status == Status::InvalidPhase);
        CHECK(binding.bound());
        CHECK(fastfile::AbortFxFastFileZoneAdapterDisk32(
                  binding.fxZoneAdapterWorkspace())
              == fastfile::FxFastFileZoneAdapterDisk32Status::Success);
        CHECK(runtime_storage::TryDestroyZoneRuntimeStorage(&binding)
              == Status::Success);
    }
    {
        Plan plan{};
        CHECK(PlanStorage(0, 32, &plan));
        AlignedSlab slab(plan.totalBytes);
        CHECK(slab);
        Binding binding;
        CHECK(runtime_storage::TryBindZoneRuntimeStorage(
                  slab.data(), slab.size(), &plan, &binding)
              == Status::Success);
        CHECK(binding.scriptStringEntries() == nullptr);
        auto *const value = binding.scriptStringJournal();
        const db::zone_load::ZoneLoadContextKey key{1, 2, 0};
        CHECK(journal::TryInitializeScriptStringJournal(
                  value, key, nullptr, 0, 0)
              == journal::ScriptStringJournalStatus::Success);
        CHECK(runtime_storage::TryDestroyZoneRuntimeStorage(&binding)
              == Status::InvalidPhase);
        CHECK(journal::TrySealScriptStringJournal(value, key)
              == journal::ScriptStringJournalStatus::Success);
        CHECK(journal::TryBeginScriptStringTransfer(value, key)
              == journal::ScriptStringJournalStatus::Success);
        CHECK(journal::TryTransferNextScriptString(value, key, {})
              == journal::ScriptStringJournalStatus::Success);
        CHECK(journal::TryPrepareScriptStringJournalCommit(value, key)
              == journal::ScriptStringJournalStatus::Success);
        journal::FinalizeScriptStringJournalCommit(*value);
        CHECK(value->phase() == journal::ScriptStringJournalPhase::Committed);
        CHECK(value->storage() == nullptr && value->capacity() == 0);
        CHECK(runtime_storage::TryDestroyZoneRuntimeStorage(&binding)
              == Status::Success);
    }
    {
        Plan plan{};
        CHECK(PlanStorage(0, 32, &plan));
        AlignedSlab slab(plan.totalBytes);
        CHECK(slab);
        Binding binding;
        CHECK(runtime_storage::TryBindZoneRuntimeStorage(
                  slab.data(), slab.size(), &plan, &binding)
              == Status::Success);
        auto *const value = binding.scriptStringJournal();
        journal::ScriptStringJournalTestAccess::SetFlags(value, 0x80);
        CHECK(!value->readyForDestruction());
        CHECK(runtime_storage::TryDestroyZoneRuntimeStorage(&binding)
              == Status::InvalidPhase);
        CHECK(binding.bound());
        journal::ScriptStringJournalTestAccess::SetFlags(value, 0x82);
        CHECK(!value->initialized() && value->callbackActive());
        CHECK(runtime_storage::TryDestroyZoneRuntimeStorage(&binding)
              == Status::InvalidPhase);
        CHECK(binding.bound());
        journal::ScriptStringJournalTestAccess::SetFlags(value, 0);
        CHECK(value->readyForDestruction());
        CHECK(runtime_storage::TryDestroyZoneRuntimeStorage(&binding)
              == Status::Success);
    }
    {
        Plan plan{};
        CHECK(PlanStorage(0, 32, &plan));
        AlignedSlab slab(plan.totalBytes);
        CHECK(slab);
        Binding binding;
        CHECK(runtime_storage::TryBindZoneRuntimeStorage(
                  slab.data(), slab.size(), &plan, &binding)
              == Status::Success);
        auto *const value = binding.scriptStringJournal();
        const db::zone_load::ZoneLoadContextKey key{2, 3, 0};
        CHECK(journal::TryInitializeScriptStringJournal(
                  value, key, nullptr, 0, 0)
              == journal::ScriptStringJournalStatus::Success);
        CHECK(journal::TryBeginScriptStringRollback(value, key)
              == journal::ScriptStringJournalStatus::Success);
        CHECK(value->phase() == journal::ScriptStringJournalPhase::RolledBack);
        CHECK(runtime_storage::TryDestroyZoneRuntimeStorage(&binding)
              == Status::Success);
    }
    return true;
}

bool TestCompositionAuthentication()
{
    Plan plan{};
    CHECK(PlanStorage(3, 256, &plan));
    AlignedSlab slab(plan.totalBytes);
    CHECK(slab);
    Binding binding;

    const CompositionExpectation empty{};
    CHECK(runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, empty, CompositionMode::Pristine));
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, empty, CompositionMode::Placed));
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding,
        empty,
        static_cast<CompositionMode>(UINT8_C(0xFF))));

    CHECK(runtime_storage::TryBindZoneRuntimeStorage(
              slab.data(), slab.size(), &plan, &binding)
          == Status::Success);
    auto *const journalValue = binding.scriptStringJournal();
    auto *const entries = binding.scriptStringEntries();
    const db::zone_load::ZoneLoadContextKey key{91, 3, 0};
    const std::uint64_t arenaIdentity = key.generation;
    CHECK(binding.fxNativeArena()->TryBind(
              binding.fxArenaBacking(), plan.arenaBudget, arenaIdentity)
          == fastfile::FxFastFileNativeArenaStatus::Success);

    const CompositionExpectation placed{
        key,
        arenaIdentity,
        journalValue,
        entries,
        plan.scriptStringCapacity,
        0};
    CHECK(runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, placed, CompositionMode::Placed));

    auto *const workspace = binding.fxZoneAdapterWorkspace();
    using WorkspaceAccess =
        fastfile::FxFastFileZoneAdapterDisk32WorkspaceTestAccess;
    CHECK(workspace->readyForCompositionAuthentication());
    const auto workspaceCompositionRejected = [&]() noexcept
    {
        return !workspace->readyForCompositionAuthentication()
            && !runtime_storage::AuthenticateZoneRuntimeStorageComposition(
                binding, placed, CompositionMode::Placed);
    };
    const auto restoreWorkspace = [&]() noexcept
    {
        WorkspaceAccess::RestoreStableState(workspace);
        return workspace->readyForCompositionAuthentication()
            && runtime_storage::AuthenticateZoneRuntimeStorageComposition(
                binding, placed, CompositionMode::Placed);
    };

    WorkspaceAccess::SetArena(workspace, binding.fxNativeArena());
    CHECK(workspaceCompositionRejected());
    CHECK(restoreWorkspace());

    WorkspaceAccess::SetCursor(workspace, {journalValue, nullptr});
    CHECK(workspaceCompositionRejected());
    CHECK(restoreWorkspace());
    WorkspaceAccess::SetCursor(
        workspace, {nullptr, ReenterDestroyFromSpanOracle});
    CHECK(workspaceCompositionRejected());
    CHECK(restoreWorkspace());

    WorkspaceAccess::SetRecordingCounts(workspace, 1, 0);
    CHECK(workspaceCompositionRejected());
    CHECK(restoreWorkspace());
    WorkspaceAccess::SetRecordingCounts(workspace, 0, 1);
    CHECK(workspaceCompositionRejected());
    CHECK(restoreWorkspace());

    WorkspaceAccess::SetHints(workspace, 1, 0);
    CHECK(workspaceCompositionRejected());
    CHECK(restoreWorkspace());
    WorkspaceAccess::SetHints(workspace, 0, 1);
    CHECK(workspaceCompositionRejected());
    CHECK(restoreWorkspace());

    WorkspaceAccess::CorruptDormantFrame(workspace);
    CHECK(workspaceCompositionRejected());
    CHECK(restoreWorkspace());
    WorkspaceAccess::SetOperating(workspace, true);
    CHECK(workspaceCompositionRejected());
    CHECK(restoreWorkspace());

    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, placed, CompositionMode::Pristine));
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, placed, CompositionMode::Active));
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, placed, CompositionMode::Detached));

    CompositionExpectation wrong = placed;
    ++wrong.key.generation;
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, wrong, CompositionMode::Placed));
    wrong = placed;
    ++wrong.arenaZoneIdentity;
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, wrong, CompositionMode::Placed));
    wrong = placed;
    wrong.journal = reinterpret_cast<const journal::ScriptStringJournal *>(
        entries);
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, wrong, CompositionMode::Placed));
    wrong = placed;
    wrong.entries = nullptr;
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, wrong, CompositionMode::Placed));
    wrong = placed;
    ++wrong.capacity;
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, wrong, CompositionMode::Placed));
    wrong = placed;
    ++wrong.expectedCount;
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, wrong, CompositionMode::Placed));

    fastfile::FxFastFileNativeArenaTransaction transaction{};
    CHECK(binding.fxNativeArena()->TryBeginTransaction(&transaction)
          == fastfile::FxFastFileNativeArenaStatus::Success);
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, placed, CompositionMode::Placed));
    CHECK(binding.fxNativeArena()->TryAbandon(transaction)
          == fastfile::FxFastFileNativeArenaStatus::Success);
    CHECK(runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, placed, CompositionMode::Placed));

    CompositionExpectation active = placed;
    active.expectedCount = 1;
    CHECK(journal::TryInitializeScriptStringJournal(
              journalValue,
              key,
              entries,
              plan.scriptStringCapacity,
              active.expectedCount)
          == journal::ScriptStringJournalStatus::Success);
    CHECK(runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, active, CompositionMode::Active));
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, placed, CompositionMode::Placed));
    wrong = active;
    --wrong.expectedCount;
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, wrong, CompositionMode::Active));
    wrong = active;
    ++wrong.expectedCount;
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, wrong, CompositionMode::Active));

    journal::ScriptStringJournalTestAccess::SetFlags(journalValue, 0x03);
    CHECK(journalValue->initialized() && journalValue->callbackActive());
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, active, CompositionMode::Active));
    journal::ScriptStringJournalTestAccess::SetFlags(journalValue, 0x05);
    CHECK(journalValue->initialized() && journalValue->poisoned());
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, active, CompositionMode::Active));
    journal::ScriptStringJournalTestAccess::SetFlags(journalValue, 0x01);
    journal::ScriptStringJournalTestAccess::SetCapacity(journalValue, 2);
    CHECK(journalValue->initialized());
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, active, CompositionMode::Active));
    journal::ScriptStringJournalTestAccess::SetCapacity(
        journalValue, plan.scriptStringCapacity);
    journal::ScriptStringJournalTestAccess::SetExpectedCount(
        journalValue, 0);
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, active, CompositionMode::Active));
    journal::ScriptStringJournalTestAccess::SetExpectedCount(
        journalValue, active.expectedCount);
    CHECK(runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, active, CompositionMode::Active));

    CHECK(journal::TryBeginScriptStringRollback(journalValue, key)
          == journal::ScriptStringJournalStatus::Success);
    CHECK(journalValue->phase()
          == journal::ScriptStringJournalPhase::RolledBack);
    CHECK(runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, active, CompositionMode::Detached));
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, active, CompositionMode::Active));
    wrong = active;
    ++wrong.expectedCount;
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, wrong, CompositionMode::Detached));

    CHECK(runtime_storage::TryDestroyZoneRuntimeStorage(&binding)
          == Status::Success);
    const CompositionExpectation destroyed{
        {},
        0,
        journalValue,
        entries,
        plan.scriptStringCapacity,
        active.expectedCount};
    CHECK(runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, destroyed, CompositionMode::Destroyed));
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, placed, CompositionMode::Destroyed));
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, destroyed, CompositionMode::Detached));
    wrong = destroyed;
    wrong.journal = nullptr;
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, wrong, CompositionMode::Destroyed));
    wrong = destroyed;
    wrong.entries = nullptr;
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, wrong, CompositionMode::Destroyed));
    wrong = destroyed;
    ++wrong.capacity;
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, wrong, CompositionMode::Destroyed));
    wrong = destroyed;
    wrong.expectedCount = 0;
    CHECK(runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, wrong, CompositionMode::Destroyed));
    wrong = destroyed;
    wrong.expectedCount = plan.scriptStringCapacity;
    CHECK(runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, wrong, CompositionMode::Destroyed));
    ++wrong.expectedCount;
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, wrong, CompositionMode::Destroyed));
    CHECK(binding.destroyed() && binding.slab() == nullptr
          && binding.plan() == nullptr
          && binding.scriptStringJournal() == nullptr
          && binding.scriptStringEntries() == nullptr
          && binding.fxNativeArena() == nullptr
          && binding.fxZoneAdapterWorkspace() == nullptr
          && binding.fxArenaBacking() == nullptr);
    return true;
}

bool TestZeroDemandCompositionAuthentication()
{
    Plan plan{};
    CHECK(PlanStorage(3, 256, &plan));
    CHECK(plan.scriptStringCapacity == 3);
    AlignedSlab slab(plan.totalBytes);
    CHECK(slab);
    Binding binding;
    CHECK(runtime_storage::TryBindZoneRuntimeStorage(
              slab.data(), slab.size(), &plan, &binding)
          == Status::Success);

    auto *const journalValue = binding.scriptStringJournal();
    auto *const entries = binding.scriptStringEntries();
    const db::zone_load::ZoneLoadContextKey key{92, 4, 0};
    const std::uint64_t arenaIdentity = key.generation;
    CHECK(entries != nullptr);
    CHECK(binding.fxNativeArena()->TryBind(
              binding.fxArenaBacking(), plan.arenaBudget, arenaIdentity)
          == fastfile::FxFastFileNativeArenaStatus::Success);

    const CompositionExpectation expectation{
        key,
        arenaIdentity,
        journalValue,
        entries,
        plan.scriptStringCapacity,
        0};
    CHECK(runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, expectation, CompositionMode::Placed));
    CHECK(journal::TryInitializeScriptStringJournal(
              journalValue,
              key,
              entries,
              plan.scriptStringCapacity,
              0)
          == journal::ScriptStringJournalStatus::Success);
    CHECK(journalValue->storage() == entries);
    CHECK(journalValue->capacity() == plan.scriptStringCapacity);
    CHECK(journalValue->expectedCount() == 0);
    CHECK(runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, expectation, CompositionMode::Active));

    CompositionExpectation wrong = expectation;
    wrong.entries = nullptr;
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, wrong, CompositionMode::Active));
    wrong = expectation;
    ++wrong.expectedCount;
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, wrong, CompositionMode::Active));

    CHECK(journal::TryBeginScriptStringRollback(journalValue, key)
          == journal::ScriptStringJournalStatus::Success);
    CHECK(runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, expectation, CompositionMode::Detached));
    wrong = expectation;
    ++wrong.expectedCount;
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, wrong, CompositionMode::Detached));

    CHECK(runtime_storage::TryDestroyZoneRuntimeStorage(&binding)
          == Status::Success);
    const CompositionExpectation destroyed{
        {},
        0,
        journalValue,
        entries,
        plan.scriptStringCapacity,
        0};
    CHECK(runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, destroyed, CompositionMode::Destroyed));
    wrong = destroyed;
    wrong.expectedCount = plan.scriptStringCapacity + 1u;
    CHECK(!runtime_storage::AuthenticateZoneRuntimeStorageComposition(
        binding, wrong, CompositionMode::Destroyed));
    return true;
}

bool TestMaximumEntries()
{
    Plan plan{};
    CHECK(PlanStorage(journal::kMaxScriptStringJournalEntries, 16, &plan));
    AlignedSlab slab(plan.totalBytes);
    CHECK(slab);
    Binding binding;
    CHECK(runtime_storage::TryBindZoneRuntimeStorage(
              slab.data(), slab.size(), &plan, &binding)
          == Status::Success);
    CHECK(binding.scriptStringEntries()[0].state
          == journal::ScriptStringJournalEntryState::Released);
    CHECK(binding.scriptStringEntries()[
              journal::kMaxScriptStringJournalEntries - 1u].state
          == journal::ScriptStringJournalEntryState::Released);
    CHECK(runtime_storage::TryDestroyZoneRuntimeStorage(&binding)
          == Status::Success);
    return true;
}
} // namespace

int main()
{
    return TestPlanning() && TestBinding() && TestBindFailuresAndAliasing()
            && TestLifetimeGates() && TestCompositionAuthentication()
            && TestZeroDemandCompositionAuthentication()
            && TestMaximumEntries()
        ? 0
        : 1;
}
