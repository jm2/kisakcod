#include <database/db_registry_ownership_coordinator.h>
#include <qcommon/sys_sync.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <thread>
#include <type_traits>

namespace
{
using db::registry_ownership::RegistryOwnershipBulkResult;
using db::registry_ownership::RegistryOwnershipCoordinator;
using db::registry_ownership::RegistryOwnershipCoordinatorAdmission;
using db::registry_ownership::RegistryOwnershipCoordinatorPhase;
using db::registry_ownership::RegistryOwnershipCoordinatorMode;
using db::registry_ownership::RegistryOwnershipStatus;

enum class Event : std::uint8_t
{
    TransactionBegin,
    TransactionFinish,
    ControllerSnapshot,
    ControllerAuthenticate,
    HashLock,
    HashUnlock,
    BatchBegin,
    BatchFinish,
    AddUser4,
    BulkAddUser4,
    InternName,
    ReAddName,
    BulkReAddName,
    TransferUsers,
    ShutdownUser8,
};

std::array<Event, 256> g_events{};
std::uint32_t g_eventCount = 0;
bool g_hashLocked = false;
std::uint32_t g_hashReadCount = 0;
RegistryOwnershipCoordinator *g_reentryOnHashLock = nullptr;
RegistryOwnershipCoordinator *g_reentryOnHashUnlock = nullptr;
RegistryOwnershipStatus g_reentryStatus = RegistryOwnershipStatus::Success;

std::recursive_mutex g_transactionMutex;
thread_local std::uint32_t g_transactionLockDepth = 0;
std::uint64_t g_transactionEnterCount = 0;
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
bool g_leaveTransactionNoncanonical = false;
bool g_corruptTransactionOnFailedBegin = false;

script_string::OwnershipBatch *g_activeBatch = nullptr;
std::uint64_t g_activeBatchSerial = 0;
std::uint64_t g_nextBatchSerial = 70;
bool g_batchCanonical = true;
bool g_leaveBatchNoncanonical = false;
script_string::OwnershipBatchStatus g_batchBeginStatus =
    script_string::OwnershipBatchStatus::Success;
script_string::OwnershipBatchStatus g_batchFinishStatus =
    script_string::OwnershipBatchStatus::Success;
script_string::DatabaseUserAddStatus g_addStatus =
    script_string::DatabaseUserAddStatus::Added;
script_string::DatabaseUserAddBulkResult g_bulkAddResult{
    script_string::DatabaseUserAddBulkStatus::Success, 8, 4};
script_string::DatabaseNameResult g_nameResult{
    script_string::DatabaseNameStatus::Success, 77, "canonical-name"};
script_string::DatabaseNameStatus g_reAddStatus =
    script_string::DatabaseNameStatus::Success;
script_string::DatabaseUserAddBulkResult g_bulkReAddResult{
    script_string::DatabaseUserAddBulkStatus::Success, 3, 2};
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

void ForceReleaseTransactionLock() noexcept
{
    while (g_transactionLockDepth != 0)
    {
        --g_transactionLockDepth;
        g_transactionMutex.unlock();
    }
}

void ResetHarness() noexcept
{
    g_activeBatch = nullptr;
    g_activeBatchSerial = 0;
    g_batchCanonical = true;
    g_hashLocked = false;
    g_hashReadCount = 0;
    g_activeTransaction = nullptr;
    g_activeTransactionSerial = 0;
    ForceReleaseTransactionLock();
    db::registry_ownership::
        SetRegistryOwnershipCoordinatorBoundaryForTesting(
            0, 0, 0, 0, 0, 0);
    db::registry_ownership::
        SetNextRegistryOwnershipCoordinatorSerialForTesting(0);

    ClearEvents();
    g_reentryOnHashLock = nullptr;
    g_reentryOnHashUnlock = nullptr;
    g_reentryStatus = RegistryOwnershipStatus::Success;
    g_nextTransactionSerial = 40;
    g_transactionBeginStatus = db::script_string_transaction::
        ScriptStringTransactionStatus::Success;
    g_transactionFinishStatus = db::script_string_transaction::
        ScriptStringTransactionStatus::Success;
    g_leaveTransactionNoncanonical = false;
    g_corruptTransactionOnFailedBegin = false;
    g_nextBatchSerial = 70;
    g_leaveBatchNoncanonical = false;
    g_batchBeginStatus = script_string::OwnershipBatchStatus::Success;
    g_batchFinishStatus = script_string::OwnershipBatchStatus::Success;
    g_addStatus = script_string::DatabaseUserAddStatus::Added;
    g_bulkAddResult = {
        script_string::DatabaseUserAddBulkStatus::Success, 8, 4};
    g_nameResult = {
        script_string::DatabaseNameStatus::Success,
        77,
        "canonical-name"};
    g_reAddStatus = script_string::DatabaseNameStatus::Success;
    g_bulkReAddResult = {
        script_string::DatabaseUserAddBulkStatus::Success, 3, 2};
    g_transferStatus = script_string::DatabaseSweepStatus::Success;
    g_shutdownStatus = script_string::DatabaseSweepStatus::Success;
    g_borrowController = nullptr;
    g_controllerKey = {UINT64_C(9), UINT32_C(3), 0};
    g_controllerTransactionSerial = 91;
    g_controllerSnapshotAllowed = true;
    g_controllerAuthenticationAllowed = true;
    g_transactionEnterCount = 0;
}

void RecoverPoisonedCoordinator(
    RegistryOwnershipCoordinator *const coordinator) noexcept
{
    g_activeBatch = nullptr;
    g_activeBatchSerial = 0;
    g_batchCanonical = true;
    g_hashLocked = false;
    g_activeTransaction = nullptr;
    g_activeTransactionSerial = 0;
    ForceReleaseTransactionLock();
    db::registry_ownership::
        SetRegistryOwnershipCoordinatorBoundaryForTesting(
            0, 0, 0, 0, 0, 0);
    db::registry_ownership::RegistryOwnershipCoordinatorTestAccess::
        ResetStorageForTesting(coordinator);
}

[[nodiscard]] bool TestRetainedHashOrderAndBulk() noexcept
{
    ResetHarness();
    const auto admission = RegistryOwnershipCoordinatorAdmission::ForTesting();
    RegistryOwnershipCoordinator coordinator;
    RegistryOwnershipCoordinator overlapping;
    if (!Check(
            db::registry_ownership::
                TryBeginStandaloneRegistryOwnershipCoordinator(
                    admission, &coordinator)
                == RegistryOwnershipStatus::Success,
            "standalone admission failed")
        || !CheckEvents(
            std::array{Event::TransactionBegin, Event::HashLock},
            "standalone did not retain transaction then hash")
        || !Check(
            coordinator.phase() == RegistryOwnershipCoordinatorPhase::Ready
                && coordinator.mode()
                    == RegistryOwnershipCoordinatorMode::Standalone
                && coordinator.serial() != 0
                && coordinator.hashLockRetained() && g_hashLocked,
            "standalone published an invalid retained boundary"))
    {
        return false;
    }

    ClearEvents();
    if (!Check(
            db::registry_ownership::
                TryBeginStandaloneRegistryOwnershipCoordinator(
                    admission, &overlapping)
                == RegistryOwnershipStatus::Busy,
            "overlapping admission was not busy")
        || !Check(g_eventCount == 0,
            "overlapping admission entered a backend"))
    {
        return false;
    }

    ClearEvents();
    if (!Check(
            db::registry_ownership::TryRegistryAddDatabaseUser4(
                &coordinator, 12)
                == RegistryOwnershipStatus::Success,
            "scalar add failed")
        || !CheckEvents(
            std::array{
                Event::BatchBegin, Event::AddUser4, Event::BatchFinish},
            "scalar operation reacquired/released the hash"))
    {
        return false;
    }

    std::array<std::uint32_t, 12> ids{};
    RegistryOwnershipBulkResult bulkOutput{99, 88};
    ClearEvents();
    if (!Check(
            db::registry_ownership::TryRegistryAddDatabaseUsers4(
                &coordinator,
                ids.data(),
                static_cast<std::uint32_t>(ids.size()),
                &bulkOutput)
                == RegistryOwnershipStatus::Success,
            "bulk add failed")
        || !Check(bulkOutput.addedCount == 8
                && bulkOutput.unchangedCount == 4,
            "bulk add did not publish exact counts")
        || !CheckEvents(
            std::array{
                Event::BatchBegin,
                Event::BulkAddUser4,
                Event::BatchFinish},
            "N-ID bulk used more than one batch"))
    {
        return false;
    }

    db::registry_ownership::RegistryOwnershipName output{55, "sentinel"};
    g_nameResult = {
        script_string::DatabaseNameStatus::CapacityNoChange, 0, nullptr};
    if (!Check(
            db::registry_ownership::TryRegistryInternBoundedName(
                &coordinator, "name", 5, &output)
                == RegistryOwnershipStatus::CapacityExceeded,
            "bounded name status mapping failed")
        || !Check(output.stringId == 55
                && output.canonicalName[0] == 's',
            "bounded name failure changed output"))
    {
        return false;
    }
    g_nameResult = {
        script_string::DatabaseNameStatus::Success, 91, "published"};
    if (!Check(
            db::registry_ownership::TryRegistryInternBoundedName(
                &coordinator, "name", 5, &output)
                == RegistryOwnershipStatus::Success,
            "bounded name success failed")
        || !Check(output.stringId == 91
                && output.canonicalName[0] == 'p',
            "bounded name success omitted output"))
    {
        return false;
    }

    ClearEvents();
    if (!Check(
            db::registry_ownership::FinishRegistryOwnershipCoordinator(
                &coordinator)
                == RegistryOwnershipStatus::Success,
            "standalone finish failed")
        || !CheckEvents(
            std::array{Event::HashUnlock, Event::TransactionFinish},
            "finish did not release hash then transaction")
        || !Check(coordinator.isEmptyCanonical(),
            "finish did not restore canonical storage"))
    {
        return false;
    }
    return true;
}

[[nodiscard]] bool TestPreheldAndReentryRejection() noexcept
{
    ResetHarness();
    const auto admission = RegistryOwnershipCoordinatorAdmission::ForTesting();
    RegistryOwnershipCoordinator coordinator;
    g_hashLocked = true;
    if (!Check(
            db::registry_ownership::
                TryBeginStandaloneRegistryOwnershipCoordinator(
                    admission, &coordinator)
                == RegistryOwnershipStatus::Busy,
            "pre-held hash was not rejected")
        || !Check(g_eventCount == 0,
            "pre-held rejection attempted nested acquisition")
        || !Check(g_transactionEnterCount == 0,
            "pre-held rejection entered transaction serializer"))
    {
        return false;
    }
    g_hashLocked = false;

    g_hashReadCount = 1;
    ClearEvents();
    const std::uint64_t transactionEnterCount = g_transactionEnterCount;
    if (!Check(
            db::registry_ownership::
                TryBeginStandaloneRegistryOwnershipCoordinator(
                    admission, &coordinator)
                == RegistryOwnershipStatus::Busy,
            "pre-held hash reader was not rejected")
        || !CheckEvents(
            std::array{Event::TransactionBegin, Event::TransactionFinish},
            "pre-held reader did not unwind the outer transaction")
        || !Check(
            g_transactionEnterCount > transactionEnterCount
                && coordinator.isEmptyCanonical(),
            "pre-held reader left retained coordinator authority"))
    {
        return false;
    }
    g_hashReadCount = 0;

    ClearEvents();
    g_reentryOnHashLock = &coordinator;
    if (!Check(
            db::registry_ownership::
                TryBeginStandaloneRegistryOwnershipCoordinator(
                    admission, &coordinator)
                == RegistryOwnershipStatus::Success,
            "hash-acquire reentry setup failed")
        || !Check(g_reentryStatus == RegistryOwnershipStatus::Busy,
            "hash-acquire reentry was not busy")
        || !CheckEvents(
            std::array{Event::TransactionBegin, Event::HashLock},
            "hash-acquire reentry entered an inner batch"))
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
        || !CheckEvents(std::array{Event::BatchBegin},
            "batch contention changed an outer boundary")
        || !Check(coordinator.hashLockRetained() && g_hashLocked
                && coordinator.phase() == RegistryOwnershipCoordinatorPhase::Ready,
            "batch contention released retained hash"))
    {
        return false;
    }

    g_batchBeginStatus = script_string::OwnershipBatchStatus::InvalidState;
    if (!Check(
            db::registry_ownership::TryRegistryAddDatabaseUser4(
                &coordinator, 20)
                == RegistryOwnershipStatus::InvalidState,
            "batch InvalidState mapping failed")
        || !Check(coordinator.hashLockRetained() && g_hashLocked,
            "batch InvalidState released retained hash"))
    {
        return false;
    }

    g_batchBeginStatus = script_string::OwnershipBatchStatus::Success;
    g_reentryOnHashUnlock = &coordinator;
    ClearEvents();
    if (!Check(
            db::registry_ownership::FinishRegistryOwnershipCoordinator(
                &coordinator)
                == RegistryOwnershipStatus::Success,
            "reentry coordinator finish failed")
        || !Check(g_reentryStatus == RegistryOwnershipStatus::Busy,
            "hash-release reentry was not busy")
        || !CheckEvents(
            std::array{Event::HashUnlock, Event::TransactionFinish},
            "hash-release reentry entered a backend"))
    {
        return false;
    }
    return true;
}

[[nodiscard]] bool TestBorrowedAndMirrorAuthentication() noexcept
{
    ResetHarness();
    const auto admission = RegistryOwnershipCoordinatorAdmission::ForTesting();
    db::zone_script_string_ownership::
        ZoneScriptStringOwnershipController controller;
    RegistryOwnershipCoordinator coordinator;
    g_borrowController = &controller;
    const db::zone_load::ZoneLoadContextKey wrongKey{
        UINT64_C(10), UINT32_C(3), 0};
    if (!Check(
            db::registry_ownership::TryBorrowRegistryOwnershipCoordinator(
                admission, &coordinator, &controller, wrongKey)
                == RegistryOwnershipStatus::InvalidState,
            "borrow accepted a wrong generation key"))
    {
        return false;
    }

    g_controllerSnapshotAllowed = false;
    if (!Check(
            db::registry_ownership::TryBorrowRegistryOwnershipCoordinator(
                admission, &coordinator, &controller, g_controllerKey)
                == RegistryOwnershipStatus::InvalidState,
            "borrow accepted inactive controller authority"))
    {
        return false;
    }
    g_controllerSnapshotAllowed = true;
    ClearEvents();
    if (!Check(
            db::registry_ownership::TryBorrowRegistryOwnershipCoordinator(
                admission, &coordinator, &controller, g_controllerKey)
                == RegistryOwnershipStatus::Success,
            "borrowed admission failed")
        || !CheckEvents(
            std::array{
                Event::ControllerSnapshot,
                Event::HashLock,
                Event::ControllerAuthenticate},
            "borrowed authority/order mismatch"))
    {
        return false;
    }

    g_controllerAuthenticationAllowed = false;
    ClearEvents();
    if (!Check(
            db::registry_ownership::TryRegistryAddDatabaseUser4(
                &coordinator, 30)
                == RegistryOwnershipStatus::UnsafeFailure,
            "revoked borrowed authority was recoverable")
        || !CheckEvents(std::array{Event::ControllerAuthenticate},
            "revoked borrowed authority entered an inner batch")
        || !Check(coordinator.poisoned() && coordinator.hashLockRetained()
                && g_hashLocked,
            "revoked borrowed authority did not fail closed"))
    {
        return false;
    }
    RecoverPoisonedCoordinator(&coordinator);

    ResetHarness();
    RegistryOwnershipCoordinator mirrored;
    if (!Check(
            db::registry_ownership::
                TryBeginStandaloneRegistryOwnershipCoordinator(
                    admission, &mirrored)
                == RegistryOwnershipStatus::Success,
            "mirror setup admission failed"))
    {
        return false;
    }
    db::registry_ownership::RegistryOwnershipCoordinatorTestAccess::
        SetRepresentationMirrors(
            &mirrored,
            mirrored.serial() + 1,
            0,
            0,
            g_activeTransactionSerial,
            {},
            static_cast<std::uint8_t>(RegistryOwnershipCoordinatorPhase::Ready),
            static_cast<std::uint8_t>(RegistryOwnershipCoordinatorMode::Standalone),
            true);
    ClearEvents();
    if (!Check(
            db::registry_ownership::TryRegistryAddDatabaseUser4(
                &mirrored, 31)
                == RegistryOwnershipStatus::UnsafeFailure,
            "torn local serial mirror authenticated")
        || !Check(g_eventCount == 0,
            "torn local serial entered backend")
        || !Check(mirrored.poisoned() && g_hashLocked,
            "torn local serial did not retain hash"))
    {
        return false;
    }
    RecoverPoisonedCoordinator(&mirrored);
    return true;
}

[[nodiscard]] bool TestUnsafeCloseAndUnknownContainment() noexcept
{
    ResetHarness();
    const auto admission = RegistryOwnershipCoordinatorAdmission::ForTesting();
    RegistryOwnershipCoordinator invalidClose;
    if (!Check(
            db::registry_ownership::
                TryBeginStandaloneRegistryOwnershipCoordinator(
                    admission, &invalidClose)
                == RegistryOwnershipStatus::Success,
            "invalid-close setup failed"))
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
                Event::BatchBegin, Event::AddUser4, Event::BatchFinish},
            "invalid batch close released an outer boundary")
        || !Check(invalidClose.poisoned()
                && invalidClose.hashLockRetained() && g_hashLocked
                && g_activeTransaction != nullptr && g_activeBatch != nullptr,
            "invalid batch close did not retain exact boundaries"))
    {
        return false;
    }
    RecoverPoisonedCoordinator(&invalidClose);

    ResetHarness();
    RegistryOwnershipCoordinator unknown;
    if (!Check(
            db::registry_ownership::
                TryBeginStandaloneRegistryOwnershipCoordinator(
                    admission, &unknown)
                == RegistryOwnershipStatus::Success,
            "unknown-status setup failed"))
    {
        return false;
    }
    g_addStatus = static_cast<script_string::DatabaseUserAddStatus>(0xFF);
    if (!Check(
            db::registry_ownership::TryRegistryAddDatabaseUser4(&unknown, 41)
                == RegistryOwnershipStatus::UnsafeFailure,
            "unknown backend enum was not terminal")
        || !Check(unknown.poisoned() && unknown.hashLockRetained()
                && g_hashLocked && g_activeBatch == nullptr,
            "unknown backend enum released outer boundary"))
    {
        return false;
    }
    RecoverPoisonedCoordinator(&unknown);

    ResetHarness();
    RegistryOwnershipCoordinator outerClose;
    if (!Check(
            db::registry_ownership::
                TryBeginStandaloneRegistryOwnershipCoordinator(
                    admission, &outerClose)
                == RegistryOwnershipStatus::Success,
            "outer-close setup failed"))
    {
        return false;
    }
    g_transactionFinishStatus = db::script_string_transaction::
        ScriptStringTransactionStatus::InvalidToken;
    ClearEvents();
    if (!Check(
            db::registry_ownership::FinishRegistryOwnershipCoordinator(
                &outerClose)
                == RegistryOwnershipStatus::UnsafeFailure,
            "transaction finish failure was not terminal")
        || !CheckEvents(
            std::array{Event::HashUnlock, Event::TransactionFinish},
            "transaction finish failure ordering changed")
        || !Check(outerClose.poisoned()
                && !outerClose.hashLockRetained() && !g_hashLocked
                && g_activeTransaction != nullptr,
            "transaction finish failure lost poisoned receipt"))
    {
        return false;
    }
    ClearEvents();
    RegistryOwnershipCoordinator blocked;
    if (!Check(
            db::registry_ownership::
                TryBeginStandaloneRegistryOwnershipCoordinator(
                    admission, &blocked)
                == RegistryOwnershipStatus::UnsafeFailure,
            "poisoned global boundary reopened")
        || !Check(g_eventCount == 0,
            "poisoned admission touched backend"))
    {
        return false;
    }
    RecoverPoisonedCoordinator(&outerClose);
    return true;
}

[[nodiscard]] bool TestAuthPrecedenceAndSerialMirrors() noexcept
{
    ResetHarness();
    const auto admission = RegistryOwnershipCoordinatorAdmission::ForTesting();
    RegistryOwnershipCoordinator exhausted;
    db::registry_ownership::
        SetNextRegistryOwnershipCoordinatorSerialForTesting(UINT64_MAX);
    if (!Check(
            db::registry_ownership::
                TryBeginStandaloneRegistryOwnershipCoordinator(
                    admission, &exhausted)
                == RegistryOwnershipStatus::UnsafeFailure,
            "coordinator serial wrapped")
        || !Check(g_eventCount == 0,
            "serial exhaustion acquired transaction/hash")
        || !Check(exhausted.isEmptyCanonical(),
            "serial exhaustion changed storage"))
    {
        return false;
    }

    ResetHarness();
    g_transactionBeginStatus = db::script_string_transaction::
        ScriptStringTransactionStatus::Busy;
    g_corruptTransactionOnFailedBegin = true;
    RegistryOwnershipCoordinator retainedOnRejectedBegin;
    if (!Check(
            db::registry_ownership::
                TryBeginStandaloneRegistryOwnershipCoordinator(
                    admission, &retainedOnRejectedBegin)
                == RegistryOwnershipStatus::UnsafeFailure,
            "noncanonical rejected transaction begin was recoverable")
        || !CheckEvents(std::array{Event::TransactionBegin},
            "rejected transaction begin touched hash/backend")
        || !Check(retainedOnRejectedBegin.poisoned(),
            "noncanonical rejected begin did not poison globally"))
    {
        return false;
    }
    g_leaveTransactionNoncanonical = false;
    g_corruptTransactionOnFailedBegin = false;
    RecoverPoisonedCoordinator(&retainedOnRejectedBegin);

    ResetHarness();
    RegistryOwnershipCoordinator forgedActiveSerial;
    if (!Check(
            db::registry_ownership::
                TryBeginStandaloneRegistryOwnershipCoordinator(
                    admission, &forgedActiveSerial)
                == RegistryOwnershipStatus::Success,
            "active-serial relation setup failed"))
    {
        return false;
    }
    const std::uintptr_t forgedAddress =
        reinterpret_cast<std::uintptr_t>(&forgedActiveSerial);
    db::registry_ownership::
        SetRegistryOwnershipCoordinatorBoundaryForTesting(
            forgedAddress,
            forgedActiveSerial.serial() + 1,
            forgedAddress,
            forgedActiveSerial.serial() + 1,
            1,
            1);
    ClearEvents();
    if (!Check(
            db::registry_ownership::TryRegistryAddDatabaseUser4(
                &forgedActiveSerial, 77)
                == RegistryOwnershipStatus::UnsafeFailure,
            "active serial beyond monotonic counter authenticated")
        || !Check(g_eventCount == 0,
            "forged active serial entered backend"))
    {
        return false;
    }
    RecoverPoisonedCoordinator(&forgedActiveSerial);

    ResetHarness();
    db::registry_ownership::
        SetRegistryOwnershipCoordinatorGlobalMirrorsForTesting(
            1, 0, 0, 0, 0, 0, 0, 0, 0, false, false);
    RegistryOwnershipCoordinator tornCounter;
    if (!Check(
            db::registry_ownership::
                TryBeginStandaloneRegistryOwnershipCoordinator(
                    admission, &tornCounter)
                == RegistryOwnershipStatus::UnsafeFailure,
            "torn next-serial mirror admitted")
        || !Check(g_eventCount == 0,
            "torn next-serial mirror touched backend"))
    {
        return false;
    }

    ResetHarness();
    RegistryOwnershipCoordinator tornLive;
    if (!Check(
            db::registry_ownership::
                TryBeginStandaloneRegistryOwnershipCoordinator(
                    admission, &tornLive)
                == RegistryOwnershipStatus::Success,
            "auth-precedence setup failed"))
    {
        return false;
    }
    const std::uintptr_t address =
        reinterpret_cast<std::uintptr_t>(&tornLive);
    db::registry_ownership::
        SetRegistryOwnershipCoordinatorBoundaryForTesting(
            address,
            tornLive.serial(),
            address ^ static_cast<std::uintptr_t>(1),
            tornLive.serial(),
            1,
            1);
    ClearEvents();
    if (!Check(
            db::registry_ownership::TryRegistryInternBoundedName(
                &tornLive, "name", 5, nullptr)
                == RegistryOwnershipStatus::UnsafeFailure,
            "bad argument masked torn live authority")
        || !Check(g_eventCount == 0,
            "torn live authority entered a batch")
        || !Check(tornLive.poisoned() && g_hashLocked,
            "torn global receipt did not fail closed"))
    {
        return false;
    }
    RecoverPoisonedCoordinator(&tornLive);
    return true;
}

[[nodiscard]] bool TestDestructorAbandonment() noexcept
{
    ResetHarness();
    const auto admission = RegistryOwnershipCoordinatorAdmission::ForTesting();
    auto *const coordinator = new RegistryOwnershipCoordinator;
    if (!Check(
            db::registry_ownership::
                TryBeginStandaloneRegistryOwnershipCoordinator(
                    admission, coordinator)
                == RegistryOwnershipStatus::Success,
            "abandonment setup failed"))
    {
        delete coordinator;
        return false;
    }
    ClearEvents();
    delete coordinator;
    if (!Check(g_eventCount == 0,
            "destructor abandonment invoked backend/unlock")
        || !Check(g_hashLocked && g_activeTransaction != nullptr,
            "destructor abandonment released retained boundary"))
    {
        return false;
    }

    g_hashLocked = false;
    g_activeTransaction = nullptr;
    g_activeTransactionSerial = 0;
    ForceReleaseTransactionLock();
    db::registry_ownership::
        SetRegistryOwnershipCoordinatorBoundaryForTesting(
            0, 0, 0, 0, 0, 0);

    ClearEvents();
    {
        RegistryOwnershipCoordinator canonical;
    }
    return Check(g_eventCount == 0,
        "canonical destructor changed boundary");
}

[[nodiscard]] bool TestForeignThreadSerialization() noexcept
{
    ResetHarness();
    const auto admission = RegistryOwnershipCoordinatorAdmission::ForTesting();
    RegistryOwnershipCoordinator coordinator;
    if (!Check(
            db::registry_ownership::
                TryBeginStandaloneRegistryOwnershipCoordinator(
                    admission, &coordinator)
                == RegistryOwnershipStatus::Success,
            "foreign-thread setup failed"))
    {
        return false;
    }

    std::atomic<bool> started{false};
    std::atomic<bool> completed{false};
    std::atomic<std::uint8_t> observed{0xFF};
    std::thread observer([&]() noexcept {
        started.store(true, std::memory_order_release);
        observed.store(
            static_cast<std::uint8_t>(coordinator.phase()),
            std::memory_order_relaxed);
        completed.store(true, std::memory_order_release);
    });
    while (!started.load(std::memory_order_acquire))
        std::this_thread::yield();
    for (std::uint32_t spin = 0; spin < 10000; ++spin)
        std::this_thread::yield();
    const bool blocked = !completed.load(std::memory_order_acquire);
    const bool finished =
        db::registry_ownership::FinishRegistryOwnershipCoordinator(
            &coordinator)
        == RegistryOwnershipStatus::Success;
    observer.join();
    return Check(blocked,
            "foreign getter read unsynchronized active globals")
        && Check(finished, "foreign-thread setup did not finish")
        && Check(completed.load(std::memory_order_acquire),
            "foreign getter remained blocked after finish")
        && Check(observed.load(std::memory_order_relaxed)
                == static_cast<std::uint8_t>(
                    RegistryOwnershipCoordinatorPhase::Empty),
            "foreign getter did not observe synchronized terminal state");
}
} // namespace

FastCriticalSection db_hashCritSect{};

void KISAK_CDECL Sys_EnterCriticalSection(const int critSect)
{
    if (critSect != CRITSECT_DB_SCRIPT_STRING_TRANSACTION)
        std::abort();
    g_transactionMutex.lock();
    ++g_transactionLockDepth;
    ++g_transactionEnterCount;
}

void KISAK_CDECL Sys_LeaveCriticalSection(const int critSect)
{
    if (critSect != CRITSECT_DB_SCRIPT_STRING_TRANSACTION
        || g_transactionLockDepth == 0)
    {
        std::abort();
    }
    --g_transactionLockDepth;
    g_transactionMutex.unlock();
}

void KISAK_CDECL Sys_LockWrite(FastCriticalSection *const critSect)
{
    if (!Sys_TryLockWrite(critSect))
        std::abort();
}

bool KISAK_CDECL Sys_TryLockWrite(FastCriticalSection *const critSect)
{
    if (critSect != &db_hashCritSect || g_hashLocked || g_hashReadCount != 0)
        return false;
    Record(Event::HashLock);
    if (g_reentryOnHashLock)
    {
        RegistryOwnershipCoordinator *const coordinator =
            g_reentryOnHashLock;
        g_reentryOnHashLock = nullptr;
        g_reentryStatus =
            db::registry_ownership::TryRegistryAddDatabaseUser4(
                coordinator, 999);
    }
    g_hashLocked = true;
    return true;
}

void KISAK_CDECL Sys_UnlockWrite(FastCriticalSection *const critSect)
{
    if (critSect != &db_hashCritSect || !g_hashLocked)
        std::abort();
    Record(Event::HashUnlock);
    if (g_reentryOnHashUnlock)
    {
        RegistryOwnershipCoordinator *const coordinator =
            g_reentryOnHashUnlock;
        g_reentryOnHashUnlock = nullptr;
        g_reentryStatus =
            db::registry_ownership::TryRegistryAddDatabaseUser4(
                coordinator, 998);
    }
    g_hashLocked = false;
}

bool KISAK_CDECL Sys_IsWriteLocked(const FastCriticalSection *const critSect)
{
    return critSect == &db_hashCritSect && g_hashLocked;
}

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
    return nullptr;
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
        g_activeBatchSerial = 0;
        g_batchCanonical = !g_leaveBatchNoncanonical;
    }
    return g_batchFinishStatus;
}

DatabaseUserAddStatus TryAddDatabaseUser4Reference(
    const RegistryOwnershipAdmission &,
    std::uint32_t) noexcept
{
    if (!g_activeBatch)
        std::abort();
    Record(Event::AddUser4);
    return g_addStatus;
}

DatabaseUserAddBulkResult TryAddDatabaseUser4References(
    const RegistryOwnershipAdmission &,
    const std::uint32_t *,
    std::uint32_t) noexcept
{
    if (!g_activeBatch)
        std::abort();
    Record(Event::BulkAddUser4);
    return g_bulkAddResult;
}

DatabaseNameResult TryInternDatabaseUser4Name(
    const RegistryOwnershipAdmission &,
    const char *,
    std::uint32_t,
    int type) noexcept
{
    if (!g_activeBatch || type != 6)
        std::abort();
    Record(Event::InternName);
    return g_nameResult;
}

DatabaseNameStatus TryReAddRetainedDatabaseName(
    const RegistryOwnershipAdmission &,
    const char *) noexcept
{
    if (!g_activeBatch)
        std::abort();
    Record(Event::ReAddName);
    return g_reAddStatus;
}

DatabaseUserAddBulkResult TryReAddRetainedDatabaseNames(
    const RegistryOwnershipAdmission &,
    const char *const *,
    std::uint32_t) noexcept
{
    if (!g_activeBatch)
        std::abort();
    Record(Event::BulkReAddName);
    return g_bulkReAddResult;
}

DatabaseSweepStatus TryTransferDatabaseUsers4To8(
    const RegistryOwnershipAdmission &) noexcept
{
    if (!g_activeBatch)
        std::abort();
    Record(Event::TransferUsers);
    return g_transferStatus;
}

DatabaseSweepStatus TryShutdownDatabaseUser8(
    const RegistryOwnershipAdmission &) noexcept
{
    if (!g_activeBatch)
        std::abort();
    Record(Event::ShutdownUser8);
    return g_shutdownStatus;
}
} // namespace script_string

namespace db::script_string_transaction
{
bool ScriptStringTransactionToken::active() const noexcept
{
    Sys_EnterCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
    const bool value = g_activeTransaction == this;
    Sys_LeaveCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
    return value;
}

std::uint32_t ScriptStringTransactionToken::serial() const noexcept
{
    Sys_EnterCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
    const std::uint32_t value =
        g_activeTransaction == this ? g_activeTransactionSerial : 0;
    Sys_LeaveCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
    return value;
}

bool ScriptStringTransactionToken::canonicalInactive() const noexcept
{
    Sys_EnterCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
    const bool value = g_activeTransaction != this
        && !g_leaveTransactionNoncanonical;
    Sys_LeaveCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
    return value;
}

ScriptStringTransactionStatus TryBeginScriptStringTransaction(
    ScriptStringTransactionToken *const token) noexcept
{
    Sys_EnterCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
    Record(Event::TransactionBegin);
    if (g_transactionBeginStatus != ScriptStringTransactionStatus::Success)
    {
        if (g_corruptTransactionOnFailedBegin)
            g_leaveTransactionNoncanonical = true;
        Sys_LeaveCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
        return g_transactionBeginStatus;
    }
    if (g_activeTransaction != nullptr)
    {
        Sys_LeaveCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
        return ScriptStringTransactionStatus::Busy;
    }
    g_activeTransaction = token;
    g_activeTransactionSerial = ++g_nextTransactionSerial;
    return ScriptStringTransactionStatus::Success;
}

ScriptStringTransactionStatus FinishScriptStringTransaction(
    ScriptStringTransactionToken *const token) noexcept
{
    Sys_EnterCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
    Record(Event::TransactionFinish);
    if (g_transactionFinishStatus != ScriptStringTransactionStatus::Success)
    {
        Sys_LeaveCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
        return g_transactionFinishStatus;
    }
    if (g_activeTransaction != token)
    {
        Sys_LeaveCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
        return ScriptStringTransactionStatus::InvalidToken;
    }
    g_activeTransaction = nullptr;
    g_activeTransactionSerial = 0;
    Sys_LeaveCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
    Sys_LeaveCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
    return ScriptStringTransactionStatus::Success;
}

bool OwnsScriptStringTransaction(
    const ScriptStringTransactionToken &token) noexcept
{
    Sys_EnterCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
    const bool value = g_activeTransaction == &token
        && g_activeTransactionSerial != 0;
    Sys_LeaveCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
    return value;
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
    static_assert(
        sizeof(RegistryOwnershipCoordinator)
        == (sizeof(void *) == 4 ? 0x78 : 0x80));
    static_assert(std::is_standard_layout_v<RegistryOwnershipCoordinator>);
    static_assert(!std::is_copy_constructible_v<RegistryOwnershipCoordinator>);
    static_assert(!std::is_move_constructible_v<RegistryOwnershipCoordinator>);
    static_assert(
        !std::is_trivially_destructible_v<RegistryOwnershipCoordinator>);
    static_assert(
        !std::is_default_constructible_v<
            RegistryOwnershipCoordinatorAdmission>);
    static_assert(
        !std::is_copy_constructible_v<
            RegistryOwnershipCoordinatorAdmission>);
    static_assert(script_string::kRegistryOwnershipBulkCapacity == 19999);

    if (!TestRetainedHashOrderAndBulk()
        || !TestPreheldAndReentryRejection()
        || !TestBorrowedAndMirrorAuthentication()
        || !TestUnsafeCloseAndUnknownContainment()
        || !TestAuthPrecedenceAndSerialMirrors()
        || !TestDestructorAbandonment()
        || !TestForeignThreadSerialization())
    {
        return 1;
    }
    std::puts("registry ownership coordinator contracts passed");
    return 0;
}
