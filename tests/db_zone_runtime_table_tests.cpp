#include <database/db_zone_runtime_table.h>

#include <script/scr_string_transaction.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <type_traits>

void MyAssertHandler(const char *, int, int, const char *, ...)
{
    std::abort();
}

namespace script_string
{
AcquireResult TryAcquireOrdinaryStringOfSize(
    const char *,
    std::uint32_t,
    int) noexcept
{
    return {AcquireStatus::UnsafeFailure, 0};
}

TransferStatus TryTransferOrdinaryToDatabaseUser(std::uint32_t) noexcept
{
    return TransferStatus::UnsafeFailure;
}

ReleaseStatus TryRemoveOrdinaryReference(std::uint32_t) noexcept
{
    return ReleaseStatus::UnsafeFailure;
}

ReleaseStatus TryRemoveDatabaseUserReference(std::uint32_t) noexcept
{
    return ReleaseStatus::UnsafeFailure;
}
} // namespace script_string

namespace
{
namespace zone_load = db::zone_load;
namespace zone_runtime = db::zone_runtime;
namespace zone_slots = db::zone_slots;
namespace zone_ownership = db::zone_script_string_ownership;

using zone_load::TryBeginZoneLoadContextAbandonment;
using zone_load::TryFinishZoneLoadContextAbandonment;
using zone_load::TryInitializeZoneLoadContextSlot;
using zone_load::ZoneLoadCleanupCallbackStatus;
using zone_load::ZoneLoadCleanupCallbacks;
using zone_load::ZoneLoadCleanupOperation;
using zone_load::ZoneLoadContextKey;
using zone_load::ZoneLoadContextPhase;
using zone_load::ZoneLoadContextStatus;
using zone_load::ZoneLoadContextSlotTestAccess;
using zone_ownership::ZoneScriptStringOwnershipControllerTestAccess;
using zone_ownership::ZoneScriptStringOwnershipPhase;
using zone_ownership::ZoneScriptStringOwnershipStatus;
using zone_runtime::ProductionZoneRuntimeTable;
using zone_runtime::TryClaimZoneRuntimeGeneration;
using zone_runtime::TryGetZoneRuntimeEntry;
using zone_runtime::TryGetZoneRuntimeGeneration;
using zone_runtime::TryInitializeZoneRuntimeTable;
using zone_runtime::TryResetZoneRuntimeTerminalReceipt;
using zone_runtime::TryUnloadZoneRuntimeGeneration;
using zone_runtime::ZoneRuntimeEntry;
using zone_runtime::ZoneRuntimeGenerationView;
using zone_runtime::ZoneRuntimeTable;
using zone_runtime::ZoneRuntimeTableStatus;
using zone_runtime::ZoneRuntimeTableTestAccess;

int failures = 0;

void Check(
    const bool condition,
    const char *const expression,
    const int line)
{
    if (condition)
        return;
    std::fprintf(stderr, "line %d: check failed: %s\n", line, expression);
    ++failures;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

ZoneLoadCleanupCallbackStatus CompleteCleanup(
    void *const context,
    const ZoneLoadCleanupOperation) noexcept
{
    if (!context)
        return ZoneLoadCleanupCallbackStatus::UnsafeFailure;
    ++*static_cast<std::uint32_t *>(context);
    return ZoneLoadCleanupCallbackStatus::Success;
}

ZoneLoadCleanupCallbacks MakeCleanupCallbacks(
    std::uint32_t *const count) noexcept
{
    return {count, CompleteCleanup};
}

zone_ownership::ZoneScriptStringUnpublishStatus EnsureUnreachable(
    void *const context) noexcept
{
    return context
        ? zone_ownership::ZoneScriptStringUnpublishStatus::Success
        : zone_ownership::ZoneScriptStringUnpublishStatus::UnsafeFailure;
}

struct AdmissionProbe final
{
    ZoneRuntimeTable *table = nullptr;
    ZoneLoadContextKey key{};
    ZoneRuntimeTableStatus status = ZoneRuntimeTableStatus::InvalidState;
    ZoneRuntimeGenerationView view{};
};

void ObserveAdmittingController(void *const context) noexcept
{
    AdmissionProbe *const probe = static_cast<AdmissionProbe *>(context);
    if (!probe || !probe->table || !probe->key)
        return;
    probe->status = TryGetZoneRuntimeGeneration(
        probe->table,
        probe->key.slot,
        probe->key,
        &probe->view);
}

void AdmitNoop(void *) noexcept
{
}

constexpr std::array<ZoneLoadCleanupOperation, 6>
    kLiveUnloadOperations{
        ZoneLoadCleanupOperation::RemoveLiveAssetsAndReferences,
        ZoneLoadCleanupOperation::
            InvalidateAliasDirectStreamAndDelayState,
        ZoneLoadCleanupOperation::ReleaseGeometry,
        ZoneLoadCleanupOperation::
            TearDownNativeArenaWorkspaceAndSidecars,
        ZoneLoadCleanupOperation::FreePhysicalMemory,
        ZoneLoadCleanupOperation::RemoveLiveRegistryAndHandles,
    };

struct LiveUnloadDriver final
{
    ZoneRuntimeTable *table = nullptr;
    ZoneLoadContextKey key{};
    ZoneLoadCleanupOperation retryOperation =
        ZoneLoadCleanupOperation::ReleaseSlot;
    ZoneLoadCleanupOperation unsafeOperation =
        ZoneLoadCleanupOperation::ReleaseSlot;
    bool returnUnknown = false;
    bool retried = false;
    bool failed = false;
    bool attemptReentry = false;
    bool attemptedReentry = false;
    bool physicalMemoryFreed = false;
    bool usedContextAfterFree = false;
    ZoneRuntimeTableStatus lookupReentry =
        ZoneRuntimeTableStatus::Success;
    ZoneRuntimeTableStatus unloadReentry =
        ZoneRuntimeTableStatus::Success;
    ZoneRuntimeTableStatus resetReentry =
        ZoneRuntimeTableStatus::Success;
    ZoneRuntimeTableStatus claimReentry =
        ZoneRuntimeTableStatus::Success;
    std::array<ZoneLoadCleanupOperation, 12> operations{};
    std::size_t operationCount = 0;
};

ZoneLoadCleanupCallbackStatus PerformLiveUnload(
    void *const context,
    const ZoneLoadCleanupOperation operation) noexcept
{
    auto &driver = *static_cast<LiveUnloadDriver *>(context);
    CHECK(driver.table != nullptr);
    CHECK(driver.operationCount < driver.operations.size());
    if (driver.operationCount < driver.operations.size())
        driver.operations[driver.operationCount++] = operation;
    if (driver.physicalMemoryFreed)
        driver.usedContextAfterFree = true;
    if (operation == ZoneLoadCleanupOperation::FreePhysicalMemory)
        driver.physicalMemoryFreed = true;

    if (driver.attemptReentry && !driver.attemptedReentry && driver.table)
    {
        driver.attemptedReentry = true;
        ZoneRuntimeGenerationView view{};
        driver.lookupReentry = TryGetZoneRuntimeGeneration(
            driver.table, driver.key.slot, driver.key, &view);
        driver.unloadReentry = TryUnloadZoneRuntimeGeneration(
            driver.table,
            driver.key.slot,
            driver.key,
            {&driver, PerformLiveUnload});
        driver.resetReentry = TryResetZoneRuntimeTerminalReceipt(
            driver.table, driver.key.slot, driver.key);
        ZoneLoadContextKey claim = driver.key;
        driver.claimReentry = TryClaimZoneRuntimeGeneration(
            driver.table, driver.key.slot, &claim);
        CHECK(claim == driver.key);
        CHECK(!view);
    }

    if (driver.returnUnknown && !driver.failed)
    {
        driver.failed = true;
        return static_cast<ZoneLoadCleanupCallbackStatus>(UINT8_C(0xFF));
    }
    if (operation == driver.unsafeOperation && !driver.failed)
    {
        driver.failed = true;
        return ZoneLoadCleanupCallbackStatus::UnsafeFailure;
    }
    if (operation == driver.retryOperation && !driver.retried)
    {
        driver.retried = true;
        return ZoneLoadCleanupCallbackStatus::Retry;
    }
    return ZoneLoadCleanupCallbackStatus::Success;
}

bool MakeLiveGeneration(
    ZoneRuntimeTable &table,
    const std::uint32_t physicalSlot,
    ZoneLoadContextKey *const outKey,
    db::script_string_journal::ScriptStringJournal *const journal) noexcept
{
    if (!outKey || !journal)
        return false;
    if (TryInitializeZoneRuntimeTable(&table)
            != ZoneRuntimeTableStatus::Success
        || TryClaimZoneRuntimeGeneration(&table, physicalSlot, outKey)
            != ZoneRuntimeTableStatus::Success)
    {
        return false;
    }
    auto *const lifecycle =
        ZoneRuntimeTableTestAccess::Lifecycle(&table, physicalSlot);
    auto *const ownership =
        ZoneRuntimeTableTestAccess::Ownership(&table, physicalSlot);
    if (!lifecycle || !ownership)
        return false;
    return zone_ownership::TryBeginZoneScriptStringOwnership(
               ownership,
               lifecycle,
               *outKey,
               journal,
               nullptr,
               0,
               0)
                == ZoneScriptStringOwnershipStatus::Success
        && zone_ownership::TrySealZoneScriptStrings(ownership)
                == ZoneScriptStringOwnershipStatus::Success
        && zone_ownership::TryBeginZoneScriptStringTransfer(ownership)
                == ZoneScriptStringOwnershipStatus::Success
        && zone_ownership::TryTransferNextZoneScriptString(ownership)
                == ZoneScriptStringOwnershipStatus::Success
        && zone_ownership::TryPrepareZoneScriptStringCommit(ownership)
                == ZoneScriptStringOwnershipStatus::Success
        && zone_ownership::TryCommitZoneScriptStringsAndAdmit(
               ownership, {nullptr, AdmitNoop})
                == ZoneScriptStringOwnershipStatus::Success;
}

void TestLayoutNoexceptAndDefaultState()
{
    static_assert(zone_slots::kDefaultZoneSlot == 0);
    static_assert(zone_slots::kFirstUsableZoneSlot == 1);
    static_assert(zone_slots::kUsableZoneSlotCount == 32);
    static_assert(zone_slots::kPhysicalZoneSlotCount == 33);
    static_assert(alignof(ZoneRuntimeEntry) == 8);
    static_assert(alignof(ZoneRuntimeTable) == 8);
    static_assert(std::is_nothrow_default_constructible_v<ZoneRuntimeEntry>);
    static_assert(std::is_nothrow_destructible_v<ZoneRuntimeEntry>);
    static_assert(!std::is_copy_constructible_v<ZoneRuntimeEntry>);
    static_assert(!std::is_move_constructible_v<ZoneRuntimeEntry>);
    static_assert(std::is_nothrow_default_constructible_v<ZoneRuntimeTable>);
    static_assert(std::is_nothrow_destructible_v<ZoneRuntimeTable>);
    static_assert(!std::is_copy_constructible_v<ZoneRuntimeTable>);
    static_assert(!std::is_move_constructible_v<ZoneRuntimeTable>);
    static_assert(sizeof(ZoneRuntimeGenerationView) == 0x18);
    static_assert(sizeof(ZoneLoadCleanupCallbacks) == 2 * sizeof(void *));
    static_assert(sizeof(ZoneRuntimeTableStatus) == 1);
    static_assert(sizeof(ZoneScriptStringOwnershipPhase) == 1);
    static_assert(noexcept(TryUnloadZoneRuntimeGeneration(
        static_cast<ZoneRuntimeTable *>(nullptr),
        1,
        ZoneLoadContextKey{},
        ZoneLoadCleanupCallbacks{})));
    static_assert(noexcept(TryResetZoneRuntimeTerminalReceipt(
        static_cast<ZoneRuntimeTable *>(nullptr),
        1,
        ZoneLoadContextKey{})));

    ZoneRuntimeTable table{};
    CHECK(!table.initialized());
    CHECK(
        TryInitializeZoneRuntimeTable(nullptr)
        == ZoneRuntimeTableStatus::InvalidArgument);

    const ZoneRuntimeEntry *entry = nullptr;
    CHECK(
        TryGetZoneRuntimeEntry(&table, 1, &entry)
        == ZoneRuntimeTableStatus::InvalidState);
    CHECK(entry == nullptr);
    ZoneLoadContextKey key{};
    CHECK(
        TryClaimZoneRuntimeGeneration(&table, 1, &key)
        == ZoneRuntimeTableStatus::InvalidState);
    CHECK(!key);
    ZoneRuntimeGenerationView view{};
    CHECK(
        TryGetZoneRuntimeGeneration(&table, 1, key, &view)
        == ZoneRuntimeTableStatus::InvalidState);
    CHECK(!view);
    CHECK(
        TryGetZoneRuntimeEntry(&table, 1, nullptr)
        == ZoneRuntimeTableStatus::InvalidArgument);
    CHECK(
        TryClaimZoneRuntimeGeneration(&table, 1, nullptr)
        == ZoneRuntimeTableStatus::InvalidArgument);
    CHECK(
        TryGetZoneRuntimeGeneration(&table, 1, key, nullptr)
        == ZoneRuntimeTableStatus::InvalidArgument);
    CHECK(
        TryUnloadZoneRuntimeGeneration(nullptr, 1, key, {})
        == ZoneRuntimeTableStatus::InvalidArgument);
    CHECK(
        TryResetZoneRuntimeTerminalReceipt(nullptr, 1, key)
        == ZoneRuntimeTableStatus::InvalidArgument);

    ZoneRuntimeTable &production = ProductionZoneRuntimeTable();
    CHECK(&production == &ProductionZoneRuntimeTable());
    CHECK(&production != &table);
}

void TestAllPhysicalSlotsAndStableAddresses()
{
    ZoneRuntimeTable table{};
    CHECK(
        TryInitializeZoneRuntimeTable(&table)
        == ZoneRuntimeTableStatus::Success);
    CHECK(table.initialized());
    CHECK(
        TryInitializeZoneRuntimeTable(&table)
        == ZoneRuntimeTableStatus::Success);

    const ZoneRuntimeEntry *unchanged = nullptr;
    CHECK(
        TryGetZoneRuntimeEntry(
            &table,
            static_cast<std::uint32_t>(
                zone_slots::kFirstUsableZoneSlot),
            &unchanged)
        == ZoneRuntimeTableStatus::Success);
    CHECK(unchanged != nullptr);
    const ZoneRuntimeEntry *const sentinel = unchanged;
    CHECK(
        TryGetZoneRuntimeEntry(
            &table,
            static_cast<std::uint32_t>(zone_slots::kDefaultZoneSlot),
            &unchanged)
        == ZoneRuntimeTableStatus::InvalidSlot);
    CHECK(unchanged == sentinel);
    CHECK(
        TryGetZoneRuntimeEntry(
            &table,
            static_cast<std::uint32_t>(
                zone_slots::kPhysicalZoneSlotCount),
            &unchanged)
        == ZoneRuntimeTableStatus::InvalidSlot);
    CHECK(unchanged == sentinel);
    CHECK(
        TryGetZoneRuntimeEntry(&table, UINT32_MAX, &unchanged)
        == ZoneRuntimeTableStatus::InvalidSlot);
    CHECK(unchanged == sentinel);
    ZoneLoadContextKey invalidSlotKey{};
    CHECK(
        TryClaimZoneRuntimeGeneration(&table, 0, &invalidSlotKey)
        == ZoneRuntimeTableStatus::InvalidSlot);
    CHECK(!invalidSlotKey);
    CHECK(
        TryClaimZoneRuntimeGeneration(
            &table,
            static_cast<std::uint32_t>(
                zone_slots::kPhysicalZoneSlotCount),
            &invalidSlotKey)
        == ZoneRuntimeTableStatus::InvalidSlot);
    CHECK(!invalidSlotKey);

    std::array<const ZoneRuntimeEntry *,
        zone_slots::kUsableZoneSlotCount> addresses{};
    for (std::uint32_t physicalSlot = static_cast<std::uint32_t>(
             zone_slots::kFirstUsableZoneSlot);
         physicalSlot < zone_slots::kPhysicalZoneSlotCount;
         ++physicalSlot)
    {
        const std::size_t index = physicalSlot
            - zone_slots::kFirstUsableZoneSlot;
        CHECK(
            TryGetZoneRuntimeEntry(
                &table, physicalSlot, &addresses[index])
            == ZoneRuntimeTableStatus::Success);
        CHECK(addresses[index] != nullptr);
        CHECK(addresses[index]->lifecycle().initialized());
        CHECK(addresses[index]->lifecycle().slotIndex() == physicalSlot);
        CHECK(addresses[index]->lifecycle().generation() == 0);
        CHECK(addresses[index]->lifecycle().phase()
            == ZoneLoadContextPhase::Empty);
        CHECK(!addresses[index]->key());

        const ZoneRuntimeEntry *secondLookup = nullptr;
        CHECK(
            TryGetZoneRuntimeEntry(
                &table, physicalSlot, &secondLookup)
            == ZoneRuntimeTableStatus::Success);
        CHECK(secondLookup == addresses[index]);
    }

    for (std::size_t index = 1; index < addresses.size(); ++index)
    {
        const std::uintptr_t previous =
            reinterpret_cast<std::uintptr_t>(addresses[index - 1]);
        const std::uintptr_t current =
            reinterpret_cast<std::uintptr_t>(addresses[index]);
        CHECK(current - previous == sizeof(ZoneRuntimeEntry));
    }
}

void TestClaimAuthenticationAndAdjacentIsolation()
{
    ZoneRuntimeTable table{};
    CHECK(
        TryInitializeZoneRuntimeTable(&table)
        == ZoneRuntimeTableStatus::Success);

    ZoneLoadContextKey firstKey{};
    CHECK(
        TryClaimZoneRuntimeGeneration(&table, 1, &firstKey)
        == ZoneRuntimeTableStatus::Success);
    CHECK(firstKey.generation == 1);
    CHECK(firstKey.slot == 1);
    CHECK(firstKey.reserved == 0);

    ZoneRuntimeGenerationView firstView{};
    CHECK(
        TryGetZoneRuntimeGeneration(&table, 1, firstKey, &firstView)
        == ZoneRuntimeTableStatus::Success);
    CHECK(static_cast<bool>(firstView));
    CHECK(firstView.key == firstKey);
    CHECK(firstView.entry->lifecycle().slotIndex() == 1);
    CHECK(firstView.entry->lifecycle().phase()
        == ZoneLoadContextPhase::Loading);

    const ZoneRuntimeEntry *firstEntry = nullptr;
    const ZoneRuntimeEntry *secondEntry = nullptr;
    CHECK(
        TryGetZoneRuntimeEntry(&table, 1, &firstEntry)
        == ZoneRuntimeTableStatus::Success);
    CHECK(
        TryGetZoneRuntimeEntry(&table, 2, &secondEntry)
        == ZoneRuntimeTableStatus::Success);
    CHECK(firstView.key == firstEntry->key());
    CHECK(firstView.entry == firstEntry);
    CHECK(secondEntry->lifecycle().generation() == 0);
    CHECK(!secondEntry->key());

    ZoneLoadContextKey wrongSlotClaim = firstKey;
    CHECK(
        TryClaimZoneRuntimeGeneration(&table, 2, &wrongSlotClaim)
        == ZoneRuntimeTableStatus::StaleKey);
    CHECK(wrongSlotClaim == firstKey);
    CHECK(secondEntry->lifecycle().generation() == 0);
    CHECK(!secondEntry->key());

    CHECK(
        TryClaimZoneRuntimeGeneration(&table, 1, &firstKey)
        == ZoneRuntimeTableStatus::Success);
    CHECK(firstKey.generation == 1);
    ZoneLoadContextKey duplicateClaim{};
    CHECK(
        TryClaimZoneRuntimeGeneration(&table, 1, &duplicateClaim)
        == ZoneRuntimeTableStatus::InvalidPhase);
    CHECK(!duplicateClaim);
    ZoneLoadContextKey malformedClaim{0, 0, 0};
    CHECK(
        TryClaimZoneRuntimeGeneration(&table, 3, &malformedClaim)
        == ZoneRuntimeTableStatus::InvalidKey);
    CHECK(malformedClaim == ZoneLoadContextKey(0, 0, 0));

    ZoneLoadContextKey secondKey{};
    CHECK(
        TryClaimZoneRuntimeGeneration(&table, 2, &secondKey)
        == ZoneRuntimeTableStatus::Success);
    CHECK(secondKey.generation == 1);
    CHECK(secondKey.slot == 2);
    CHECK(firstEntry->key() == firstKey);
    CHECK(firstEntry->lifecycle().generation() == 1);

    ZoneRuntimeGenerationView unchanged = firstView;
    ZoneLoadContextKey stale = firstKey;
    ++stale.generation;
    CHECK(
        TryGetZoneRuntimeGeneration(&table, 1, stale, &unchanged)
        == ZoneRuntimeTableStatus::StaleKey);
    CHECK(unchanged.key == firstView.key);
    CHECK(unchanged.entry == firstView.entry);

    ZoneLoadContextKey crossSlot = firstKey;
    crossSlot.slot = 2;
    CHECK(
        TryGetZoneRuntimeGeneration(&table, 1, crossSlot, &unchanged)
        == ZoneRuntimeTableStatus::StaleKey);
    CHECK(unchanged.key == firstView.key);
    ZoneLoadContextKey malformed = firstKey;
    malformed.reserved = 1;
    CHECK(
        TryGetZoneRuntimeGeneration(&table, 1, malformed, &unchanged)
        == ZoneRuntimeTableStatus::InvalidKey);
    CHECK(unchanged.key == firstView.key);
    ZoneLoadContextKey reservedSlot{1, 0, 0};
    CHECK(
        TryGetZoneRuntimeGeneration(&table, 0, reservedSlot, &unchanged)
        == ZoneRuntimeTableStatus::InvalidSlot);
    CHECK(unchanged.key == firstView.key);
}

void TestGenerationAdvanceRejectsAba()
{
    ZoneRuntimeTable table{};
    CHECK(
        TryInitializeZoneRuntimeTable(&table)
        == ZoneRuntimeTableStatus::Success);

    ZoneLoadContextKey oldKey{};
    CHECK(
        TryClaimZoneRuntimeGeneration(&table, 32, &oldKey)
        == ZoneRuntimeTableStatus::Success);
    ZoneRuntimeGenerationView oldView{};
    CHECK(
        TryGetZoneRuntimeGeneration(&table, 32, oldKey, &oldView)
        == ZoneRuntimeTableStatus::Success);
    zone_load::ZoneLoadContextSlot *const lifecycle =
        ZoneRuntimeTableTestAccess::Lifecycle(&table, 32);
    CHECK(lifecycle != nullptr);

    CHECK(
        TryBeginZoneLoadContextAbandonment(
            lifecycle, oldKey)
        == ZoneLoadContextStatus::Success);
    std::uint32_t cleanupCount = 0;
    CHECK(
        TryFinishZoneLoadContextAbandonment(
            lifecycle,
            oldKey,
            MakeCleanupCallbacks(&cleanupCount))
        == ZoneLoadContextStatus::Success);
    CHECK(cleanupCount == 9);
    CHECK(lifecycle->phase() == ZoneLoadContextPhase::Empty);

    ZoneRuntimeGenerationView unchanged = oldView;
    CHECK(
        TryGetZoneRuntimeGeneration(&table, 32, oldKey, &unchanged)
        == ZoneRuntimeTableStatus::StaleKey);
    CHECK(unchanged.entry == oldView.entry);

    ZoneLoadContextKey newKey{};
    CHECK(
        TryClaimZoneRuntimeGeneration(&table, 32, &newKey)
        == ZoneRuntimeTableStatus::Success);
    CHECK(newKey.generation == oldKey.generation + 1);
    CHECK(newKey.slot == oldKey.slot);
    CHECK(
        TryGetZoneRuntimeGeneration(&table, 32, oldKey, &unchanged)
        == ZoneRuntimeTableStatus::StaleKey);

    ZoneRuntimeGenerationView newView{};
    CHECK(
        TryGetZoneRuntimeGeneration(&table, 32, newKey, &newView)
        == ZoneRuntimeTableStatus::Success);
    CHECK(newView.entry == oldView.entry);
    CHECK(oldView.key == oldKey);
    CHECK(newView.key == newKey);
    CHECK(newView.key != oldView.key);

    CHECK(
        TryInitializeZoneRuntimeTable(&table)
        == ZoneRuntimeTableStatus::InvalidState);
    CHECK(table.initialized());
    CHECK(newView.key == newKey);
    CHECK(
        TryClaimZoneRuntimeGeneration(&table, 32, &newKey)
        == ZoneRuntimeTableStatus::Success);
}

void TestPartialInitializationAndCorruptionFailClosed()
{
    ZoneRuntimeTable partial{};
    zone_load::ZoneLoadContextSlot *const partialLifecycle =
        ZoneRuntimeTableTestAccess::Lifecycle(&partial, 1);
    CHECK(partialLifecycle != nullptr);
    CHECK(
        TryInitializeZoneLoadContextSlot(partialLifecycle, 1)
        == ZoneLoadContextStatus::Success);
    CHECK(
        TryInitializeZoneRuntimeTable(&partial)
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(!partial.initialized());
    CHECK(
        TryInitializeZoneRuntimeTable(&partial)
        == ZoneRuntimeTableStatus::UnsafeFailure);
    ZoneLoadContextKey partialKey{};
    CHECK(
        TryClaimZoneRuntimeGeneration(&partial, 1, &partialKey)
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(!partialKey);

    ZoneRuntimeTable badHeader{};
    CHECK(
        TryInitializeZoneRuntimeTable(&badHeader)
        == ZoneRuntimeTableStatus::Success);
    ZoneRuntimeTableTestAccess::SetReserved(&badHeader, 1);
    const ZoneRuntimeEntry *unchanged = nullptr;
    CHECK(
        TryGetZoneRuntimeEntry(&badHeader, 1, &unchanged)
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(unchanged == nullptr);
    CHECK(!badHeader.initialized());
    CHECK(
        TryInitializeZoneRuntimeTable(&badHeader)
        == ZoneRuntimeTableStatus::UnsafeFailure);

    ZoneRuntimeTable badKey{};
    CHECK(
        TryInitializeZoneRuntimeTable(&badKey)
        == ZoneRuntimeTableStatus::Success);
    ZoneRuntimeTableTestAccess::SetKey(
        &badKey, 1, ZoneLoadContextKey{17, 1, 0});
    CHECK(
        TryGetZoneRuntimeEntry(&badKey, 1, &unchanged)
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(!badKey.initialized());
    ZoneLoadContextKey candidate{};
    CHECK(
        TryClaimZoneRuntimeGeneration(&badKey, 1, &candidate)
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(!candidate);
}

void TestHiddenCorruptionAndCleanupReentryFailClosed()
{
    constexpr std::uint8_t kInitializedFlag = UINT8_C(1) << 0;
    constexpr std::uint8_t kCleanupActiveFlag = UINT8_C(1) << 1;
    constexpr std::uint8_t kCleanupPoisonedFlag = UINT8_C(1) << 2;

    ZoneRuntimeTable hiddenController{};
    auto *const hiddenOwnership =
        ZoneRuntimeTableTestAccess::Ownership(&hiddenController, 1);
    CHECK(hiddenOwnership != nullptr);
    db::script_string_journal::ScriptStringJournalEntry hiddenEntry{};
    ZoneScriptStringOwnershipControllerTestAccess::SetStorage(
        hiddenOwnership, &hiddenEntry);
    CHECK(
        TryInitializeZoneRuntimeTable(&hiddenController)
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(!hiddenController.initialized());

    ZoneRuntimeTable hiddenLifecycle{};
    auto *const reservedLifecycle =
        ZoneRuntimeTableTestAccess::Lifecycle(&hiddenLifecycle, 0);
    CHECK(reservedLifecycle != nullptr);
    ZoneLoadContextSlotTestAccess::SetFlags(
        reservedLifecycle, UINT8_C(0x80));
    CHECK(
        TryInitializeZoneRuntimeTable(&hiddenLifecycle)
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(!hiddenLifecycle.initialized());

    ZoneRuntimeTable cleanup{};
    CHECK(
        TryInitializeZoneRuntimeTable(&cleanup)
        == ZoneRuntimeTableStatus::Success);
    ZoneLoadContextKey cleanupKey{};
    CHECK(
        TryClaimZoneRuntimeGeneration(&cleanup, 1, &cleanupKey)
        == ZoneRuntimeTableStatus::Success);
    auto *const cleanupLifecycle =
        ZoneRuntimeTableTestAccess::Lifecycle(&cleanup, 1);
    CHECK(cleanupLifecycle != nullptr);
    CHECK(
        TryBeginZoneLoadContextAbandonment(
            cleanupLifecycle, cleanupKey)
        == ZoneLoadContextStatus::Success);
    ZoneLoadContextSlotTestAccess::SetFlags(
        cleanupLifecycle,
        kInitializedFlag | kCleanupActiveFlag);
    ZoneRuntimeGenerationView cleanupView{};
    CHECK(
        TryGetZoneRuntimeGeneration(
            &cleanup, 1, cleanupKey, &cleanupView)
        == ZoneRuntimeTableStatus::Busy);
    CHECK(!cleanupView);
    CHECK(cleanup.initialized());
    ZoneLoadContextSlotTestAccess::SetFlags(
        cleanupLifecycle,
        kInitializedFlag | kCleanupPoisonedFlag);
    CHECK(
        TryGetZoneRuntimeGeneration(
            &cleanup, 1, cleanupKey, &cleanupView)
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(!cleanupView);
    CHECK(!cleanup.initialized());

    ZoneRuntimeTable controllerCallback{};
    CHECK(
        TryInitializeZoneRuntimeTable(&controllerCallback)
        == ZoneRuntimeTableStatus::Success);
    ZoneLoadContextKey callbackKey{};
    CHECK(
        TryClaimZoneRuntimeGeneration(
            &controllerCallback, 2, &callbackKey)
        == ZoneRuntimeTableStatus::Success);
    auto *const callbackOwnership =
        ZoneRuntimeTableTestAccess::Ownership(
            &controllerCallback, 2);
    CHECK(callbackOwnership != nullptr);
    ZoneScriptStringOwnershipControllerTestAccess::SetKey(
        callbackOwnership, callbackKey);
    ZoneScriptStringOwnershipControllerTestAccess::SetPhase(
        callbackOwnership,
        ZoneScriptStringOwnershipPhase::UnpublishingCallback);
    ZoneRuntimeGenerationView callbackView{};
    CHECK(
        TryGetZoneRuntimeGeneration(
            &controllerCallback, 2, callbackKey, &callbackView)
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(!callbackView);
    CHECK(!controllerCallback.initialized());

    ZoneRuntimeTable poisonedController{};
    CHECK(
        TryInitializeZoneRuntimeTable(&poisonedController)
        == ZoneRuntimeTableStatus::Success);
    ZoneLoadContextKey poisonedKey{};
    CHECK(
        TryClaimZoneRuntimeGeneration(
            &poisonedController, 3, &poisonedKey)
        == ZoneRuntimeTableStatus::Success);
    auto *const poisonedOwnership =
        ZoneRuntimeTableTestAccess::Ownership(
            &poisonedController, 3);
    CHECK(poisonedOwnership != nullptr);
    ZoneScriptStringOwnershipControllerTestAccess::SetKey(
        poisonedOwnership, poisonedKey);
    ZoneScriptStringOwnershipControllerTestAccess::SetPhase(
        poisonedOwnership,
        ZoneScriptStringOwnershipPhase::UnsafeFailure);
    ZoneRuntimeGenerationView poisonedView{};
    CHECK(
        TryGetZoneRuntimeGeneration(
            &poisonedController, 3, poisonedKey, &poisonedView)
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(!poisonedView);
    CHECK(!poisonedController.initialized());
}

void TestControllerPhaseAndSerializerMatrix()
{
    ZoneRuntimeTable table{};
    CHECK(
        TryInitializeZoneRuntimeTable(&table)
        == ZoneRuntimeTableStatus::Success);
    ZoneLoadContextKey key{};
    CHECK(
        TryClaimZoneRuntimeGeneration(&table, 4, &key)
        == ZoneRuntimeTableStatus::Success);
    auto *const lifecycle =
        ZoneRuntimeTableTestAccess::Lifecycle(&table, 4);
    auto *const ownership =
        ZoneRuntimeTableTestAccess::Ownership(&table, 4);
    CHECK(lifecycle != nullptr);
    CHECK(ownership != nullptr);

    db::script_string_journal::ScriptStringJournal journal{};
    CHECK(
        zone_ownership::TryBeginZoneScriptStringOwnership(
            ownership,
            lifecycle,
            key,
            &journal,
            nullptr,
            0,
            0)
        == ZoneScriptStringOwnershipStatus::Success);
    CHECK(ownership->serializerRetained());
    ZoneRuntimeGenerationView stagingView{};
    CHECK(
        TryGetZoneRuntimeGeneration(&table, 4, key, &stagingView)
        == ZoneRuntimeTableStatus::Success);
    CHECK(stagingView.entry != nullptr);

    CHECK(
        zone_ownership::TrySealZoneScriptStrings(ownership)
        == ZoneScriptStringOwnershipStatus::Success);
    CHECK(
        zone_ownership::TryBeginZoneScriptStringTransfer(ownership)
        == ZoneScriptStringOwnershipStatus::Success);
    CHECK(
        zone_ownership::TryTransferNextZoneScriptString(ownership)
        == ZoneScriptStringOwnershipStatus::Success);
    CHECK(
        zone_ownership::TryPrepareZoneScriptStringCommit(ownership)
        == ZoneScriptStringOwnershipStatus::Success);

    AdmissionProbe probe{&table, key};
    CHECK(
        zone_ownership::TryCommitZoneScriptStringsAndAdmit(
            ownership,
            {&probe, ObserveAdmittingController})
        == ZoneScriptStringOwnershipStatus::Success);
    CHECK(probe.status == ZoneRuntimeTableStatus::Busy);
    CHECK(!probe.view);
    CHECK(ownership->phase() == ZoneScriptStringOwnershipPhase::Live);
    CHECK(!ownership->serializerRetained());
    CHECK(lifecycle->phase() == ZoneLoadContextPhase::Live);

    ZoneRuntimeGenerationView liveView{};
    CHECK(
        TryGetZoneRuntimeGeneration(&table, 4, key, &liveView)
        == ZoneRuntimeTableStatus::Success);
    CHECK(liveView.entry == stagingView.entry);

    ZoneRuntimeTable crossPhase{};
    CHECK(
        TryInitializeZoneRuntimeTable(&crossPhase)
        == ZoneRuntimeTableStatus::Success);
    ZoneLoadContextKey crossPhaseKey{};
    CHECK(
        TryClaimZoneRuntimeGeneration(
            &crossPhase, 5, &crossPhaseKey)
        == ZoneRuntimeTableStatus::Success);
    auto *const crossPhaseOwnership =
        ZoneRuntimeTableTestAccess::Ownership(&crossPhase, 5);
    CHECK(crossPhaseOwnership != nullptr);
    ZoneScriptStringOwnershipControllerTestAccess::SetKey(
        crossPhaseOwnership, crossPhaseKey);
    ZoneScriptStringOwnershipControllerTestAccess::SetPhase(
        crossPhaseOwnership, ZoneScriptStringOwnershipPhase::Live);
    ZoneRuntimeGenerationView crossPhaseView{};
    CHECK(
        TryGetZoneRuntimeGeneration(
            &crossPhase, 5, crossPhaseKey, &crossPhaseView)
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(!crossPhaseView);
    CHECK(!crossPhase.initialized());
}

void TestLiveUnloadRetryResetReuseAndAba()
{
    for (const ZoneLoadCleanupOperation retryOperation :
         kLiveUnloadOperations)
    {
        ZoneRuntimeTable table{};
        db::script_string_journal::ScriptStringJournal journal{};
        ZoneLoadContextKey oldKey{};
        CHECK(MakeLiveGeneration(table, 6, &oldKey, &journal));
        ZoneRuntimeGenerationView oldView{};
        CHECK(
            TryGetZoneRuntimeGeneration(&table, 6, oldKey, &oldView)
            == ZoneRuntimeTableStatus::Success);
        ZoneLoadContextKey malformed = oldKey;
        malformed.reserved = 1;
        CHECK(
            TryUnloadZoneRuntimeGeneration(&table, 6, malformed, {})
            == ZoneRuntimeTableStatus::InvalidKey);
        CHECK(
            TryResetZoneRuntimeTerminalReceipt(&table, 6, malformed)
            == ZoneRuntimeTableStatus::InvalidKey);
        ZoneLoadContextKey crossSlot = oldKey;
        crossSlot.slot = 7;
        CHECK(
            TryUnloadZoneRuntimeGeneration(&table, 6, crossSlot, {})
            == ZoneRuntimeTableStatus::StaleKey);
        CHECK(
            TryResetZoneRuntimeTerminalReceipt(&table, 6, crossSlot)
            == ZoneRuntimeTableStatus::StaleKey);
        CHECK(
            TryUnloadZoneRuntimeGeneration(
                &table, 0, ZoneLoadContextKey{1, 0, 0}, {})
            == ZoneRuntimeTableStatus::InvalidSlot);
        const ZoneRuntimeEntry *adjacent = nullptr;
        CHECK(
            TryGetZoneRuntimeEntry(&table, 7, &adjacent)
            == ZoneRuntimeTableStatus::Success);
        CHECK(adjacent != nullptr);

        LiveUnloadDriver driver{};
        driver.table = &table;
        driver.key = oldKey;
        driver.retryOperation = retryOperation;
        driver.attemptReentry = true;
        const ZoneLoadCleanupCallbacks callbacks{
            &driver,
            PerformLiveUnload};
        CHECK(
            TryUnloadZoneRuntimeGeneration(
                &table, 6, oldKey, callbacks)
            == ZoneRuntimeTableStatus::Retry);
        auto *const lifecycle =
            ZoneRuntimeTableTestAccess::Lifecycle(&table, 6);
        auto *const ownership =
            ZoneRuntimeTableTestAccess::Ownership(&table, 6);
        CHECK(lifecycle != nullptr);
        CHECK(ownership != nullptr);
        CHECK(lifecycle->phase() == ZoneLoadContextPhase::Abandoning);
        CHECK(
            lifecycle->terminalKind()
            == zone_load::ZoneLoadTerminalKind::Unloaded);
        CHECK(lifecycle->nextCleanupOperation() == retryOperation);
        CHECK(
            ownership->phase()
            == ZoneScriptStringOwnershipPhase::Unloading);
        CHECK(ownership->serializerRetained());
        CHECK(driver.attemptedReentry);
        CHECK(driver.lookupReentry == ZoneRuntimeTableStatus::Busy);
        CHECK(driver.unloadReentry == ZoneRuntimeTableStatus::Busy);
        CHECK(driver.resetReentry == ZoneRuntimeTableStatus::Busy);
        CHECK(driver.claimReentry == ZoneRuntimeTableStatus::Busy);

        LiveUnloadDriver swapped{};
        swapped.table = &table;
        swapped.key = oldKey;
        const std::size_t beforeMismatch = driver.operationCount;
        CHECK(
            TryUnloadZoneRuntimeGeneration(
                &table,
                6,
                oldKey,
                {&swapped, PerformLiveUnload})
            == ZoneRuntimeTableStatus::InvalidState);
        CHECK(driver.operationCount == beforeMismatch);
        CHECK(lifecycle->nextCleanupOperation() == retryOperation);
        CHECK(ownership->serializerRetained());

        CHECK(
            TryUnloadZoneRuntimeGeneration(
                &table, 6, oldKey, callbacks)
            == ZoneRuntimeTableStatus::Success);
        CHECK(
            ownership->phase()
            == ZoneScriptStringOwnershipPhase::Unloaded);
        CHECK(!ownership->serializerRetained());
        CHECK(lifecycle->phase() == ZoneLoadContextPhase::Empty);
        CHECK(
            lifecycle->terminalKind()
            == zone_load::ZoneLoadTerminalKind::Unloaded);
        CHECK(driver.physicalMemoryFreed);
        CHECK(driver.usedContextAfterFree);
        CHECK(driver.operationCount == kLiveUnloadOperations.size() + 1);
        std::size_t retryIndex = 0;
        while (retryIndex < kLiveUnloadOperations.size()
            && kLiveUnloadOperations[retryIndex] != retryOperation)
        {
            ++retryIndex;
        }
        CHECK(retryIndex < kLiveUnloadOperations.size());
        std::size_t observed = 0;
        for (std::size_t index = 0; index <= retryIndex; ++index)
            CHECK(driver.operations[observed++] == kLiveUnloadOperations[index]);
        for (std::size_t index = retryIndex;
             index < kLiveUnloadOperations.size();
             ++index)
        {
            CHECK(driver.operations[observed++] == kLiveUnloadOperations[index]);
        }
        CHECK(observed == driver.operationCount);
        CHECK(
            TryUnloadZoneRuntimeGeneration(&table, 6, oldKey, {})
            == ZoneRuntimeTableStatus::Success);
        CHECK(driver.operationCount == kLiveUnloadOperations.size() + 1);

        ZoneLoadContextKey blockedClaim{};
        CHECK(
            TryClaimZoneRuntimeGeneration(&table, 6, &blockedClaim)
            == ZoneRuntimeTableStatus::InvalidState);
        CHECK(!blockedClaim);
        CHECK(
            TryResetZoneRuntimeTerminalReceipt(&table, 6, oldKey)
            == ZoneRuntimeTableStatus::Success);
        CHECK(ownership->isEmptyCanonical());
        CHECK(table.initialized());
        CHECK(lifecycle->generation() == oldKey.generation);
        CHECK(
            lifecycle->terminalKind()
            == zone_load::ZoneLoadTerminalKind::Unloaded);
        CHECK(
            TryResetZoneRuntimeTerminalReceipt(&table, 6, oldKey)
            == ZoneRuntimeTableStatus::Success);
        CHECK(
            TryUnloadZoneRuntimeGeneration(&table, 6, oldKey, {})
            == ZoneRuntimeTableStatus::Success);

        ZoneLoadContextKey newKey{};
        CHECK(
            TryClaimZoneRuntimeGeneration(&table, 6, &newKey)
            == ZoneRuntimeTableStatus::Success);
        CHECK(newKey.generation == oldKey.generation + 1);
        CHECK(newKey.slot == oldKey.slot);
        CHECK(oldView.key == oldKey);
        ZoneRuntimeGenerationView newView{};
        CHECK(
            TryGetZoneRuntimeGeneration(&table, 6, newKey, &newView)
            == ZoneRuntimeTableStatus::Success);
        CHECK(newView.entry == oldView.entry);
        CHECK(newView.key != oldView.key);
        CHECK(
            TryUnloadZoneRuntimeGeneration(&table, 6, oldKey, {})
            == ZoneRuntimeTableStatus::StaleKey);
        CHECK(
            TryResetZoneRuntimeTerminalReceipt(&table, 6, oldKey)
            == ZoneRuntimeTableStatus::StaleKey);
        CHECK(adjacent->lifecycle().generation() == 0);
        CHECK(!adjacent->key());
    }
}

void TestAbandonedReceiptResetAndGenerationExhaustion()
{
    ZoneRuntimeTable table{};
    CHECK(
        TryInitializeZoneRuntimeTable(&table)
        == ZoneRuntimeTableStatus::Success);
    ZoneLoadContextKey oldKey{};
    CHECK(
        TryClaimZoneRuntimeGeneration(&table, 8, &oldKey)
        == ZoneRuntimeTableStatus::Success);
    auto *const lifecycle =
        ZoneRuntimeTableTestAccess::Lifecycle(&table, 8);
    auto *const ownership =
        ZoneRuntimeTableTestAccess::Ownership(&table, 8);
    CHECK(lifecycle != nullptr);
    CHECK(ownership != nullptr);
    db::script_string_journal::ScriptStringJournal journal{};
    CHECK(
        zone_ownership::TryBeginZoneScriptStringOwnership(
            ownership,
            lifecycle,
            oldKey,
            &journal,
            nullptr,
            0,
            0)
        == ZoneScriptStringOwnershipStatus::Success);
    std::uint32_t cleanupCount = 0;
    CHECK(
        zone_ownership::TryBeginZoneScriptStringRollback(
            ownership,
            {&cleanupCount, EnsureUnreachable, CompleteCleanup})
        == ZoneScriptStringOwnershipStatus::Success);
    CHECK(
        zone_ownership::TryFinishZoneScriptStringAbandonment(ownership)
        == ZoneScriptStringOwnershipStatus::Success);
    CHECK(
        ownership->phase()
        == ZoneScriptStringOwnershipPhase::Abandoned);
    CHECK(cleanupCount == 8);
    ZoneLoadContextKey blocked{};
    CHECK(
        TryClaimZoneRuntimeGeneration(&table, 8, &blocked)
        == ZoneRuntimeTableStatus::InvalidState);
    CHECK(!blocked);
    CHECK(
        TryUnloadZoneRuntimeGeneration(&table, 8, oldKey, {})
        == ZoneRuntimeTableStatus::InvalidPhase);
    CHECK(
        TryResetZoneRuntimeTerminalReceipt(&table, 8, oldKey)
        == ZoneRuntimeTableStatus::Success);
    CHECK(ownership->isEmptyCanonical());
    CHECK(
        TryResetZoneRuntimeTerminalReceipt(&table, 8, oldKey)
        == ZoneRuntimeTableStatus::Success);
    CHECK(lifecycle->generation() == oldKey.generation);
    CHECK(
        lifecycle->terminalKind()
        == zone_load::ZoneLoadTerminalKind::Abandoned);

    ZoneLoadContextKey nextKey{};
    CHECK(
        TryClaimZoneRuntimeGeneration(&table, 8, &nextKey)
        == ZoneRuntimeTableStatus::Success);
    CHECK(nextKey.generation == oldKey.generation + 1);
    CHECK(
        TryResetZoneRuntimeTerminalReceipt(&table, 8, oldKey)
        == ZoneRuntimeTableStatus::StaleKey);

    ZoneRuntimeTable preBegin{};
    CHECK(
        TryInitializeZoneRuntimeTable(&preBegin)
        == ZoneRuntimeTableStatus::Success);
    ZoneLoadContextKey preBeginKey{};
    CHECK(
        TryClaimZoneRuntimeGeneration(&preBegin, 9, &preBeginKey)
        == ZoneRuntimeTableStatus::Success);
    auto *const preBeginLifecycle =
        ZoneRuntimeTableTestAccess::Lifecycle(&preBegin, 9);
    auto *const preBeginOwnership =
        ZoneRuntimeTableTestAccess::Ownership(&preBegin, 9);
    CHECK(preBeginLifecycle != nullptr);
    CHECK(preBeginOwnership != nullptr);
    CHECK(
        TryBeginZoneLoadContextAbandonment(
            preBeginLifecycle, preBeginKey)
        == ZoneLoadContextStatus::Success);
    std::uint32_t preBeginCleanupCount = 0;
    CHECK(
        TryFinishZoneLoadContextAbandonment(
            preBeginLifecycle,
            preBeginKey,
            MakeCleanupCallbacks(&preBeginCleanupCount))
        == ZoneLoadContextStatus::Success);
    CHECK(preBeginOwnership->isEmptyCanonical());
    CHECK(
        TryResetZoneRuntimeTerminalReceipt(
            &preBegin, 9, preBeginKey)
        == ZoneRuntimeTableStatus::Success);

    const std::uint64_t maximumGeneration =
        (std::numeric_limits<std::uint64_t>::max)();
    const ZoneLoadContextKey exhaustedKey{
        maximumGeneration,
        preBeginKey.slot,
        0};
    ZoneLoadContextSlotTestAccess::SetGeneration(
        preBeginLifecycle, maximumGeneration);
    ZoneRuntimeTableTestAccess::SetKey(
        &preBegin, 9, exhaustedKey);
    ZoneLoadContextKey exhaustedClaim{};
    CHECK(
        TryClaimZoneRuntimeGeneration(
            &preBegin, 9, &exhaustedClaim)
        == ZoneRuntimeTableStatus::GenerationExhausted);
    CHECK(!exhaustedClaim);
    CHECK(preBeginLifecycle->generation() == maximumGeneration);
    CHECK(
        preBeginLifecycle->terminalKind()
        == zone_load::ZoneLoadTerminalKind::Abandoned);
    CHECK(
        TryResetZoneRuntimeTerminalReceipt(
            &preBegin, 9, exhaustedKey)
        == ZoneRuntimeTableStatus::Success);
    CHECK(preBegin.initialized());
}

void TestTerminalAdapterPhaseSerializerAndCorruptionGates()
{
    ZoneRuntimeTable staging{};
    CHECK(
        TryInitializeZoneRuntimeTable(&staging)
        == ZoneRuntimeTableStatus::Success);
    ZoneLoadContextKey stagingKey{};
    CHECK(
        TryClaimZoneRuntimeGeneration(&staging, 10, &stagingKey)
        == ZoneRuntimeTableStatus::Success);
    auto *const stagingLifecycle =
        ZoneRuntimeTableTestAccess::Lifecycle(&staging, 10);
    auto *const stagingOwnership =
        ZoneRuntimeTableTestAccess::Ownership(&staging, 10);
    CHECK(stagingLifecycle != nullptr);
    CHECK(stagingOwnership != nullptr);
    db::script_string_journal::ScriptStringJournal stagingJournal{};
    CHECK(
        zone_ownership::TryBeginZoneScriptStringOwnership(
            stagingOwnership,
            stagingLifecycle,
            stagingKey,
            &stagingJournal,
            nullptr,
            0,
            0)
        == ZoneScriptStringOwnershipStatus::Success);
    CHECK(stagingOwnership->serializerRetained());
    CHECK(
        TryUnloadZoneRuntimeGeneration(&staging, 10, stagingKey, {})
        == ZoneRuntimeTableStatus::InvalidPhase);
    CHECK(stagingOwnership->serializerRetained());
    std::uint32_t stagingCleanup = 0;
    CHECK(
        zone_ownership::TryBeginZoneScriptStringRollback(
            stagingOwnership,
            {&stagingCleanup, EnsureUnreachable, CompleteCleanup})
        == ZoneScriptStringOwnershipStatus::Success);
    CHECK(
        zone_ownership::TryFinishZoneScriptStringAbandonment(
            stagingOwnership)
        == ZoneScriptStringOwnershipStatus::Success);

    ZoneRuntimeTable wrongPointer{};
    db::script_string_journal::ScriptStringJournal wrongPointerJournal{};
    ZoneLoadContextKey wrongPointerKey{};
    CHECK(MakeLiveGeneration(
        wrongPointer, 11, &wrongPointerKey, &wrongPointerJournal));
    auto *const wrongPointerOwnership =
        ZoneRuntimeTableTestAccess::Ownership(&wrongPointer, 11);
    CHECK(wrongPointerOwnership != nullptr);
    zone_load::ZoneLoadContextSlot foreignLifecycle{};
    zone_load::ZoneLoadContextKey foreignKey{};
    CHECK(
        TryInitializeZoneLoadContextSlot(&foreignLifecycle, 11)
        == ZoneLoadContextStatus::Success);
    CHECK(
        zone_load::TryClaimZoneLoadContext(
            &foreignLifecycle, &foreignKey)
        == ZoneLoadContextStatus::Success);
    CHECK(
        zone_load::TryCommitZoneLoadContext(
            &foreignLifecycle, foreignKey)
        == ZoneLoadContextStatus::Success);
    ZoneScriptStringOwnershipControllerTestAccess::SetLifecycle(
        wrongPointerOwnership, &foreignLifecycle);
    LiveUnloadDriver wrongPointerDriver{&wrongPointer, wrongPointerKey};
    CHECK(
        TryUnloadZoneRuntimeGeneration(
            &wrongPointer,
            11,
            wrongPointerKey,
            {&wrongPointerDriver, PerformLiveUnload})
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(wrongPointerDriver.operationCount == 0);
    CHECK(!wrongPointer.initialized());

    ZoneRuntimeTable hiddenStorage{};
    db::script_string_journal::ScriptStringJournal hiddenStorageJournal{};
    ZoneLoadContextKey hiddenStorageKey{};
    CHECK(MakeLiveGeneration(
        hiddenStorage, 12, &hiddenStorageKey, &hiddenStorageJournal));
    auto *const hiddenStorageOwnership =
        ZoneRuntimeTableTestAccess::Ownership(&hiddenStorage, 12);
    db::script_string_journal::ScriptStringJournalEntry hiddenEntry{};
    ZoneScriptStringOwnershipControllerTestAccess::SetStorage(
        hiddenStorageOwnership, &hiddenEntry);
    CHECK(
        TryResetZoneRuntimeTerminalReceipt(
            &hiddenStorage, 12, hiddenStorageKey)
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(!hiddenStorage.initialized());

    ZoneRuntimeTable falseSerializer{};
    db::script_string_journal::ScriptStringJournal falseSerializerJournal{};
    ZoneLoadContextKey falseSerializerKey{};
    CHECK(MakeLiveGeneration(
        falseSerializer,
        13,
        &falseSerializerKey,
        &falseSerializerJournal));
    auto *const falseSerializerOwnership =
        ZoneRuntimeTableTestAccess::Ownership(&falseSerializer, 13);
    ZoneScriptStringOwnershipControllerTestAccess::SetTransactionSerial(
        falseSerializerOwnership, 77);
    CHECK(
        TryUnloadZoneRuntimeGeneration(
            &falseSerializer, 13, falseSerializerKey, {})
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(!falseSerializer.initialized());

    ZoneRuntimeTable badResume{};
    db::script_string_journal::ScriptStringJournal badResumeJournal{};
    ZoneLoadContextKey badResumeKey{};
    CHECK(MakeLiveGeneration(
        badResume, 14, &badResumeKey, &badResumeJournal));
    auto *const badResumeOwnership =
        ZoneRuntimeTableTestAccess::Ownership(&badResume, 14);
    ZoneScriptStringOwnershipControllerTestAccess::SetResumePhase(
        badResumeOwnership,
        ZoneScriptStringOwnershipPhase::Staging);
    CHECK(
        TryUnloadZoneRuntimeGeneration(&badResume, 14, badResumeKey, {})
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(!badResume.initialized());

    ZoneRuntimeTable badReserved{};
    db::script_string_journal::ScriptStringJournal badReservedJournal{};
    ZoneLoadContextKey badReservedKey{};
    CHECK(MakeLiveGeneration(
        badReserved, 18, &badReservedKey, &badReservedJournal));
    auto *const badReservedOwnership =
        ZoneRuntimeTableTestAccess::Ownership(&badReserved, 18);
    ZoneScriptStringOwnershipControllerTestAccess::SetReserved(
        badReservedOwnership, 1, 0);
    CHECK(
        TryUnloadZoneRuntimeGeneration(
            &badReserved, 18, badReservedKey, {})
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(!badReserved.initialized());

    ZoneRuntimeTable syntheticCallback{};
    db::script_string_journal::ScriptStringJournal callbackJournal{};
    ZoneLoadContextKey callbackKey{};
    CHECK(MakeLiveGeneration(
        syntheticCallback, 15, &callbackKey, &callbackJournal));
    auto *const callbackOwnership =
        ZoneRuntimeTableTestAccess::Ownership(&syntheticCallback, 15);
    LiveUnloadDriver callbackDriver{&syntheticCallback, callbackKey};
    ZoneScriptStringOwnershipControllerTestAccess::SetCleanupBinding(
        callbackOwnership, &callbackDriver, PerformLiveUnload);
    ZoneScriptStringOwnershipControllerTestAccess::SetPhase(
        callbackOwnership,
        ZoneScriptStringOwnershipPhase::UnloadingCallback);
    CHECK(
        TryUnloadZoneRuntimeGeneration(
            &syntheticCallback,
            15,
            callbackKey,
            {&callbackDriver, PerformLiveUnload})
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(callbackDriver.operationCount == 0);
    CHECK(!syntheticCallback.initialized());

    ZoneRuntimeTable terminalMismatch{};
    db::script_string_journal::ScriptStringJournal terminalJournal{};
    ZoneLoadContextKey terminalKey{};
    CHECK(MakeLiveGeneration(
        terminalMismatch, 16, &terminalKey, &terminalJournal));
    LiveUnloadDriver terminalDriver{&terminalMismatch, terminalKey};
    CHECK(
        TryUnloadZoneRuntimeGeneration(
            &terminalMismatch,
            16,
            terminalKey,
            {&terminalDriver, PerformLiveUnload})
        == ZoneRuntimeTableStatus::Success);
    auto *const terminalOwnership =
        ZoneRuntimeTableTestAccess::Ownership(&terminalMismatch, 16);
    ZoneScriptStringOwnershipControllerTestAccess::SetPhase(
        terminalOwnership,
        ZoneScriptStringOwnershipPhase::Abandoned);
    CHECK(
        TryResetZoneRuntimeTerminalReceipt(
            &terminalMismatch, 16, terminalKey)
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(!terminalMismatch.initialized());
}

void TestUnsafeLiveUnloadBoundary(
    const std::size_t boundary,
    const bool unknownStatus)
{
    CHECK(boundary < kLiveUnloadOperations.size());
    ZoneRuntimeTable table{};
    db::script_string_journal::ScriptStringJournal journal{};
    ZoneLoadContextKey key{};
    CHECK(MakeLiveGeneration(table, 17, &key, &journal));
    LiveUnloadDriver driver{};
    driver.table = &table;
    driver.key = key;
    driver.unsafeOperation = kLiveUnloadOperations[boundary];
    driver.returnUnknown = unknownStatus;
    CHECK(
        TryUnloadZoneRuntimeGeneration(
            &table, 17, key, {&driver, PerformLiveUnload})
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(driver.failed);
    CHECK(!table.initialized());
    auto *const lifecycle =
        ZoneRuntimeTableTestAccess::Lifecycle(&table, 17);
    auto *const ownership =
        ZoneRuntimeTableTestAccess::Ownership(&table, 17);
    CHECK(lifecycle != nullptr);
    CHECK(ownership != nullptr);
    CHECK(lifecycle->cleanupPoisoned());
    CHECK(ownership->poisoned());
    CHECK(ownership->serializerRetained());
    const std::size_t operationCount = driver.operationCount;
    CHECK(
        TryUnloadZoneRuntimeGeneration(
            &table, 17, key, {&driver, PerformLiveUnload})
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(driver.operationCount == operationCount);
}

} // namespace

int main(const int argc, char **const argv)
{
    if (argc == 3
        && std::string_view(argv[1]) == "--unsafe-live-unload")
    {
        const unsigned long parsed = std::strtoul(argv[2], nullptr, 10);
        const bool unknownStatus = parsed == kLiveUnloadOperations.size();
        const std::size_t boundary = unknownStatus ? 0 : parsed;
        TestUnsafeLiveUnloadBoundary(boundary, unknownStatus);
        return failures == 0 ? 0 : 1;
    }
    TestLayoutNoexceptAndDefaultState();
    TestAllPhysicalSlotsAndStableAddresses();
    TestClaimAuthenticationAndAdjacentIsolation();
    TestGenerationAdvanceRejectsAba();
    TestPartialInitializationAndCorruptionFailClosed();
    TestHiddenCorruptionAndCleanupReentryFailClosed();
    TestControllerPhaseAndSerializerMatrix();
    TestLiveUnloadRetryResetReuseAndAba();
    TestAbandonedReceiptResetAndGenerationExhaustion();
    TestTerminalAdapterPhaseSerializerAndCorruptionGates();

    if (failures != 0)
    {
        std::fprintf(
            stderr,
            "Zone runtime table tests failed: %d\n",
            failures);
        return 1;
    }
    std::puts("Zone runtime table tests passed");
    return 0;
}
