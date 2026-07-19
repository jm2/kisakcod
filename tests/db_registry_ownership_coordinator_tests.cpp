#include <database/db_registry_ownership_coordinator.h>
#include <qcommon/sys_sync.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <type_traits>

namespace
{
using db::registry_ownership::RegistryOwnershipCoordinator;
using db::registry_ownership::RegistryOwnershipStatus;

enum class Event : std::uint8_t
{
    TransactionBegin,
    TransactionFinish,
    ControllerSnapshot,
    ControllerAuthenticate,
    HashLock,
    BatchBegin,
    AddUser4,
    InternName,
    ReAddName,
    TransferUsers,
    ShutdownUser8,
    BatchFinish,
    HashUnlock,
};

std::array<Event, 64> g_events{};
std::uint32_t g_eventCount = 0;
bool g_hashLocked = false;
RegistryOwnershipCoordinator *g_reentryCoordinator = nullptr;
RegistryOwnershipStatus g_reentryStatus = RegistryOwnershipStatus::Success;

const db::script_string_transaction::ScriptStringTransactionToken *
    g_activeTransaction = nullptr;
std::uint32_t g_nextTransactionSerial = 40;
std::uint32_t g_activeTransactionSerial = 0;
db::script_string_transaction::ScriptStringTransactionStatus
    g_transactionBeginStatus = db::script_string_transaction::
        ScriptStringTransactionStatus::Success;
db::script_string_transaction::ScriptStringTransactionStatus
    g_transactionFinishStatus = db::script_string_transaction::
        ScriptStringTransactionStatus::Success;

script_string::OwnershipBatch *g_activeBatch = nullptr;
bool g_batchCanonical = true;
bool g_leaveBatchNoncanonical = false;
script_string::OwnershipBatchStatus g_batchBeginStatus =
    script_string::OwnershipBatchStatus::Success;
script_string::OwnershipBatchStatus g_batchFinishStatus =
    script_string::OwnershipBatchStatus::Success;
script_string::DatabaseUserAddStatus g_addStatus =
    script_string::DatabaseUserAddStatus::Added;
script_string::DatabaseNameResult g_nameResult{
    script_string::DatabaseNameStatus::Success,
    77,
    "canonical-name"};
script_string::DatabaseNameStatus g_reAddStatus =
    script_string::DatabaseNameStatus::Success;
script_string::DatabaseSweepStatus g_transferStatus =
    script_string::DatabaseSweepStatus::Success;
script_string::DatabaseSweepStatus g_shutdownStatus =
    script_string::DatabaseSweepStatus::Success;

const db::zone_script_string_ownership::
    ZoneScriptStringOwnershipController *g_borrowController = nullptr;
db::zone_load::ZoneLoadContextKey g_controllerKey{
    UINT64_C(9), UINT32_C(3), 0};
std::uint32_t g_controllerTransactionSerial = 91;
bool g_controllerSnapshotAllowed = true;
bool g_controllerAuthenticationAllowed = true;

void Record(const Event event) noexcept
{
    if (g_eventCount >= g_events.size())
        std::abort();
    g_events[g_eventCount++] = event;
}

void ClearEvents() noexcept
{
    g_eventCount = 0;
}

[[nodiscard]] bool Check(
    const bool condition,
    const char *const message) noexcept
{
    if (!condition)
        std::fprintf(stderr, "registry coordinator test failed: %s\n", message);
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
    db::registry_ownership::
        SetRegistryOwnershipCoordinatorBoundaryForTesting(
            0, 0, 0, 0, 0, 0);
    db::registry_ownership::
        SetNextRegistryOwnershipCoordinatorSerialForTesting(0);
    ClearEvents();
    g_hashLocked = false;
    g_reentryCoordinator = nullptr;
    g_reentryStatus = RegistryOwnershipStatus::Success;
    g_activeTransaction = nullptr;
    g_activeTransactionSerial = 0;
    g_nextTransactionSerial = 40;
    g_transactionBeginStatus = db::script_string_transaction::
        ScriptStringTransactionStatus::Success;
    g_transactionFinishStatus = db::script_string_transaction::
        ScriptStringTransactionStatus::Success;
    g_activeBatch = nullptr;
    g_batchCanonical = true;
    g_leaveBatchNoncanonical = false;
    g_batchBeginStatus = script_string::OwnershipBatchStatus::Success;
    g_batchFinishStatus = script_string::OwnershipBatchStatus::Success;
    g_addStatus = script_string::DatabaseUserAddStatus::Added;
    g_nameResult = {
        script_string::DatabaseNameStatus::Success,
        77,
        "canonical-name"};
    g_reAddStatus = script_string::DatabaseNameStatus::Success;
    g_transferStatus = script_string::DatabaseSweepStatus::Success;
    g_shutdownStatus = script_string::DatabaseSweepStatus::Success;
    g_borrowController = nullptr;
    g_controllerKey = {UINT64_C(9), UINT32_C(3), 0};
    g_controllerTransactionSerial = 91;
    g_controllerSnapshotAllowed = true;
    g_controllerAuthenticationAllowed = true;
}

[[nodiscard]] bool TestStandaloneOrderAndStatusMapping() noexcept
{
    ResetHarness();
    RegistryOwnershipCoordinator coordinator;
    RegistryOwnershipCoordinator overlapping;
    if (!Check(coordinator.isEmptyCanonical(),
            "fresh coordinator was not canonical")
        || !Check(
            db::registry_ownership::
                TryBeginStandaloneRegistryOwnershipCoordinator(&coordinator)
                == RegistryOwnershipStatus::Success,
            "standalone admission failed")
        || !CheckEvents(
            std::array{Event::TransactionBegin},
            "standalone admission did not acquire the outer transaction")
        || !Check(
            coordinator.mode()
                == db::registry_ownership::
                    RegistryOwnershipCoordinatorMode::Standalone
                && coordinator.phase()
                    == db::registry_ownership::
                        RegistryOwnershipCoordinatorPhase::Ready
                && coordinator.serial() != 0,
            "standalone admission published the wrong state"))
    {
        return false;
    }

    ClearEvents();
    if (!Check(
            db::registry_ownership::
                TryBeginStandaloneRegistryOwnershipCoordinator(&overlapping)
                == RegistryOwnershipStatus::Busy,
            "overlapping standalone admission was not busy")
        || !CheckEvents(
            std::array{Event::TransactionBegin},
            "overlapping admission entered inner locks"))
    {
        return false;
    }

    ClearEvents();
    if (!Check(
            db::registry_ownership::TryRegistryAddDatabaseUser4(
                &coordinator, 12)
                == RegistryOwnershipStatus::Success,
            "user4 add status mapping failed")
        || !CheckEvents(
            std::array{
                Event::HashLock,
                Event::BatchBegin,
                Event::AddUser4,
                Event::BatchFinish,
                Event::HashUnlock},
            "user4 add lock order changed"))
    {
        return false;
    }

    g_addStatus = script_string::DatabaseUserAddStatus::AlreadyOwnedNoChange;
    if (!Check(
            db::registry_ownership::TryRegistryAddDatabaseUser4(
                &coordinator, 12)
                == RegistryOwnershipStatus::NoChange,
            "idempotent user4 add status mapping failed"))
    {
        return false;
    }
    g_addStatus =
        script_string::DatabaseUserAddStatus::OwnershipMismatchNoChange;
    if (!Check(
            db::registry_ownership::TryRegistryAddDatabaseUser4(
                &coordinator, 12)
                == RegistryOwnershipStatus::OwnershipMismatch,
            "user4 ownership-mismatch status mapping failed"))
    {
        return false;
    }
    g_addStatus =
        script_string::DatabaseUserAddStatus::RefCountExhaustedNoChange;
    if (!Check(
            db::registry_ownership::TryRegistryAddDatabaseUser4(
                &coordinator, 12)
                == RegistryOwnershipStatus::RefCountExhausted,
            "user4 refcount status mapping failed"))
    {
        return false;
    }

    db::registry_ownership::RegistryOwnershipName output{
        UINT32_C(0xA55AA55A),
        "unchanged"};
    g_nameResult = {
        script_string::DatabaseNameStatus::CapacityNoChange,
        0,
        nullptr};
    if (!Check(
            db::registry_ownership::TryRegistryInternBoundedName(
                &coordinator, "name", 5, 15, &output)
                == RegistryOwnershipStatus::CapacityExceeded,
            "bounded-name capacity status mapping failed")
        || !Check(
            output.stringId == UINT32_C(0xA55AA55A)
                && output.canonicalName != nullptr
                && output.canonicalName[0] == 'u',
            "bounded-name failure changed output"))
    {
        return false;
    }
    g_nameResult = {
        script_string::DatabaseNameStatus::Success,
        88,
        "published"};
    if (!Check(
            db::registry_ownership::TryRegistryInternBoundedName(
                &coordinator, "name", 5, 15, &output)
                == RegistryOwnershipStatus::Success,
            "bounded-name success failed")
        || !Check(
            output.stringId == 88 && output.canonicalName != nullptr
                && output.canonicalName[0] == 'p',
            "bounded-name success did not publish output"))
    {
        return false;
    }

    g_transferStatus = script_string::DatabaseSweepStatus::CapacityNoChange;
    if (!Check(
            db::registry_ownership::
                TryRegistryTransferDatabaseUsers4To8(&coordinator)
                == RegistryOwnershipStatus::CapacityExceeded,
            "transfer capacity status mapping failed"))
    {
        return false;
    }
    g_shutdownStatus = script_string::DatabaseSweepStatus::Success;
    if (!Check(
            db::registry_ownership::TryRegistryShutdownDatabaseUser8(
                &coordinator)
                == RegistryOwnershipStatus::Success,
            "shutdown success mapping failed"))
    {
        return false;
    }
    g_reAddStatus =
        script_string::DatabaseNameStatus::OwnershipMismatchNoChange;
    if (!Check(
            db::registry_ownership::
                TryRegistryReAddRetainedDefaultName(
                    &coordinator, "retained")
                == RegistryOwnershipStatus::OwnershipMismatch,
            "retained-name mismatch mapping failed"))
    {
        return false;
    }

    ClearEvents();
    if (!Check(
            db::registry_ownership::FinishRegistryOwnershipCoordinator(
                &coordinator)
                == RegistryOwnershipStatus::Success,
            "standalone close failed")
        || !CheckEvents(
            std::array{Event::TransactionFinish},
            "standalone close did not release the outer transaction last")
        || !Check(coordinator.isEmptyCanonical(),
            "standalone close did not restore canonical storage"))
    {
        return false;
    }
    return true;
}

[[nodiscard]] bool TestReentryAndRecoverableBatchAdmission() noexcept
{
    ResetHarness();
    RegistryOwnershipCoordinator coordinator;
    if (!Check(
            db::registry_ownership::
                TryBeginStandaloneRegistryOwnershipCoordinator(&coordinator)
                == RegistryOwnershipStatus::Success,
            "reentry coordinator admission failed"))
    {
        return false;
    }

    ClearEvents();
    g_reentryCoordinator = &coordinator;
    if (!Check(
            db::registry_ownership::TryRegistryAddDatabaseUser4(
                &coordinator, 20)
                == RegistryOwnershipStatus::Success,
            "outer reentry operation failed")
        || !Check(g_reentryStatus == RegistryOwnershipStatus::Busy,
            "same-thread registry reentry did not fail busy")
        || !CheckEvents(
            std::array{
                Event::HashLock,
                Event::BatchBegin,
                Event::AddUser4,
                Event::BatchFinish,
                Event::HashUnlock},
            "same-thread reentry attempted a nested registry lock"))
    {
        return false;
    }

    g_batchBeginStatus = script_string::OwnershipBatchStatus::Busy;
    ClearEvents();
    if (!Check(
            db::registry_ownership::TryRegistryAddDatabaseUser4(
                &coordinator, 20)
                == RegistryOwnershipStatus::Busy,
            "recoverable batch contention was not busy")
        || !CheckEvents(
            std::array{
                Event::HashLock, Event::BatchBegin, Event::HashUnlock},
            "recoverable batch contention did not release the hash lock")
        || !Check(
            coordinator.phase()
                == db::registry_ownership::
                    RegistryOwnershipCoordinatorPhase::Ready,
            "recoverable batch contention changed coordinator phase"))
    {
        return false;
    }
    g_batchBeginStatus = script_string::OwnershipBatchStatus::InvalidState;
    if (!Check(
            db::registry_ownership::TryRegistryAddDatabaseUser4(
                &coordinator, 20)
                == RegistryOwnershipStatus::InvalidState,
            "recoverable uninitialized string state mapping failed"))
    {
        return false;
    }
    g_batchBeginStatus = script_string::OwnershipBatchStatus::Success;
    return Check(
        db::registry_ownership::FinishRegistryOwnershipCoordinator(
            &coordinator)
            == RegistryOwnershipStatus::Success,
        "reentry coordinator close failed");
}

[[nodiscard]] bool TestBorrowedAuthentication() noexcept
{
    ResetHarness();
    db::zone_script_string_ownership::
        ZoneScriptStringOwnershipController controller;
    RegistryOwnershipCoordinator coordinator;
    g_borrowController = &controller;
    const db::zone_load::ZoneLoadContextKey wrongKey{
        UINT64_C(10), UINT32_C(3), 0};
    if (!Check(
            db::registry_ownership::TryBorrowRegistryOwnershipCoordinator(
                &coordinator, &controller, wrongKey)
                == RegistryOwnershipStatus::InvalidKey,
            "borrow accepted the wrong lifecycle key"))
    {
        return false;
    }

    g_controllerSnapshotAllowed = false;
    if (!Check(
            db::registry_ownership::TryBorrowRegistryOwnershipCoordinator(
                &coordinator, &controller, g_controllerKey)
                == RegistryOwnershipStatus::InvalidState,
            "borrow accepted an inactive controller transaction"))
    {
        return false;
    }
    g_controllerSnapshotAllowed = true;
    ClearEvents();
    if (!Check(
            db::registry_ownership::TryBorrowRegistryOwnershipCoordinator(
                &coordinator, &controller, g_controllerKey)
                == RegistryOwnershipStatus::Success,
            "borrowed coordinator admission failed")
        || !CheckEvents(
            std::array{Event::ControllerSnapshot},
            "borrowed admission did not snapshot controller authority")
        || !Check(
            coordinator.mode()
                == db::registry_ownership::
                    RegistryOwnershipCoordinatorMode::BorrowedZoneController,
            "borrowed admission published the wrong mode"))
    {
        return false;
    }

    ClearEvents();
    if (!Check(
            db::registry_ownership::TryRegistryAddDatabaseUser4(
                &coordinator, 30)
                == RegistryOwnershipStatus::Success,
            "borrowed operation failed")
        || !CheckEvents(
            std::array{
                Event::ControllerAuthenticate,
                Event::HashLock,
                Event::BatchBegin,
                Event::AddUser4,
                Event::BatchFinish,
                Event::HashUnlock},
            "borrowed operation did not authenticate before inner locks"))
    {
        return false;
    }

    ClearEvents();
    if (!Check(
            db::registry_ownership::FinishRegistryOwnershipCoordinator(
                &coordinator)
                == RegistryOwnershipStatus::Success,
            "borrowed coordinator close failed")
        || !CheckEvents(
            std::array{Event::ControllerAuthenticate},
            "borrowed close released a transaction it did not own")
        || !Check(coordinator.isEmptyCanonical(),
            "borrowed close did not restore canonical storage"))
    {
        return false;
    }
    return true;
}

[[nodiscard]] bool TestTerminalFailuresRetainOuterBoundaries() noexcept
{
    ResetHarness();
    RegistryOwnershipCoordinator invalidClose;
    if (!Check(
            db::registry_ownership::
                TryBeginStandaloneRegistryOwnershipCoordinator(&invalidClose)
                == RegistryOwnershipStatus::Success,
            "invalid-close setup admission failed"))
    {
        return false;
    }
    g_batchFinishStatus = script_string::OwnershipBatchStatus::InvalidToken;
    ClearEvents();
    if (!Check(
            db::registry_ownership::TryRegistryAddDatabaseUser4(
                &invalidClose, 40)
                == RegistryOwnershipStatus::UnsafeFailure,
            "invalid batch close was not terminal")
        || !CheckEvents(
            std::array{
                Event::HashLock,
                Event::BatchBegin,
                Event::AddUser4,
                Event::BatchFinish},
            "invalid batch close released an outer lock")
        || !Check(invalidClose.poisoned()
                && invalidClose.hashLockRetained()
                && g_hashLocked && g_activeTransaction != nullptr
                && g_activeBatch != nullptr,
            "invalid batch close did not retain every outer boundary"))
    {
        return false;
    }

    // Test-only recovery of deliberately stranded fake state.
    g_activeBatch = nullptr;
    g_batchCanonical = true;
    g_hashLocked = false;
    g_activeTransaction = nullptr;
    g_activeTransactionSerial = 0;
    db::registry_ownership::
        SetRegistryOwnershipCoordinatorBoundaryForTesting(
            0, 0, 0, 0, 0, 0);

    RegistryOwnershipCoordinator torn;
    g_batchFinishStatus = script_string::OwnershipBatchStatus::Success;
    if (!Check(
            db::registry_ownership::
                TryBeginStandaloneRegistryOwnershipCoordinator(&torn)
                == RegistryOwnershipStatus::Success,
            "torn-registry setup admission failed"))
    {
        return false;
    }
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(&torn);
    db::registry_ownership::
        SetRegistryOwnershipCoordinatorBoundaryForTesting(
            address,
            torn.serial(),
            address ^ static_cast<std::uintptr_t>(1),
            torn.serial(),
            1,
            1);
    ClearEvents();
    if (!Check(
            db::registry_ownership::TryRegistryAddDatabaseUser4(&torn, 41)
                == RegistryOwnershipStatus::UnsafeFailure,
            "torn coordinator registry authenticated")
        || !Check(torn.poisoned() && !g_hashLocked,
            "torn coordinator registry entered an inner lock")
        || !Check(g_eventCount == 0,
            "torn coordinator registry invoked a backend"))
    {
        return false;
    }

    g_activeTransaction = nullptr;
    g_activeTransactionSerial = 0;
    db::registry_ownership::
        SetRegistryOwnershipCoordinatorBoundaryForTesting(
            0, 0, 0, 0, 0, 0);
    return true;
}

[[nodiscard]] bool TestSerialExhaustionAndOutputFailureAtomicity() noexcept
{
    ResetHarness();
    RegistryOwnershipCoordinator coordinator;
    db::registry_ownership::
        SetNextRegistryOwnershipCoordinatorSerialForTesting(UINT64_MAX);
    if (!Check(
            db::registry_ownership::
                TryBeginStandaloneRegistryOwnershipCoordinator(&coordinator)
                == RegistryOwnershipStatus::UnsafeFailure,
            "coordinator serial wrapped")
        || !CheckEvents(
            std::array{Event::TransactionBegin, Event::TransactionFinish},
            "serial exhaustion retained a transaction")
        || !Check(coordinator.isEmptyCanonical(),
            "serial exhaustion changed coordinator storage"))
    {
        return false;
    }

    db::registry_ownership::
        SetNextRegistryOwnershipCoordinatorSerialForTesting(0);
    ClearEvents();
    if (!Check(
            db::registry_ownership::
                TryBeginStandaloneRegistryOwnershipCoordinator(&coordinator)
                == RegistryOwnershipStatus::Success,
            "output-atomicity setup admission failed"))
    {
        return false;
    }
    db::registry_ownership::RegistryOwnershipName output{55, "sentinel"};
    g_nameResult = {
        script_string::DatabaseNameStatus::Success,
        99,
        "must-not-publish"};
    g_batchFinishStatus = script_string::OwnershipBatchStatus::UnsafeFailure;
    if (!Check(
            db::registry_ownership::TryRegistryInternBoundedName(
                &coordinator, "name", 5, 15, &output)
                == RegistryOwnershipStatus::UnsafeFailure,
            "unsafe operation close did not fail")
        || !Check(output.stringId == 55
                && output.canonicalName != nullptr
                && output.canonicalName[0] == 's',
            "unsafe operation close published output")
        || !Check(coordinator.poisoned() && coordinator.hashLockRetained()
                && g_hashLocked && g_activeBatch == nullptr,
            "authenticated unsafe close did not contain partial mutation"))
    {
        return false;
    }

    g_activeTransaction = nullptr;
    g_activeTransactionSerial = 0;
    g_hashLocked = false;
    db::registry_ownership::
        SetRegistryOwnershipCoordinatorBoundaryForTesting(
            0, 0, 0, 0, 0, 0);
    return true;
}
} // namespace

FastCriticalSection db_hashCritSect{};

void KISAK_CDECL Sys_LockWrite(FastCriticalSection *const critSect)
{
    if (critSect != &db_hashCritSect || g_hashLocked)
        std::abort();
    Record(Event::HashLock);
    if (g_reentryCoordinator)
    {
        RegistryOwnershipCoordinator *const coordinator =
            g_reentryCoordinator;
        g_reentryCoordinator = nullptr;
        g_reentryStatus =
            db::registry_ownership::TryRegistryAddDatabaseUser4(
                coordinator, 999);
    }
    g_hashLocked = true;
}

void KISAK_CDECL Sys_UnlockWrite(FastCriticalSection *const critSect)
{
    if (critSect != &db_hashCritSect || !g_hashLocked)
        std::abort();
    Record(Event::HashUnlock);
    g_hashLocked = false;
}

bool KISAK_CDECL Sys_IsWriteLocked(const FastCriticalSection *const critSect)
{
    return critSect == &db_hashCritSect && g_hashLocked;
}

MT_ValidationLease::~MT_ValidationLease() noexcept = default;

namespace script_string
{
OwnershipBatch::~OwnershipBatch() noexcept = default;

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
        g_batchCanonical = false;
    }
    return g_batchBeginStatus;
}

OwnershipBatchStatus FinishOwnershipBatch(
    OwnershipBatch *const batch) noexcept
{
    Record(Event::BatchFinish);
    if (g_activeBatch != batch)
        return OwnershipBatchStatus::InvalidToken;
    if (g_batchFinishStatus != OwnershipBatchStatus::InvalidToken)
    {
        g_activeBatch = nullptr;
        g_batchCanonical = !g_leaveBatchNoncanonical;
    }
    return g_batchFinishStatus;
}

DatabaseUserAddStatus TryAddDatabaseUser4Reference(
    OwnershipBatch &batch,
    std::uint32_t) noexcept
{
    if (g_activeBatch != &batch)
        std::abort();
    Record(Event::AddUser4);
    return g_addStatus;
}

DatabaseNameResult TryInternDatabaseUser4Name(
    OwnershipBatch &batch,
    const char *,
    std::uint32_t,
    int) noexcept
{
    if (g_activeBatch != &batch)
        std::abort();
    Record(Event::InternName);
    return g_nameResult;
}

DatabaseNameStatus TryReAddRetainedDatabaseName(
    OwnershipBatch &batch,
    const char *) noexcept
{
    if (g_activeBatch != &batch)
        std::abort();
    Record(Event::ReAddName);
    return g_reAddStatus;
}

DatabaseSweepStatus TryTransferDatabaseUsers4To8(
    OwnershipBatch &batch) noexcept
{
    if (g_activeBatch != &batch)
        std::abort();
    Record(Event::TransferUsers);
    return g_transferStatus;
}

DatabaseSweepStatus TryShutdownDatabaseUser8(
    OwnershipBatch &batch) noexcept
{
    if (g_activeBatch != &batch)
        std::abort();
    Record(Event::ShutdownUser8);
    return g_shutdownStatus;
}
} // namespace script_string

namespace db::script_string_transaction
{
bool ScriptStringTransactionToken::active() const noexcept
{
    return g_activeTransaction == this;
}

std::uint32_t ScriptStringTransactionToken::serial() const noexcept
{
    return g_activeTransaction == this ? g_activeTransactionSerial : 0;
}

bool ScriptStringTransactionToken::canonicalInactive() const noexcept
{
    return g_activeTransaction != this;
}

ScriptStringTransactionStatus TryBeginScriptStringTransaction(
    ScriptStringTransactionToken *const token) noexcept
{
    Record(Event::TransactionBegin);
    if (g_transactionBeginStatus != ScriptStringTransactionStatus::Success)
        return g_transactionBeginStatus;
    if (g_activeTransaction != nullptr)
        return ScriptStringTransactionStatus::Busy;
    g_activeTransaction = token;
    g_activeTransactionSerial = ++g_nextTransactionSerial;
    return ScriptStringTransactionStatus::Success;
}

ScriptStringTransactionStatus FinishScriptStringTransaction(
    ScriptStringTransactionToken *const token) noexcept
{
    Record(Event::TransactionFinish);
    if (g_transactionFinishStatus != ScriptStringTransactionStatus::Success)
        return g_transactionFinishStatus;
    if (g_activeTransaction != token)
        return ScriptStringTransactionStatus::InvalidToken;
    g_activeTransaction = nullptr;
    g_activeTransactionSerial = 0;
    return ScriptStringTransactionStatus::Success;
}

bool OwnsScriptStringTransaction(
    const ScriptStringTransactionToken &token) noexcept
{
    return g_activeTransaction == &token && g_activeTransactionSerial != 0;
}
} // namespace db::script_string_transaction

namespace db::zone_script_string_ownership
{
const zone_load::ZoneLoadContextKey &
ZoneScriptStringOwnershipController::key() const noexcept
{
    return g_controllerKey;
}

bool ZoneScriptStringOwnershipController::trySnapshotRegistryTransaction(
    const zone_load::ZoneLoadContextKey &expectedKey,
    std::uint32_t *const outSerial) const noexcept
{
    Record(Event::ControllerSnapshot);
    if (this != g_borrowController || !outSerial
        || expectedKey != g_controllerKey || !g_controllerSnapshotAllowed)
    {
        return false;
    }
    *outSerial = g_controllerTransactionSerial;
    return true;
}

bool ZoneScriptStringOwnershipController::authenticatesRegistryTransaction(
    const zone_load::ZoneLoadContextKey &expectedKey,
    const std::uint32_t expectedSerial) const noexcept
{
    Record(Event::ControllerAuthenticate);
    return this == g_borrowController && expectedKey == g_controllerKey
        && expectedSerial == g_controllerTransactionSerial
        && g_controllerAuthenticationAllowed;
}
} // namespace db::zone_script_string_ownership

int main()
{
    static_assert(sizeof(RegistryOwnershipCoordinator) == 0x50);
    static_assert(std::is_standard_layout_v<RegistryOwnershipCoordinator>);
    static_assert(!std::is_copy_constructible_v<RegistryOwnershipCoordinator>);
    static_assert(!std::is_move_constructible_v<RegistryOwnershipCoordinator>);
    static_assert(
        !std::is_trivially_destructible_v<RegistryOwnershipCoordinator>);

    if (!TestStandaloneOrderAndStatusMapping()
        || !TestReentryAndRecoverableBatchAdmission()
        || !TestBorrowedAuthentication()
        || !TestTerminalFailuresRetainOuterBoundaries()
        || !TestSerialExhaustionAndOutputFailureAtomicity())
    {
        return 1;
    }
    std::puts("registry ownership coordinator contracts passed");
    return 0;
}
