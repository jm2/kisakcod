#include <database/db_zone_runtime_table.h>
#include <database/db_zone_memory.h>

#include <qcommon/com_error.h>
#include <script/scr_string_transaction.h>

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <memory>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

void MyAssertHandler(const char *, int, int, const char *, ...)
{
    std::abort();
}

void __cdecl Com_Error(errorParm_t, const char *, ...)
{
    std::abort();
}

namespace runtime_table_backend
{
constexpr std::size_t kMaxCalls = 16;

struct FakeBackend final
{
    std::array<script_string::AcquireResult, kMaxCalls> acquire{};
    std::size_t acquireCount = 0;
    std::size_t acquireCalls = 0;

    std::array<script_string::TransferStatus, kMaxCalls> transfer{};
    std::size_t transferCount = 0;
    std::size_t transferCalls = 0;
    std::array<std::uint32_t, kMaxCalls> transferredIds{};

    std::array<script_string::ReleaseStatus, kMaxCalls> ordinary{};
    std::size_t ordinaryCount = 0;
    std::size_t ordinaryCalls = 0;
    std::array<std::uint32_t, kMaxCalls> ordinaryIds{};

    std::array<script_string::ReleaseStatus, kMaxCalls> database{};
    std::size_t databaseCount = 0;
    std::size_t databaseCalls = 0;
    std::array<std::uint32_t, kMaxCalls> databaseIds{};

    db::zone_runtime::ZoneRuntimeTable *corruptTable = nullptr;
    db::zone_load::ZoneLoadContextKey corruptKey{};
    std::uint32_t corruptSlot = 0;
    bool corruptDurableKeyOnAcquire = false;
};

FakeBackend backend{};
} // namespace runtime_table_backend

namespace script_string
{
AcquireResult TryAcquireOrdinaryStringOfSize(
    const char *,
    std::uint32_t,
    int) noexcept
{
    using namespace runtime_table_backend;
    const std::size_t call = backend.acquireCalls++;
    if (backend.corruptDurableKeyOnAcquire && backend.corruptTable)
    {
        db::zone_runtime::ZoneRuntimeTableTestAccess::SetKey(
            backend.corruptTable,
            backend.corruptSlot,
            backend.corruptKey);
    }
    return call < backend.acquireCount
        ? backend.acquire[call]
        : AcquireResult{AcquireStatus::UnsafeFailure, 0};
}

TransferStatus TryTransferOrdinaryToDatabaseUser(
    const std::uint32_t stringId) noexcept
{
    using namespace runtime_table_backend;
    const std::size_t call = backend.transferCalls++;
    if (call >= backend.transferredIds.size())
        return TransferStatus::UnsafeFailure;
    backend.transferredIds[call] = stringId;
    return call < backend.transferCount
        ? backend.transfer[call]
        : TransferStatus::UnsafeFailure;
}

ReleaseStatus TryRemoveOrdinaryReference(
    const std::uint32_t stringId) noexcept
{
    using namespace runtime_table_backend;
    const std::size_t call = backend.ordinaryCalls++;
    if (call >= backend.ordinaryIds.size())
        return ReleaseStatus::UnsafeFailure;
    backend.ordinaryIds[call] = stringId;
    return call < backend.ordinaryCount
        ? backend.ordinary[call]
        : ReleaseStatus::UnsafeFailure;
}

ReleaseStatus TryRemoveDatabaseUserReference(
    const std::uint32_t stringId) noexcept
{
    using namespace runtime_table_backend;
    const std::size_t call = backend.databaseCalls++;
    if (call >= backend.databaseIds.size())
        return ReleaseStatus::UnsafeFailure;
    backend.databaseIds[call] = stringId;
    return call < backend.databaseCount
        ? backend.database[call]
        : ReleaseStatus::UnsafeFailure;
}
} // namespace script_string

namespace
{
namespace zone_load = db::zone_load;
namespace zone_runtime = db::zone_runtime;
namespace zone_pending_copy = db::zone_pending_copy;
namespace zone_runtime_storage = db::zone_runtime_storage;
namespace zone_stream_ownership = db::zone_stream_ownership;
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
using zone_runtime::TryBeginZoneRuntimeScriptStringOwnership;
using zone_runtime::TryBeginZoneRuntimeScriptStringRollback;
using zone_runtime::TryBeginZoneRuntimeScriptStringTransfer;
using zone_runtime::TryCommitZoneRuntimeScriptStringsAndAdmit;
using zone_runtime::TryFinishZoneRuntimeScriptStringAbandonment;
using zone_runtime::TryGetZoneRuntimeEntry;
using zone_runtime::TryGetZoneRuntimeGeneration;
using zone_runtime::TryInitializeZoneRuntimeTable;
using zone_runtime::TryPrepareZoneRuntimeScriptStringCommit;
using zone_runtime::TryResetZoneRuntimeTerminalReceipt;
using zone_runtime::TryRollbackNextZoneRuntimeScriptString;
using zone_runtime::TrySealZoneRuntimeScriptStrings;
using zone_runtime::TryStageZoneRuntimeScriptString;
using zone_runtime::TryTransferNextZoneRuntimeScriptString;
using zone_runtime::TryUnloadZoneRuntimeGeneration;
using zone_runtime::ZoneRuntimeEntry;
using zone_runtime::ZoneRuntimeGenerationView;
using zone_runtime::ZoneRuntimeReceiptCapsule;
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

void ResetBackend() noexcept
{
    runtime_table_backend::backend = {};
}

void PushAcquire(
    const script_string::AcquireStatus status,
    const std::uint32_t stringId) noexcept
{
    auto &backend = runtime_table_backend::backend;
    CHECK(backend.acquireCount < backend.acquire.size());
    if (backend.acquireCount < backend.acquire.size())
        backend.acquire[backend.acquireCount++] = {status, stringId};
}

void PushTransfer(const script_string::TransferStatus status) noexcept
{
    auto &backend = runtime_table_backend::backend;
    CHECK(backend.transferCount < backend.transfer.size());
    if (backend.transferCount < backend.transfer.size())
        backend.transfer[backend.transferCount++] = status;
}

void PushOrdinary(const script_string::ReleaseStatus status) noexcept
{
    auto &backend = runtime_table_backend::backend;
    CHECK(backend.ordinaryCount < backend.ordinary.size());
    if (backend.ordinaryCount < backend.ordinary.size())
        backend.ordinary[backend.ordinaryCount++] = status;
}

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

struct MutableRuntimeFixture final
{
    ZoneRuntimeTable table{};
    ZoneLoadContextKey key{};
    db::script_string_journal::ScriptStringJournal journal{};
    std::array<db::script_string_journal::ScriptStringJournalEntry, 4>
        storage{};
    std::uint32_t physicalSlot = 0;

    bool claim(const std::uint32_t slot) noexcept
    {
        physicalSlot = slot;
        return TryInitializeZoneRuntimeTable(&table)
                == ZoneRuntimeTableStatus::Success
            && TryClaimZoneRuntimeGeneration(&table, physicalSlot, &key)
                == ZoneRuntimeTableStatus::Success;
    }

    ZoneRuntimeTableStatus begin(
        const std::uint32_t storageCapacity,
        const std::uint32_t expectedCount) noexcept
    {
        return TryBeginZoneRuntimeScriptStringOwnership(
            &table,
            physicalSlot,
            key,
            &journal,
            storage.data(),
            storageCapacity,
            expectedCount);
    }
};

struct MutableAdmissionProbe final
{
    ZoneRuntimeTable *table = nullptr;
    ZoneLoadContextKey key{};
    bool called = false;
    ZoneRuntimeTableStatus stageReentry = ZoneRuntimeTableStatus::Success;
    ZoneRuntimeTableStatus prepareReentry = ZoneRuntimeTableStatus::Success;
    std::uint32_t stageOutput = UINT32_C(0xA5A55A5A);
};

void ObserveMutableAdmission(void *const context) noexcept
{
    auto &probe = *static_cast<MutableAdmissionProbe *>(context);
    probe.called = true;
    CHECK(probe.table != nullptr);
    if (!probe.table)
        return;
    probe.stageReentry = TryStageZoneRuntimeScriptString(
        probe.table,
        probe.key.slot,
        probe.key,
        {"reentry\0", 8, 1},
        &probe.stageOutput);
    probe.prepareReentry = TryPrepareZoneRuntimeScriptStringCommit(
        probe.table, probe.key.slot, probe.key);
}

struct MutableRollbackDriver final
{
    ZoneRuntimeTable *table = nullptr;
    ZoneLoadContextKey key{};
    bool retryFirstEnsure = false;
    bool retriedEnsure = false;
    bool attemptEnsureReentry = false;
    bool attemptedEnsureReentry = false;
    ZoneRuntimeTableStatus ensureReentry = ZoneRuntimeTableStatus::Success;
    ZoneRuntimeTableStatus stageReentry = ZoneRuntimeTableStatus::Success;
    std::uint32_t stageOutput = UINT32_C(0xF00DBAAD);
    std::size_t ensureCalls = 0;
    ZoneLoadCleanupOperation retryOperation =
        ZoneLoadCleanupOperation::ReleaseSlot;
    bool retriedCleanup = false;
    bool attemptCleanupReentry = false;
    bool attemptedCleanupReentry = false;
    ZoneRuntimeTableStatus cleanupReentry = ZoneRuntimeTableStatus::Success;
    std::array<ZoneLoadCleanupOperation, 16> operations{};
    std::size_t operationCount = 0;
};

ZoneLoadCleanupCallbackStatus PerformMutableRollbackCleanup(
    void *context,
    ZoneLoadCleanupOperation operation) noexcept;

zone_ownership::ZoneScriptStringUnpublishStatus
EnsureMutableRuntimeUnreachable(void *const context) noexcept
{
    auto &driver = *static_cast<MutableRollbackDriver *>(context);
    ++driver.ensureCalls;
    if (driver.attemptEnsureReentry && !driver.attemptedEnsureReentry)
    {
        driver.attemptedEnsureReentry = true;
        driver.ensureReentry = TryBeginZoneRuntimeScriptStringRollback(
            driver.table,
            driver.key.slot,
            driver.key,
            {&driver,
                EnsureMutableRuntimeUnreachable,
                PerformMutableRollbackCleanup});
        driver.stageReentry = TryStageZoneRuntimeScriptString(
            driver.table,
            driver.key.slot,
            driver.key,
            {"reentry\0", 8, 1},
            &driver.stageOutput);
    }
    if (driver.retryFirstEnsure && !driver.retriedEnsure)
    {
        driver.retriedEnsure = true;
        return zone_ownership::ZoneScriptStringUnpublishStatus::Retry;
    }
    return zone_ownership::ZoneScriptStringUnpublishStatus::Success;
}

ZoneLoadCleanupCallbackStatus PerformMutableRollbackCleanup(
    void *const context,
    const ZoneLoadCleanupOperation operation) noexcept
{
    auto &driver = *static_cast<MutableRollbackDriver *>(context);
    CHECK(
        operation
        != ZoneLoadCleanupOperation::
            MakePartialAssetsAndStagedReferencesUnreachable);
    CHECK(driver.operationCount < driver.operations.size());
    if (driver.operationCount < driver.operations.size())
        driver.operations[driver.operationCount++] = operation;
    if (driver.attemptCleanupReentry && !driver.attemptedCleanupReentry)
    {
        driver.attemptedCleanupReentry = true;
        driver.cleanupReentry =
            TryFinishZoneRuntimeScriptStringAbandonment(
                driver.table, driver.key.slot, driver.key);
    }
    if (operation == driver.retryOperation && !driver.retriedCleanup)
    {
        driver.retriedCleanup = true;
        return ZoneLoadCleanupCallbackStatus::Retry;
    }
    return ZoneLoadCleanupCallbackStatus::Success;
}

zone_ownership::ZoneScriptStringRollbackCallbacks MakeMutableRollbackCallbacks(
    MutableRollbackDriver *const driver) noexcept
{
    return {
        driver,
        EnsureMutableRuntimeUnreachable,
        PerformMutableRollbackCleanup};
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
    static_assert(alignof(ZoneRuntimeReceiptCapsule) == 8);
    static_assert(alignof(ZoneRuntimeTable) == 8);
    static_assert(sizeof(ZoneRuntimeReceiptCapsule)
        == (sizeof(void *) == 4 ? 0xD0u : 0x120u));
    static_assert(sizeof(ZoneRuntimeEntry)
        == (sizeof(void *) == 4 ? 0x130u : 0x198u));
    static_assert(sizeof(ZoneRuntimeTable)
        == (sizeof(void *) == 4 ? 0xE900u : 0xF700u));
    static_assert(sizeof(physical_memory::AllocationReceipt)
        == (sizeof(void *) == 4 ? 0x20u : 0x30u));
    static_assert(sizeof(
        zone_stream_ownership::ZoneStreamGenerationReceipt)
        == (sizeof(void *) == 4 ? 0x20u : 0x28u));
    static_assert(sizeof(
        zone_pending_copy::PendingCopyAdmissionReceipt)
        == (sizeof(void *) == 4 ? 0x38u : 0x48u));
    static_assert(sizeof(
        zone_runtime_storage::ZoneRuntimeStorageBinding)
        == (sizeof(void *) == 4 ? 0x58u : 0x80u));
    static_assert(sizeof(zone_stream_ownership::ActiveZoneStreamBinding)
        == (sizeof(void *) == 4 ? 0x68u : 0xC0u));
    static_assert(sizeof(zone_pending_copy::PendingCopyLedger)
        == (sizeof(void *) == 4 ? 0xC160u : 0xC1A0u));
    static_assert(!std::is_default_constructible_v<
        ZoneRuntimeReceiptCapsule>);
    static_assert(!std::is_destructible_v<ZoneRuntimeReceiptCapsule>);
    static_assert(!std::is_copy_constructible_v<
        ZoneRuntimeReceiptCapsule>);
    static_assert(!std::is_move_constructible_v<
        ZoneRuntimeReceiptCapsule>);
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
    static_assert(static_cast<unsigned>(ZoneRuntimeTableStatus::Retry) == 10);
    static_assert(
        static_cast<unsigned>(ZoneRuntimeTableStatus::Rejected) == 11);
    static_assert(
        static_cast<unsigned>(ZoneRuntimeTableStatus::CountMismatch) == 12);
    static_assert(
        static_cast<unsigned>(ZoneRuntimeTableStatus::CapacityExceeded) == 13);
    static_assert(sizeof(ZoneScriptStringOwnershipPhase) == 1);
    static_assert(noexcept(TryBeginZoneRuntimeScriptStringOwnership(
        static_cast<ZoneRuntimeTable *>(nullptr),
        1,
        ZoneLoadContextKey{},
        static_cast<db::script_string_journal::ScriptStringJournal *>(nullptr),
        static_cast<db::script_string_journal::ScriptStringJournalEntry *>(
            nullptr),
        0,
        0)));
    static_assert(noexcept(TryStageZoneRuntimeScriptString(
        static_cast<ZoneRuntimeTable *>(nullptr),
        1,
        ZoneLoadContextKey{},
        {},
        static_cast<std::uint32_t *>(nullptr))));
    static_assert(noexcept(TryCommitZoneRuntimeScriptStringsAndAdmit(
        static_cast<ZoneRuntimeTable *>(nullptr),
        1,
        ZoneLoadContextKey{},
        {})));
    static_assert(noexcept(TryFinishZoneRuntimeScriptStringAbandonment(
        static_cast<ZoneRuntimeTable *>(nullptr),
        1,
        ZoneLoadContextKey{})));
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
    CHECK(ZoneRuntimeTableTestAccess::SharedResourcesPristine(&table));
    auto *const active =
        ZoneRuntimeTableTestAccess::ActiveStreamBinding(&table);
    auto *const pendingLedger =
        ZoneRuntimeTableTestAccess::PendingCopyLedger(&table);
    CHECK(active != nullptr);
    CHECK(pendingLedger != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(active)
        == reinterpret_cast<std::uintptr_t>(&table)
            + zone_slots::kPhysicalZoneSlotCount
                * sizeof(ZoneRuntimeEntry));
    CHECK(reinterpret_cast<std::uintptr_t>(pendingLedger)
        == reinterpret_cast<std::uintptr_t>(active) + sizeof(*active));
    CHECK(active->canonical());
    CHECK(
        active->phase()
        == zone_stream_ownership::ActiveZoneStreamPhase::Idle);
    CHECK(!pendingLedger->initialized());

    std::array<physical_memory::AllocationReceipt *,
        zone_slots::kPhysicalZoneSlotCount> allocationReceipts{};
    std::array<zone_stream_ownership::ZoneStreamGenerationReceipt *,
        zone_slots::kPhysicalZoneSlotCount> streamReceipts{};
    std::array<zone_pending_copy::PendingCopyAdmissionReceipt *,
        zone_slots::kPhysicalZoneSlotCount> pendingReceipts{};
    std::array<zone_runtime_storage::ZoneRuntimeStorageBinding *,
        zone_slots::kPhysicalZoneSlotCount> storageBindings{};
    const std::uintptr_t tableBegin =
        reinterpret_cast<std::uintptr_t>(&table);
    const std::uintptr_t tableEnd = tableBegin + sizeof(table);
    for (std::uint32_t physicalSlot = 0;
         physicalSlot < zone_slots::kPhysicalZoneSlotCount;
         ++physicalSlot)
    {
        allocationReceipts[physicalSlot] =
            ZoneRuntimeTableTestAccess::AllocationReceipt(
                &table, physicalSlot);
        streamReceipts[physicalSlot] =
            ZoneRuntimeTableTestAccess::StreamGenerationReceipt(
                &table, physicalSlot);
        pendingReceipts[physicalSlot] =
            ZoneRuntimeTableTestAccess::PendingCopyAdmissionReceipt(
                &table, physicalSlot);
        storageBindings[physicalSlot] =
            ZoneRuntimeTableTestAccess::StorageBinding(
                &table, physicalSlot);
        CHECK(allocationReceipts[physicalSlot] != nullptr);
        CHECK(streamReceipts[physicalSlot] != nullptr);
        CHECK(pendingReceipts[physicalSlot] != nullptr);
        CHECK(storageBindings[physicalSlot] != nullptr);
        CHECK(ZoneRuntimeTableTestAccess::ReceiptCapsulePristine(
            &table, physicalSlot));
        CHECK(streamReceipts[physicalSlot]->canonical());
        CHECK(
            streamReceipts[physicalSlot]->phase()
            == zone_stream_ownership::ZoneStreamGenerationPhase::NeverBound);
        CHECK(pendingReceipts[physicalSlot]->canonical());
        CHECK(
            pendingReceipts[physicalSlot]->phase()
            == zone_pending_copy::PendingCopyAdmissionPhase::Pristine);
        CHECK(!storageBindings[physicalSlot]->bound());
        CHECK(!storageBindings[physicalSlot]->destroyed());

        const void *const objects[] = {
            allocationReceipts[physicalSlot],
            streamReceipts[physicalSlot],
            pendingReceipts[physicalSlot],
            storageBindings[physicalSlot],
        };
        for (std::size_t index = 0; index < std::size(objects); ++index)
        {
            const std::uintptr_t address =
                reinterpret_cast<std::uintptr_t>(objects[index]);
            CHECK(address >= tableBegin && address < tableEnd);
            for (std::size_t prior = 0; prior < index; ++prior)
                CHECK(objects[index] != objects[prior]);
        }
        if (physicalSlot != 0)
        {
            CHECK(
                reinterpret_cast<std::uintptr_t>(
                    allocationReceipts[physicalSlot])
                    - reinterpret_cast<std::uintptr_t>(
                        allocationReceipts[physicalSlot - 1])
                == sizeof(ZoneRuntimeEntry));
        }
    }

    CHECK(
        TryInitializeZoneRuntimeTable(&table)
        == ZoneRuntimeTableStatus::Success);
    CHECK(table.initialized());
    CHECK(ZoneRuntimeTableTestAccess::SharedResourcesPristine(&table));
    for (std::uint32_t physicalSlot = 0;
         physicalSlot < zone_slots::kPhysicalZoneSlotCount;
         ++physicalSlot)
    {
        CHECK(ZoneRuntimeTableTestAccess::ReceiptCapsulePristine(
            &table, physicalSlot));
        CHECK(ZoneRuntimeTableTestAccess::AllocationReceipt(
            &table, physicalSlot) == allocationReceipts[physicalSlot]);
        CHECK(ZoneRuntimeTableTestAccess::StreamGenerationReceipt(
            &table, physicalSlot) == streamReceipts[physicalSlot]);
        CHECK(ZoneRuntimeTableTestAccess::PendingCopyAdmissionReceipt(
            &table, physicalSlot) == pendingReceipts[physicalSlot]);
        CHECK(ZoneRuntimeTableTestAccess::StorageBinding(
            &table, physicalSlot) == storageBindings[physicalSlot]);
    }
    CHECK(ZoneRuntimeTableTestAccess::ActiveStreamBinding(&table) == active);
    CHECK(
        ZoneRuntimeTableTestAccess::PendingCopyLedger(&table)
        == pendingLedger);
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

    ZoneRuntimeTable hiddenGeneration{};
    CHECK(
        TryInitializeZoneRuntimeTable(&hiddenGeneration)
        == ZoneRuntimeTableStatus::Success);
    auto *const hiddenGenerationLifecycle =
        ZoneRuntimeTableTestAccess::Lifecycle(&hiddenGeneration, 2);
    CHECK(hiddenGenerationLifecycle != nullptr);
    ZoneLoadContextSlotTestAccess::SetGeneration(
        hiddenGenerationLifecycle, 23);
    ZoneLoadContextKey hiddenCandidate{};
    CHECK(
        TryClaimZoneRuntimeGeneration(
            &hiddenGeneration, 2, &hiddenCandidate)
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(!hiddenCandidate);
    CHECK(hiddenGenerationLifecycle->generation() == 23);
    CHECK(!hiddenGeneration.initialized());
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

void TestKeyedMutableCommitAndAuthentication()
{
    ResetBackend();
    MutableRuntimeFixture fixture{};
    CHECK(fixture.claim(19));

    ZoneLoadContextKey malformed = fixture.key;
    malformed.reserved = 1;
    ZoneLoadContextKey stale = fixture.key;
    ++stale.generation;
    db::script_string_journal::ScriptStringJournal rejectedJournal{};
    db::script_string_journal::ScriptStringJournalEntry rejectedStorage{};
    CHECK(
        TryBeginZoneRuntimeScriptStringOwnership(
            nullptr,
            fixture.physicalSlot,
            fixture.key,
            &rejectedJournal,
            &rejectedStorage,
            1,
            1)
        == ZoneRuntimeTableStatus::InvalidArgument);
    CHECK(
        TryBeginZoneRuntimeScriptStringOwnership(
            &fixture.table,
            fixture.physicalSlot,
            malformed,
            &rejectedJournal,
            &rejectedStorage,
            1,
            1)
        == ZoneRuntimeTableStatus::InvalidKey);
    CHECK(
        TryBeginZoneRuntimeScriptStringOwnership(
            &fixture.table,
            0,
            fixture.key,
            &rejectedJournal,
            &rejectedStorage,
            1,
            1)
        == ZoneRuntimeTableStatus::InvalidSlot);
    CHECK(
        TryBeginZoneRuntimeScriptStringOwnership(
            &fixture.table,
            fixture.physicalSlot + 1,
            fixture.key,
            &rejectedJournal,
            &rejectedStorage,
            1,
            1)
        == ZoneRuntimeTableStatus::StaleKey);
    CHECK(
        TryBeginZoneRuntimeScriptStringOwnership(
            &fixture.table,
            fixture.physicalSlot,
            stale,
            &rejectedJournal,
            &rejectedStorage,
            1,
            1)
        == ZoneRuntimeTableStatus::StaleKey);
    CHECK(!rejectedJournal.initialized());

    std::uint32_t unchanged = UINT32_C(0xDEADBEEF);
    MutableRollbackDriver staleDriver{&fixture.table, stale};
    CHECK(
        TryStageZoneRuntimeScriptString(
            &fixture.table,
            fixture.physicalSlot,
            stale,
            {"stale\0", 6, 1},
            &unchanged)
        == ZoneRuntimeTableStatus::StaleKey);
    CHECK(unchanged == UINT32_C(0xDEADBEEF));
    CHECK(
        TryStageZoneRuntimeScriptString(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            {"ignored\0", 8, 1},
            nullptr)
        == ZoneRuntimeTableStatus::InvalidArgument);
    CHECK(
        TrySealZoneRuntimeScriptStrings(
            &fixture.table, fixture.physicalSlot, stale)
        == ZoneRuntimeTableStatus::StaleKey);
    CHECK(
        TryBeginZoneRuntimeScriptStringTransfer(
            &fixture.table, fixture.physicalSlot, stale)
        == ZoneRuntimeTableStatus::StaleKey);
    CHECK(
        TryTransferNextZoneRuntimeScriptString(
            &fixture.table, fixture.physicalSlot, stale)
        == ZoneRuntimeTableStatus::StaleKey);
    CHECK(
        TryPrepareZoneRuntimeScriptStringCommit(
            &fixture.table, fixture.physicalSlot, stale)
        == ZoneRuntimeTableStatus::StaleKey);
    CHECK(
        TryCommitZoneRuntimeScriptStringsAndAdmit(
            &fixture.table,
            fixture.physicalSlot,
            stale,
            {nullptr, AdmitNoop})
        == ZoneRuntimeTableStatus::StaleKey);
    CHECK(
        TryBeginZoneRuntimeScriptStringRollback(
            &fixture.table,
            fixture.physicalSlot,
            stale,
            MakeMutableRollbackCallbacks(&staleDriver))
        == ZoneRuntimeTableStatus::StaleKey);
    CHECK(
        TryRollbackNextZoneRuntimeScriptString(
            &fixture.table, fixture.physicalSlot, stale)
        == ZoneRuntimeTableStatus::StaleKey);
    CHECK(
        TryFinishZoneRuntimeScriptStringAbandonment(
            &fixture.table, fixture.physicalSlot, stale)
        == ZoneRuntimeTableStatus::StaleKey);
    CHECK(fixture.table.initialized());

    CHECK(fixture.begin(2, 2) == ZoneRuntimeTableStatus::Success);
    CHECK(
        fixture.begin(2, 2) == ZoneRuntimeTableStatus::InvalidState);

    PushAcquire(script_string::AcquireStatus::InvalidArgumentNoChange, 0);
    unchanged = UINT32_C(0x13579BDF);
    CHECK(
        TryStageZoneRuntimeScriptString(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            {"rejected\0", 9, 1},
            &unchanged)
        == ZoneRuntimeTableStatus::Rejected);
    CHECK(unchanged == UINT32_C(0x13579BDF));
    CHECK(fixture.table.initialized());

    PushAcquire(script_string::AcquireStatus::Acquired, 71);
    PushAcquire(script_string::AcquireStatus::Acquired, 72);
    std::uint32_t firstId = 0;
    std::uint32_t secondId = 0;
    CHECK(
        TryStageZoneRuntimeScriptString(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            {"first\0", 6, 2},
            &firstId)
        == ZoneRuntimeTableStatus::Success);
    CHECK(firstId == 71);
    CHECK(
        TryStageZoneRuntimeScriptString(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            {"second\0", 7, 2},
            &secondId)
        == ZoneRuntimeTableStatus::Success);
    CHECK(secondId == 72);
    unchanged = UINT32_C(0x2468ACE0);
    CHECK(
        TryStageZoneRuntimeScriptString(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            {"overflow\0", 9, 2},
            &unchanged)
        == ZoneRuntimeTableStatus::CapacityExceeded);
    CHECK(unchanged == UINT32_C(0x2468ACE0));

    CHECK(
        TrySealZoneRuntimeScriptStrings(
            &fixture.table, fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(
        TrySealZoneRuntimeScriptStrings(
            &fixture.table, fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(
        TryBeginZoneRuntimeScriptStringTransfer(
            &fixture.table, fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    PushTransfer(script_string::TransferStatus::DatabaseUserClaimed);
    PushTransfer(script_string::TransferStatus::DuplicateReleased);
    CHECK(
        TryTransferNextZoneRuntimeScriptString(
            &fixture.table, fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(
        TryTransferNextZoneRuntimeScriptString(
            &fixture.table, fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    const std::size_t transferCalls =
        runtime_table_backend::backend.transferCalls;
    CHECK(
        TryTransferNextZoneRuntimeScriptString(
            &fixture.table, fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(runtime_table_backend::backend.transferCalls == transferCalls);
    CHECK(
        TryPrepareZoneRuntimeScriptStringCommit(
            &fixture.table, fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(
        TryPrepareZoneRuntimeScriptStringCommit(
            &fixture.table, fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);

    MutableAdmissionProbe admission{&fixture.table, fixture.key};
    CHECK(
        TryCommitZoneRuntimeScriptStringsAndAdmit(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            {&admission, ObserveMutableAdmission})
        == ZoneRuntimeTableStatus::Success);
    CHECK(admission.called);
    CHECK(admission.stageReentry == ZoneRuntimeTableStatus::Busy);
    CHECK(admission.prepareReentry == ZoneRuntimeTableStatus::Busy);
    CHECK(admission.stageOutput == UINT32_C(0xA5A55A5A));
    CHECK(fixture.table.initialized());
    CHECK(
        TryStageZoneRuntimeScriptString(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            {"live\0", 5, 1},
            &unchanged)
        == ZoneRuntimeTableStatus::InvalidPhase);
}

void TestKeyedMutableRecoverableAbandonment()
{
    ResetBackend();
    MutableRuntimeFixture fixture{};
    CHECK(fixture.claim(20));
    CHECK(fixture.begin(1, 2) == ZoneRuntimeTableStatus::CapacityExceeded);
    CHECK(fixture.table.initialized());
    const ZoneRuntimeEntry *entry = nullptr;
    CHECK(
        TryGetZoneRuntimeEntry(
            &fixture.table, fixture.physicalSlot, &entry)
        == ZoneRuntimeTableStatus::Success);
    CHECK(entry != nullptr);
    CHECK(entry->scriptStringOwnership().isEmptyCanonical());

    CHECK(fixture.begin(2, 2) == ZoneRuntimeTableStatus::Success);
    PushAcquire(script_string::AcquireStatus::Acquired, 101);
    std::uint32_t output = 0;
    CHECK(
        TryStageZoneRuntimeScriptString(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            {"one\0", 4, 3},
            &output)
        == ZoneRuntimeTableStatus::Success);
    CHECK(output == 101);
    CHECK(
        TrySealZoneRuntimeScriptStrings(
            &fixture.table, fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::CountMismatch);
    CHECK(fixture.table.initialized());
    PushAcquire(script_string::AcquireStatus::Acquired, 202);
    CHECK(
        TryStageZoneRuntimeScriptString(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            {"two\0", 4, 3},
            &output)
        == ZoneRuntimeTableStatus::Success);
    CHECK(output == 202);
    CHECK(
        TrySealZoneRuntimeScriptStrings(
            &fixture.table, fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);

    MutableRollbackDriver driver{};
    driver.table = &fixture.table;
    driver.key = fixture.key;
    driver.retryFirstEnsure = true;
    driver.attemptEnsureReentry = true;
    driver.retryOperation = ZoneLoadCleanupOperation::ReleaseGeometry;
    driver.attemptCleanupReentry = true;
    const auto callbacks = MakeMutableRollbackCallbacks(&driver);
    CHECK(
        TryBeginZoneRuntimeScriptStringRollback(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            callbacks)
        == ZoneRuntimeTableStatus::Retry);
    CHECK(driver.attemptedEnsureReentry);
    CHECK(driver.ensureReentry == ZoneRuntimeTableStatus::Busy);
    CHECK(driver.stageReentry == ZoneRuntimeTableStatus::Busy);
    CHECK(driver.stageOutput == UINT32_C(0xF00DBAAD));
    MutableRollbackDriver swapped{};
    CHECK(
        TryBeginZoneRuntimeScriptStringRollback(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            MakeMutableRollbackCallbacks(&swapped))
        == ZoneRuntimeTableStatus::InvalidState);
    CHECK(fixture.table.initialized());
    CHECK(
        TryBeginZoneRuntimeScriptStringRollback(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            callbacks)
        == ZoneRuntimeTableStatus::Success);

    PushOrdinary(script_string::ReleaseStatus::Success);
    PushOrdinary(script_string::ReleaseStatus::Success);
    CHECK(
        TryRollbackNextZoneRuntimeScriptString(
            &fixture.table, fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(
        TryRollbackNextZoneRuntimeScriptString(
            &fixture.table, fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    const std::size_t ordinaryCalls =
        runtime_table_backend::backend.ordinaryCalls;
    CHECK(
        TryRollbackNextZoneRuntimeScriptString(
            &fixture.table, fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(runtime_table_backend::backend.ordinaryCalls == ordinaryCalls);
    CHECK(runtime_table_backend::backend.ordinaryIds[0] == 202);
    CHECK(runtime_table_backend::backend.ordinaryIds[1] == 101);

    CHECK(
        TryFinishZoneRuntimeScriptStringAbandonment(
            &fixture.table, fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Retry);
    CHECK(driver.attemptedCleanupReentry);
    CHECK(driver.cleanupReentry == ZoneRuntimeTableStatus::Busy);
    CHECK(fixture.table.initialized());
    CHECK(
        TryFinishZoneRuntimeScriptStringAbandonment(
            &fixture.table, fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(
        TryFinishZoneRuntimeScriptStringAbandonment(
            &fixture.table, fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(
        TryResetZoneRuntimeTerminalReceipt(
            &fixture.table, fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    ZoneLoadContextKey nextKey{};
    CHECK(
        TryClaimZoneRuntimeGeneration(
            &fixture.table, fixture.physicalSlot, &nextKey)
        == ZoneRuntimeTableStatus::Success);
    CHECK(nextKey.generation == fixture.key.generation + 1);
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

void ExpectReceiptCorruptionFailsClosed(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot)
{
    const ZoneRuntimeEntry *unchanged = nullptr;
    CHECK(
        TryGetZoneRuntimeEntry(table, physicalSlot, &unchanged)
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(unchanged == nullptr);
    CHECK(!table->initialized());
}

void TestPassiveReceiptPristineAuthentication()
{
    // Allocation authority is present but remains unclaimed by table init and
    // ordinary generation claims. A real external claim is detected before
    // any runtime-table output is published.
    {
        auto table = std::make_unique<ZoneRuntimeTable>();
        CHECK(TryInitializeZoneRuntimeTable(table.get())
            == ZoneRuntimeTableStatus::Success);
        ZoneLoadContextKey key{};
        CHECK(TryClaimZoneRuntimeGeneration(table.get(), 1, &key)
            == ZoneRuntimeTableStatus::Success);
        CHECK(ZoneRuntimeTableTestAccess::ReceiptCapsulePristine(
            table.get(), 1));

        std::array<std::uint8_t, 1024> backing{};
        PhysicalMemory memory{};
        memory.buf = backing.data();
        memory.prim[0].pos = 0;
        memory.prim[1].pos = static_cast<std::uint32_t>(backing.size());
        static char allocationName;
        auto *const receipt =
            ZoneRuntimeTableTestAccess::AllocationReceipt(table.get(), 1);
        CHECK(receipt != nullptr);
        CHECK(physical_memory::TryBegin(
            &memory, 0, &allocationName, receipt)
            == physical_memory::AllocationScopeStatus::Success);
        ExpectReceiptCorruptionFailsClosed(table.get(), 1);
        CHECK(physical_memory::TryEnd(receipt)
            == physical_memory::AllocationScopeStatus::Success);
        CHECK(physical_memory::TryFree(receipt)
            == physical_memory::AllocationScopeStatus::Success);
    }

    // Beginning a stream-generation receipt does not enroll or acquire the
    // singleton, but it is still non-pristine authority and must fail closed.
    {
        auto table = std::make_unique<ZoneRuntimeTable>();
        CHECK(TryInitializeZoneRuntimeTable(table.get())
            == ZoneRuntimeTableStatus::Success);
        ZoneLoadContextKey key{};
        CHECK(TryClaimZoneRuntimeGeneration(table.get(), 2, &key)
            == ZoneRuntimeTableStatus::Success);
        auto *const lifecycle =
            ZoneRuntimeTableTestAccess::Lifecycle(table.get(), 2);
        auto *const receipt =
            ZoneRuntimeTableTestAccess::StreamGenerationReceipt(
                table.get(), 2);
        auto *const active =
            ZoneRuntimeTableTestAccess::ActiveStreamBinding(table.get());
        CHECK(lifecycle != nullptr);
        CHECK(receipt != nullptr);
        CHECK(active != nullptr);
        CHECK(zone_stream_ownership::TryBeginZoneStreamGeneration(
            receipt, lifecycle, key)
            == zone_stream_ownership::ZoneStreamOwnershipStatus::Success);
        ExpectReceiptCorruptionFailsClosed(table.get(), 2);
        CHECK(zone_stream_ownership::TryInvalidateZoneStreams(
            active, receipt, key)
            == zone_stream_ownership::ZoneStreamOwnershipStatus::Success);
    }

    // The one shared ledger and one per-entry admission receipt are composed
    // at stable addresses but not initialized or admitted by the table.
    {
        auto table = std::make_unique<ZoneRuntimeTable>();
        CHECK(TryInitializeZoneRuntimeTable(table.get())
            == ZoneRuntimeTableStatus::Success);
        ZoneLoadContextKey key{};
        CHECK(TryClaimZoneRuntimeGeneration(table.get(), 3, &key)
            == ZoneRuntimeTableStatus::Success);
        auto *const lifecycle =
            ZoneRuntimeTableTestAccess::Lifecycle(table.get(), 3);
        auto *const receipt =
            ZoneRuntimeTableTestAccess::PendingCopyAdmissionReceipt(
                table.get(), 3);
        auto *const ledger =
            ZoneRuntimeTableTestAccess::PendingCopyLedger(table.get());
        CHECK(lifecycle != nullptr);
        CHECK(receipt != nullptr);
        CHECK(ledger != nullptr);
        CHECK(zone_pending_copy::TryInitializePendingCopyLedger(ledger)
            == zone_pending_copy::PendingCopyStatus::Success);
        CHECK(zone_pending_copy::TryBeginPendingCopyAdmission(
            ledger, receipt, lifecycle, key)
            == zone_pending_copy::PendingCopyStatus::Success);
        ExpectReceiptCorruptionFailsClosed(table.get(), 3);
        CHECK(zone_pending_copy::TryDiscardPendingCopyAdmission(
            receipt, key)
            == zone_pending_copy::PendingCopyStatus::Success);
        CHECK(zone_pending_copy::TryResetPendingCopyAdmissionReceipt(
            receipt, key)
            == zone_pending_copy::PendingCopyStatus::Success);
    }

    // A placement binding is likewise only storage. Binding it through its
    // own API is observable as non-pristine, and the slab is cleaned up
    // explicitly because the durable handle never performs destructor work.
    {
        auto table = std::make_unique<ZoneRuntimeTable>();
        CHECK(TryInitializeZoneRuntimeTable(table.get())
            == ZoneRuntimeTableStatus::Success);
        zone_runtime_storage::ZoneRuntimeStoragePlan plan{};
        CHECK(zone_runtime_storage::TryPlanZoneRuntimeStorage(
            0, 64, &plan)
            == zone_runtime_storage::ZoneRuntimeStorageStatus::Success);
        std::vector<std::byte> allocation(
            static_cast<std::size_t>(plan.totalBytes) + 15u);
        void *slab = allocation.data();
        std::size_t slabCapacity = allocation.size();
        CHECK(std::align(16, plan.totalBytes, slab, slabCapacity) != nullptr);
        auto *const binding =
            ZoneRuntimeTableTestAccess::StorageBinding(table.get(), 4);
        CHECK(binding != nullptr);
        CHECK(zone_runtime_storage::TryBindZoneRuntimeStorage(
            slab, slabCapacity, &plan, binding)
            == zone_runtime_storage::ZoneRuntimeStorageStatus::Success);
        ExpectReceiptCorruptionFailsClosed(table.get(), 4);
        CHECK(zone_runtime_storage::TryDestroyZoneRuntimeStorage(binding)
            == zone_runtime_storage::ZoneRuntimeStorageStatus::Success);
    }

    // A real singleton acquisition proves that the active controller exists
    // exactly once at table scope and is included in header authentication.
    {
        auto table = std::make_unique<ZoneRuntimeTable>();
        CHECK(TryInitializeZoneRuntimeTable(table.get())
            == ZoneRuntimeTableStatus::Success);
        ZoneLoadContextKey key{};
        CHECK(TryClaimZoneRuntimeGeneration(table.get(), 5, &key)
            == ZoneRuntimeTableStatus::Success);
        auto *const lifecycle =
            ZoneRuntimeTableTestAccess::Lifecycle(table.get(), 5);
        auto *const receipt =
            ZoneRuntimeTableTestAccess::StreamGenerationReceipt(
                table.get(), 5);
        auto *const active =
            ZoneRuntimeTableTestAccess::ActiveStreamBinding(table.get());
        CHECK(lifecycle != nullptr);
        CHECK(receipt != nullptr);
        CHECK(active != nullptr);
        CHECK(zone_stream_ownership::TryBeginZoneStreamGeneration(
            receipt, lifecycle, key)
            == zone_stream_ownership::ZoneStreamOwnershipStatus::Success);

        alignas(16) std::array<std::array<std::uint8_t, 64>,
            db::relocation::kBlockCount> blockStorage{};
        XZoneMemory zone{};
        std::array<db::relocation::BlockView,
            db::relocation::kBlockCount> blocks{};
        for (std::size_t index = 0; index < blocks.size(); ++index)
        {
            zone.blocks[index].data = blockStorage[index].data();
            zone.blocks[index].size = static_cast<std::uint32_t>(
                blockStorage[index].size());
            blocks[index].base = reinterpret_cast<std::uintptr_t>(
                blockStorage[index].data());
            blocks[index].size = zone.blocks[index].size;
        }
        CHECK(zone_stream_ownership::TryBindZoneStreams(
            active,
            receipt,
            key,
            &zone,
            blocks.data(),
            blocks.size())
            == zone_stream_ownership::ZoneStreamOwnershipStatus::Success);
        ExpectReceiptCorruptionFailsClosed(table.get(), 5);
        CHECK(zone_stream_ownership::TryInvalidateZoneStreams(
            active, receipt, key)
            == zone_stream_ownership::ZoneStreamOwnershipStatus::Success);
    }
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

void TestUnsafeMutableBoundary(const bool corruptPostcondition)
{
    ResetBackend();
    MutableRuntimeFixture fixture{};
    CHECK(fixture.claim(21));
    CHECK(fixture.begin(1, 1) == ZoneRuntimeTableStatus::Success);
    if (corruptPostcondition)
    {
        auto &backend = runtime_table_backend::backend;
        backend.corruptTable = &fixture.table;
        backend.corruptSlot = fixture.physicalSlot;
        backend.corruptKey = fixture.key;
        ++backend.corruptKey.generation;
        backend.corruptDurableKeyOnAcquire = true;
        PushAcquire(script_string::AcquireStatus::Acquired, 31337);
    }
    else
    {
        PushAcquire(script_string::AcquireStatus::UnsafeFailure, 0);
    }

    std::uint32_t output = UINT32_C(0x1234ABCD);
    CHECK(
        TryStageZoneRuntimeScriptString(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            {"unsafe\0", 7, 4},
            &output)
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(output == UINT32_C(0x1234ABCD));
    CHECK(runtime_table_backend::backend.acquireCalls == 1);
    CHECK(!fixture.table.initialized());
    const std::size_t acquireCalls =
        runtime_table_backend::backend.acquireCalls;
    CHECK(
        TryStageZoneRuntimeScriptString(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            {"blocked\0", 8, 4},
            &output)
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(runtime_table_backend::backend.acquireCalls == acquireCalls);
    CHECK(output == UINT32_C(0x1234ABCD));
}

} // namespace

int main(const int argc, char **const argv)
{
    if (argc != 1)
    {
        if (argc == 3
            && std::string_view(argv[1]) == "--unsafe-mutable")
        {
            const std::string_view kind(argv[2]);
            if (kind == "backend")
                TestUnsafeMutableBoundary(false);
            else if (kind == "postcondition")
                TestUnsafeMutableBoundary(true);
            else
                return 2;
            return failures == 0 ? 0 : 1;
        }
        if (argc != 3
            || std::string_view(argv[1]) != "--unsafe-live-unload")
        {
            return 2;
        }
        const std::string_view boundaryText(argv[2]);
        std::size_t parsed = 0;
        const auto [end, error] = std::from_chars(
            boundaryText.data(),
            boundaryText.data() + boundaryText.size(),
            parsed);
        if (error != std::errc{}
            || end != boundaryText.data() + boundaryText.size()
            || parsed > kLiveUnloadOperations.size())
        {
            return 2;
        }
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
    TestKeyedMutableCommitAndAuthentication();
    TestKeyedMutableRecoverableAbandonment();
    TestLiveUnloadRetryResetReuseAndAba();
    TestAbandonedReceiptResetAndGenerationExhaustion();
    TestTerminalAdapterPhaseSerializerAndCorruptionGates();
    TestPassiveReceiptPristineAuthentication();

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
