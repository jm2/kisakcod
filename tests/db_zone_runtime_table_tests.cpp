#include <database/db_zone_runtime_table.h>
#include <database/db_stream_state.h>
#include <database/db_zone_memory.h>

#include <qcommon/com_error.h>
#include <script/scr_string_transaction.h>

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

void __cdecl Com_Printf(int, const char *, ...)
{
}

double __cdecl ConvertToMB(const int bytes)
{
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

void __cdecl Sys_OutOfMemErrorInternal(const char *, int)
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
using zone_runtime::TryAllocateZoneRuntimeMemory;
using zone_runtime::TryAppendZoneRuntimePendingCopy;
using zone_runtime::TryBeginZoneRuntimeGenerationAbandonment;
using zone_runtime::TryBeginZoneRuntimePendingCopies;
using zone_runtime::TryBeginZoneRuntimePendingCopyDrain;
using zone_runtime::TryBeginZoneRuntimePhysicalAllocation;
using zone_runtime::TryBeginZoneRuntimeStreamGeneration;
using zone_runtime::TryBindZoneRuntimeGenerationCallbacks;
using zone_runtime::TryBindZoneRuntimeStorage;
using zone_runtime::TryBindZoneRuntimeStreams;
using zone_runtime::TryClaimZoneRuntimeGeneration;
using zone_runtime::TryBeginZoneRuntimeScriptStringOwnership;
using zone_runtime::TryBeginZoneRuntimeScriptStringRollback;
using zone_runtime::TryBeginZoneRuntimeScriptStringTransfer;
using zone_runtime::TryCommitZoneRuntimeGeneration;
using zone_runtime::TryCommitZoneRuntimeScriptStringsAndAdmit;
using zone_runtime::TryContinueZoneRuntimeGenerationAbandonment;
using zone_runtime::TryDrainNextZoneRuntimePendingCopy;
using zone_runtime::TryEndZoneRuntimePhysicalAllocation;
using zone_runtime::TryFinishZoneRuntimeScriptStringAbandonment;
using zone_runtime::TryFinishZoneRuntimePendingCopyDrain;
using zone_runtime::TryGetZoneRuntimeEntry;
using zone_runtime::TryGetZoneRuntimeGeneration;
using zone_runtime::TryInitializeZoneRuntimeTable;
using zone_runtime::TryInvalidateZoneRuntimeStreams;
using zone_runtime::TryPrepareZoneRuntimeAdmission;
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
using zone_runtime::ZoneRuntimeGenerationCallbacks;
using zone_runtime::ZoneRuntimeExecutionMode;
using zone_runtime::ZoneRuntimeSetupStage;
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

ZoneLoadCleanupCallbackStatus CompleteCleanupWithoutContextMutation(
    void *const context,
    const ZoneLoadCleanupOperation) noexcept
{
    return context
        ? ZoneLoadCleanupCallbackStatus::Success
        : ZoneLoadCleanupCallbackStatus::UnsafeFailure;
}

ZoneLoadCleanupCallbacks MakeCleanupCallbacks(
    std::uint32_t *const count) noexcept
{
    return {count, CompleteCleanup};
}

struct CompositeRuntimeDriver final
{
    ZoneRuntimeTable *table = nullptr;
    ZoneLoadContextKey key{};
    std::array<ZoneLoadCleanupOperation, 16> externalOperations{};
    std::size_t externalOperationCount = 0;
    std::uint32_t ensureCalls = 0;
    std::uint32_t pendingCompletionCalls = 0;
    std::uint32_t admitCalls = 0;
    bool retryEnsure = false;
    bool retriedEnsure = false;
    bool attemptEnsureReentry = false;
    bool attemptedEnsureReentry = false;
    ZoneRuntimeTableStatus ensureReentry =
        ZoneRuntimeTableStatus::Success;
    bool retryReleaseGeometry = false;
    bool retriedReleaseGeometry = false;
    bool attemptCleanupReentry = false;
    bool attemptedCleanupReentry = false;
    ZoneRuntimeTableStatus cleanupReentry =
        ZoneRuntimeTableStatus::Success;
    ZoneRuntimeTableStatus pendingCompletionReentry =
        ZoneRuntimeTableStatus::Success;
};

zone_ownership::ZoneScriptStringUnpublishStatus
EnsureCompositeRuntimeUnreachable(void *const context) noexcept
{
    auto *const driver = static_cast<CompositeRuntimeDriver *>(context);
    if (!driver)
    {
        return zone_ownership::
            ZoneScriptStringUnpublishStatus::UnsafeFailure;
    }
    ++driver->ensureCalls;
    if (driver->attemptEnsureReentry
        && !driver->attemptedEnsureReentry)
    {
        driver->attemptedEnsureReentry = true;
        driver->ensureReentry =
            TryBeginZoneRuntimeGenerationAbandonment(
                driver->table,
                driver->key.slot,
                driver->key);
    }
    if (driver->retryEnsure && !driver->retriedEnsure)
    {
        driver->retriedEnsure = true;
        return zone_ownership::
            ZoneScriptStringUnpublishStatus::Retry;
    }
    return zone_ownership::ZoneScriptStringUnpublishStatus::Success;
}

ZoneLoadCleanupCallbackStatus PerformCompositeRuntimeCleanup(
    void *const context,
    const ZoneLoadCleanupOperation operation) noexcept
{
    auto *const driver = static_cast<CompositeRuntimeDriver *>(context);
    if (!driver
        || driver->externalOperationCount
            >= driver->externalOperations.size())
    {
        return ZoneLoadCleanupCallbackStatus::UnsafeFailure;
    }
    driver->externalOperations[driver->externalOperationCount++] =
        operation;
    if (driver->attemptCleanupReentry
        && !driver->attemptedCleanupReentry
        && operation
            == ZoneLoadCleanupOperation::
                TearDownNativeArenaWorkspaceAndSidecars)
    {
        driver->attemptedCleanupReentry = true;
        driver->cleanupReentry = TryUnloadZoneRuntimeGeneration(
            driver->table, driver->key.slot, driver->key);
    }
    if (driver->retryReleaseGeometry
        && !driver->retriedReleaseGeometry
        && operation == ZoneLoadCleanupOperation::ReleaseGeometry)
    {
        driver->retriedReleaseGeometry = true;
        return ZoneLoadCleanupCallbackStatus::Retry;
    }
    return ZoneLoadCleanupCallbackStatus::Success;
}

void CompleteCompositePendingAdmission(void *const context) noexcept
{
    auto *const driver = static_cast<CompositeRuntimeDriver *>(context);
    if (driver)
    {
        ++driver->pendingCompletionCalls;
        driver->pendingCompletionReentry = TryCommitZoneRuntimeGeneration(
            driver->table, driver->key.slot, driver->key);
    }
}

void AdmitCompositeRuntimeLive(void *const context) noexcept
{
    auto *const driver = static_cast<CompositeRuntimeDriver *>(context);
    if (driver)
        ++driver->admitCalls;
}

ZoneRuntimeGenerationCallbacks MakeCompositeRuntimeCallbacks(
    CompositeRuntimeDriver *const driver) noexcept
{
    return {
        driver,
        EnsureCompositeRuntimeUnreachable,
        PerformCompositeRuntimeCleanup,
        CompleteCompositePendingAdmission,
        AdmitCompositeRuntimeLive,
    };
}

struct PendingCopyDrainProbe final
{
    std::uint32_t calls = 0;
    zone_pending_copy::PendingCopyRecord record{};
};

zone_pending_copy::PendingCopyDrainCallbackStatus ConsumePendingCopy(
    void *const context,
    const zone_pending_copy::PendingCopyRecord record) noexcept
{
    auto *const probe = static_cast<PendingCopyDrainProbe *>(context);
    if (!probe)
    {
        return zone_pending_copy::
            PendingCopyDrainCallbackStatus::UnsafeFailure;
    }
    ++probe->calls;
    probe->record = record;
    return zone_pending_copy::PendingCopyDrainCallbackStatus::Success;
}

struct CompositeRuntimeFixture final
{
    std::unique_ptr<ZoneRuntimeTable> table =
        std::make_unique<ZoneRuntimeTable>();
    CompositeRuntimeDriver driver{};
    ZoneLoadContextKey key{};
    zone_runtime_storage::ZoneRuntimeStoragePlan storagePlan{};
    pmem_runtime::AllocationResult slabAllocation{};
    // Keep the repeatedly published result at a stable, naturally aligned
    // heap address without adding MSVC C4324 padding to this fixture.
    std::unique_ptr<pmem_runtime::AllocationResult> streamAllocation =
        std::make_unique<pmem_runtime::AllocationResult>();
    ZoneRuntimeTableStatus streamAllocationTableStatus =
        ZoneRuntimeTableStatus::InvalidArgument;
    std::size_t streamAllocationIndex = 0;
    // This full-width witness identifies a partial loop and keeps the
    // alignment-specified zone from adding implicit Win32 tail padding.
    std::size_t streamAllocationAttemptCount = 0;
    alignas(ZoneRuntimeGenerationView) XZoneMemory zone{};
    std::array<db::relocation::BlockView,
        db::relocation::kBlockCount> blocks{};
    std::uint32_t physicalSlot = 0;
    std::uint32_t allocationType = 0;

    bool enroll(const std::uint32_t slot) noexcept
    {
        physicalSlot = slot;
        if (TryInitializeZoneRuntimeTable(table.get())
                != ZoneRuntimeTableStatus::Success
            || TryClaimZoneRuntimeGeneration(
                   table.get(), physicalSlot, &key)
                != ZoneRuntimeTableStatus::Success)
        {
            return false;
        }
        driver.table = table.get();
        driver.key = key;
        return TryBindZoneRuntimeGenerationCallbacks(
                   table.get(),
                   physicalSlot,
                   key,
                   MakeCompositeRuntimeCallbacks(&driver))
            == ZoneRuntimeTableStatus::Success;
    }

    bool beginAllocation(const std::uint32_t type = 0) noexcept
    {
        allocationType = type;
        return TryBeginZoneRuntimePhysicalAllocation(
                   table.get(),
                   physicalSlot,
                   key,
                   "runtime_table_adversarial",
                   allocationType)
            == ZoneRuntimeTableStatus::Success;
    }

    bool allocateStorage(
        const std::uint32_t scriptStringCapacity = 0) noexcept
    {
        return zone_runtime_storage::TryPlanZoneRuntimeStorage(
                   scriptStringCapacity, 4096, &storagePlan)
                == zone_runtime_storage::
                    ZoneRuntimeStorageStatus::Success
            && TryAllocateZoneRuntimeMemory(
                   table.get(),
                   physicalSlot,
                   key,
                   storagePlan.totalBytes,
                   16,
                   0,
                   &slabAllocation)
                == ZoneRuntimeTableStatus::Success
            && slabAllocation.status
                == pmem_runtime::AllocationStatus::Success
            && slabAllocation.address != nullptr;
    }

    bool bindStorage() noexcept
    {
        return TryBindZoneRuntimeStorage(
                   table.get(),
                   physicalSlot,
                   key,
                   slabAllocation.address,
                   storagePlan.totalBytes,
                   &storagePlan)
            == ZoneRuntimeTableStatus::Success;
    }

    bool setupStorage(
        const std::uint32_t scriptStringCapacity = 0) noexcept
    {
        return allocateStorage(scriptStringCapacity) && bindStorage();
    }

    bool beginStreamGeneration() noexcept
    {
        return TryBeginZoneRuntimeStreamGeneration(
                   table.get(), physicalSlot, key)
            == ZoneRuntimeTableStatus::Success;
    }

    bool allocateStreamBlocks() noexcept
    {
        streamAllocationAttemptCount = 0;
        for (std::size_t index = 0; index < blocks.size(); ++index)
        {
            *streamAllocation = {};
            streamAllocationIndex = index;
            ++streamAllocationAttemptCount;
            streamAllocationTableStatus = TryAllocateZoneRuntimeMemory(
                    table.get(),
                    physicalSlot,
                    key,
                    64,
                    16,
                    0,
                    streamAllocation.get());
            if (streamAllocationTableStatus
                    != ZoneRuntimeTableStatus::Success
                || streamAllocation->status
                    != pmem_runtime::AllocationStatus::Success
                || !streamAllocation->address)
            {
                return false;
            }
            zone.blocks[index].data = streamAllocation->address;
            zone.blocks[index].size = 64;
            blocks[index] = {
                reinterpret_cast<std::uintptr_t>(streamAllocation->address),
                64,
            };
        }
        return true;
    }

    bool bindStreams() noexcept
    {
        return TryBindZoneRuntimeStreams(
                   table.get(),
                   physicalSlot,
                   key,
                   &zone,
                   blocks.data(),
                   blocks.size())
            == ZoneRuntimeTableStatus::Success;
    }

    bool setupStreams() noexcept
    {
        return beginStreamGeneration()
            && allocateStreamBlocks()
            && bindStreams();
    }

    bool beginPendingCopies(const bool appendRecord = false) noexcept
    {
        if (TryBeginZoneRuntimePendingCopies(
                table.get(), physicalSlot, key)
            != ZoneRuntimeTableStatus::Success)
        {
            return false;
        }
        return !appendRecord
            || TryAppendZoneRuntimePendingCopy(
                   table.get(), physicalSlot, key, 7)
                == ZoneRuntimeTableStatus::Success;
    }

    bool beginExactScriptStrings(
        const std::uint32_t expectedCount) noexcept
    {
        auto *const storage = ZoneRuntimeTableTestAccess::StorageBinding(
            table.get(), physicalSlot);
        return storage && storage->scriptStringJournal()
            && TryBeginZoneRuntimeScriptStringOwnership(
                   table.get(),
                   physicalSlot,
                   key,
                   storage->scriptStringJournal(),
                   storage->scriptStringEntries(),
                   storagePlan.scriptStringCapacity,
                   expectedCount)
                == ZoneRuntimeTableStatus::Success;
    }

    bool beginExactScriptStrings() noexcept
    {
        return beginExactScriptStrings(storagePlan.scriptStringCapacity);
    }

    const ZoneRuntimeEntry *entry() noexcept
    {
        const ZoneRuntimeEntry *result = nullptr;
        CHECK(TryGetZoneRuntimeEntry(table.get(), physicalSlot, &result)
            == ZoneRuntimeTableStatus::Success);
        return result;
    }
};

bool ContinueCompositeAbandonmentToTerminal(
    CompositeRuntimeFixture &fixture) noexcept
{
    for (std::size_t attempt = 0; attempt < 32; ++attempt)
    {
        const ZoneRuntimeEntry *const entry = fixture.entry();
        if (!entry)
            return false;
        if (entry->executionMode() == ZoneRuntimeExecutionMode::Terminal)
            return true;
        const ZoneRuntimeTableStatus status =
            TryContinueZoneRuntimeGenerationAbandonment(
                fixture.table.get(),
                fixture.physicalSlot,
                fixture.key);
        CHECK(status == ZoneRuntimeTableStatus::Success
            || status == ZoneRuntimeTableStatus::Retry);
        if (status != ZoneRuntimeTableStatus::Success
            && status != ZoneRuntimeTableStatus::Retry)
        {
            return false;
        }
    }
    CHECK(false);
    return false;
}

bool DriveCompositeAbandonmentToTerminal(
    CompositeRuntimeFixture &fixture) noexcept
{
    const ZoneRuntimeTableStatus begin =
        TryBeginZoneRuntimeGenerationAbandonment(
            fixture.table.get(), fixture.physicalSlot, fixture.key);
    CHECK(begin == ZoneRuntimeTableStatus::Success
        || begin == ZoneRuntimeTableStatus::Retry);
    return (begin == ZoneRuntimeTableStatus::Success
            || begin == ZoneRuntimeTableStatus::Retry)
        && ContinueCompositeAbandonmentToTerminal(fixture);
}

bool ResetCompositeTerminalReceipt(
    CompositeRuntimeFixture &fixture) noexcept
{
    CHECK(TryContinueZoneRuntimeGenerationAbandonment(
        fixture.table.get(), fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryResetZoneRuntimeTerminalReceipt(
        fixture.table.get(), fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryResetZoneRuntimeTerminalReceipt(
        fixture.table.get(), fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(ZoneRuntimeTableTestAccess::ReceiptCapsulePristine(
        fixture.table.get(), fixture.physicalSlot));
    const ZoneRuntimeEntry *const entry = fixture.entry();
    CHECK(entry != nullptr);
    CHECK(entry && entry->generationBindingPristine());
    return entry && entry->generationBindingPristine();
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
        == (sizeof(void *) == 4 ? 0x190u : 0x228u));
    static_assert(sizeof(ZoneRuntimeTable)
        == (sizeof(void *) == 4 ? 0xF568u : 0x109A0u));
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

void TestLookupAndClaimOutputAliasPreflight()
{
    // Misaligned claim output must not advance the lifecycle generation or
    // publish a durable key before rejecting the caller-owned destination.
    {
        auto table = std::make_unique<ZoneRuntimeTable>();
        CHECK(TryInitializeZoneRuntimeTable(table.get())
            == ZoneRuntimeTableStatus::Success);
        alignas(ZoneLoadContextKey)
            std::array<std::uint8_t,
                sizeof(ZoneLoadContextKey) + 1> misaligned{};
        misaligned.fill(UINT8_C(0));
        const auto before = misaligned;
        auto *const output = reinterpret_cast<ZoneLoadContextKey *>(
            misaligned.data() + 1);
        CHECK(TryClaimZoneRuntimeGeneration(table.get(), 1, output)
            == ZoneRuntimeTableStatus::InvalidArgument);
        CHECK(misaligned == before);
        auto *const lifecycle = ZoneRuntimeTableTestAccess::Lifecycle(
            table.get(), 1);
        CHECK(lifecycle != nullptr);
        CHECK(lifecycle && lifecycle->generation() == 0);
        CHECK(lifecycle
            && lifecycle->phase() == ZoneLoadContextPhase::Empty);
        ZoneLoadContextKey valid{};
        CHECK(TryClaimZoneRuntimeGeneration(table.get(), 1, &valid)
            == ZoneRuntimeTableStatus::Success);
        CHECK(valid == ZoneLoadContextKey(1, 1, 0));
    }

    // A pristine receipt supplies aligned, zero-filled bytes inside the
    // table. It must not be usable as claim output and thereby overwrite the
    // receipt after advancing the lifecycle.
    {
        auto table = std::make_unique<ZoneRuntimeTable>();
        CHECK(TryInitializeZoneRuntimeTable(table.get())
            == ZoneRuntimeTableStatus::Success);
        auto *const receipt =
            ZoneRuntimeTableTestAccess::AllocationReceipt(table.get(), 2);
        CHECK(receipt != nullptr);
        auto *const output = reinterpret_cast<ZoneLoadContextKey *>(receipt);
        std::array<std::uint8_t, sizeof(ZoneLoadContextKey)> before{};
        const auto *const bytes =
            reinterpret_cast<const std::uint8_t *>(receipt);
        for (std::size_t index = 0; index < before.size(); ++index)
            before[index] = bytes[index];
        CHECK(TryClaimZoneRuntimeGeneration(table.get(), 2, output)
            == ZoneRuntimeTableStatus::InvalidArgument);
        for (std::size_t index = 0; index < before.size(); ++index)
            CHECK(bytes[index] == before[index]);
        auto *const lifecycle = ZoneRuntimeTableTestAccess::Lifecycle(
            table.get(), 2);
        CHECK(lifecycle != nullptr);
        CHECK(lifecycle && lifecycle->generation() == 0);
        CHECK(lifecycle
            && lifecycle->phase() == ZoneLoadContextPhase::Empty);
        CHECK(ZoneRuntimeTableTestAccess::ReceiptCapsulePristine(
            table.get(), 2));
        ZoneLoadContextKey valid{};
        CHECK(TryClaimZoneRuntimeGeneration(table.get(), 2, &valid)
            == ZoneRuntimeTableStatus::Success);
        CHECK(valid == ZoneLoadContextKey(1, 2, 0));
    }

    // Lookup publication must reject table-relative destinations before the
    // pointer/view write can turn a passive receipt into forged authority.
    {
        auto table = std::make_unique<ZoneRuntimeTable>();
        CHECK(TryInitializeZoneRuntimeTable(table.get())
            == ZoneRuntimeTableStatus::Success);
        auto *const receipt =
            ZoneRuntimeTableTestAccess::AllocationReceipt(table.get(), 3);
        CHECK(receipt != nullptr);
        auto *const output =
            reinterpret_cast<const ZoneRuntimeEntry **>(receipt);
        std::array<std::uint8_t, sizeof(const ZoneRuntimeEntry *)> before{};
        const auto *const bytes =
            reinterpret_cast<const std::uint8_t *>(receipt);
        for (std::size_t index = 0; index < before.size(); ++index)
            before[index] = bytes[index];
        CHECK(TryGetZoneRuntimeEntry(table.get(), 3, output)
            == ZoneRuntimeTableStatus::InvalidArgument);
        for (std::size_t index = 0; index < before.size(); ++index)
            CHECK(bytes[index] == before[index]);
        CHECK(ZoneRuntimeTableTestAccess::ReceiptCapsulePristine(
            table.get(), 3));
        const ZoneRuntimeEntry *external = nullptr;
        CHECK(TryGetZoneRuntimeEntry(table.get(), 3, &external)
            == ZoneRuntimeTableStatus::Success);
        CHECK(external != nullptr);
    }

    {
        auto table = std::make_unique<ZoneRuntimeTable>();
        CHECK(TryInitializeZoneRuntimeTable(table.get())
            == ZoneRuntimeTableStatus::Success);
        ZoneLoadContextKey key{};
        CHECK(TryClaimZoneRuntimeGeneration(table.get(), 4, &key)
            == ZoneRuntimeTableStatus::Success);
        auto *const receipt =
            ZoneRuntimeTableTestAccess::AllocationReceipt(table.get(), 4);
        CHECK(receipt != nullptr);
        auto *const output =
            reinterpret_cast<ZoneRuntimeGenerationView *>(receipt);
        std::array<std::uint8_t, sizeof(ZoneRuntimeGenerationView)> before{};
        const auto *const bytes =
            reinterpret_cast<const std::uint8_t *>(receipt);
        for (std::size_t index = 0; index < before.size(); ++index)
            before[index] = bytes[index];
        CHECK(TryGetZoneRuntimeGeneration(
            table.get(), 4, key, output)
            == ZoneRuntimeTableStatus::InvalidArgument);
        for (std::size_t index = 0; index < before.size(); ++index)
            CHECK(bytes[index] == before[index]);
        CHECK(ZoneRuntimeTableTestAccess::ReceiptCapsulePristine(
            table.get(), 4));
        ZoneRuntimeGenerationView external{};
        CHECK(TryGetZoneRuntimeGeneration(
            table.get(), 4, key, &external)
            == ZoneRuntimeTableStatus::Success);
        CHECK(external.key == key);
        CHECK(external.entry != nullptr);
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

void TestPassiveOwnershipPlacementAliasPreflight()
{
    ResetBackend();
    MutableRuntimeFixture fixture{};
    CHECK(fixture.claim(13));
    auto *const lifecycle = ZoneRuntimeTableTestAccess::Lifecycle(
        &fixture.table, fixture.physicalSlot);
    auto *const ownership = ZoneRuntimeTableTestAccess::Ownership(
        &fixture.table, fixture.physicalSlot);
    auto *const tableStorage =
        ZoneRuntimeTableTestAccess::StorageBinding(
            &fixture.table, fixture.physicalSlot);
    CHECK(lifecycle != nullptr);
    CHECK(ownership != nullptr);
    CHECK(tableStorage != nullptr);

    alignas(db::script_string_journal::ScriptStringJournal)
        std::array<std::uint8_t,
            sizeof(db::script_string_journal::ScriptStringJournal) + 1>
            misalignedJournalStorage{};
    misalignedJournalStorage.fill(UINT8_C(0x00));
    const auto misalignedBefore = misalignedJournalStorage;
    db::script_string_journal::ScriptStringJournalEntry externalEntry{};
    CHECK(TryBeginZoneRuntimeScriptStringOwnership(
        &fixture.table,
        fixture.physicalSlot,
        fixture.key,
        reinterpret_cast<
            db::script_string_journal::ScriptStringJournal *>(
                misalignedJournalStorage.data() + 1),
        &externalEntry,
        1,
        1)
        == ZoneRuntimeTableStatus::InvalidArgument);
    CHECK(misalignedJournalStorage == misalignedBefore);
    CHECK(lifecycle
        && lifecycle->phase() == ZoneLoadContextPhase::Loading);
    CHECK(ownership && ownership->isEmptyCanonical());
    CHECK(ZoneRuntimeTableTestAccess::ReceiptCapsulePristine(
        &fixture.table, fixture.physicalSlot));

    std::array<std::uint8_t,
        sizeof(db::script_string_journal::ScriptStringJournal)>
        tableJournalBefore{};
    const auto *const tableStorageBytes =
        reinterpret_cast<const std::uint8_t *>(tableStorage);
    for (std::size_t index = 0; index < tableJournalBefore.size(); ++index)
        tableJournalBefore[index] = tableStorageBytes[index];
    CHECK(TryBeginZoneRuntimeScriptStringOwnership(
        &fixture.table,
        fixture.physicalSlot,
        fixture.key,
        reinterpret_cast<
            db::script_string_journal::ScriptStringJournal *>(
                tableStorage),
        &externalEntry,
        1,
        1)
        == ZoneRuntimeTableStatus::InvalidArgument);
    for (std::size_t index = 0; index < tableJournalBefore.size(); ++index)
        CHECK(tableStorageBytes[index] == tableJournalBefore[index]);
    CHECK(lifecycle
        && lifecycle->phase() == ZoneLoadContextPhase::Loading);
    CHECK(ownership && ownership->isEmptyCanonical());
    CHECK(ZoneRuntimeTableTestAccess::ReceiptCapsulePristine(
        &fixture.table, fixture.physicalSlot));

    db::script_string_journal::ScriptStringJournal externalJournal{};
    CHECK(TryBeginZoneRuntimeScriptStringOwnership(
        &fixture.table,
        fixture.physicalSlot,
        fixture.key,
        &externalJournal,
        reinterpret_cast<
            db::script_string_journal::ScriptStringJournalEntry *>(
                tableStorage),
        1,
        1)
        == ZoneRuntimeTableStatus::InvalidArgument);
    CHECK(!externalJournal.initialized());
    for (std::size_t index = 0; index < tableJournalBefore.size(); ++index)
        CHECK(tableStorageBytes[index] == tableJournalBefore[index]);
    CHECK(lifecycle
        && lifecycle->phase() == ZoneLoadContextPhase::Loading);
    CHECK(ownership && ownership->isEmptyCanonical());
    CHECK(ZoneRuntimeTableTestAccess::ReceiptCapsulePristine(
        &fixture.table, fixture.physicalSlot));

    CHECK(fixture.begin(1, 1) == ZoneRuntimeTableStatus::Success);
    MutableRollbackDriver driver{};
    driver.table = &fixture.table;
    driver.key = fixture.key;
    CHECK(TryBeginZoneRuntimeScriptStringRollback(
        &fixture.table,
        fixture.physicalSlot,
        fixture.key,
        MakeMutableRollbackCallbacks(&driver))
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryRollbackNextZoneRuntimeScriptString(
        &fixture.table, fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryFinishZoneRuntimeScriptStringAbandonment(
        &fixture.table, fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryResetZoneRuntimeTerminalReceipt(
        &fixture.table, fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
}

void TestPassiveCallbackAliasPreflight()
{
    // Admission and the subsequent compatibility unload share one passive
    // Live generation so every rejected callback is followed by the exact
    // valid transition it otherwise could have performed.
    {
        MutableRuntimeFixture fixture{};
        CHECK(fixture.claim(14));
        CHECK(TryBeginZoneRuntimeScriptStringOwnership(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            &fixture.journal,
            nullptr,
            0,
            0)
            == ZoneRuntimeTableStatus::Success);
        CHECK(TrySealZoneRuntimeScriptStrings(
            &fixture.table, fixture.physicalSlot, fixture.key)
            == ZoneRuntimeTableStatus::Success);
        CHECK(TryBeginZoneRuntimeScriptStringTransfer(
            &fixture.table, fixture.physicalSlot, fixture.key)
            == ZoneRuntimeTableStatus::Success);
        CHECK(TryTransferNextZoneRuntimeScriptString(
            &fixture.table, fixture.physicalSlot, fixture.key)
            == ZoneRuntimeTableStatus::Success);
        CHECK(TryPrepareZoneRuntimeScriptStringCommit(
            &fixture.table, fixture.physicalSlot, fixture.key)
            == ZoneRuntimeTableStatus::Success);

        auto *const lifecycle = ZoneRuntimeTableTestAccess::Lifecycle(
            &fixture.table, fixture.physicalSlot);
        auto *const ownership = ZoneRuntimeTableTestAccess::Ownership(
            &fixture.table, fixture.physicalSlot);
        auto *const tableStorage =
            ZoneRuntimeTableTestAccess::StorageBinding(
                &fixture.table, fixture.physicalSlot);
        CHECK(lifecycle != nullptr);
        CHECK(ownership != nullptr);
        CHECK(tableStorage != nullptr);
        const auto &tableAdmission = *reinterpret_cast<const
            zone_ownership::ZoneScriptStringAdmissionCallback *>(
                tableStorage);
        CHECK(TryCommitZoneRuntimeScriptStringsAndAdmit(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            tableAdmission)
            == ZoneRuntimeTableStatus::InvalidArgument);
        CHECK(ownership
            && ownership->phase()
                == ZoneScriptStringOwnershipPhase::CommitReady);
        CHECK(lifecycle
            && lifecycle->phase() == ZoneLoadContextPhase::Loading);

        CHECK(TryCommitZoneRuntimeScriptStringsAndAdmit(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            {&fixture.table, AdmitNoop})
            == ZoneRuntimeTableStatus::InvalidArgument);
        CHECK(ownership
            && ownership->phase()
                == ZoneScriptStringOwnershipPhase::CommitReady);
        CHECK(lifecycle
            && lifecycle->phase() == ZoneLoadContextPhase::Loading);
        CHECK(TryCommitZoneRuntimeScriptStringsAndAdmit(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            {nullptr, AdmitNoop})
            == ZoneRuntimeTableStatus::Success);
        CHECK(ownership
            && ownership->phase()
                == ZoneScriptStringOwnershipPhase::Live);
        CHECK(lifecycle
            && lifecycle->phase() == ZoneLoadContextPhase::Live);

        const auto &tableCleanup = *reinterpret_cast<const
            ZoneLoadCleanupCallbacks *>(tableStorage);
        CHECK(TryUnloadZoneRuntimeGeneration(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            tableCleanup)
            == ZoneRuntimeTableStatus::InvalidArgument);
        CHECK(ownership
            && ownership->phase()
                == ZoneScriptStringOwnershipPhase::Live);
        CHECK(lifecycle
            && lifecycle->phase() == ZoneLoadContextPhase::Live);
        CHECK(TryUnloadZoneRuntimeGeneration(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            {&fixture.table, CompleteCleanupWithoutContextMutation})
            == ZoneRuntimeTableStatus::InvalidArgument);
        CHECK(ownership
            && ownership->phase()
                == ZoneScriptStringOwnershipPhase::Live);
        CHECK(lifecycle
            && lifecycle->phase() == ZoneLoadContextPhase::Live);

        std::uint32_t cleanupCount = 0;
        CHECK(TryUnloadZoneRuntimeGeneration(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            MakeCleanupCallbacks(&cleanupCount))
            == ZoneRuntimeTableStatus::Success);
        CHECK(cleanupCount == kLiveUnloadOperations.size());
        CHECK(TryResetZoneRuntimeTerminalReceipt(
            &fixture.table, fixture.physicalSlot, fixture.key)
            == ZoneRuntimeTableStatus::Success);
    }

    // Rollback callback identity is bound while ownership is still Staging.
    // Both alias forms must leave that phase available for one valid begin.
    {
        MutableRuntimeFixture fixture{};
        CHECK(fixture.claim(15));
        CHECK(TryBeginZoneRuntimeScriptStringOwnership(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            &fixture.journal,
            nullptr,
            0,
            0)
            == ZoneRuntimeTableStatus::Success);
        auto *const lifecycle = ZoneRuntimeTableTestAccess::Lifecycle(
            &fixture.table, fixture.physicalSlot);
        auto *const ownership = ZoneRuntimeTableTestAccess::Ownership(
            &fixture.table, fixture.physicalSlot);
        auto *const tableStorage =
            ZoneRuntimeTableTestAccess::StorageBinding(
                &fixture.table, fixture.physicalSlot);
        CHECK(lifecycle != nullptr);
        CHECK(ownership != nullptr);
        CHECK(tableStorage != nullptr);

        const auto &tableRollback = *reinterpret_cast<const
            zone_ownership::ZoneScriptStringRollbackCallbacks *>(
                tableStorage);
        CHECK(TryBeginZoneRuntimeScriptStringRollback(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            tableRollback)
            == ZoneRuntimeTableStatus::InvalidArgument);
        CHECK(ownership
            && ownership->phase()
                == ZoneScriptStringOwnershipPhase::Staging);
        CHECK(lifecycle
            && lifecycle->phase() == ZoneLoadContextPhase::Loading);

        CHECK(TryBeginZoneRuntimeScriptStringRollback(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            {&fixture.table,
                EnsureUnreachable,
                CompleteCleanupWithoutContextMutation})
            == ZoneRuntimeTableStatus::InvalidArgument);
        CHECK(ownership
            && ownership->phase()
                == ZoneScriptStringOwnershipPhase::Staging);
        CHECK(lifecycle
            && lifecycle->phase() == ZoneLoadContextPhase::Loading);

        MutableRollbackDriver driver{};
        driver.table = &fixture.table;
        driver.key = fixture.key;
        CHECK(TryBeginZoneRuntimeScriptStringRollback(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            MakeMutableRollbackCallbacks(&driver))
            == ZoneRuntimeTableStatus::Success);
        CHECK(TryRollbackNextZoneRuntimeScriptString(
            &fixture.table, fixture.physicalSlot, fixture.key)
            == ZoneRuntimeTableStatus::Success);
        CHECK(TryFinishZoneRuntimeScriptStringAbandonment(
            &fixture.table, fixture.physicalSlot, fixture.key)
            == ZoneRuntimeTableStatus::Success);
        CHECK(TryResetZoneRuntimeTerminalReceipt(
            &fixture.table, fixture.physicalSlot, fixture.key)
            == ZoneRuntimeTableStatus::Success);
    }
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
            fixture.key,
            &rejectedJournal,
            &rejectedStorage,
            0,
            0)
        == ZoneRuntimeTableStatus::InvalidArgument);
    CHECK(
        TryBeginZoneRuntimeScriptStringOwnership(
            &fixture.table,
            fixture.physicalSlot,
            fixture.key,
            &rejectedJournal,
            nullptr,
            1,
            0)
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
        static const char allocationName[] = "test_allocation";
        auto *const receipt =
            ZoneRuntimeTableTestAccess::AllocationReceipt(table.get(), 1);
        CHECK(receipt != nullptr);
        CHECK(physical_memory::TryBegin(
            &memory, 0, allocationName, receipt)
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

void TestPassiveTableWideSingletonAuthentication()
{
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

    const auto expectDirtyStreamRejected = []()
    {
        auto table = std::make_unique<ZoneRuntimeTable>();
        CHECK(!ZoneRuntimeTableTestAccess::SharedResourcesPristine(
            table.get()));
        CHECK(TryInitializeZoneRuntimeTable(table.get())
            == ZoneRuntimeTableStatus::UnsafeFailure);
        CHECK(!table->initialized());
    };

    // Every independently mutable legacy stream field participates in the
    // passive table header. A pristine embedded binding cannot authenticate
    // stale process-wide state merely because its own bytes remain zero.
    g_streamDelayIndex = 1;
    expectDirtyStreamRejected();
    g_streamDelayIndex = 0;
    g_streamPosIndex = 1;
    expectDirtyStreamRejected();
    g_streamPosIndex = 0;
    g_streamPosStackIndex = 1;
    expectDirtyStreamRejected();
    g_streamPosStackIndex = 0;
    g_streamZoneMem = &zone;
    expectDirtyStreamRejected();
    g_streamZoneMem = nullptr;
    g_streamPos = blockStorage[0].data();
    expectDirtyStreamRejected();
    g_streamPos = nullptr;
    g_streamPosArray[4] = blockStorage[4].data();
    expectDirtyStreamRejected();
    g_streamPosArray[4] = nullptr;
    g_streamDelayArray[11].ptr = blockStorage[2].data();
    expectDirtyStreamRejected();
    g_streamDelayArray[11].ptr = nullptr;
    g_streamDelayArray[11].size = 8;
    expectDirtyStreamRejected();
    g_streamDelayArray[11].size = 0;
    g_streamPosStack[9].pos = blockStorage[3].data();
    expectDirtyStreamRejected();
    g_streamPosStack[9].pos = nullptr;
    g_streamPosStack[9].index = 3;
    expectDirtyStreamRejected();
    g_streamPosStack[9].index = 0;

    // Header reauthentication also detects contamination after successful
    // table initialization and leaves the caller's output unpublished.
    {
        auto table = std::make_unique<ZoneRuntimeTable>();
        CHECK(TryInitializeZoneRuntimeTable(table.get())
            == ZoneRuntimeTableStatus::Success);
        const ZoneRuntimeEntry *entry = nullptr;
        g_streamPos = blockStorage[0].data();
        CHECK(TryGetZoneRuntimeEntry(table.get(), 1, &entry)
            == ZoneRuntimeTableStatus::UnsafeFailure);
        CHECK(entry == nullptr);
        CHECK(!table->initialized());
        g_streamPos = nullptr;
    }

    // A different pristine-looking controller can acquire the one hidden
    // process singleton. The table must authenticate the hidden owner rather
    // than accepting only its own still-pristine binding bytes.
    {
        auto table = std::make_unique<ZoneRuntimeTable>();
        CHECK(TryInitializeZoneRuntimeTable(table.get())
            == ZoneRuntimeTableStatus::Success);

        zone_load::ZoneLoadContextSlot foreignLifecycle{};
        CHECK(TryInitializeZoneLoadContextSlot(&foreignLifecycle, 31)
            == ZoneLoadContextStatus::Success);
        ZoneLoadContextKey foreignKey{};
        CHECK(zone_load::TryClaimZoneLoadContext(
            &foreignLifecycle, &foreignKey)
            == ZoneLoadContextStatus::Success);
        zone_stream_ownership::ZoneStreamGenerationReceipt foreignReceipt{};
        zone_stream_ownership::ActiveZoneStreamBinding foreignActive{};
        CHECK(zone_stream_ownership::TryBeginZoneStreamGeneration(
            &foreignReceipt, &foreignLifecycle, foreignKey)
            == zone_stream_ownership::ZoneStreamOwnershipStatus::Success);
        CHECK(zone_stream_ownership::TryBindZoneStreams(
            &foreignActive,
            &foreignReceipt,
            foreignKey,
            &zone,
            blocks.data(),
            blocks.size())
            == zone_stream_ownership::ZoneStreamOwnershipStatus::Success);

        auto *const tableActive =
            ZoneRuntimeTableTestAccess::ActiveStreamBinding(table.get());
        CHECK(tableActive != nullptr);
        CHECK(tableActive->canonical());
        CHECK(tableActive->phase()
            == zone_stream_ownership::ActiveZoneStreamPhase::Idle);
        CHECK(!ZoneRuntimeTableTestAccess::SharedResourcesPristine(
            table.get()));
        ExpectReceiptCorruptionFailsClosed(table.get(), 1);

        CHECK(zone_stream_ownership::TryInvalidateZoneStreams(
            &foreignActive, &foreignReceipt, foreignKey)
            == zone_stream_ownership::ZoneStreamOwnershipStatus::Success);
        std::uint32_t cleanupCount = 0;
        CHECK(TryBeginZoneLoadContextAbandonment(
            &foreignLifecycle, foreignKey)
            == ZoneLoadContextStatus::Success);
        CHECK(TryFinishZoneLoadContextAbandonment(
            &foreignLifecycle,
            foreignKey,
            MakeCleanupCallbacks(&cleanupCount))
            == ZoneLoadContextStatus::Success);
        CHECK(cleanupCount == 9);
    }

    // Ready-empty is canonical ledger state, but it remains forbidden by the
    // passive tripwire until exact-key enrollment replaces this predicate.
    {
        auto table = std::make_unique<ZoneRuntimeTable>();
        CHECK(TryInitializeZoneRuntimeTable(table.get())
            == ZoneRuntimeTableStatus::Success);
        auto *const ledger =
            ZoneRuntimeTableTestAccess::PendingCopyLedger(table.get());
        CHECK(ledger != nullptr);
        CHECK(zone_pending_copy::TryInitializePendingCopyLedger(ledger)
            == zone_pending_copy::PendingCopyStatus::Success);
        CHECK(ledger->canonical());
        CHECK(!ZoneRuntimeTableTestAccess::SharedResourcesPristine(
            table.get()));
        ExpectReceiptCorruptionFailsClosed(table.get(), 1);
    }

    auto clean = std::make_unique<ZoneRuntimeTable>();
    CHECK(ZoneRuntimeTableTestAccess::SharedResourcesPristine(clean.get()));
    CHECK(TryInitializeZoneRuntimeTable(clean.get())
        == ZoneRuntimeTableStatus::Success);
}

void TestCompositeRuntimeLiveUnloadResetAndReuse()
{
    const pmem_runtime::InitializationStatus initialization =
        pmem_runtime::TryInitialize();
    CHECK(initialization == pmem_runtime::InitializationStatus::Success
        || initialization
            == pmem_runtime::InitializationStatus::AlreadyInitialized);
    if (initialization != pmem_runtime::InitializationStatus::Success
        && initialization
            != pmem_runtime::InitializationStatus::AlreadyInitialized)
    {
        return;
    }

    auto table = std::make_unique<ZoneRuntimeTable>();
    CompositeRuntimeDriver driver{};
    constexpr std::uint32_t physicalSlot = 22;
    ZoneLoadContextKey key{};
    CHECK(TryInitializeZoneRuntimeTable(table.get())
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryClaimZoneRuntimeGeneration(table.get(), physicalSlot, &key)
        == ZoneRuntimeTableStatus::Success);
    driver.table = table.get();
    driver.key = key;
    driver.retryReleaseGeometry = true;
    driver.attemptCleanupReentry = true;

    const ZoneRuntimeGenerationCallbacks callbacks =
        MakeCompositeRuntimeCallbacks(&driver);
    CHECK(TryBindZoneRuntimeGenerationCallbacks(
        table.get(), physicalSlot, key, callbacks)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryBindZoneRuntimeGenerationCallbacks(
        table.get(), physicalSlot, key, callbacks)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryBeginZoneRuntimePhysicalAllocation(
        table.get(), physicalSlot, key, "runtime_table_e2e", 0)
        == ZoneRuntimeTableStatus::Success);

    zone_runtime_storage::ZoneRuntimeStoragePlan plan{};
    CHECK(zone_runtime_storage::TryPlanZoneRuntimeStorage(
        0, 4096, &plan)
        == zone_runtime_storage::ZoneRuntimeStorageStatus::Success);
    pmem_runtime::AllocationResult slabAllocation{};
    CHECK(TryAllocateZoneRuntimeMemory(
        table.get(),
        physicalSlot,
        key,
        plan.totalBytes,
        16,
        0,
        &slabAllocation)
        == ZoneRuntimeTableStatus::Success);
    CHECK(slabAllocation.status == pmem_runtime::AllocationStatus::Success);
    CHECK(slabAllocation.address != nullptr);
    if (!slabAllocation.address)
        return;
    CHECK(TryBindZoneRuntimeStorage(
        table.get(),
        physicalSlot,
        key,
        slabAllocation.address,
        plan.totalBytes,
        &plan)
        == ZoneRuntimeTableStatus::Success);

    CHECK(TryBeginZoneRuntimeStreamGeneration(
        table.get(), physicalSlot, key)
        == ZoneRuntimeTableStatus::Success);
    XZoneMemory zone{};
    std::array<db::relocation::BlockView,
        db::relocation::kBlockCount> blocks{};
    for (std::size_t index = 0; index < blocks.size(); ++index)
    {
        pmem_runtime::AllocationResult blockAllocation{};
        CHECK(TryAllocateZoneRuntimeMemory(
            table.get(), physicalSlot, key, 64, 16, 0,
            &blockAllocation)
            == ZoneRuntimeTableStatus::Success);
        CHECK(blockAllocation.status
            == pmem_runtime::AllocationStatus::Success);
        CHECK(blockAllocation.address != nullptr);
        if (!blockAllocation.address)
            return;
        zone.blocks[index].data = blockAllocation.address;
        zone.blocks[index].size = 64;
        blocks[index] = {
            reinterpret_cast<std::uintptr_t>(blockAllocation.address),
            64,
        };
    }
    CHECK(TryBindZoneRuntimeStreams(
        table.get(),
        physicalSlot,
        key,
        &zone,
        blocks.data(),
        blocks.size())
        == ZoneRuntimeTableStatus::Success);

    CHECK(TryBeginZoneRuntimePendingCopies(
        table.get(), physicalSlot, key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryAppendZoneRuntimePendingCopy(
        table.get(), physicalSlot, key, 7)
        == ZoneRuntimeTableStatus::Success);

    auto *const storage =
        ZoneRuntimeTableTestAccess::StorageBinding(
            table.get(), physicalSlot);
    CHECK(storage != nullptr);
    if (!storage)
        return;
    CHECK(storage->scriptStringJournal() != nullptr);
    CHECK(storage->scriptStringEntries() == nullptr);
    CHECK(TryBeginZoneRuntimeScriptStringOwnership(
        table.get(),
        physicalSlot,
        key,
        storage->scriptStringJournal(),
        storage->scriptStringEntries(),
        0,
        0)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TrySealZoneRuntimeScriptStrings(
        table.get(), physicalSlot, key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryBeginZoneRuntimeScriptStringTransfer(
        table.get(), physicalSlot, key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryTransferNextZoneRuntimeScriptString(
        table.get(), physicalSlot, key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryPrepareZoneRuntimeScriptStringCommit(
        table.get(), physicalSlot, key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryPrepareZoneRuntimeAdmission(
        table.get(), physicalSlot, key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryInvalidateZoneRuntimeStreams(
        table.get(), physicalSlot, key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryEndZoneRuntimePhysicalAllocation(
        table.get(), physicalSlot, key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryCommitZoneRuntimeGeneration(
        table.get(), physicalSlot, key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(driver.pendingCompletionCalls == 1);
    CHECK(driver.admitCalls == 1);
    CHECK(driver.ensureCalls == 0);
    CHECK(driver.pendingCompletionReentry == ZoneRuntimeTableStatus::Busy);

    // Exact terminal retries cannot replay either admission callback.
    CHECK(TryCommitZoneRuntimeGeneration(
        table.get(), physicalSlot, key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(driver.pendingCompletionCalls == 1);
    CHECK(driver.admitCalls == 1);

    CHECK(TryUnloadZoneRuntimeGeneration(
        table.get(), physicalSlot, key)
        == ZoneRuntimeTableStatus::Retry);
    CHECK(driver.externalOperationCount == 2);
    CHECK(TryUnloadZoneRuntimeGeneration(
        table.get(), physicalSlot, key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(driver.externalOperationCount == 5);
    const std::array expectedOperations{
        ZoneLoadCleanupOperation::RemoveLiveAssetsAndReferences,
        ZoneLoadCleanupOperation::ReleaseGeometry,
        ZoneLoadCleanupOperation::ReleaseGeometry,
        ZoneLoadCleanupOperation::
            TearDownNativeArenaWorkspaceAndSidecars,
        ZoneLoadCleanupOperation::RemoveLiveRegistryAndHandles,
    };
    for (std::size_t index = 0;
        index < expectedOperations.size()
            && index < driver.externalOperationCount;
        ++index)
    {
        CHECK(driver.externalOperations[index] == expectedOperations[index]);
    }
    CHECK(driver.attemptedCleanupReentry);
    CHECK(driver.cleanupReentry == ZoneRuntimeTableStatus::Busy);

    // The exact unloaded retry is callback-free. Reset then advances the
    // lifecycle generation while the old key remains stale evidence.
    CHECK(TryUnloadZoneRuntimeGeneration(
        table.get(), physicalSlot, key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(driver.externalOperationCount == 5);
    CHECK(TryResetZoneRuntimeTerminalReceipt(
        table.get(), physicalSlot, key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryResetZoneRuntimeTerminalReceipt(
        table.get(), physicalSlot, key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(ZoneRuntimeTableTestAccess::ReceiptCapsulePristine(
        table.get(), physicalSlot));

    const ZoneLoadContextKey oldKey = key;
    key = {};
    CHECK(TryClaimZoneRuntimeGeneration(table.get(), physicalSlot, &key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(key.generation == oldKey.generation + 1);
    CHECK(TryCommitZoneRuntimeGeneration(
        table.get(), physicalSlot, oldKey)
        == ZoneRuntimeTableStatus::StaleKey);
}

void TestCompositePartialStageAbandonmentAndReuse()
{
    const pmem_runtime::InitializationStatus initialization =
        pmem_runtime::TryInitialize();
    CHECK(initialization
            == pmem_runtime::InitializationStatus::AlreadyInitialized
        || initialization
            == pmem_runtime::InitializationStatus::Success);
    if (initialization
            != pmem_runtime::InitializationStatus::AlreadyInitialized
        && initialization
            != pmem_runtime::InitializationStatus::Success)
    {
        return;
    }

    // Callback enrollment is itself an abandonable stage: no PMem, storage,
    // stream, or pending-copy authority may be invented by cleanup.
    {
        CompositeRuntimeFixture fixture{};
        CHECK(fixture.enroll(23));
        const ZoneRuntimeEntry *entry = fixture.entry();
        CHECK(entry != nullptr);
        CHECK(entry
            && entry->setupStage()
                == ZoneRuntimeSetupStage::CallbacksBound);
        CHECK(DriveCompositeAbandonmentToTerminal(fixture));
        entry = fixture.entry();
        CHECK(entry
            && entry->executionMode()
                == ZoneRuntimeExecutionMode::Terminal);
        CHECK(entry
            && entry->setupStage()
                == ZoneRuntimeSetupStage::CallbacksBound);
        CHECK(fixture.driver.ensureCalls == 1);
        auto *const stream =
            ZoneRuntimeTableTestAccess::StreamGenerationReceipt(
                fixture.table.get(), fixture.physicalSlot);
        CHECK(stream != nullptr);
        CHECK(stream
            && stream->phase()
                == zone_stream_ownership::
                    ZoneStreamGenerationPhase::NeverBound);
        CHECK(ResetCompositeTerminalReceipt(fixture));
    }

    // Storage cleanup must accept a stream generation that was never begun.
    // Terminal reset then permits exactly one newer key; the old one remains
    // stale evidence rather than silently becoming authority for that claim.
    {
        CompositeRuntimeFixture fixture{};
        CHECK(fixture.enroll(24));
        CHECK(fixture.beginAllocation());
        CHECK(fixture.setupStorage());
        fixture.driver.attemptCleanupReentry = true;
        auto *const stream =
            ZoneRuntimeTableTestAccess::StreamGenerationReceipt(
                fixture.table.get(), fixture.physicalSlot);
        CHECK(stream != nullptr);
        CHECK(stream
            && stream->phase()
                == zone_stream_ownership::
                    ZoneStreamGenerationPhase::NeverBound);
        CHECK(DriveCompositeAbandonmentToTerminal(fixture));
        CHECK(stream
            && stream->phase()
                == zone_stream_ownership::
                    ZoneStreamGenerationPhase::NeverBound);
        CHECK(fixture.driver.attemptedCleanupReentry);
        CHECK(fixture.driver.cleanupReentry
            == ZoneRuntimeTableStatus::Busy);
        auto *const storage =
            ZoneRuntimeTableTestAccess::StorageBinding(
                fixture.table.get(), fixture.physicalSlot);
        CHECK(storage != nullptr);
        CHECK(storage && storage->destroyed());
        auto *const allocation =
            ZoneRuntimeTableTestAccess::AllocationReceipt(
                fixture.table.get(), fixture.physicalSlot);
        CHECK(allocation != nullptr);
        CHECK(allocation
            && pmem_runtime::TryAuthenticateAllocationReceipt(
                   allocation,
                   fixture.allocationType,
                   pmem_runtime::AllocationReceiptPhase::Freed)
                == pmem_runtime::AllocationReceiptStatus::Success);

        const ZoneLoadContextKey oldKey = fixture.key;
        CHECK(ResetCompositeTerminalReceipt(fixture));
        ZoneLoadContextKey replacement{};
        CHECK(TryClaimZoneRuntimeGeneration(
            fixture.table.get(), fixture.physicalSlot, &replacement)
            == ZoneRuntimeTableStatus::Success);
        CHECK(replacement.generation == oldKey.generation + 1);
        CHECK(TryBeginZoneRuntimeGenerationAbandonment(
            fixture.table.get(), fixture.physicalSlot, oldKey)
            == ZoneRuntimeTableStatus::StaleKey);
    }

    // A generation with both a bound singleton and a pending record must
    // invalidate and discard those authorities before publishing Abandoned.
    {
        CompositeRuntimeFixture fixture{};
        CHECK(fixture.enroll(25));
        CHECK(fixture.beginAllocation());
        CHECK(fixture.setupStorage());
        CHECK(reinterpret_cast<std::uintptr_t>(
                  fixture.streamAllocation.get())
                % alignof(pmem_runtime::AllocationResult)
            == 0);
        CHECK(fixture.beginStreamGeneration());
        CHECK(fixture.allocateStreamBlocks());
        CHECK(fixture.streamAllocationTableStatus
            == ZoneRuntimeTableStatus::Success);
        CHECK(fixture.streamAllocation->status
            == pmem_runtime::AllocationStatus::Success);
        CHECK(fixture.streamAllocationIndex + 1 == fixture.blocks.size());
        CHECK(fixture.streamAllocationAttemptCount == fixture.blocks.size());
        CHECK(fixture.bindStreams());
        CHECK(fixture.beginPendingCopies(true));
        CHECK(DriveCompositeAbandonmentToTerminal(fixture));
        auto *const stream =
            ZoneRuntimeTableTestAccess::StreamGenerationReceipt(
                fixture.table.get(), fixture.physicalSlot);
        auto *const pending =
            ZoneRuntimeTableTestAccess::PendingCopyAdmissionReceipt(
                fixture.table.get(), fixture.physicalSlot);
        CHECK(stream != nullptr);
        CHECK(pending != nullptr);
        CHECK(stream
            && stream->phase()
                == zone_stream_ownership::
                    ZoneStreamGenerationPhase::Invalidated);
        CHECK(pending
            && pending->phase()
                == zone_pending_copy::
                    PendingCopyAdmissionPhase::Discarded);
        CHECK(pending && pending->recordCount() == 0);
        CHECK(ResetCompositeTerminalReceipt(fixture));
    }
}

void TestCompositeScriptStringCapacityAndDemand()
{
    const pmem_runtime::InitializationStatus initialization =
        pmem_runtime::TryInitialize();
    CHECK(initialization
            == pmem_runtime::InitializationStatus::AlreadyInitialized
        || initialization
            == pmem_runtime::InitializationStatus::Success);
    if (initialization
            != pmem_runtime::InitializationStatus::AlreadyInitialized
        && initialization
            != pmem_runtime::InitializationStatus::Success)
    {
        return;
    }

    using JournalEntry =
        db::script_string_journal::ScriptStringJournalEntry;
    struct CapacitySpanAliasProbe final
    {
        alignas(JournalEntry)
            std::array<std::uint8_t, sizeof(JournalEntry)> prefix{};
        ZoneRuntimeTable table{};
    };

    // The first entry ends exactly where the table begins, but the complete
    // three-entry capacity span overlaps it. The preflight must cover the
    // capacity, not merely the one entry expected by this generation.
    auto aliasProbe = std::make_unique<CapacitySpanAliasProbe>();
    ZoneLoadContextKey aliasKey{};
    constexpr std::uint32_t aliasSlot = 31;
    CHECK(TryInitializeZoneRuntimeTable(&aliasProbe->table)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryClaimZoneRuntimeGeneration(
        &aliasProbe->table, aliasSlot, &aliasKey)
        == ZoneRuntimeTableStatus::Success);
    db::script_string_journal::ScriptStringJournal aliasJournal{};
    auto *const overlappingEntries = reinterpret_cast<JournalEntry *>(
        reinterpret_cast<std::uintptr_t>(&aliasProbe->table)
        - sizeof(JournalEntry));
    CHECK(reinterpret_cast<std::uintptr_t>(overlappingEntries)
            % alignof(JournalEntry)
        == 0);
    CHECK(TryBeginZoneRuntimeScriptStringOwnership(
        &aliasProbe->table,
        aliasSlot,
        aliasKey,
        &aliasJournal,
        overlappingEntries,
        3,
        1) == ZoneRuntimeTableStatus::InvalidArgument);
    CHECK(!aliasJournal.initialized());
    const ZoneRuntimeEntry *aliasEntry = nullptr;
    CHECK(TryGetZoneRuntimeEntry(
        &aliasProbe->table, aliasSlot, &aliasEntry)
        == ZoneRuntimeTableStatus::Success);
    CHECK(aliasEntry != nullptr);
    CHECK(aliasEntry
        && aliasEntry->scriptStringOwnership().isEmptyCanonical());
    CHECK(aliasProbe->table.initialized());

    // Binding retains the allocation capacity with zero expected demand.
    // An oversized demand must leave that placed state retryable; only the
    // successful lower ownership begin publishes the actual expected count.
    {
        CompositeRuntimeFixture fixture{};
        CHECK(fixture.enroll(31));
        CHECK(fixture.beginAllocation());
        CHECK(fixture.setupStorage(3));
        auto *const storage = ZoneRuntimeTableTestAccess::StorageBinding(
            fixture.table.get(), fixture.physicalSlot);
        CHECK(storage != nullptr);
        auto *const journal = storage
            ? storage->scriptStringJournal()
            : nullptr;
        CHECK(journal != nullptr);
        CHECK(storage && storage->scriptStringEntries() != nullptr);
        CHECK(journal && !journal->initialized());
        CHECK(fixture.setupStreams());
        CHECK(fixture.beginPendingCopies());

        CHECK(TryBeginZoneRuntimeScriptStringOwnership(
            fixture.table.get(),
            fixture.physicalSlot,
            fixture.key,
            journal,
            storage ? storage->scriptStringEntries() : nullptr,
            fixture.storagePlan.scriptStringCapacity,
            4) == ZoneRuntimeTableStatus::CapacityExceeded);
        const ZoneRuntimeEntry *entry = fixture.entry();
        CHECK(entry
            && entry->setupStage()
                == ZoneRuntimeSetupStage::PendingCopyBegun);
        CHECK(entry
            && entry->scriptStringOwnership().isEmptyCanonical());
        CHECK(journal && !journal->initialized());
        CHECK(fixture.table->initialized());

        CHECK(fixture.beginExactScriptStrings(1));
        entry = fixture.entry();
        CHECK(entry
            && entry->setupStage()
                == ZoneRuntimeSetupStage::ScriptStringsBegun);
        CHECK(journal && journal->initialized());
        CHECK(journal && journal->capacity() == 3);
        CHECK(journal && journal->expectedCount() == 1);
        CHECK(journal && journal->entryCount() == 0);

        CHECK(DriveCompositeAbandonmentToTerminal(fixture));
        CHECK(storage && storage->destroyed());
        CHECK(ResetCompositeTerminalReceipt(fixture));
    }

    // A nonempty retained placement with zero actual strings is canonical
    // through ownership begin and complete storage teardown.
    {
        CompositeRuntimeFixture fixture{};
        CHECK(fixture.enroll(32));
        CHECK(fixture.beginAllocation());
        CHECK(fixture.setupStorage(3));
        CHECK(fixture.setupStreams());
        CHECK(fixture.beginPendingCopies());
        auto *const storage = ZoneRuntimeTableTestAccess::StorageBinding(
            fixture.table.get(), fixture.physicalSlot);
        CHECK(storage != nullptr);
        auto *const journal = storage
            ? storage->scriptStringJournal()
            : nullptr;
        CHECK(journal != nullptr);

        CHECK(fixture.beginExactScriptStrings(0));
        const ZoneRuntimeEntry *const entry = fixture.entry();
        CHECK(entry
            && entry->setupStage()
                == ZoneRuntimeSetupStage::ScriptStringsBegun);
        CHECK(journal && journal->initialized());
        CHECK(journal && journal->capacity() == 3);
        CHECK(journal && journal->expectedCount() == 0);
        CHECK(journal && journal->entryCount() == 0);

        CHECK(DriveCompositeAbandonmentToTerminal(fixture));
        CHECK(storage && storage->destroyed());
        CHECK(ResetCompositeTerminalReceipt(fixture));
    }
}

void TestCompositeRecoverablePlacementAndRangeRejection()
{
    CompositeRuntimeFixture fixture{};
    CHECK(fixture.enroll(26));
    CHECK(fixture.beginAllocation());

    // Output publication is part of the allocation trust boundary. A bad
    // destination must be rejected before the PMem cursor moves, even when
    // the requested allocation and exact generation authority are valid.
    constexpr std::uint32_t kAllocationProbeSize = 48;
    constexpr std::uint32_t kAllocationProbeAlignment = 16;
    pmem_runtime::AllocationResult canaryAllocation{};
    CHECK(TryAllocateZoneRuntimeMemory(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        64,
        kAllocationProbeAlignment,
        0,
        &canaryAllocation)
        == ZoneRuntimeTableStatus::Success);
    CHECK(canaryAllocation.status
        == pmem_runtime::AllocationStatus::Success);
    CHECK(canaryAllocation.address != nullptr);
    if (!canaryAllocation.address)
        return;

    const pmem_runtime::DiagnosticSnapshot allocationCursor =
        pmem_runtime::TryCaptureDiagnosticSnapshot();
    CHECK(allocationCursor.status
        == pmem_runtime::DiagnosticSnapshotStatus::Success);
    CHECK(allocationCursor.lowCount != 0);
    const auto checkAllocationCursorUnchanged = [&]() noexcept {
        const pmem_runtime::DiagnosticSnapshot current =
            pmem_runtime::TryCaptureDiagnosticSnapshot();
        CHECK(current.status
            == pmem_runtime::DiagnosticSnapshotStatus::Success);
        CHECK(current.freeBytes == allocationCursor.freeBytes);
        CHECK(current.lowCount == allocationCursor.lowCount);
        CHECK(current.highCount == allocationCursor.highCount);
        if (current.lowCount == allocationCursor.lowCount
            && current.lowCount != 0)
        {
            CHECK(current.low[current.lowCount - 1].bytes
                == allocationCursor.low[allocationCursor.lowCount - 1]
                       .bytes);
        }
        if (current.highCount == allocationCursor.highCount
            && current.highCount != 0)
        {
            CHECK(current.high[current.highCount - 1].bytes
                == allocationCursor
                       .high[allocationCursor.highCount - 1]
                       .bytes);
        }
    };

    // The destination is aligned but lies wholly inside the managed extent.
    // Preserve a byte canary as well as the allocator diagnostic cursor.
    auto *const managedOutput =
        reinterpret_cast<pmem_runtime::AllocationResult *>(
            canaryAllocation.address);
    for (std::size_t index = 0;
         index < sizeof(pmem_runtime::AllocationResult);
         ++index)
    {
        canaryAllocation.address[index] = UINT8_C(0xA7);
    }
    CHECK(TryAllocateZoneRuntimeMemory(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        kAllocationProbeSize,
        kAllocationProbeAlignment,
        0,
        managedOutput)
        == ZoneRuntimeTableStatus::InvalidArgument);
    for (std::size_t index = 0;
         index < sizeof(pmem_runtime::AllocationResult);
         ++index)
    {
        CHECK(canaryAllocation.address[index] == UINT8_C(0xA7));
    }
    checkAllocationCursorUnchanged();

    // A table-relative destination could overwrite the durable authority
    // that post-authentication depends on. Its complete output-sized prefix
    // must remain byte-for-byte unchanged.
    std::array<std::uint8_t, sizeof(pmem_runtime::AllocationResult)>
        tablePrefix{};
    const auto *const tableBytes =
        reinterpret_cast<const std::uint8_t *>(fixture.table.get());
    for (std::size_t index = 0; index < tablePrefix.size(); ++index)
        tablePrefix[index] = tableBytes[index];
    CHECK(TryAllocateZoneRuntimeMemory(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        kAllocationProbeSize,
        kAllocationProbeAlignment,
        0,
        reinterpret_cast<pmem_runtime::AllocationResult *>(
            fixture.table.get()))
        == ZoneRuntimeTableStatus::InvalidArgument);
    for (std::size_t index = 0; index < tablePrefix.size(); ++index)
        CHECK(tableBytes[index] == tablePrefix[index]);
    checkAllocationCursorUnchanged();

    alignas(pmem_runtime::AllocationResult)
        std::array<std::uint8_t,
            sizeof(pmem_runtime::AllocationResult) + 1>
            misalignedOutput{};
    misalignedOutput.fill(UINT8_C(0x5D));
    const auto misalignedBefore = misalignedOutput;
    CHECK(TryAllocateZoneRuntimeMemory(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        kAllocationProbeSize,
        kAllocationProbeAlignment,
        0,
        reinterpret_cast<pmem_runtime::AllocationResult *>(
            misalignedOutput.data() + 1))
        == ZoneRuntimeTableStatus::InvalidArgument);
    CHECK(misalignedOutput == misalignedBefore);
    checkAllocationCursorUnchanged();

    struct alignas(
        alignof(ZoneLoadContextKey) > alignof(pmem_runtime::AllocationResult)
            ? alignof(ZoneLoadContextKey)
            : alignof(pmem_runtime::AllocationResult))
        KeyAliasedOutputStorage final
    {
        ZoneLoadContextKey key{};
        std::array<std::uint8_t,
            sizeof(pmem_runtime::AllocationResult)> tail{};
    };
    KeyAliasedOutputStorage keyAliasedOutput{};
    keyAliasedOutput.key = fixture.key;
    keyAliasedOutput.tail.fill(UINT8_C(0x6B));
    const auto keyAliasTailBefore = keyAliasedOutput.tail;
    CHECK(TryAllocateZoneRuntimeMemory(
        fixture.table.get(),
        fixture.physicalSlot,
        keyAliasedOutput.key,
        kAllocationProbeSize,
        kAllocationProbeAlignment,
        0,
        reinterpret_cast<pmem_runtime::AllocationResult *>(
            &keyAliasedOutput.key))
        == ZoneRuntimeTableStatus::InvalidArgument);
    CHECK(keyAliasedOutput.key == fixture.key);
    CHECK(keyAliasedOutput.tail == keyAliasTailBefore);
    checkAllocationCursorUnchanged();

    const ZoneRuntimeEntry *entry = fixture.entry();
    CHECK(entry
        && entry->setupStage()
            == ZoneRuntimeSetupStage::AllocationBegun);
    auto *const allocationReceipt =
        ZoneRuntimeTableTestAccess::AllocationReceipt(
            fixture.table.get(), fixture.physicalSlot);
    CHECK(allocationReceipt != nullptr);
    CHECK(allocationReceipt
        && pmem_runtime::TryAuthenticateAllocationReceipt(
               allocationReceipt,
               fixture.allocationType,
               pmem_runtime::AllocationReceiptPhase::Begun)
            == pmem_runtime::AllocationReceiptStatus::Success);

    pmem_runtime::AllocationResult nextAllocation{};
    CHECK(TryAllocateZoneRuntimeMemory(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        kAllocationProbeSize,
        kAllocationProbeAlignment,
        0,
        &nextAllocation)
        == ZoneRuntimeTableStatus::Success);
    CHECK(nextAllocation.status
        == pmem_runtime::AllocationStatus::Success);
    CHECK(nextAllocation.address == canaryAllocation.address + 64);

    CHECK(fixture.allocateStorage());
    if (!fixture.slabAllocation.address)
        return;

    auto *const storage = ZoneRuntimeTableTestAccess::StorageBinding(
        fixture.table.get(), fixture.physicalSlot);
    CHECK(storage != nullptr);
    CHECK(storage && !storage->bound() && !storage->destroyed());

    zone_runtime_storage::ZoneRuntimeStoragePlan malformedPlan =
        fixture.storagePlan;
    ++malformedPlan.arenaBudget;
    CHECK(TryBindZoneRuntimeStorage(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        fixture.slabAllocation.address,
        fixture.storagePlan.totalBytes,
        &malformedPlan)
        == ZoneRuntimeTableStatus::InvalidArgument);

    std::vector<std::uint8_t> foreignStorage(
        fixture.storagePlan.totalBytes);
    CHECK(TryBindZoneRuntimeStorage(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        foreignStorage.data(),
        foreignStorage.size(),
        &fixture.storagePlan)
        == ZoneRuntimeTableStatus::InvalidArgument);
    CHECK(TryBindZoneRuntimeStorage(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        fixture.slabAllocation.address,
        fixture.storagePlan.totalBytes - 1,
        &fixture.storagePlan)
        == ZoneRuntimeTableStatus::InvalidArgument);
    entry = fixture.entry();
    CHECK(entry
        && entry->setupStage()
            == ZoneRuntimeSetupStage::AllocationBegun);
    CHECK(storage && !storage->bound() && !storage->destroyed());
    CHECK(fixture.bindStorage());

    CHECK(fixture.beginStreamGeneration());
    CHECK(fixture.allocateStreamBlocks());
    auto *const stream =
        ZoneRuntimeTableTestAccess::StreamGenerationReceipt(
            fixture.table.get(), fixture.physicalSlot);
    auto *const active =
        ZoneRuntimeTableTestAccess::ActiveStreamBinding(
            fixture.table.get());
    CHECK(stream != nullptr);
    CHECK(active != nullptr);

    alignas(db::relocation::BlockView)
        std::array<std::uint8_t,
            sizeof(db::relocation::BlockView)
                    * db::relocation::kBlockCount
                + 1>
            misalignedBlockStorage{};
    misalignedBlockStorage.fill(UINT8_C(0x4E));
    const auto misalignedBlockStorageBefore = misalignedBlockStorage;
    CHECK(TryBindZoneRuntimeStreams(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        &fixture.zone,
        reinterpret_cast<const db::relocation::BlockView *>(
            misalignedBlockStorage.data() + 1),
        db::relocation::kBlockCount)
        == ZoneRuntimeTableStatus::InvalidArgument);
    CHECK(misalignedBlockStorage == misalignedBlockStorageBefore);
    entry = fixture.entry();
    CHECK(entry
        && entry->setupStage()
            == ZoneRuntimeSetupStage::StreamGenerationBegun);
    CHECK(stream
        && stream->phase()
            == zone_stream_ownership::
                ZoneStreamGenerationPhase::NeverBound);
    CHECK(active
        && active->phase()
            == zone_stream_ownership::ActiveZoneStreamPhase::Idle);

    // Descriptor snapshots have no mutation hook in this target. Cover the
    // immutable identity boundary directly: a zone identity cannot borrow
    // table storage, and rejection must leave both authorities unpublished.
    CHECK(TryBindZoneRuntimeStreams(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        reinterpret_cast<const XZoneMemory *>(fixture.table.get()),
        fixture.blocks.data(),
        fixture.blocks.size())
        == ZoneRuntimeTableStatus::InvalidArgument);
    entry = fixture.entry();
    CHECK(entry
        && entry->setupStage()
            == ZoneRuntimeSetupStage::StreamGenerationBegun);
    CHECK(stream
        && stream->phase()
            == zone_stream_ownership::
                ZoneStreamGenerationPhase::NeverBound);
    CHECK(active
        && active->phase()
            == zone_stream_ownership::ActiveZoneStreamPhase::Idle);

    // All ranges belong to this receipt, but block zero aliases the already
    // placed runtime-storage slab. The composite owner must reject that
    // cross-component overlap before publishing the process singleton.
    CHECK(fixture.storagePlan.totalBytes >= 64);
    XZoneMemory overlappingZone = fixture.zone;
    auto overlappingBlocks = fixture.blocks;
    overlappingZone.blocks[0].data = fixture.slabAllocation.address;
    overlappingZone.blocks[0].size = 64;
    overlappingBlocks[0] = {
        reinterpret_cast<std::uintptr_t>(
            fixture.slabAllocation.address),
        64,
    };
    CHECK(TryBindZoneRuntimeStreams(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        &overlappingZone,
        overlappingBlocks.data(),
        overlappingBlocks.size())
        == ZoneRuntimeTableStatus::InvalidArgument);
    entry = fixture.entry();
    CHECK(entry
        && entry->setupStage()
            == ZoneRuntimeSetupStage::StreamGenerationBegun);
    CHECK(stream
        && stream->phase()
            == zone_stream_ownership::
                ZoneStreamGenerationPhase::NeverBound);
    CHECK(active
        && active->phase()
            == zone_stream_ownership::ActiveZoneStreamPhase::Idle);

    alignas(16) std::array<std::uint8_t, 64> foreignBlock{};
    auto mixedBlocks = fixture.blocks;
    mixedBlocks[4].base = reinterpret_cast<std::uintptr_t>(
        foreignBlock.data());
    CHECK(TryBindZoneRuntimeStreams(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        &fixture.zone,
        mixedBlocks.data(),
        mixedBlocks.size())
        == ZoneRuntimeTableStatus::InvalidArgument);
    entry = fixture.entry();
    CHECK(entry
        && entry->setupStage()
            == ZoneRuntimeSetupStage::StreamGenerationBegun);
    CHECK(stream
        && stream->phase()
            == zone_stream_ownership::
                ZoneStreamGenerationPhase::NeverBound);
    CHECK(active
        && active->phase()
            == zone_stream_ownership::ActiveZoneStreamPhase::Idle);
    CHECK(fixture.bindStreams());
    CHECK(fixture.beginPendingCopies());

    db::script_string_journal::ScriptStringJournal foreignJournal{};
    CHECK(TryBeginZoneRuntimeScriptStringOwnership(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        &foreignJournal,
        nullptr,
        0,
        0)
        == ZoneRuntimeTableStatus::InvalidArgument);
    auto *const pending =
        ZoneRuntimeTableTestAccess::PendingCopyAdmissionReceipt(
            fixture.table.get(), fixture.physicalSlot);
    CHECK(pending != nullptr);
    CHECK(pending
        && pending->phase()
            == zone_pending_copy::PendingCopyAdmissionPhase::Collecting);
    CHECK(pending && pending->recordCount() == 0);
    entry = fixture.entry();
    CHECK(entry
        && entry->setupStage()
            == ZoneRuntimeSetupStage::PendingCopyBegun);
    CHECK(fixture.beginExactScriptStrings());

    CHECK(DriveCompositeAbandonmentToTerminal(fixture));
    CHECK(ResetCompositeTerminalReceipt(fixture));
}

void TestCompositeStageOutputAliasPreflight()
{
    ResetBackend();
    CompositeRuntimeFixture fixture{};
    CHECK(fixture.enroll(28));
    CHECK(fixture.beginAllocation());

    pmem_runtime::AllocationResult managedOutputAllocation{};
    CHECK(TryAllocateZoneRuntimeMemory(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        64,
        16,
        0,
        &managedOutputAllocation)
        == ZoneRuntimeTableStatus::Success);
    CHECK(managedOutputAllocation.status
        == pmem_runtime::AllocationStatus::Success);
    CHECK(managedOutputAllocation.address != nullptr);
    if (!managedOutputAllocation.address)
        return;

    constexpr std::size_t kManagedOutputBytes = 64;
    std::array<std::byte, kManagedOutputBytes> managedOutputBefore{};
    for (std::size_t index = 0; index < kManagedOutputBytes; ++index)
        managedOutputAllocation.address[index] = UINT8_C(0x3C);
    std::memcpy(
        managedOutputBefore.data(),
        managedOutputAllocation.address,
        managedOutputBefore.size());
    const auto managedOutputUnchanged = [&]() noexcept {
        CHECK(std::memcmp(
            managedOutputBefore.data(),
            managedOutputAllocation.address,
            managedOutputBefore.size()) == 0);
    };

    CHECK(TryGetZoneRuntimeEntry(
        fixture.table.get(),
        fixture.physicalSlot,
        reinterpret_cast<const ZoneRuntimeEntry **>(
            managedOutputAllocation.address))
        == ZoneRuntimeTableStatus::InvalidArgument);
    managedOutputUnchanged();
    CHECK(TryGetZoneRuntimeGeneration(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        reinterpret_cast<ZoneRuntimeGenerationView *>(
            managedOutputAllocation.address))
        == ZoneRuntimeTableStatus::InvalidArgument);
    managedOutputUnchanged();
    constexpr std::uint32_t managedClaimSlot = 32;
    CHECK(TryClaimZoneRuntimeGeneration(
        fixture.table.get(),
        managedClaimSlot,
        reinterpret_cast<ZoneLoadContextKey *>(
            managedOutputAllocation.address))
        == ZoneRuntimeTableStatus::InvalidArgument);
    managedOutputUnchanged();
    const auto *const managedClaimLifecycle =
        ZoneRuntimeTableTestAccess::Lifecycle(
            fixture.table.get(), managedClaimSlot);
    CHECK(managedClaimLifecycle != nullptr);
    CHECK(managedClaimLifecycle
        && managedClaimLifecycle->phase()
            == ZoneLoadContextPhase::Empty);

    // The singleton controls remain protected while the embedded binding is
    // still Idle. Each public output shape targets the same aligned stream
    // stack prefix and must fail before phase checks, allocation, or claim.
    constexpr std::size_t kIdleStreamOutputBytes = 64;
    constexpr std::size_t kIdleStreamOutputAlignment =
        alignof(ZoneRuntimeGenerationView)
            > alignof(pmem_runtime::AllocationResult)
        ? alignof(ZoneRuntimeGenerationView)
        : alignof(pmem_runtime::AllocationResult);
    static_assert(
        sizeof(g_streamPosStack)
        >= kIdleStreamOutputBytes + kIdleStreamOutputAlignment - 1);
    static_assert(
        kIdleStreamOutputAlignment >= alignof(ZoneLoadContextKey));
    static_assert(
        kIdleStreamOutputAlignment >= alignof(const ZoneRuntimeEntry *));
    void *idleStreamStorage = static_cast<void *>(g_streamPosStack);
    std::size_t idleStreamStorageBytes = sizeof(g_streamPosStack);
    void *const idleStreamOutput = std::align(
        kIdleStreamOutputAlignment,
        kIdleStreamOutputBytes,
        idleStreamStorage,
        idleStreamStorageBytes);
    CHECK(idleStreamOutput != nullptr);
    if (!idleStreamOutput)
        return;
    std::array<std::byte, kIdleStreamOutputBytes>
        idleStreamOutputBefore{};
    std::memcpy(
        idleStreamOutputBefore.data(),
        idleStreamOutput,
        idleStreamOutputBefore.size());
    const auto idleStreamOutputUnchanged = [&]() noexcept {
        CHECK(std::memcmp(
            idleStreamOutputBefore.data(),
            idleStreamOutput,
            idleStreamOutputBefore.size()) == 0);
    };
    CHECK(TryGetZoneRuntimeEntry(
        fixture.table.get(),
        fixture.physicalSlot,
        reinterpret_cast<const ZoneRuntimeEntry **>(idleStreamOutput))
        == ZoneRuntimeTableStatus::InvalidArgument);
    idleStreamOutputUnchanged();
    CHECK(TryClaimZoneRuntimeGeneration(
        fixture.table.get(),
        managedClaimSlot,
        reinterpret_cast<ZoneLoadContextKey *>(idleStreamOutput))
        == ZoneRuntimeTableStatus::InvalidArgument);
    idleStreamOutputUnchanged();
    CHECK(TryGetZoneRuntimeGeneration(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        reinterpret_cast<ZoneRuntimeGenerationView *>(idleStreamOutput))
        == ZoneRuntimeTableStatus::InvalidArgument);
    idleStreamOutputUnchanged();
    CHECK(TryAllocateZoneRuntimeMemory(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        32,
        16,
        0,
        reinterpret_cast<pmem_runtime::AllocationResult *>(idleStreamOutput))
        == ZoneRuntimeTableStatus::InvalidArgument);
    idleStreamOutputUnchanged();
    CHECK(TryStageZoneRuntimeScriptString(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        {"idle-stream-output\0", 19, 1},
        reinterpret_cast<std::uint32_t *>(idleStreamOutput))
        == ZoneRuntimeTableStatus::InvalidArgument);
    idleStreamOutputUnchanged();

    CHECK(fixture.setupStorage(1));
    CHECK(fixture.setupStreams());
    CHECK(fixture.beginPendingCopies());
    CHECK(fixture.beginExactScriptStrings());
    auto *const storage = ZoneRuntimeTableTestAccess::StorageBinding(
        fixture.table.get(), fixture.physicalSlot);
    CHECK(storage != nullptr);
    auto *const journal = storage ? storage->scriptStringJournal() : nullptr;
    CHECK(journal != nullptr);
    CHECK(journal && journal->entryCount() == 0);

    std::array<std::byte, sizeof(XZoneMemory)> zoneBefore{};
    std::memcpy(zoneBefore.data(), &fixture.zone, zoneBefore.size());
    const auto zoneUnchanged = [&]() noexcept {
        CHECK(std::memcmp(
            zoneBefore.data(), &fixture.zone, zoneBefore.size()) == 0);
    };
    CHECK(TryGetZoneRuntimeEntry(
        fixture.table.get(),
        fixture.physicalSlot,
        reinterpret_cast<const ZoneRuntimeEntry **>(&fixture.zone))
        == ZoneRuntimeTableStatus::InvalidArgument);
    zoneUnchanged();
    CHECK(TryGetZoneRuntimeGeneration(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        reinterpret_cast<ZoneRuntimeGenerationView *>(&fixture.zone))
        == ZoneRuntimeTableStatus::InvalidArgument);
    zoneUnchanged();
    CHECK(TryClaimZoneRuntimeGeneration(
        fixture.table.get(),
        managedClaimSlot,
        reinterpret_cast<ZoneLoadContextKey *>(&fixture.zone))
        == ZoneRuntimeTableStatus::InvalidArgument);
    zoneUnchanged();

    const pmem_runtime::DiagnosticSnapshot allocationBefore =
        pmem_runtime::TryCaptureDiagnosticSnapshot();
    CHECK(allocationBefore.status
        == pmem_runtime::DiagnosticSnapshotStatus::Success);
    CHECK(TryAllocateZoneRuntimeMemory(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        32,
        16,
        0,
        reinterpret_cast<pmem_runtime::AllocationResult *>(
            &fixture.zone))
        == ZoneRuntimeTableStatus::InvalidArgument);
    zoneUnchanged();
    const pmem_runtime::DiagnosticSnapshot allocationAfter =
        pmem_runtime::TryCaptureDiagnosticSnapshot();
    CHECK(allocationAfter.status
        == pmem_runtime::DiagnosticSnapshotStatus::Success);
    CHECK(allocationAfter.freeBytes == allocationBefore.freeBytes);
    CHECK(allocationAfter.lowCount == allocationBefore.lowCount);
    CHECK(allocationAfter.highCount == allocationBefore.highCount);
    CHECK(TryStageZoneRuntimeScriptString(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        {"zone-output\0", 12, 1},
        reinterpret_cast<std::uint32_t *>(&fixture.zone))
        == ZoneRuntimeTableStatus::InvalidArgument);
    zoneUnchanged();

    const std::uint8_t *const streamPosBefore = g_streamPos;
    CHECK(TryGetZoneRuntimeEntry(
        fixture.table.get(),
        fixture.physicalSlot,
        reinterpret_cast<const ZoneRuntimeEntry **>(
            static_cast<void *>(&g_streamPos)))
        == ZoneRuntimeTableStatus::InvalidArgument);
    CHECK(g_streamPos == streamPosBefore);

    std::array<std::byte, sizeof(ZoneLoadContextKey)>
        streamPosArrayBefore{};
    std::memcpy(
        streamPosArrayBefore.data(),
        g_streamPosArray,
        streamPosArrayBefore.size());
    CHECK(TryClaimZoneRuntimeGeneration(
        fixture.table.get(),
        managedClaimSlot,
        reinterpret_cast<ZoneLoadContextKey *>(
            static_cast<void *>(g_streamPosArray)))
        == ZoneRuntimeTableStatus::InvalidArgument);
    CHECK(std::memcmp(
        streamPosArrayBefore.data(),
        g_streamPosArray,
        streamPosArrayBefore.size()) == 0);

    std::array<std::byte, sizeof(ZoneRuntimeGenerationView)>
        streamDelayBefore{};
    std::memcpy(
        streamDelayBefore.data(),
        g_streamDelayArray,
        streamDelayBefore.size());
    CHECK(TryGetZoneRuntimeGeneration(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        reinterpret_cast<ZoneRuntimeGenerationView *>(
            static_cast<void *>(g_streamDelayArray)))
        == ZoneRuntimeTableStatus::InvalidArgument);
    CHECK(std::memcmp(
        streamDelayBefore.data(),
        g_streamDelayArray,
        streamDelayBefore.size()) == 0);

    std::array<std::byte, sizeof(pmem_runtime::AllocationResult)>
        streamStackBefore{};
    std::memcpy(
        streamStackBefore.data(),
        g_streamPosStack,
        streamStackBefore.size());
    CHECK(TryAllocateZoneRuntimeMemory(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        32,
        16,
        0,
        reinterpret_cast<pmem_runtime::AllocationResult *>(
            static_cast<void *>(g_streamPosStack)))
        == ZoneRuntimeTableStatus::InvalidArgument);
    CHECK(std::memcmp(
        streamStackBefore.data(),
        g_streamPosStack,
        streamStackBefore.size()) == 0);

    const std::uint32_t streamIndexBefore = g_streamPosIndex;
    CHECK(TryStageZoneRuntimeScriptString(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        {"stream-output\0", 14, 1},
        &g_streamPosIndex)
        == ZoneRuntimeTableStatus::InvalidArgument);
    CHECK(g_streamPosIndex == streamIndexBefore);
    CHECK(runtime_table_backend::backend.acquireCalls == 0);
    CHECK(managedClaimLifecycle
        && managedClaimLifecycle->phase()
            == ZoneLoadContextPhase::Empty);

    auto *const allocationReceipt =
        ZoneRuntimeTableTestAccess::AllocationReceipt(
            fixture.table.get(), fixture.physicalSlot);
    CHECK(allocationReceipt != nullptr);
    std::array<std::uint8_t, sizeof(std::uint32_t)>
        tableOutputBefore{};
    const auto *const tableOutputBytes =
        reinterpret_cast<const std::uint8_t *>(allocationReceipt);
    for (std::size_t index = 0; index < tableOutputBefore.size(); ++index)
        tableOutputBefore[index] = tableOutputBytes[index];
    CHECK(TryStageZoneRuntimeScriptString(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        {"table-output\0", 13, 1},
        reinterpret_cast<std::uint32_t *>(allocationReceipt))
        == ZoneRuntimeTableStatus::InvalidArgument);
    for (std::size_t index = 0; index < tableOutputBefore.size(); ++index)
        CHECK(tableOutputBytes[index] == tableOutputBefore[index]);

    auto *const managedOutput = reinterpret_cast<std::uint32_t *>(
        managedOutputAllocation.address);
    CHECK(TryStageZoneRuntimeScriptString(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        {"managed-output\0", 15, 1},
        managedOutput)
        == ZoneRuntimeTableStatus::InvalidArgument);
    for (std::size_t index = 0; index < sizeof(*managedOutput); ++index)
        CHECK(managedOutputAllocation.address[index] == UINT8_C(0x3C));

    ZoneLoadContextKey keyAliasedOutput = fixture.key;
    CHECK(TryStageZoneRuntimeScriptString(
        fixture.table.get(),
        fixture.physicalSlot,
        keyAliasedOutput,
        {"key-output\0", 11, 1},
        reinterpret_cast<std::uint32_t *>(&keyAliasedOutput))
        == ZoneRuntimeTableStatus::InvalidArgument);
    CHECK(keyAliasedOutput == fixture.key);

    db::script_string_adapter::ScriptStringSourceView
        sourceAliasedOutput{"source-output\0", 14, 1};
    const auto sourceBytesBefore = sourceAliasedOutput.bytes;
    const std::uint32_t sourceCountBefore =
        sourceAliasedOutput.byteCount;
    const int sourceTypeBefore = sourceAliasedOutput.type;
    CHECK(TryStageZoneRuntimeScriptString(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        sourceAliasedOutput,
        reinterpret_cast<std::uint32_t *>(
            &sourceAliasedOutput))
        == ZoneRuntimeTableStatus::InvalidArgument);
    CHECK(sourceAliasedOutput.bytes == sourceBytesBefore);
    CHECK(sourceAliasedOutput.byteCount == sourceCountBefore);
    CHECK(sourceAliasedOutput.type == sourceTypeBefore);

    CHECK(runtime_table_backend::backend.acquireCalls == 0);
    CHECK(journal && journal->entryCount() == 0);
    const ZoneRuntimeEntry *const entry = fixture.entry();
    CHECK(entry
        && entry->setupStage()
            == ZoneRuntimeSetupStage::ScriptStringsBegun);
    CHECK(entry
        && entry->scriptStringOwnership().phase()
            == ZoneScriptStringOwnershipPhase::Staging);
    CHECK(fixture.table->initialized());

    PushAcquire(script_string::AcquireStatus::Acquired, 4242);
    std::uint32_t validOutput = 0;
    CHECK(TryStageZoneRuntimeScriptString(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        {"valid\0", 6, 1},
        &validOutput)
        == ZoneRuntimeTableStatus::Success);
    CHECK(validOutput == 4242);
    CHECK(runtime_table_backend::backend.acquireCalls == 1);
    CHECK(journal && journal->entryCount() == 1);

    PushOrdinary(script_string::ReleaseStatus::Success);
    CHECK(DriveCompositeAbandonmentToTerminal(fixture));
    CHECK(runtime_table_backend::backend.ordinaryCalls == 1);
    CHECK(ResetCompositeTerminalReceipt(fixture));
}

void TestCompositeCallbackContextAliasPreflightAndDrain()
{
    CompositeRuntimeFixture fixture{};
    fixture.physicalSlot = 29;
    CHECK(TryInitializeZoneRuntimeTable(fixture.table.get())
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryClaimZoneRuntimeGeneration(
        fixture.table.get(), fixture.physicalSlot, &fixture.key)
        == ZoneRuntimeTableStatus::Success);
    fixture.driver.table = fixture.table.get();
    fixture.driver.key = fixture.key;

    ZoneRuntimeGenerationCallbacks tableAliasedCallbacks =
        MakeCompositeRuntimeCallbacks(&fixture.driver);
    tableAliasedCallbacks.context = fixture.table.get();
    CHECK(TryBindZoneRuntimeGenerationCallbacks(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        tableAliasedCallbacks)
        == ZoneRuntimeTableStatus::InvalidArgument);
    const ZoneRuntimeEntry *entry = fixture.entry();
    CHECK(entry != nullptr);
    CHECK(entry && entry->generationBindingPristine());
    CHECK(entry
        && entry->setupStage() == ZoneRuntimeSetupStage::Passive);
    auto *const ledger = ZoneRuntimeTableTestAccess::PendingCopyLedger(
        fixture.table.get());
    CHECK(ledger != nullptr);
    CHECK(ledger && !ledger->initialized());
    CHECK(ZoneRuntimeTableTestAccess::SharedResourcesPristine(
        fixture.table.get()));

    CHECK(TryBindZoneRuntimeGenerationCallbacks(
        fixture.table.get(),
        fixture.physicalSlot,
        fixture.key,
        MakeCompositeRuntimeCallbacks(&fixture.driver))
        == ZoneRuntimeTableStatus::Success);
    CHECK(fixture.beginAllocation());
    CHECK(fixture.setupStorage());
    CHECK(fixture.setupStreams());
    CHECK(fixture.beginPendingCopies(true));
    CHECK(fixture.beginExactScriptStrings());
    CHECK(TrySealZoneRuntimeScriptStrings(
        fixture.table.get(), fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryBeginZoneRuntimeScriptStringTransfer(
        fixture.table.get(), fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryTransferNextZoneRuntimeScriptString(
        fixture.table.get(), fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryPrepareZoneRuntimeScriptStringCommit(
        fixture.table.get(), fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryPrepareZoneRuntimeAdmission(
        fixture.table.get(), fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryInvalidateZoneRuntimeStreams(
        fixture.table.get(), fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryEndZoneRuntimePhysicalAllocation(
        fixture.table.get(), fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryCommitZoneRuntimeGeneration(
        fixture.table.get(), fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);

    auto *const pending =
        ZoneRuntimeTableTestAccess::PendingCopyAdmissionReceipt(
            fixture.table.get(), fixture.physicalSlot);
    CHECK(pending != nullptr);
    CHECK(pending
        && pending->phase()
            == zone_pending_copy::PendingCopyAdmissionPhase::Admitted);
    CHECK(pending && pending->recordCount() == 1);
    CHECK(TryBeginZoneRuntimePendingCopyDrain(
        fixture.table.get(),
        {fixture.table.get(), ConsumePendingCopy})
        == ZoneRuntimeTableStatus::InvalidArgument);
    CHECK(pending
        && pending->phase()
            == zone_pending_copy::PendingCopyAdmissionPhase::Admitted);
    CHECK(pending && pending->recordCount() == 1);
    CHECK(fixture.table->initialized());

    PendingCopyDrainProbe probe{};
    CHECK(TryBeginZoneRuntimePendingCopyDrain(
        fixture.table.get(), {&probe, ConsumePendingCopy})
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryDrainNextZoneRuntimePendingCopy(fixture.table.get())
        == ZoneRuntimeTableStatus::Success);
    CHECK(probe.calls == 1);
    CHECK(probe.record.key == fixture.key);
    CHECK(probe.record.assetEntryIndex == 7);
    CHECK(TryFinishZoneRuntimePendingCopyDrain(fixture.table.get())
        == ZoneRuntimeTableStatus::Success);
    CHECK(pending
        && pending->phase()
            == zone_pending_copy::PendingCopyAdmissionPhase::Drained);

    CHECK(TryUnloadZoneRuntimeGeneration(
        fixture.table.get(), fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryResetZoneRuntimeTerminalReceipt(
        fixture.table.get(), fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(ZoneRuntimeTableTestAccess::ReceiptCapsulePristine(
        fixture.table.get(), fixture.physicalSlot));
}

void TestCompositeCallbacksRejectLegacyOwnershipBeforeMutation()
{
    ResetBackend();
    auto table = std::make_unique<ZoneRuntimeTable>();
    constexpr std::uint32_t physicalSlot = 30;
    ZoneLoadContextKey key{};
    CHECK(TryInitializeZoneRuntimeTable(table.get())
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryClaimZoneRuntimeGeneration(
        table.get(), physicalSlot, &key)
        == ZoneRuntimeTableStatus::Success);

    db::script_string_journal::ScriptStringJournal legacyJournal{};
    CHECK(TryBeginZoneRuntimeScriptStringOwnership(
        table.get(),
        physicalSlot,
        key,
        &legacyJournal,
        nullptr,
        0,
        0) == ZoneRuntimeTableStatus::Success);

    auto *const ownership = ZoneRuntimeTableTestAccess::Ownership(
        table.get(), physicalSlot);
    auto *const pending = ZoneRuntimeTableTestAccess::PendingCopyLedger(
        table.get());
    CHECK(ownership != nullptr);
    CHECK(pending != nullptr);
    CHECK(ownership
        && ownership->phase()
            == ZoneScriptStringOwnershipPhase::Staging);
    CHECK(pending && !pending->initialized());
    CHECK(ZoneRuntimeTableTestAccess::SharedResourcesPristine(table.get()));

    CompositeRuntimeDriver driver{};
    driver.table = table.get();
    driver.key = key;
    CHECK(TryBindZoneRuntimeGenerationCallbacks(
        table.get(),
        physicalSlot,
        key,
        MakeCompositeRuntimeCallbacks(&driver))
        == ZoneRuntimeTableStatus::InvalidPhase);

    const ZoneRuntimeEntry *entry = nullptr;
    CHECK(TryGetZoneRuntimeEntry(
        table.get(), physicalSlot, &entry)
        == ZoneRuntimeTableStatus::Success);
    CHECK(entry != nullptr);
    CHECK(entry && entry->generationBindingPristine());
    CHECK(entry
        && entry->setupStage() == ZoneRuntimeSetupStage::Passive);
    CHECK(ownership
        && ownership->phase()
            == ZoneScriptStringOwnershipPhase::Staging);
    CHECK(pending && !pending->initialized());
    CHECK(ZoneRuntimeTableTestAccess::SharedResourcesPristine(table.get()));
    CHECK(table->initialized());

    std::uint32_t cleanupCount = 0;
    CHECK(zone_ownership::TryBeginZoneScriptStringRollback(
        ownership,
        {&cleanupCount, EnsureUnreachable, CompleteCleanup})
        == ZoneScriptStringOwnershipStatus::Success);
    CHECK(zone_ownership::TryFinishZoneScriptStringAbandonment(ownership)
        == ZoneScriptStringOwnershipStatus::Success);
    CHECK(cleanupCount == 8);
}

void TestCompositeAbandonmentRetryAndReentryPreservation()
{
    CompositeRuntimeFixture fixture{};
    CHECK(fixture.enroll(27));
    CHECK(fixture.beginAllocation());
    CHECK(fixture.setupStorage());
    CHECK(fixture.setupStreams());
    CHECK(fixture.beginPendingCopies(true));
    CHECK(fixture.beginExactScriptStrings());
    fixture.driver.retryEnsure = true;
    fixture.driver.attemptEnsureReentry = true;
    fixture.driver.retryReleaseGeometry = true;
    fixture.driver.attemptCleanupReentry = true;

    CHECK(TryBeginZoneRuntimeGenerationAbandonment(
        fixture.table.get(), fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Retry);
    CHECK(fixture.driver.ensureCalls == 1);
    CHECK(fixture.driver.retriedEnsure);
    CHECK(fixture.driver.attemptedEnsureReentry);
    CHECK(fixture.driver.ensureReentry == ZoneRuntimeTableStatus::Busy);
    const ZoneRuntimeEntry *entry = fixture.entry();
    CHECK(entry
        && entry->executionMode()
            == ZoneRuntimeExecutionMode::Abandoning);
    CHECK(entry
        && entry->setupStage()
            == ZoneRuntimeSetupStage::ScriptStringsBegun);
    auto *const storage = ZoneRuntimeTableTestAccess::StorageBinding(
        fixture.table.get(), fixture.physicalSlot);
    auto *const stream =
        ZoneRuntimeTableTestAccess::StreamGenerationReceipt(
            fixture.table.get(), fixture.physicalSlot);
    auto *const pending =
        ZoneRuntimeTableTestAccess::PendingCopyAdmissionReceipt(
            fixture.table.get(), fixture.physicalSlot);
    auto *const allocation =
        ZoneRuntimeTableTestAccess::AllocationReceipt(
            fixture.table.get(), fixture.physicalSlot);
    CHECK(storage && storage->bound());
    CHECK(stream
        && stream->phase()
            == zone_stream_ownership::ZoneStreamGenerationPhase::Bound);
    CHECK(pending
        && pending->phase()
            == zone_pending_copy::PendingCopyAdmissionPhase::Collecting);
    CHECK(pending && pending->recordCount() == 1);
    CHECK(allocation
        && pmem_runtime::TryAuthenticateAllocationReceipt(
               allocation,
               fixture.allocationType,
               pmem_runtime::AllocationReceiptPhase::Begun)
            == pmem_runtime::AllocationReceiptStatus::Success);

    bool observedGeometryRetry = false;
    for (std::size_t attempt = 0; attempt < 16; ++attempt)
    {
        const ZoneRuntimeTableStatus status =
            TryContinueZoneRuntimeGenerationAbandonment(
                fixture.table.get(),
                fixture.physicalSlot,
                fixture.key);
        CHECK(status == ZoneRuntimeTableStatus::Success
            || status == ZoneRuntimeTableStatus::Retry);
        if (status == ZoneRuntimeTableStatus::Retry
            && fixture.driver.retriedReleaseGeometry)
        {
            observedGeometryRetry = true;
            break;
        }
    }
    CHECK(observedGeometryRetry);
    auto *const lifecycle = ZoneRuntimeTableTestAccess::Lifecycle(
        fixture.table.get(), fixture.physicalSlot);
    CHECK(lifecycle != nullptr);
    CHECK(lifecycle
        && lifecycle->nextCleanupOperation()
            == ZoneLoadCleanupOperation::ReleaseGeometry);
    CHECK(storage && storage->bound());
    CHECK(stream
        && stream->phase()
            == zone_stream_ownership::
                ZoneStreamGenerationPhase::Invalidated);
    CHECK(pending
        && pending->phase()
            == zone_pending_copy::PendingCopyAdmissionPhase::Discarded);
    CHECK(pending && pending->recordCount() == 0);
    CHECK(allocation
        && pmem_runtime::TryAuthenticateAllocationReceipt(
               allocation,
               fixture.allocationType,
               pmem_runtime::AllocationReceiptPhase::Begun)
            == pmem_runtime::AllocationReceiptStatus::Success);

    CHECK(ContinueCompositeAbandonmentToTerminal(fixture));
    CHECK(fixture.driver.retriedReleaseGeometry);
    CHECK(fixture.driver.attemptedCleanupReentry);
    CHECK(fixture.driver.cleanupReentry == ZoneRuntimeTableStatus::Busy);
    CHECK(storage && storage->destroyed());
    CHECK(allocation
        && pmem_runtime::TryAuthenticateAllocationReceipt(
               allocation,
               fixture.allocationType,
               pmem_runtime::AllocationReceiptPhase::Freed)
            == pmem_runtime::AllocationReceiptStatus::Success);
    CHECK(ResetCompositeTerminalReceipt(fixture));
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

void TestFacadeReleaseSafetyClassification()
{
    ZoneRuntimeTable pristine{};
    CHECK(ZoneRuntimeTableTestAccess::ReleaseSafety(nullptr)
        == ZoneRuntimeTableStatus::InvalidArgument);
    CHECK(ZoneRuntimeTableTestAccess::ReleaseSafety(&pristine)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryInitializeZoneRuntimeTable(&pristine)
        == ZoneRuntimeTableStatus::Success);
    CHECK(ZoneRuntimeTableTestAccess::ReleaseSafety(&pristine)
        == ZoneRuntimeTableStatus::Success);

    ResetBackend();
    MutableRuntimeFixture fixture{};
    CHECK(fixture.claim(22));
    CHECK(fixture.begin(1, 1) == ZoneRuntimeTableStatus::Success);
    CHECK(ZoneRuntimeTableTestAccess::ReleaseSafety(&fixture.table)
        == ZoneRuntimeTableStatus::Busy);

    MutableRollbackDriver driver{};
    driver.table = &fixture.table;
    driver.key = fixture.key;
    CHECK(TryBeginZoneRuntimeScriptStringRollback(
        &fixture.table,
        fixture.physicalSlot,
        fixture.key,
        MakeMutableRollbackCallbacks(&driver))
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryRollbackNextZoneRuntimeScriptString(
        &fixture.table, fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(TryFinishZoneRuntimeScriptStringAbandonment(
        &fixture.table, fixture.physicalSlot, fixture.key)
        == ZoneRuntimeTableStatus::Success);
    CHECK(ZoneRuntimeTableTestAccess::ReleaseSafety(&fixture.table)
        == ZoneRuntimeTableStatus::Success);

    ZoneRuntimeTable corrupt{};
    CHECK(TryInitializeZoneRuntimeTable(&corrupt)
        == ZoneRuntimeTableStatus::Success);
    ZoneRuntimeTableTestAccess::SetReserved(&corrupt, UINT32_C(0xFFFFFFFF));
    CHECK(ZoneRuntimeTableTestAccess::ReleaseSafety(&corrupt)
        == ZoneRuntimeTableStatus::UnsafeFailure);
    CHECK(!corrupt.initialized());
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
    TestLookupAndClaimOutputAliasPreflight();
    TestClaimAuthenticationAndAdjacentIsolation();
    TestGenerationAdvanceRejectsAba();
    TestPartialInitializationAndCorruptionFailClosed();
    TestHiddenCorruptionAndCleanupReentryFailClosed();
    TestControllerPhaseAndSerializerMatrix();
    TestPassiveOwnershipPlacementAliasPreflight();
    TestPassiveCallbackAliasPreflight();
    TestKeyedMutableCommitAndAuthentication();
    TestKeyedMutableRecoverableAbandonment();
    TestLiveUnloadRetryResetReuseAndAba();
    TestAbandonedReceiptResetAndGenerationExhaustion();
    TestTerminalAdapterPhaseSerializerAndCorruptionGates();
    TestPassiveTableWideSingletonAuthentication();
    TestPassiveReceiptPristineAuthentication();
    TestCompositeRuntimeLiveUnloadResetAndReuse();
    TestCompositePartialStageAbandonmentAndReuse();
    TestCompositeScriptStringCapacityAndDemand();
    TestCompositeRecoverablePlacementAndRangeRejection();
    TestCompositeStageOutputAliasPreflight();
    TestCompositeCallbackContextAliasPreflightAndDrain();
    TestCompositeCallbacksRejectLegacyOwnershipBeforeMutation();
    TestCompositeAbandonmentRetryAndReentryPreservation();
    TestFacadeReleaseSafetyClassification();

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
