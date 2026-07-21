#include <database/db_zone_runtime_facade.h>
#include <database/db_zone_runtime_table.h>
#include <database/db_zone_load_context.h>
#include <database/db_zone_script_string_ownership.h>
#include <database/db_registry_ownership_coordinator.h>
#include <database/db_script_string_journal.h>
#include <database/db_script_string_transaction.h>
#include <script/scr_string_transaction.h>

#include <qcommon/com_error.h>
#include <qcommon/sys_sync.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

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

namespace
{
using db::registry_ownership::RegistryOwnershipStatus;
using db::zone_load::ZoneLoadContextKey;
using db::zone_runtime::ProductionZoneRuntimeTable;
using db::zone_runtime::TryBeginZoneRuntimeScriptStringOwnership;
using db::zone_runtime::TryClaimZoneRuntimeGeneration;
using db::zone_runtime::TryInitializeZoneRuntimeTable;
using db::zone_runtime::ZoneRuntimeFacade;
using db::zone_runtime::ZoneRuntimeFacadeStatus;
using db::zone_runtime::ZoneRuntimeTable;
using db::zone_runtime::ZoneRuntimeTableStatus;

constexpr std::uint32_t kPhysicalSlot = 4;

enum class Event : std::uint8_t
{
    BeginAccess,
    FinishAccess,
    TableInitialize,
    TableClaim,
    OwnershipBegin,
    BorrowRegistry,
    AddUser4,
    FinishRegistry,
    BatchBegin,
    BatchFinish,
};

std::array<Event, 32> g_events{};
std::uint32_t g_eventCount = 0;
std::array<std::uint32_t, 4> g_addUser4StringIds{};
std::uint32_t g_addUser4CallCount = 0;
std::uint32_t g_beginBatchCallCount = 0;
std::uint32_t g_finishBatchCallCount = 0;
script_string::OwnershipBatch *g_activeBatch = nullptr;
std::uint64_t g_activeBatchSerial = 0;
std::uint64_t g_nextBatchSerial = 70;
bool g_batchCanonical = true;
script_string::OwnershipBatchStatus g_batchBeginStatus =
    script_string::OwnershipBatchStatus::Success;
script_string::OwnershipBatchStatus g_batchFinishStatus =
    script_string::OwnershipBatchStatus::Success;
script_string::DatabaseUserAddStatus g_addStatus =
    script_string::DatabaseUserAddStatus::Added;

void Record(const Event event) noexcept
{
    if (g_eventCount >= g_events.size())
        std::abort();
    g_events[g_eventCount++] = event;
}

[[nodiscard]] bool Check(
    const bool condition,
    const char *const message) noexcept
{
    if (!condition)
    {
        std::fprintf(
            stderr,
            "facade->table->controller->coordinator->registry chain test "
            "failed: %s\n",
            message);
    }
    return condition;
}

template <std::size_t Count>
[[nodiscard]] bool CheckEvents(
    const std::array<Event, Count> &expected,
    const char *const message) noexcept
{
    if (g_eventCount != Count)
        return Check(false, message);
    for (std::size_t index = 0; index < Count; ++index)
    {
        if (g_events[index] != expected[index])
            return Check(false, message);
    }
    return true;
}

void ResetHarness() noexcept
{
    g_eventCount = 0;
    g_addUser4StringIds = {};
    g_addUser4CallCount = 0;
    g_beginBatchCallCount = 0;
    g_finishBatchCallCount = 0;
    g_activeBatch = nullptr;
    g_activeBatchSerial = 0;
    g_batchCanonical = true;
    g_batchBeginStatus = script_string::OwnershipBatchStatus::Success;
    g_batchFinishStatus = script_string::OwnershipBatchStatus::Success;
    g_addStatus = script_string::DatabaseUserAddStatus::Added;
    const ZoneRuntimeFacadeStatus retiredAccess =
        ZoneRuntimeFacade::FinishAccess();
    (void)retiredAccess;
}

void AppendAddUser4Id(const std::uint32_t stringId) noexcept
{
    if (g_addUser4CallCount >= g_addUser4StringIds.size())
        std::abort();
    g_addUser4StringIds[g_addUser4CallCount++] = stringId;
}

struct FullChainFixture final
{
    ZoneRuntimeTable &table = ProductionZoneRuntimeTable();
    ZoneLoadContextKey key{};
    db::script_string_journal::ScriptStringJournal journal{};
    std::array<db::script_string_journal::ScriptStringJournalEntry, 4>
        entries{};

    [[nodiscard]] bool tableInitialized() const noexcept
    {
        return table.initialized();
    }

    bool enrollTable() noexcept
    {
        if (TryInitializeZoneRuntimeTable(&table)
                != ZoneRuntimeTableStatus::Success)
            return false;
        Record(Event::TableInitialize);
        if (TryClaimZoneRuntimeGeneration(&table, kPhysicalSlot, &key)
                != ZoneRuntimeTableStatus::Success)
            return false;
        Record(Event::TableClaim);
        return true;
    }

    bool beginOwnership() noexcept
    {
        const ZoneRuntimeTableStatus beginStatus =
            TryBeginZoneRuntimeScriptStringOwnership(
                &table,
                kPhysicalSlot,
                key,
                &journal,
                entries.data(),
                static_cast<std::uint32_t>(entries.size()),
                1);
        if (beginStatus != ZoneRuntimeTableStatus::Success)
            return false;
        Record(Event::OwnershipBegin);
        return true;
    }
};

[[nodiscard]] bool TestFullChainBorrowEnrollsAddAndFinishes() noexcept
{
    ResetHarness();
    FullChainFixture fixture{};

    if (!Check(
            ZoneRuntimeFacade::TryBeginAccess()
                == ZoneRuntimeFacadeStatus::Success,
            "initial facade access did not enroll")
        || !Check(fixture.enrollTable(),
            "table enroll did not complete")
        || !Check(fixture.tableInitialized(), "table not initialized")
        || !Check(fixture.beginOwnership(),
            "script-string ownership begin did not succeed"))
    {
        return false;
    }
    Record(Event::BeginAccess);

    const RegistryOwnershipStatus borrow =
        ZoneRuntimeFacade::TryBorrowRegistryOwnership(
            fixture.key.slot == 0 ? kPhysicalSlot : fixture.key.slot,
            fixture.key);
    if (!Check(
            borrow == RegistryOwnershipStatus::Success,
            "facade->table->controller->coordinator borrow did not enroll")
        || !Check(g_beginBatchCallCount == 0,
            "coordinator must not begin a batch on bare borrow"))
    {
        return false;
    }
    Record(Event::BorrowRegistry);

    if (!Check(
            ZoneRuntimeFacade::TryAddDatabaseUser4(7u)
                == RegistryOwnershipStatus::Success,
            "facade->coordinator->registry user-4 add failed")
        || !Check(g_beginBatchCallCount == 1,
            "coordinator did not begin one ownership batch on add")
        || !Check(
            ZoneRuntimeFacade::TryAddDatabaseUser4(8u)
                == RegistryOwnershipStatus::Success,
            "second registry add through chain failed")
        || !Check(g_beginBatchCallCount == 2,
            "second add must close and reopen the batch")
        || !Check(g_finishBatchCallCount == 2,
            "each operation must close its own batch")
        || !Check(g_addUser4CallCount == 2,
            "registry backend did not record both add calls")
        || !Check(g_addUser4StringIds[0] == 7u,
            "first add-user-4 id not observed by backend")
        || !Check(g_addUser4StringIds[1] == 8u,
            "second add-user-4 id not observed by backend"))
    {
        return false;
    }
    Record(Event::AddUser4);

    if (!Check(
            ZoneRuntimeFacade::FinishRegistryOwnership()
                == RegistryOwnershipStatus::Success,
            "facade->coordinator finish failed"))
    {
        return false;
    }
    Record(Event::FinishRegistry);

    if (!Check(
            ZoneRuntimeFacade::FinishAccess()
                != ZoneRuntimeFacadeStatus::Success,
            "facade access with an active controller must not retire to "
            "Success: the chain driver owns the controller, so the runtime "
            "table still retains the staging serializer until the next "
            "abandonment cycle runs"))
    {
        return false;
    }
    Record(Event::FinishAccess);
    const ZoneRuntimeFacadeStatus orphanFinish =
        ZoneRuntimeFacade::FinishAccess();
    (void)orphanFinish;

    if (!CheckEvents(
            std::array{
                Event::TableInitialize,
                Event::TableClaim,
                Event::OwnershipBegin,
                Event::BeginAccess,
                Event::BorrowRegistry,
                Event::BatchBegin,
                Event::BatchFinish,
                Event::BatchBegin,
                Event::BatchFinish,
                Event::AddUser4,
                Event::FinishRegistry,
                Event::FinishAccess,
            },
            "event ordering mismatch across the chain"))
    {
        std::fprintf(stderr, "actual events[%u]:\n", g_eventCount);
        for (std::uint32_t i = 0; i < g_eventCount; ++i)
        {
            std::fprintf(stderr, "  [%u]=%u\n", i,
                static_cast<unsigned>(g_events[i]));
        }
        return false;
    }
    return true;
}
} // namespace

FastCriticalSection db_hashCritSect{};

MT_ValidationLease::~MT_ValidationLease() noexcept = default;

namespace script_string
{
RegistryOwnershipAdmission::RegistryOwnershipAdmission(
    const std::uintptr_t coordinatorAddress,
    const std::uint64_t coordinatorSerial,
    const std::uintptr_t batchAddress,
    const std::uint64_t batchSerial) noexcept
    : coordinatorAddress_(coordinatorAddress),
      coordinatorAddressMirror_(coordinatorAddress),
      coordinatorSerial_(coordinatorSerial),
      coordinatorSerialMirror_(coordinatorSerial),
      batchAddress_(batchAddress),
      batchAddressMirror_(batchAddress),
      batchSerial_(batchSerial),
      batchSerialMirror_(batchSerial)
{
}

OwnershipBatch *RegistryOwnershipAdmission::tryAuthenticateBatchLocked()
    const noexcept
{
    return g_activeBatch;
}

OwnershipBatch::~OwnershipBatch() noexcept = default;

std::uint64_t OwnershipBatch::serial() const noexcept
{
    return g_activeBatch == this ? g_activeBatchSerial : 0;
}

bool OwnershipBatch::canonicalInactive() const noexcept
{
    return g_batchCanonical && g_activeBatch != this;
}

OwnershipBatchStatus TryBeginOwnershipBatch(
    OwnershipBatch *const batch) noexcept
{
    Record(Event::BatchBegin);
    if (g_batchBeginStatus == OwnershipBatchStatus::Success)
    {
        if (g_activeBatch != nullptr || !g_batchCanonical)
            std::abort();
        g_activeBatch = batch;
        g_activeBatchSerial = ++g_nextBatchSerial;
        g_batchCanonical = false;
        ++g_beginBatchCallCount;
    }
    return g_batchBeginStatus;
}

OwnershipBatchStatus FinishOwnershipBatch(
    OwnershipBatch *const batch) noexcept
{
    Record(Event::BatchFinish);
    if (g_activeBatch != batch)
        return OwnershipBatchStatus::InvalidToken;
    if (g_batchFinishStatus == OwnershipBatchStatus::Success)
    {
        g_activeBatch = nullptr;
        g_activeBatchSerial = 0;
        g_batchCanonical = true;
        ++g_finishBatchCallCount;
    }
    return g_batchFinishStatus;
}

DatabaseUserAddStatus TryAddDatabaseUser4Reference(
    const RegistryOwnershipAdmission &admission,
    const std::uint32_t stringId) noexcept
{
    (void)admission;
    if (!g_activeBatch)
        return DatabaseUserAddStatus::UnsafeFailure;
    AppendAddUser4Id(stringId);
    return g_addStatus;
}

DatabaseUserAddBulkResult TryAddDatabaseUser4References(
    const RegistryOwnershipAdmission &,
    const std::uint32_t *,
    std::uint32_t) noexcept
{
    if (!g_activeBatch)
        std::abort();
    return {DatabaseUserAddBulkStatus::Success, 0, 0};
}

DatabaseNameResult TryInternDatabaseUser4Name(
    const RegistryOwnershipAdmission &,
    const char *,
    std::uint32_t,
    int) noexcept
{
    if (!g_activeBatch)
        std::abort();
    return {DatabaseNameStatus::InvalidArgumentNoChange, 0, nullptr};
}

DatabaseNameStatus TryReAddRetainedDatabaseName(
    const RegistryOwnershipAdmission &,
    const char *) noexcept
{
    if (!g_activeBatch)
        std::abort();
    return DatabaseNameStatus::InvalidArgumentNoChange;
}

DatabaseUserAddBulkResult TryReAddRetainedDatabaseNames(
    const RegistryOwnershipAdmission &,
    const char *const *,
    std::uint32_t) noexcept
{
    if (!g_activeBatch)
        std::abort();
    return {DatabaseUserAddBulkStatus::InvalidArgumentNoChange, 0, 0};
}

DatabaseSweepStatus TryTransferDatabaseUsers4To8(
    const RegistryOwnershipAdmission &) noexcept
{
    if (!g_activeBatch)
        std::abort();
    return DatabaseSweepStatus::CapacityNoChange;
}

DatabaseSweepStatus TryShutdownDatabaseUser8(
    const RegistryOwnershipAdmission &) noexcept
{
    if (!g_activeBatch)
        std::abort();
    return DatabaseSweepStatus::CapacityNoChange;
}

AcquireResult TryAcquireOrdinaryStringOfSize(
    const char *,
    std::uint32_t,
    int) noexcept
{
    return {AcquireStatus::UnsafeFailure, 0};
}

TransferStatus TryTransferOrdinaryToDatabaseUser(
    std::uint32_t) noexcept
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

ReleaseStatus TryRemoveDatabaseUserReferences(
    const std::uint32_t *,
    std::uint32_t) noexcept
{
    return ReleaseStatus::UnsafeFailure;
}
} // namespace script_string

int main()
{
    if (!TestFullChainBorrowEnrollsAddAndFinishes())
        return 1;
    return 0;
}
