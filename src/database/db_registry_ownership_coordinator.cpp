#include <database/db_registry_ownership_coordinator.h>

#include <qcommon/sys_sync.h>

#include <cstdint>

extern FastCriticalSection db_hashCritSect;

namespace db::registry_ownership
{
namespace
{
enum class RegistryBoundaryState : std::uint8_t
{
    Idle,
    Active,
    Poisoned,
};

std::uint64_t s_nextCoordinatorSerial = 0;
std::uintptr_t s_activeCoordinatorAddress = 0;
std::uint64_t s_activeCoordinatorSerial = 0;
std::uintptr_t s_activeCoordinatorAddressMirror = 0;
std::uint64_t s_activeCoordinatorSerialMirror = 0;
RegistryBoundaryState s_registryBoundaryState = RegistryBoundaryState::Idle;
RegistryBoundaryState s_registryBoundaryStateMirror =
    RegistryBoundaryState::Idle;

[[nodiscard]] bool IsRegistryBoundaryIdle() noexcept
{
    return s_registryBoundaryState == RegistryBoundaryState::Idle
        && s_registryBoundaryStateMirror == RegistryBoundaryState::Idle
        && s_activeCoordinatorAddress == 0
        && s_activeCoordinatorAddressMirror == 0
        && s_activeCoordinatorSerial == 0
        && s_activeCoordinatorSerialMirror == 0;
}

[[nodiscard]] bool IsRegistryBoundaryConsistentActive() noexcept
{
    return s_registryBoundaryState == RegistryBoundaryState::Active
        && s_registryBoundaryStateMirror == RegistryBoundaryState::Active
        && s_activeCoordinatorAddress != 0
        && s_activeCoordinatorAddress
            == s_activeCoordinatorAddressMirror
        && s_activeCoordinatorSerial != 0
        && s_activeCoordinatorSerial
            == s_activeCoordinatorSerialMirror;
}

void ClearRegistryBoundary() noexcept
{
    s_activeCoordinatorAddress = 0;
    s_activeCoordinatorSerial = 0;
    s_activeCoordinatorAddressMirror = 0;
    s_activeCoordinatorSerialMirror = 0;
    s_registryBoundaryState = RegistryBoundaryState::Idle;
    s_registryBoundaryStateMirror = RegistryBoundaryState::Idle;
}

[[nodiscard]] RegistryOwnershipStatus MapTransactionBeginStatus(
    const script_string_transaction::ScriptStringTransactionStatus
        status) noexcept
{
    switch (status)
    {
    case script_string_transaction::ScriptStringTransactionStatus::Success:
        return RegistryOwnershipStatus::Success;
    case script_string_transaction::ScriptStringTransactionStatus::Busy:
        return RegistryOwnershipStatus::Busy;
    case script_string_transaction::ScriptStringTransactionStatus::
        InvalidArgument:
        return RegistryOwnershipStatus::InvalidArgument;
    case script_string_transaction::ScriptStringTransactionStatus::
        InvalidToken:
    default:
        return RegistryOwnershipStatus::UnsafeFailure;
    }
}
} // namespace

RegistryOwnershipStatus RegistryOwnershipCoordinator::beginRegistered(
    RegistryOwnershipCoordinator *const coordinator,
    const RegistryOwnershipCoordinatorMode mode,
    const zone_script_string_ownership::
        ZoneScriptStringOwnershipController *const borrowedController,
    const zone_load::ZoneLoadContextKey &borrowedKey,
    const std::uint32_t borrowedTransactionSerial) noexcept
{
    if (!IsRegistryBoundaryIdle())
    {
        return IsRegistryBoundaryConsistentActive()
            ? RegistryOwnershipStatus::Busy
            : RegistryOwnershipStatus::UnsafeFailure;
    }
    if (s_nextCoordinatorSerial == UINT64_MAX)
        return RegistryOwnershipStatus::UnsafeFailure;

    const std::uint64_t serial = ++s_nextCoordinatorSerial;
    coordinator->borrowedController_ = borrowedController;
    coordinator->borrowedKey_ = borrowedKey;
    coordinator->serial_ = serial;
    coordinator->borrowedTransactionSerial_ = borrowedTransactionSerial;
    coordinator->mode_ = mode;
    coordinator->phase_ = RegistryOwnershipCoordinatorPhase::Ready;

    const std::uintptr_t address =
        reinterpret_cast<std::uintptr_t>(coordinator);
    s_activeCoordinatorAddress = address;
    s_activeCoordinatorSerial = serial;
    s_activeCoordinatorAddressMirror = address;
    s_activeCoordinatorSerialMirror = serial;
    s_registryBoundaryState = RegistryBoundaryState::Active;
    s_registryBoundaryStateMirror = RegistryBoundaryState::Active;
    return RegistryOwnershipStatus::Success;
}

RegistryOwnershipStatus RegistryOwnershipCoordinator::beginOperation(
    RegistryOwnershipCoordinator *const coordinator) noexcept
{
    if (!coordinator)
        return RegistryOwnershipStatus::InvalidArgument;
    if (coordinator->phase_
        == RegistryOwnershipCoordinatorPhase::UnsafeFailure)
    {
        return RegistryOwnershipStatus::UnsafeFailure;
    }
    if (coordinator->phase_ != RegistryOwnershipCoordinatorPhase::Ready
        && coordinator->phase_
            != RegistryOwnershipCoordinatorPhase::Operating)
    {
        return RegistryOwnershipStatus::InvalidState;
    }
    if (!coordinator->authenticatesOuterTransaction()
        || !coordinator->ownsRegistryBoundary())
    {
        coordinator->poisonBoundary();
        return RegistryOwnershipStatus::UnsafeFailure;
    }
    if (coordinator->phase_ == RegistryOwnershipCoordinatorPhase::Operating)
        return RegistryOwnershipStatus::Busy;
    if (coordinator->hashLockRetained_
        || !coordinator->operationBatch_.canonicalInactive())
    {
        coordinator->poisonBoundary();
        return RegistryOwnershipStatus::UnsafeFailure;
    }

    // Publish Operating before attempting the nonrecursive registry lock so a
    // same-thread reentry is rejected as Busy before it can deadlock.
    coordinator->phase_ = RegistryOwnershipCoordinatorPhase::Operating;
    Sys_LockWrite(&db_hashCritSect);
    coordinator->hashLockRetained_ = true;
    const script_string::OwnershipBatchStatus batchStatus =
        script_string::TryBeginOwnershipBatch(
            &coordinator->operationBatch_);
    if (batchStatus == script_string::OwnershipBatchStatus::Success)
        return RegistryOwnershipStatus::Success;

    Sys_UnlockWrite(&db_hashCritSect);
    coordinator->hashLockRetained_ = false;
    switch (batchStatus)
    {
    case script_string::OwnershipBatchStatus::Busy:
        coordinator->phase_ = RegistryOwnershipCoordinatorPhase::Ready;
        return RegistryOwnershipStatus::Busy;
    case script_string::OwnershipBatchStatus::InvalidState:
        coordinator->phase_ = RegistryOwnershipCoordinatorPhase::Ready;
        return RegistryOwnershipStatus::InvalidState;
    case script_string::OwnershipBatchStatus::InvalidArgument:
    case script_string::OwnershipBatchStatus::InvalidToken:
    case script_string::OwnershipBatchStatus::UnsafeFailure:
    default:
        coordinator->poisonBoundary();
        return RegistryOwnershipStatus::UnsafeFailure;
    }
}

RegistryOwnershipStatus RegistryOwnershipCoordinator::finishOperation(
    RegistryOwnershipCoordinator *const coordinator,
    const RegistryOwnershipStatus operationStatus,
    const bool operationUnsafe) noexcept
{
    const script_string::OwnershipBatchStatus batchStatus =
        script_string::FinishOwnershipBatch(&coordinator->operationBatch_);
    if (batchStatus == script_string::OwnershipBatchStatus::InvalidToken
        || !coordinator->operationBatch_.canonicalInactive()
        || batchStatus != script_string::OwnershipBatchStatus::Success
        || operationUnsafe)
    {
        // InvalidToken cannot prove the inner acquisitions were released. Any
        // other unsafe close can follow an allegedly impossible backend
        // mismatch after partial mutation. In both cases keep the registry
        // write lock and outer transaction retained; successful inner release
        // still respects reverse order while containing partially changed DB
        // ownership from every registry reader.
        coordinator->poisonBoundary();
        return RegistryOwnershipStatus::UnsafeFailure;
    }

    Sys_UnlockWrite(&db_hashCritSect);
    coordinator->hashLockRetained_ = false;
    coordinator->phase_ = RegistryOwnershipCoordinatorPhase::Ready;
    return operationStatus;
}

namespace
{
[[nodiscard]] RegistryOwnershipStatus MapDatabaseNameStatus(
    const script_string::DatabaseNameStatus status) noexcept
{
    switch (status)
    {
    case script_string::DatabaseNameStatus::Success:
        return RegistryOwnershipStatus::Success;
    case script_string::DatabaseNameStatus::InvalidArgumentNoChange:
        return RegistryOwnershipStatus::InvalidArgument;
    case script_string::DatabaseNameStatus::CapacityNoChange:
        return RegistryOwnershipStatus::CapacityExceeded;
    case script_string::DatabaseNameStatus::RefCountExhaustedNoChange:
        return RegistryOwnershipStatus::RefCountExhausted;
    case script_string::DatabaseNameStatus::OwnershipMismatchNoChange:
        return RegistryOwnershipStatus::OwnershipMismatch;
    case script_string::DatabaseNameStatus::UnsafeFailure:
    default:
        return RegistryOwnershipStatus::UnsafeFailure;
    }
}
} // namespace

RegistryOwnershipCoordinatorPhase
RegistryOwnershipCoordinator::phase() const noexcept
{
    return phase_;
}

RegistryOwnershipCoordinatorMode
RegistryOwnershipCoordinator::mode() const noexcept
{
    return mode_;
}

std::uint64_t RegistryOwnershipCoordinator::serial() const noexcept
{
    return serial_;
}

bool RegistryOwnershipCoordinator::hashLockRetained() const noexcept
{
    return hashLockRetained_;
}

bool RegistryOwnershipCoordinator::poisoned() const noexcept
{
    return phase_ == RegistryOwnershipCoordinatorPhase::UnsafeFailure;
}

bool RegistryOwnershipCoordinator::isEmptyCanonical() const noexcept
{
    return phase_ == RegistryOwnershipCoordinatorPhase::Empty
        && mode_ == RegistryOwnershipCoordinatorMode::None
        && !hashLockRetained_ && reserved_ == 0 && serial_ == 0
        && borrowedController_ == nullptr
        && borrowedKey_ == zone_load::ZoneLoadContextKey{}
        && borrowedTransactionSerial_ == 0
        && operationBatch_.canonicalInactive()
        && standaloneTransaction_.canonicalInactive();
}

bool RegistryOwnershipCoordinator::canonicalAfterStandaloneBegin() const noexcept
{
    return phase_ == RegistryOwnershipCoordinatorPhase::Empty
        && mode_ == RegistryOwnershipCoordinatorMode::None
        && !hashLockRetained_ && reserved_ == 0 && serial_ == 0
        && borrowedController_ == nullptr
        && borrowedKey_ == zone_load::ZoneLoadContextKey{}
        && borrowedTransactionSerial_ == 0
        && operationBatch_.canonicalInactive()
        && script_string_transaction::OwnsScriptStringTransaction(
            standaloneTransaction_);
}

bool RegistryOwnershipCoordinator::ownsRegistryBoundary() const noexcept
{
    const std::uintptr_t address =
        reinterpret_cast<std::uintptr_t>(this);
    return IsRegistryBoundaryConsistentActive()
        && s_activeCoordinatorAddress == address
        && s_activeCoordinatorSerial == serial_;
}

bool RegistryOwnershipCoordinator::authenticatesOuterTransaction() const noexcept
{
    if (reserved_ != 0 || serial_ == 0)
        return false;
    switch (mode_)
    {
    case RegistryOwnershipCoordinatorMode::Standalone:
        return borrowedController_ == nullptr
            && borrowedKey_ == zone_load::ZoneLoadContextKey{}
            && borrowedTransactionSerial_ == 0
            && script_string_transaction::OwnsScriptStringTransaction(
                standaloneTransaction_);
    case RegistryOwnershipCoordinatorMode::BorrowedZoneController:
        return borrowedController_ != nullptr
            && static_cast<bool>(borrowedKey_)
            && borrowedTransactionSerial_ != 0
            && standaloneTransaction_.canonicalInactive()
            && borrowedController_->authenticatesRegistryTransaction(
                borrowedKey_, borrowedTransactionSerial_);
    case RegistryOwnershipCoordinatorMode::None:
    default:
        return false;
    }
}

void RegistryOwnershipCoordinator::poisonBoundary() noexcept
{
    phase_ = RegistryOwnershipCoordinatorPhase::UnsafeFailure;
    const std::uintptr_t address =
        reinterpret_cast<std::uintptr_t>(this);
    if (s_activeCoordinatorAddress == address
        || s_activeCoordinatorAddressMirror == address)
    {
        s_registryBoundaryState = RegistryBoundaryState::Poisoned;
        s_registryBoundaryStateMirror = RegistryBoundaryState::Poisoned;
    }
}

void RegistryOwnershipCoordinator::resetAfterFinish() noexcept
{
    borrowedController_ = nullptr;
    borrowedKey_ = {};
    serial_ = 0;
    borrowedTransactionSerial_ = 0;
    phase_ = RegistryOwnershipCoordinatorPhase::Empty;
    mode_ = RegistryOwnershipCoordinatorMode::None;
    hashLockRetained_ = false;
    reserved_ = 0;
}

RegistryOwnershipStatus TryBeginStandaloneRegistryOwnershipCoordinator(
    RegistryOwnershipCoordinator *const coordinator) noexcept
{
    if (!coordinator)
        return RegistryOwnershipStatus::InvalidArgument;
    if (coordinator->phase_ != RegistryOwnershipCoordinatorPhase::Empty)
        return RegistryOwnershipStatus::InvalidState;

    const auto transactionStatus =
        script_string_transaction::TryBeginScriptStringTransaction(
            &coordinator->standaloneTransaction_);
    if (transactionStatus
        != script_string_transaction::ScriptStringTransactionStatus::Success)
    {
        return MapTransactionBeginStatus(transactionStatus);
    }
    if (!coordinator->canonicalAfterStandaloneBegin())
    {
        (void)script_string_transaction::FinishScriptStringTransaction(
            &coordinator->standaloneTransaction_);
        return RegistryOwnershipStatus::UnsafeFailure;
    }

    const RegistryOwnershipStatus status =
        RegistryOwnershipCoordinator::beginRegistered(
        coordinator,
        RegistryOwnershipCoordinatorMode::Standalone,
        nullptr,
        {},
        0);
    if (status != RegistryOwnershipStatus::Success)
    {
        const auto finishStatus =
            script_string_transaction::FinishScriptStringTransaction(
                &coordinator->standaloneTransaction_);
        return finishStatus
                == script_string_transaction::
                    ScriptStringTransactionStatus::Success
            ? status
            : RegistryOwnershipStatus::UnsafeFailure;
    }
    return RegistryOwnershipStatus::Success;
}

RegistryOwnershipStatus TryBorrowRegistryOwnershipCoordinator(
    RegistryOwnershipCoordinator *const coordinator,
    const zone_script_string_ownership::
        ZoneScriptStringOwnershipController *const controller,
    const zone_load::ZoneLoadContextKey &expectedKey) noexcept
{
    if (!coordinator || !controller)
        return RegistryOwnershipStatus::InvalidArgument;
    if (!static_cast<bool>(expectedKey) || controller->key() != expectedKey)
        return RegistryOwnershipStatus::InvalidKey;
    if (!coordinator->isEmptyCanonical())
        return RegistryOwnershipStatus::InvalidState;

    std::uint32_t transactionSerial = 0;
    if (!controller->trySnapshotRegistryTransaction(
            expectedKey, &transactionSerial))
    {
        return RegistryOwnershipStatus::InvalidState;
    }
    return RegistryOwnershipCoordinator::beginRegistered(
        coordinator,
        RegistryOwnershipCoordinatorMode::BorrowedZoneController,
        controller,
        expectedKey,
        transactionSerial);
}

RegistryOwnershipStatus FinishRegistryOwnershipCoordinator(
    RegistryOwnershipCoordinator *const coordinator) noexcept
{
    if (!coordinator)
        return RegistryOwnershipStatus::InvalidArgument;
    if (coordinator->phase_ == RegistryOwnershipCoordinatorPhase::Operating)
        return RegistryOwnershipStatus::Busy;
    if (coordinator->phase_
        == RegistryOwnershipCoordinatorPhase::UnsafeFailure)
    {
        return RegistryOwnershipStatus::UnsafeFailure;
    }
    if (coordinator->phase_ != RegistryOwnershipCoordinatorPhase::Ready
        || coordinator->hashLockRetained_
        || !coordinator->operationBatch_.canonicalInactive())
    {
        return RegistryOwnershipStatus::InvalidState;
    }
    if (!coordinator->authenticatesOuterTransaction()
        || !coordinator->ownsRegistryBoundary())
    {
        coordinator->poisonBoundary();
        return RegistryOwnershipStatus::UnsafeFailure;
    }

    const RegistryOwnershipCoordinatorMode mode = coordinator->mode_;
    coordinator->phase_ = RegistryOwnershipCoordinatorPhase::Finishing;
    ClearRegistryBoundary();
    if (mode == RegistryOwnershipCoordinatorMode::Standalone)
    {
        const auto status =
            script_string_transaction::FinishScriptStringTransaction(
                &coordinator->standaloneTransaction_);
        if (status
            != script_string_transaction::
                ScriptStringTransactionStatus::Success)
        {
            coordinator->phase_ =
                RegistryOwnershipCoordinatorPhase::UnsafeFailure;
            return RegistryOwnershipStatus::UnsafeFailure;
        }
    }
    coordinator->resetAfterFinish();
    return RegistryOwnershipStatus::Success;
}

RegistryOwnershipStatus TryRegistryAddDatabaseUser4(
    RegistryOwnershipCoordinator *const coordinator,
    const std::uint32_t stringId) noexcept
{
    const RegistryOwnershipStatus begin =
        RegistryOwnershipCoordinator::beginOperation(coordinator);
    if (begin != RegistryOwnershipStatus::Success)
        return begin;

    const script_string::DatabaseUserAddStatus status =
        script_string::TryAddDatabaseUser4Reference(
            coordinator->operationBatch_, stringId);
    RegistryOwnershipStatus mapped = RegistryOwnershipStatus::UnsafeFailure;
    switch (status)
    {
    case script_string::DatabaseUserAddStatus::Added:
        mapped = RegistryOwnershipStatus::Success;
        break;
    case script_string::DatabaseUserAddStatus::AlreadyOwnedNoChange:
        mapped = RegistryOwnershipStatus::NoChange;
        break;
    case script_string::DatabaseUserAddStatus::OwnershipMismatchNoChange:
        mapped = RegistryOwnershipStatus::OwnershipMismatch;
        break;
    case script_string::DatabaseUserAddStatus::RefCountExhaustedNoChange:
        mapped = RegistryOwnershipStatus::RefCountExhausted;
        break;
    case script_string::DatabaseUserAddStatus::UnsafeFailure:
    default:
        break;
    }
    return RegistryOwnershipCoordinator::finishOperation(
        coordinator,
        mapped,
        status == script_string::DatabaseUserAddStatus::UnsafeFailure);
}

RegistryOwnershipStatus TryRegistryInternBoundedName(
    RegistryOwnershipCoordinator *const coordinator,
    const char *const bytes,
    const std::uint32_t byteCount,
    const int type,
    RegistryOwnershipName *const outName) noexcept
{
    if (!outName)
        return RegistryOwnershipStatus::InvalidArgument;
    const RegistryOwnershipStatus begin =
        RegistryOwnershipCoordinator::beginOperation(coordinator);
    if (begin != RegistryOwnershipStatus::Success)
        return begin;

    const script_string::DatabaseNameResult result =
        script_string::TryInternDatabaseUser4Name(
            coordinator->operationBatch_, bytes, byteCount, type);
    const RegistryOwnershipStatus mapped = MapDatabaseNameStatus(result.status);
    const RegistryOwnershipStatus finish =
        RegistryOwnershipCoordinator::finishOperation(
        coordinator,
        mapped,
        result.status == script_string::DatabaseNameStatus::UnsafeFailure);
    if (finish == RegistryOwnershipStatus::Success)
    {
        *outName = {result.stringId, result.canonicalName};
    }
    return finish;
}

RegistryOwnershipStatus TryRegistryReAddRetainedDefaultName(
    RegistryOwnershipCoordinator *const coordinator,
    const char *const retainedCanonicalName) noexcept
{
    const RegistryOwnershipStatus begin =
        RegistryOwnershipCoordinator::beginOperation(coordinator);
    if (begin != RegistryOwnershipStatus::Success)
        return begin;
    const script_string::DatabaseNameStatus status =
        script_string::TryReAddRetainedDatabaseName(
            coordinator->operationBatch_, retainedCanonicalName);
    return RegistryOwnershipCoordinator::finishOperation(
        coordinator,
        MapDatabaseNameStatus(status),
        status == script_string::DatabaseNameStatus::UnsafeFailure);
}

RegistryOwnershipStatus TryRegistryTransferDatabaseUsers4To8(
    RegistryOwnershipCoordinator *const coordinator) noexcept
{
    const RegistryOwnershipStatus begin =
        RegistryOwnershipCoordinator::beginOperation(coordinator);
    if (begin != RegistryOwnershipStatus::Success)
        return begin;
    const script_string::DatabaseSweepStatus status =
        script_string::TryTransferDatabaseUsers4To8(
            coordinator->operationBatch_);
    const RegistryOwnershipStatus mapped = status
            == script_string::DatabaseSweepStatus::Success
        ? RegistryOwnershipStatus::Success
        : status == script_string::DatabaseSweepStatus::CapacityNoChange
        ? RegistryOwnershipStatus::CapacityExceeded
        : RegistryOwnershipStatus::UnsafeFailure;
    return RegistryOwnershipCoordinator::finishOperation(
        coordinator,
        mapped,
        status == script_string::DatabaseSweepStatus::UnsafeFailure);
}

RegistryOwnershipStatus TryRegistryShutdownDatabaseUser8(
    RegistryOwnershipCoordinator *const coordinator) noexcept
{
    const RegistryOwnershipStatus begin =
        RegistryOwnershipCoordinator::beginOperation(coordinator);
    if (begin != RegistryOwnershipStatus::Success)
        return begin;
    const script_string::DatabaseSweepStatus status =
        script_string::TryShutdownDatabaseUser8(
            coordinator->operationBatch_);
    const RegistryOwnershipStatus mapped = status
            == script_string::DatabaseSweepStatus::Success
        ? RegistryOwnershipStatus::Success
        : status == script_string::DatabaseSweepStatus::CapacityNoChange
        ? RegistryOwnershipStatus::CapacityExceeded
        : RegistryOwnershipStatus::UnsafeFailure;
    return RegistryOwnershipCoordinator::finishOperation(
        coordinator,
        mapped,
        status == script_string::DatabaseSweepStatus::UnsafeFailure);
}

#if defined(KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING)
script_string::OwnershipBatch &
RegistryOwnershipCoordinatorTestAccess::OperationBatch(
    RegistryOwnershipCoordinator *const coordinator) noexcept
{
    return coordinator->operationBatch_;
}

void RegistryOwnershipCoordinatorTestAccess::SetReserved(
    RegistryOwnershipCoordinator *const coordinator,
    const std::uint8_t reserved) noexcept
{
    if (coordinator)
        coordinator->reserved_ = reserved;
}

void SetRegistryOwnershipCoordinatorBoundaryForTesting(
    const std::uintptr_t address,
    const std::uint64_t serial,
    const std::uintptr_t addressMirror,
    const std::uint64_t serialMirror,
    const std::uint8_t state,
    const std::uint8_t stateMirror) noexcept
{
    s_activeCoordinatorAddress = address;
    s_activeCoordinatorSerial = serial;
    s_activeCoordinatorAddressMirror = addressMirror;
    s_activeCoordinatorSerialMirror = serialMirror;
    s_registryBoundaryState = static_cast<RegistryBoundaryState>(state);
    s_registryBoundaryStateMirror =
        static_cast<RegistryBoundaryState>(stateMirror);
}

void SetNextRegistryOwnershipCoordinatorSerialForTesting(
    const std::uint64_t serial) noexcept
{
    if (IsRegistryBoundaryIdle())
        s_nextCoordinatorSerial = serial;
}
#endif

} // namespace db::registry_ownership
