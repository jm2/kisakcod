#include <database/db_registry_ownership_coordinator.h>

#include <qcommon/sys_sync.h>

#include <cstdint>

extern FastCriticalSection db_hashCritSect;

namespace db::registry_ownership
{
namespace
{
constexpr std::uint64_t kCoordinatorAdmissionSeal =
    UINT64_C(0x5245475F41444D49);
constexpr std::uint64_t kCoordinatorAdmissionSealMirror =
    UINT64_C(0xADBAB8A0BEBBB2B6);

class TransactionBoundaryGuard final
{
public:
    TransactionBoundaryGuard() noexcept
    {
        Sys_EnterCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
    }

    ~TransactionBoundaryGuard() noexcept
    {
        Sys_LeaveCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
    }

    TransactionBoundaryGuard(const TransactionBoundaryGuard &) = delete;
    TransactionBoundaryGuard &operator=(
        const TransactionBoundaryGuard &) = delete;
};

enum class RegistryBoundaryState : std::uint8_t
{
    Idle,
    Active,
    Poisoned,
};

enum class RegistryBoundaryClassification : std::uint8_t
{
    Idle,
    Active,
    Poisoned,
    Torn,
};

std::uint64_t s_nextCoordinatorSerial = 0;
std::uint64_t s_nextCoordinatorSerialMirror = 0;
std::uintptr_t s_activeCoordinatorAddress = 0;
std::uintptr_t s_activeCoordinatorAddressMirror = 0;
std::uint64_t s_activeCoordinatorSerial = 0;
std::uint64_t s_activeCoordinatorSerialMirror = 0;
std::uintptr_t s_borrowedControllerAddress = 0;
std::uintptr_t s_borrowedControllerAddressMirror = 0;
zone_load::ZoneLoadContextKey s_borrowedKey{};
zone_load::ZoneLoadContextKey s_borrowedKeyMirror{};
std::uint32_t s_outerTransactionSerial = 0;
std::uint32_t s_outerTransactionSerialMirror = 0;
RegistryOwnershipCoordinatorPhase s_activePhase =
    RegistryOwnershipCoordinatorPhase::Empty;
RegistryOwnershipCoordinatorPhase s_activePhaseMirror =
    RegistryOwnershipCoordinatorPhase::Empty;
RegistryOwnershipCoordinatorMode s_activeMode =
    RegistryOwnershipCoordinatorMode::None;
RegistryOwnershipCoordinatorMode s_activeModeMirror =
    RegistryOwnershipCoordinatorMode::None;
bool s_hashLockRetained = false;
bool s_hashLockRetainedMirror = false;
RegistryBoundaryState s_registryBoundaryState = RegistryBoundaryState::Idle;
RegistryBoundaryState s_registryBoundaryStateMirror =
    RegistryBoundaryState::Idle;

[[nodiscard]] bool IsActivePhaseHashCombinationValid() noexcept
{
    if (s_hashLockRetained != s_hashLockRetainedMirror)
        return false;
    switch (s_activePhase)
    {
    case RegistryOwnershipCoordinatorPhase::Acquiring:
        return true;
    case RegistryOwnershipCoordinatorPhase::Ready:
    case RegistryOwnershipCoordinatorPhase::Operating:
        return s_hashLockRetained;
    case RegistryOwnershipCoordinatorPhase::Finishing:
        return true;
    case RegistryOwnershipCoordinatorPhase::Empty:
    case RegistryOwnershipCoordinatorPhase::UnsafeFailure:
    default:
        return false;
    }
}

[[nodiscard]] RegistryBoundaryClassification
ClassifyRegistryBoundary() noexcept
{
    if (s_registryBoundaryState == RegistryBoundaryState::Poisoned
        || s_registryBoundaryStateMirror == RegistryBoundaryState::Poisoned)
    {
        return RegistryBoundaryClassification::Poisoned;
    }
    if (s_nextCoordinatorSerial != s_nextCoordinatorSerialMirror)
        return RegistryBoundaryClassification::Torn;

    const bool idle =
        s_registryBoundaryState == RegistryBoundaryState::Idle
        && s_registryBoundaryStateMirror == RegistryBoundaryState::Idle
        && s_activeCoordinatorAddress == 0
        && s_activeCoordinatorAddressMirror == 0
        && s_activeCoordinatorSerial == 0
        && s_activeCoordinatorSerialMirror == 0
        && s_borrowedControllerAddress == 0
        && s_borrowedControllerAddressMirror == 0
        && s_borrowedKey == zone_load::ZoneLoadContextKey{}
        && s_borrowedKeyMirror == zone_load::ZoneLoadContextKey{}
        && s_outerTransactionSerial == 0
        && s_outerTransactionSerialMirror == 0
        && s_activePhase == RegistryOwnershipCoordinatorPhase::Empty
        && s_activePhaseMirror == RegistryOwnershipCoordinatorPhase::Empty
        && s_activeMode == RegistryOwnershipCoordinatorMode::None
        && s_activeModeMirror == RegistryOwnershipCoordinatorMode::None
        && !s_hashLockRetained && !s_hashLockRetainedMirror;
    if (idle)
        return RegistryBoundaryClassification::Idle;

    const bool activeBase =
        s_registryBoundaryState == RegistryBoundaryState::Active
        && s_registryBoundaryStateMirror == RegistryBoundaryState::Active
        && s_activeCoordinatorAddress != 0
        && s_activeCoordinatorAddress
            == s_activeCoordinatorAddressMirror
        && s_activeCoordinatorSerial != 0
        && s_activeCoordinatorSerial
            == s_activeCoordinatorSerialMirror
        && s_activeCoordinatorSerial == s_nextCoordinatorSerial
        && s_activePhase == s_activePhaseMirror
        && s_activeMode == s_activeModeMirror
        && s_outerTransactionSerial != 0
        && s_outerTransactionSerial == s_outerTransactionSerialMirror
        && IsActivePhaseHashCombinationValid();
    if (!activeBase)
        return RegistryBoundaryClassification::Torn;

    switch (s_activeMode)
    {
    case RegistryOwnershipCoordinatorMode::Standalone:
        return s_borrowedControllerAddress == 0
                && s_borrowedControllerAddressMirror == 0
                && s_borrowedKey == zone_load::ZoneLoadContextKey{}
                && s_borrowedKeyMirror == zone_load::ZoneLoadContextKey{}
            ? RegistryBoundaryClassification::Active
            : RegistryBoundaryClassification::Torn;
    case RegistryOwnershipCoordinatorMode::BorrowedZoneController:
        return s_borrowedControllerAddress != 0
                && s_borrowedControllerAddress
                    == s_borrowedControllerAddressMirror
                && static_cast<bool>(s_borrowedKey)
                && s_borrowedKey == s_borrowedKeyMirror
            ? RegistryBoundaryClassification::Active
            : RegistryBoundaryClassification::Torn;
    case RegistryOwnershipCoordinatorMode::None:
    default:
        return RegistryBoundaryClassification::Torn;
    }
}

[[nodiscard]] bool BoundaryMentionsAddress(
    const std::uintptr_t address) noexcept
{
    return address != 0
        && (s_activeCoordinatorAddress == address
            || s_activeCoordinatorAddressMirror == address);
}

void ClearRegistryBoundary() noexcept
{
    s_activeCoordinatorAddress = 0;
    s_activeCoordinatorAddressMirror = 0;
    s_activeCoordinatorSerial = 0;
    s_activeCoordinatorSerialMirror = 0;
    s_borrowedControllerAddress = 0;
    s_borrowedControllerAddressMirror = 0;
    s_borrowedKey = {};
    s_borrowedKeyMirror = {};
    s_outerTransactionSerial = 0;
    s_outerTransactionSerialMirror = 0;
    s_activePhase = RegistryOwnershipCoordinatorPhase::Empty;
    s_activePhaseMirror = RegistryOwnershipCoordinatorPhase::Empty;
    s_activeMode = RegistryOwnershipCoordinatorMode::None;
    s_activeModeMirror = RegistryOwnershipCoordinatorMode::None;
    s_hashLockRetained = false;
    s_hashLockRetainedMirror = false;
    s_registryBoundaryState = RegistryBoundaryState::Idle;
    s_registryBoundaryStateMirror = RegistryBoundaryState::Idle;
}

void PoisonRegistryBoundary() noexcept
{
    s_registryBoundaryState = RegistryBoundaryState::Poisoned;
    s_registryBoundaryStateMirror = RegistryBoundaryState::Poisoned;
}

[[nodiscard]] RegistryOwnershipStatus BoundaryAdmissionStatus() noexcept
{
    switch (ClassifyRegistryBoundary())
    {
    case RegistryBoundaryClassification::Idle:
        return RegistryOwnershipStatus::Success;
    case RegistryBoundaryClassification::Active:
        return RegistryOwnershipStatus::Busy;
    case RegistryBoundaryClassification::Poisoned:
    case RegistryBoundaryClassification::Torn:
    default:
        return RegistryOwnershipStatus::UnsafeFailure;
    }
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

[[nodiscard]] RegistryOwnershipStatus MapDatabaseBulkStatus(
    const script_string::DatabaseUserAddBulkStatus status) noexcept
{
    switch (status)
    {
    case script_string::DatabaseUserAddBulkStatus::Success:
        return RegistryOwnershipStatus::Success;
    case script_string::DatabaseUserAddBulkStatus::NoChange:
        return RegistryOwnershipStatus::NoChange;
    case script_string::DatabaseUserAddBulkStatus::InvalidArgumentNoChange:
        return RegistryOwnershipStatus::InvalidArgument;
    case script_string::DatabaseUserAddBulkStatus::CapacityNoChange:
        return RegistryOwnershipStatus::CapacityExceeded;
    case script_string::DatabaseUserAddBulkStatus::OwnershipMismatchNoChange:
        return RegistryOwnershipStatus::OwnershipMismatch;
    case script_string::DatabaseUserAddBulkStatus::RefCountExhaustedNoChange:
        return RegistryOwnershipStatus::RefCountExhausted;
    case script_string::DatabaseUserAddBulkStatus::UnsafeFailure:
    default:
        return RegistryOwnershipStatus::UnsafeFailure;
    }
}
} // namespace

RegistryOwnershipCoordinatorAdmission::RegistryOwnershipCoordinatorAdmission(
    const std::uint64_t seal,
    const std::uint64_t sealMirror) noexcept
    : seal_(seal), sealMirror_(sealMirror)
{
}

bool RegistryOwnershipCoordinatorAdmission::authenticates() const noexcept
{
    return seal_ == kCoordinatorAdmissionSeal
        && sealMirror_ == kCoordinatorAdmissionSealMirror;
}

#if defined(KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING)
RegistryOwnershipCoordinatorAdmission
RegistryOwnershipCoordinatorAdmission::ForTesting() noexcept
{
    return RegistryOwnershipCoordinatorAdmission{
        kCoordinatorAdmissionSeal, kCoordinatorAdmissionSealMirror};
}
#endif

RegistryOwnershipStatus RegistryOwnershipCoordinator::beginRegistered(
    RegistryOwnershipCoordinator *const coordinator,
    const RegistryOwnershipCoordinatorMode mode,
    const zone_script_string_ownership::
        ZoneScriptStringOwnershipController *const borrowedController,
    const zone_load::ZoneLoadContextKey &borrowedKey,
    const std::uint32_t borrowedTransactionSerial) noexcept
{
    const RegistryOwnershipStatus admission = BoundaryAdmissionStatus();
    if (admission != RegistryOwnershipStatus::Success)
        return admission;
    if (s_nextCoordinatorSerial != s_nextCoordinatorSerialMirror
        || s_nextCoordinatorSerial == UINT64_MAX)
    {
        PoisonRegistryBoundary();
        return RegistryOwnershipStatus::UnsafeFailure;
    }

    const std::uint32_t standaloneSerial =
        mode == RegistryOwnershipCoordinatorMode::Standalone
        ? coordinator->standaloneTransaction_.serial()
        : 0;
    const std::uint32_t outerSerial =
        mode == RegistryOwnershipCoordinatorMode::Standalone
        ? standaloneSerial
        : borrowedTransactionSerial;
    if (outerSerial == 0)
    {
        PoisonRegistryBoundary();
        return RegistryOwnershipStatus::UnsafeFailure;
    }
    const std::uint64_t serial = s_nextCoordinatorSerial + 1;
    s_nextCoordinatorSerial = serial;
    s_nextCoordinatorSerialMirror = serial;
    const std::uintptr_t coordinatorAddress =
        reinterpret_cast<std::uintptr_t>(coordinator);
    const std::uintptr_t controllerAddress =
        reinterpret_cast<std::uintptr_t>(borrowedController);

    coordinator->borrowedControllerAddress_ = controllerAddress;
    coordinator->borrowedControllerAddressMirror_ = controllerAddress;
    coordinator->borrowedKey_ = borrowedKey;
    coordinator->borrowedKeyMirror_ = borrowedKey;
    coordinator->serial_ = serial;
    coordinator->serialMirror_ = serial;
    coordinator->borrowedTransactionSerial_ = borrowedTransactionSerial;
    coordinator->borrowedTransactionSerialMirror_ =
        borrowedTransactionSerial;
    coordinator->standaloneTransactionSerial_ = standaloneSerial;
    coordinator->standaloneTransactionSerialMirror_ = standaloneSerial;
    coordinator->phase_ = RegistryOwnershipCoordinatorPhase::Acquiring;
    coordinator->phaseMirror_ = RegistryOwnershipCoordinatorPhase::Acquiring;
    coordinator->mode_ = mode;
    coordinator->modeMirror_ = mode;
    coordinator->hashLockRetained_ = false;
    coordinator->hashLockRetainedMirror_ = false;

    s_activeCoordinatorAddress = coordinatorAddress;
    s_activeCoordinatorAddressMirror = coordinatorAddress;
    s_activeCoordinatorSerial = serial;
    s_activeCoordinatorSerialMirror = serial;
    s_borrowedControllerAddress = controllerAddress;
    s_borrowedControllerAddressMirror = controllerAddress;
    s_borrowedKey = borrowedKey;
    s_borrowedKeyMirror = borrowedKey;
    s_outerTransactionSerial = outerSerial;
    s_outerTransactionSerialMirror = outerSerial;
    s_activePhase = RegistryOwnershipCoordinatorPhase::Acquiring;
    s_activePhaseMirror = RegistryOwnershipCoordinatorPhase::Acquiring;
    s_activeMode = mode;
    s_activeModeMirror = mode;
    s_hashLockRetained = false;
    s_hashLockRetainedMirror = false;
    s_registryBoundaryState = RegistryBoundaryState::Active;
    s_registryBoundaryStateMirror = RegistryBoundaryState::Active;

    if (!Sys_TryLockWrite(&db_hashCritSect))
    {
        if (!coordinator->representationConsistent()
            || !coordinator->ownsRegistryBoundary()
            || !coordinator->authenticatesOuterTransaction())
        {
            coordinator->poisonBoundary();
            return RegistryOwnershipStatus::UnsafeFailure;
        }
        ClearRegistryBoundary();
        coordinator->resetAfterFinish();
        return RegistryOwnershipStatus::Busy;
    }
    coordinator->publishHashLockRetained(true);
    if (!coordinator->representationConsistent()
        || !coordinator->ownsRegistryBoundary()
        || !coordinator->authenticatesOuterTransaction())
    {
        coordinator->poisonBoundary();
        return RegistryOwnershipStatus::UnsafeFailure;
    }
    coordinator->publishPhase(RegistryOwnershipCoordinatorPhase::Ready);
    return RegistryOwnershipStatus::Success;
}

RegistryOwnershipStatus RegistryOwnershipCoordinator::beginOperation(
    RegistryOwnershipCoordinator *const coordinator) noexcept
{
    if (!coordinator)
        return RegistryOwnershipStatus::InvalidArgument;
    const TransactionBoundaryGuard boundaryGuard;

    const std::uintptr_t address =
        reinterpret_cast<std::uintptr_t>(coordinator);
    const RegistryBoundaryClassification boundary = ClassifyRegistryBoundary();
    if (boundary == RegistryBoundaryClassification::Poisoned
        || boundary == RegistryBoundaryClassification::Torn)
    {
        if (BoundaryMentionsAddress(address)
            || coordinator->serial_ != 0
            || coordinator->serialMirror_ != 0)
        {
            coordinator->poisonBoundary();
        }
        return RegistryOwnershipStatus::UnsafeFailure;
    }
    if (boundary == RegistryBoundaryClassification::Idle)
        return RegistryOwnershipStatus::InvalidState;
    if (!BoundaryMentionsAddress(address))
        return RegistryOwnershipStatus::Busy;

    if (!coordinator->representationConsistent()
        || !coordinator->ownsRegistryBoundary()
        || !coordinator->authenticatesOuterTransaction())
    {
        coordinator->poisonBoundary();
        return RegistryOwnershipStatus::UnsafeFailure;
    }
    if (coordinator->phase_ == RegistryOwnershipCoordinatorPhase::Acquiring
        || coordinator->phase_ == RegistryOwnershipCoordinatorPhase::Operating
        || coordinator->phase_ == RegistryOwnershipCoordinatorPhase::Finishing)
    {
        return RegistryOwnershipStatus::Busy;
    }
    if (coordinator->phase_ != RegistryOwnershipCoordinatorPhase::Ready
        || !coordinator->hashLockRetained_
        || !Sys_IsWriteLocked(&db_hashCritSect)
        || !coordinator->operationBatch_.canonicalInactive())
    {
        coordinator->poisonBoundary();
        return RegistryOwnershipStatus::UnsafeFailure;
    }

    coordinator->publishPhase(RegistryOwnershipCoordinatorPhase::Operating);
    const script_string::OwnershipBatchStatus batchStatus =
        script_string::TryBeginOwnershipBatch(&coordinator->operationBatch_);
    if (batchStatus == script_string::OwnershipBatchStatus::Success)
    {
        if (coordinator->operationBatch_.canonicalInactive()
            || coordinator->operationBatch_.serial() == 0)
        {
            coordinator->poisonBoundary();
            return RegistryOwnershipStatus::UnsafeFailure;
        }
        return RegistryOwnershipStatus::Success;
    }

    const bool canonical = coordinator->operationBatch_.canonicalInactive();
    switch (batchStatus)
    {
    case script_string::OwnershipBatchStatus::Busy:
        if (canonical)
        {
            coordinator->publishPhase(
                RegistryOwnershipCoordinatorPhase::Ready);
            return RegistryOwnershipStatus::Busy;
        }
        break;
    case script_string::OwnershipBatchStatus::InvalidState:
        if (canonical)
        {
            coordinator->publishPhase(
                RegistryOwnershipCoordinatorPhase::Ready);
            return RegistryOwnershipStatus::InvalidState;
        }
        break;
    case script_string::OwnershipBatchStatus::InvalidArgument:
    case script_string::OwnershipBatchStatus::InvalidToken:
    case script_string::OwnershipBatchStatus::UnsafeFailure:
    default:
        break;
    }
    coordinator->poisonBoundary();
    return RegistryOwnershipStatus::UnsafeFailure;
}

RegistryOwnershipStatus RegistryOwnershipCoordinator::finishOperation(
    RegistryOwnershipCoordinator *const coordinator,
    const RegistryOwnershipStatus operationStatus,
    const bool operationUnsafe) noexcept
{
    const TransactionBoundaryGuard boundaryGuard;
    if (ClassifyRegistryBoundary()
            != RegistryBoundaryClassification::Active
        || !coordinator->representationConsistent()
        || coordinator->phase_
            != RegistryOwnershipCoordinatorPhase::Operating
        || !coordinator->hashLockRetained_
        || !Sys_IsWriteLocked(&db_hashCritSect)
        || !coordinator->ownsRegistryBoundary()
        || !coordinator->authenticatesOuterTransaction())
    {
        coordinator->poisonBoundary();
        return RegistryOwnershipStatus::UnsafeFailure;
    }

    const script_string::OwnershipBatchStatus batchStatus =
        script_string::FinishOwnershipBatch(&coordinator->operationBatch_);
    if (batchStatus != script_string::OwnershipBatchStatus::Success
        || !coordinator->operationBatch_.canonicalInactive()
        || operationUnsafe)
    {
        coordinator->poisonBoundary();
        return RegistryOwnershipStatus::UnsafeFailure;
    }

    if (ClassifyRegistryBoundary()
            != RegistryBoundaryClassification::Active
        || !coordinator->representationConsistent()
        || coordinator->phase_
            != RegistryOwnershipCoordinatorPhase::Operating
        || !coordinator->hashLockRetained_
        || !Sys_IsWriteLocked(&db_hashCritSect)
        || !coordinator->ownsRegistryBoundary()
        || !coordinator->authenticatesOuterTransaction())
    {
        coordinator->poisonBoundary();
        return RegistryOwnershipStatus::UnsafeFailure;
    }

    coordinator->publishPhase(RegistryOwnershipCoordinatorPhase::Ready);
    return operationStatus;
}

script_string::RegistryOwnershipAdmission
RegistryOwnershipCoordinator::makeOperationAdmission() const noexcept
{
    return script_string::RegistryOwnershipAdmission{
        reinterpret_cast<std::uintptr_t>(this),
        serial_,
        reinterpret_cast<std::uintptr_t>(&operationBatch_),
        operationBatch_.serial()};
}

RegistryOwnershipCoordinatorPhase
RegistryOwnershipCoordinator::phase() const noexcept
{
    const TransactionBoundaryGuard boundaryGuard;
    return phase_ == phaseMirror_
        ? phase_
        : RegistryOwnershipCoordinatorPhase::UnsafeFailure;
}

RegistryOwnershipCoordinatorMode
RegistryOwnershipCoordinator::mode() const noexcept
{
    const TransactionBoundaryGuard boundaryGuard;
    return mode_ == modeMirror_
        ? mode_
        : RegistryOwnershipCoordinatorMode::None;
}

std::uint64_t RegistryOwnershipCoordinator::serial() const noexcept
{
    const TransactionBoundaryGuard boundaryGuard;
    return serial_ == serialMirror_ ? serial_ : 0;
}

bool RegistryOwnershipCoordinator::hashLockRetained() const noexcept
{
    const TransactionBoundaryGuard boundaryGuard;
    return hashLockRetained_ || hashLockRetainedMirror_;
}

bool RegistryOwnershipCoordinator::poisoned() const noexcept
{
    const TransactionBoundaryGuard boundaryGuard;
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(this);
    return phase_ == RegistryOwnershipCoordinatorPhase::UnsafeFailure
        || phaseMirror_ == RegistryOwnershipCoordinatorPhase::UnsafeFailure
        || phase_ != phaseMirror_ || mode_ != modeMirror_
        || serial_ != serialMirror_
        || hashLockRetained_ != hashLockRetainedMirror_
        || (BoundaryMentionsAddress(address)
            && ClassifyRegistryBoundary()
                != RegistryBoundaryClassification::Active);
}

bool RegistryOwnershipCoordinator::isEmptyCanonical() const noexcept
{
    const TransactionBoundaryGuard boundaryGuard;
    return phase_ == RegistryOwnershipCoordinatorPhase::Empty
        && phaseMirror_ == RegistryOwnershipCoordinatorPhase::Empty
        && mode_ == RegistryOwnershipCoordinatorMode::None
        && modeMirror_ == RegistryOwnershipCoordinatorMode::None
        && !hashLockRetained_ && !hashLockRetainedMirror_
        && reserved_[0] == 0 && reserved_[1] == 0 && serial_ == 0
        && serialMirror_ == 0 && borrowedControllerAddress_ == 0
        && borrowedControllerAddressMirror_ == 0
        && borrowedKey_ == zone_load::ZoneLoadContextKey{}
        && borrowedKeyMirror_ == zone_load::ZoneLoadContextKey{}
        && borrowedTransactionSerial_ == 0
        && borrowedTransactionSerialMirror_ == 0
        && standaloneTransactionSerial_ == 0
        && standaloneTransactionSerialMirror_ == 0
        && operationBatch_.canonicalInactive()
        && standaloneTransaction_.canonicalInactive();
}

bool RegistryOwnershipCoordinator::canonicalAfterStandaloneBegin() const noexcept
{
    return phase_ == RegistryOwnershipCoordinatorPhase::Empty
        && phaseMirror_ == RegistryOwnershipCoordinatorPhase::Empty
        && mode_ == RegistryOwnershipCoordinatorMode::None
        && modeMirror_ == RegistryOwnershipCoordinatorMode::None
        && !hashLockRetained_ && !hashLockRetainedMirror_
        && reserved_[0] == 0 && reserved_[1] == 0 && serial_ == 0
        && serialMirror_ == 0 && borrowedControllerAddress_ == 0
        && borrowedControllerAddressMirror_ == 0
        && borrowedKey_ == zone_load::ZoneLoadContextKey{}
        && borrowedKeyMirror_ == zone_load::ZoneLoadContextKey{}
        && borrowedTransactionSerial_ == 0
        && borrowedTransactionSerialMirror_ == 0
        && standaloneTransactionSerial_ == 0
        && standaloneTransactionSerialMirror_ == 0
        && operationBatch_.canonicalInactive()
        && script_string_transaction::OwnsScriptStringTransaction(
            standaloneTransaction_)
        && standaloneTransaction_.serial() != 0;
}

bool RegistryOwnershipCoordinator::representationConsistent() const noexcept
{
    if (serial_ == 0 || serial_ != serialMirror_
        || borrowedControllerAddress_
            != borrowedControllerAddressMirror_
        || borrowedKey_ != borrowedKeyMirror_
        || borrowedTransactionSerial_
            != borrowedTransactionSerialMirror_
        || standaloneTransactionSerial_
            != standaloneTransactionSerialMirror_
        || phase_ != phaseMirror_ || mode_ != modeMirror_
        || hashLockRetained_ != hashLockRetainedMirror_
        || reserved_[0] != 0 || reserved_[1] != 0)
    {
        return false;
    }

    switch (mode_)
    {
    case RegistryOwnershipCoordinatorMode::Standalone:
        return borrowedControllerAddress_ == 0
            && borrowedKey_ == zone_load::ZoneLoadContextKey{}
            && borrowedTransactionSerial_ == 0
            && standaloneTransactionSerial_ != 0;
    case RegistryOwnershipCoordinatorMode::BorrowedZoneController:
        return borrowedControllerAddress_ != 0
            && static_cast<bool>(borrowedKey_)
            && borrowedTransactionSerial_ != 0
            && standaloneTransactionSerial_ == 0
            && standaloneTransaction_.canonicalInactive();
    case RegistryOwnershipCoordinatorMode::None:
    default:
        return false;
    }
}

bool RegistryOwnershipCoordinator::ownsRegistryBoundary() const noexcept
{
    if (ClassifyRegistryBoundary()
        != RegistryBoundaryClassification::Active)
    {
        return false;
    }
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(this);
    const std::uint32_t outerSerial =
        mode_ == RegistryOwnershipCoordinatorMode::Standalone
        ? standaloneTransactionSerial_
        : borrowedTransactionSerial_;
    return s_activeCoordinatorAddress == address
        && s_activeCoordinatorAddressMirror == address
        && s_activeCoordinatorSerial == serial_
        && s_activeCoordinatorSerialMirror == serialMirror_
        && s_borrowedControllerAddress == borrowedControllerAddress_
        && s_borrowedControllerAddressMirror
            == borrowedControllerAddressMirror_
        && s_borrowedKey == borrowedKey_
        && s_borrowedKeyMirror == borrowedKeyMirror_
        && s_outerTransactionSerial == outerSerial
        && s_outerTransactionSerialMirror == outerSerial
        && s_activePhase == phase_ && s_activePhaseMirror == phaseMirror_
        && s_activeMode == mode_ && s_activeModeMirror == modeMirror_
        && s_hashLockRetained == hashLockRetained_
        && s_hashLockRetainedMirror == hashLockRetainedMirror_;
}

bool RegistryOwnershipCoordinator::authenticatesOuterTransaction() const noexcept
{
    if (!representationConsistent() || !ownsRegistryBoundary())
        return false;
    switch (mode_)
    {
    case RegistryOwnershipCoordinatorMode::Standalone:
        return standaloneTransaction_.serial()
                == standaloneTransactionSerial_
            && script_string_transaction::OwnsScriptStringTransaction(
                standaloneTransaction_);
    case RegistryOwnershipCoordinatorMode::BorrowedZoneController:
    {
        // Only exact local/global numeric mirrors may be converted back to a
        // pointer. No supplied or torn pointer is dereferenced first.
        const auto *const controller = reinterpret_cast<
            const zone_script_string_ownership::
                ZoneScriptStringOwnershipController *>(
            borrowedControllerAddress_);
        return controller->authenticatesRegistryTransaction(
            borrowedKey_, borrowedTransactionSerial_);
    }
    case RegistryOwnershipCoordinatorMode::None:
    default:
        return false;
    }
}

void RegistryOwnershipCoordinator::publishPhase(
    const RegistryOwnershipCoordinatorPhase phase) noexcept
{
    phase_ = phase;
    phaseMirror_ = phase;
    s_activePhase = phase;
    s_activePhaseMirror = phase;
}

void RegistryOwnershipCoordinator::publishHashLockRetained(
    const bool retained) noexcept
{
    hashLockRetained_ = retained;
    hashLockRetainedMirror_ = retained;
    s_hashLockRetained = retained;
    s_hashLockRetainedMirror = retained;
}

void RegistryOwnershipCoordinator::poisonBoundary() noexcept
{
    phase_ = RegistryOwnershipCoordinatorPhase::UnsafeFailure;
    phaseMirror_ = RegistryOwnershipCoordinatorPhase::UnsafeFailure;
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(this);
    if (ClassifyRegistryBoundary()
            == RegistryBoundaryClassification::Active
        && !BoundaryMentionsAddress(address))
    {
        return;
    }
    if (serial_ != 0 && serial_ == serialMirror_)
    {
        s_activeCoordinatorAddress = address;
        s_activeCoordinatorAddressMirror = address;
        s_activeCoordinatorSerial = serial_;
        s_activeCoordinatorSerialMirror = serial_;
    }
    s_activePhase = RegistryOwnershipCoordinatorPhase::UnsafeFailure;
    s_activePhaseMirror = RegistryOwnershipCoordinatorPhase::UnsafeFailure;
    PoisonRegistryBoundary();
}

void RegistryOwnershipCoordinator::resetAfterFinish() noexcept
{
    borrowedControllerAddress_ = 0;
    borrowedControllerAddressMirror_ = 0;
    borrowedKey_ = {};
    borrowedKeyMirror_ = {};
    serial_ = 0;
    serialMirror_ = 0;
    borrowedTransactionSerial_ = 0;
    borrowedTransactionSerialMirror_ = 0;
    standaloneTransactionSerial_ = 0;
    standaloneTransactionSerialMirror_ = 0;
    phase_ = RegistryOwnershipCoordinatorPhase::Empty;
    phaseMirror_ = RegistryOwnershipCoordinatorPhase::Empty;
    mode_ = RegistryOwnershipCoordinatorMode::None;
    modeMirror_ = RegistryOwnershipCoordinatorMode::None;
    hashLockRetained_ = false;
    hashLockRetainedMirror_ = false;
    reserved_[0] = 0;
    reserved_[1] = 0;
}

RegistryOwnershipCoordinator::~RegistryOwnershipCoordinator() noexcept
{
    const TransactionBoundaryGuard boundaryGuard;
    if (isEmptyCanonical())
        return;
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(this);
    const RegistryBoundaryClassification boundary = ClassifyRegistryBoundary();
    if (BoundaryMentionsAddress(address)
        || ((boundary == RegistryBoundaryClassification::Idle
                || boundary == RegistryBoundaryClassification::Torn
                || boundary == RegistryBoundaryClassification::Poisoned)
            && (serial_ != 0 || serialMirror_ != 0)))
    {
        // Deliberately no batch close, hash unlock, transaction finish,
        // callback, or reporter. The process-wide poison makes abandonment
        // nonthrowing and prevents another owner entering ambiguous state.
        poisonBoundary();
    }
}

RegistryOwnershipStatus TryBeginStandaloneRegistryOwnershipCoordinator(
    const RegistryOwnershipCoordinatorAdmission &admissionCapability,
    RegistryOwnershipCoordinator *const coordinator) noexcept
{
    if (!admissionCapability.authenticates() || !coordinator)
        return RegistryOwnershipStatus::InvalidArgument;
    // This lock-state probe must precede the serializer acquisition. A legacy
    // caller already holding db_hashCritSect cannot safely wait for a foreign
    // transaction owner that may itself be waiting for the hash.
    if (Sys_IsWriteLocked(&db_hashCritSect))
        return RegistryOwnershipStatus::Busy;
    const TransactionBoundaryGuard boundaryGuard;
    const RegistryOwnershipStatus boundaryStatus = BoundaryAdmissionStatus();
    if (boundaryStatus != RegistryOwnershipStatus::Success)
        return boundaryStatus;
    if (Sys_IsWriteLocked(&db_hashCritSect))
        return RegistryOwnershipStatus::Busy;
    if (s_nextCoordinatorSerial == UINT64_MAX)
        return RegistryOwnershipStatus::UnsafeFailure;
    if (!coordinator->isEmptyCanonical())
        return RegistryOwnershipStatus::InvalidState;

    const auto transactionStatus =
        script_string_transaction::TryBeginScriptStringTransaction(
            &coordinator->standaloneTransaction_);
    if (transactionStatus
        != script_string_transaction::ScriptStringTransactionStatus::Success)
    {
        if (!coordinator->standaloneTransaction_.canonicalInactive())
        {
            coordinator->poisonBoundary();
            return RegistryOwnershipStatus::UnsafeFailure;
        }
        return MapTransactionBeginStatus(transactionStatus);
    }
    if (!coordinator->canonicalAfterStandaloneBegin())
    {
        PoisonRegistryBoundary();
        const auto finish =
            script_string_transaction::FinishScriptStringTransaction(
                &coordinator->standaloneTransaction_);
        if (finish
                != script_string_transaction::
                    ScriptStringTransactionStatus::Success
            || !coordinator->standaloneTransaction_.canonicalInactive())
        {
            coordinator->poisonBoundary();
        }
        return RegistryOwnershipStatus::UnsafeFailure;
    }

    const RegistryOwnershipStatus status =
        RegistryOwnershipCoordinator::beginRegistered(
            coordinator,
            RegistryOwnershipCoordinatorMode::Standalone,
            nullptr,
            {},
            0);
    if (status == RegistryOwnershipStatus::Success
        || coordinator->serial_ != 0 || coordinator->serialMirror_ != 0)
    {
        return status;
    }

    const auto finishStatus =
        script_string_transaction::FinishScriptStringTransaction(
            &coordinator->standaloneTransaction_);
    if (finishStatus
            != script_string_transaction::
                ScriptStringTransactionStatus::Success
        || !coordinator->standaloneTransaction_.canonicalInactive())
    {
        coordinator->poisonBoundary();
        return RegistryOwnershipStatus::UnsafeFailure;
    }
    return status;
}

RegistryOwnershipStatus TryBorrowRegistryOwnershipCoordinator(
    const RegistryOwnershipCoordinatorAdmission &admissionCapability,
    RegistryOwnershipCoordinator *const coordinator,
    const zone_script_string_ownership::
        ZoneScriptStringOwnershipController *const controller,
    const zone_load::ZoneLoadContextKey &expectedKey) noexcept
{
    if (!admissionCapability.authenticates() || !coordinator || !controller)
        return RegistryOwnershipStatus::InvalidArgument;
    if (Sys_IsWriteLocked(&db_hashCritSect))
        return RegistryOwnershipStatus::Busy;
    const TransactionBoundaryGuard boundaryGuard;
    const RegistryOwnershipStatus boundaryStatus = BoundaryAdmissionStatus();
    if (boundaryStatus != RegistryOwnershipStatus::Success)
        return boundaryStatus;
    if (Sys_IsWriteLocked(&db_hashCritSect))
        return RegistryOwnershipStatus::Busy;
    if (!coordinator->isEmptyCanonical())
        return RegistryOwnershipStatus::InvalidState;
    if (!static_cast<bool>(expectedKey))
        return RegistryOwnershipStatus::InvalidKey;

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
    const TransactionBoundaryGuard boundaryGuard;
    const std::uintptr_t address =
        reinterpret_cast<std::uintptr_t>(coordinator);
    const RegistryBoundaryClassification boundary = ClassifyRegistryBoundary();
    if (boundary == RegistryBoundaryClassification::Poisoned
        || boundary == RegistryBoundaryClassification::Torn)
    {
        if (BoundaryMentionsAddress(address)
            || coordinator->serial_ != 0
            || coordinator->serialMirror_ != 0)
        {
            coordinator->poisonBoundary();
        }
        return RegistryOwnershipStatus::UnsafeFailure;
    }
    if (boundary == RegistryBoundaryClassification::Idle)
        return RegistryOwnershipStatus::InvalidState;
    if (!BoundaryMentionsAddress(address))
        return RegistryOwnershipStatus::Busy;
    if (!coordinator->representationConsistent()
        || !coordinator->ownsRegistryBoundary()
        || !coordinator->authenticatesOuterTransaction())
    {
        coordinator->poisonBoundary();
        return RegistryOwnershipStatus::UnsafeFailure;
    }
    if (coordinator->phase_ == RegistryOwnershipCoordinatorPhase::Acquiring
        || coordinator->phase_ == RegistryOwnershipCoordinatorPhase::Operating
        || coordinator->phase_ == RegistryOwnershipCoordinatorPhase::Finishing)
    {
        return RegistryOwnershipStatus::Busy;
    }
    if (coordinator->phase_ != RegistryOwnershipCoordinatorPhase::Ready
        || !coordinator->hashLockRetained_
        || !Sys_IsWriteLocked(&db_hashCritSect)
        || !coordinator->operationBatch_.canonicalInactive())
    {
        coordinator->poisonBoundary();
        return RegistryOwnershipStatus::UnsafeFailure;
    }

    const RegistryOwnershipCoordinatorMode mode = coordinator->mode_;
    coordinator->publishPhase(RegistryOwnershipCoordinatorPhase::Finishing);
    if (!coordinator->ownsRegistryBoundary()
        || !coordinator->authenticatesOuterTransaction())
    {
        coordinator->poisonBoundary();
        return RegistryOwnershipStatus::UnsafeFailure;
    }

    Sys_UnlockWrite(&db_hashCritSect);
    coordinator->publishHashLockRetained(false);
    if (mode == RegistryOwnershipCoordinatorMode::Standalone)
    {
        const auto status =
            script_string_transaction::FinishScriptStringTransaction(
                &coordinator->standaloneTransaction_);
        if (status
                != script_string_transaction::
                    ScriptStringTransactionStatus::Success
            || !coordinator->standaloneTransaction_.canonicalInactive())
        {
            coordinator->poisonBoundary();
            return RegistryOwnershipStatus::UnsafeFailure;
        }
    }
    else if (!coordinator->authenticatesOuterTransaction())
    {
        coordinator->poisonBoundary();
        return RegistryOwnershipStatus::UnsafeFailure;
    }

    ClearRegistryBoundary();
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

    const auto admission = coordinator->makeOperationAdmission();
    const script_string::DatabaseUserAddStatus status =
        script_string::TryAddDatabaseUser4Reference(admission, stringId);
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
        mapped == RegistryOwnershipStatus::UnsafeFailure);
}

RegistryOwnershipStatus TryRegistryAddDatabaseUsers4(
    RegistryOwnershipCoordinator *const coordinator,
    const std::uint32_t *const stringIds,
    const std::uint32_t count,
    RegistryOwnershipBulkResult *const outResult) noexcept
{
    const RegistryOwnershipStatus begin =
        RegistryOwnershipCoordinator::beginOperation(coordinator);
    if (begin != RegistryOwnershipStatus::Success)
        return begin;
    if (!outResult || !stringIds || count == 0
        || count > script_string::kRegistryOwnershipBulkCapacity)
    {
        const RegistryOwnershipStatus invalid = !outResult || !stringIds
                || count == 0
            ? RegistryOwnershipStatus::InvalidArgument
            : RegistryOwnershipStatus::CapacityExceeded;
        return RegistryOwnershipCoordinator::finishOperation(
            coordinator, invalid, false);
    }

    const auto admission = coordinator->makeOperationAdmission();
    const script_string::DatabaseUserAddBulkResult result =
        script_string::TryAddDatabaseUser4References(
            admission, stringIds, count);
    const RegistryOwnershipStatus mapped =
        MapDatabaseBulkStatus(result.status);
    const RegistryOwnershipStatus finish =
        RegistryOwnershipCoordinator::finishOperation(
            coordinator,
            mapped,
            mapped == RegistryOwnershipStatus::UnsafeFailure);
    if (finish == RegistryOwnershipStatus::Success
        || finish == RegistryOwnershipStatus::NoChange)
    {
        *outResult = {result.addedCount, result.unchangedCount};
    }
    return finish;
}

RegistryOwnershipStatus TryRegistryInternBoundedName(
    RegistryOwnershipCoordinator *const coordinator,
    const char *const bytes,
    const std::uint32_t byteCount,
    RegistryOwnershipName *const outName) noexcept
{
    const RegistryOwnershipStatus begin =
        RegistryOwnershipCoordinator::beginOperation(coordinator);
    if (begin != RegistryOwnershipStatus::Success)
        return begin;
    if (!outName)
    {
        return RegistryOwnershipCoordinator::finishOperation(
            coordinator, RegistryOwnershipStatus::InvalidArgument, false);
    }

    const auto admission = coordinator->makeOperationAdmission();
    const script_string::DatabaseNameResult result =
        script_string::TryInternDatabaseUser4Name(
            admission, bytes, byteCount, 6);
    const RegistryOwnershipStatus mapped = MapDatabaseNameStatus(result.status);
    const RegistryOwnershipStatus finish =
        RegistryOwnershipCoordinator::finishOperation(
            coordinator,
            mapped,
            mapped == RegistryOwnershipStatus::UnsafeFailure);
    if (finish == RegistryOwnershipStatus::Success)
        *outName = {result.stringId, result.canonicalName};
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
    const auto admission = coordinator->makeOperationAdmission();
    const script_string::DatabaseNameStatus status =
        script_string::TryReAddRetainedDatabaseName(
            admission, retainedCanonicalName);
    const RegistryOwnershipStatus mapped = MapDatabaseNameStatus(status);
    return RegistryOwnershipCoordinator::finishOperation(
        coordinator,
        mapped,
        mapped == RegistryOwnershipStatus::UnsafeFailure);
}

RegistryOwnershipStatus TryRegistryReAddRetainedDefaultNames(
    RegistryOwnershipCoordinator *const coordinator,
    const char *const *const retainedCanonicalNames,
    const std::uint32_t count,
    RegistryOwnershipBulkResult *const outResult) noexcept
{
    const RegistryOwnershipStatus begin =
        RegistryOwnershipCoordinator::beginOperation(coordinator);
    if (begin != RegistryOwnershipStatus::Success)
        return begin;
    if (!outResult || !retainedCanonicalNames || count == 0
        || count > script_string::kRegistryOwnershipBulkCapacity)
    {
        const RegistryOwnershipStatus invalid =
            !outResult || !retainedCanonicalNames || count == 0
            ? RegistryOwnershipStatus::InvalidArgument
            : RegistryOwnershipStatus::CapacityExceeded;
        return RegistryOwnershipCoordinator::finishOperation(
            coordinator, invalid, false);
    }

    const auto admission = coordinator->makeOperationAdmission();
    const script_string::DatabaseUserAddBulkResult result =
        script_string::TryReAddRetainedDatabaseNames(
            admission, retainedCanonicalNames, count);
    const RegistryOwnershipStatus mapped =
        MapDatabaseBulkStatus(result.status);
    const RegistryOwnershipStatus finish =
        RegistryOwnershipCoordinator::finishOperation(
            coordinator,
            mapped,
            mapped == RegistryOwnershipStatus::UnsafeFailure);
    if (finish == RegistryOwnershipStatus::Success
        || finish == RegistryOwnershipStatus::NoChange)
    {
        *outResult = {result.addedCount, result.unchangedCount};
    }
    return finish;
}

RegistryOwnershipStatus TryRegistryTransferDatabaseUsers4To8(
    RegistryOwnershipCoordinator *const coordinator) noexcept
{
    const RegistryOwnershipStatus begin =
        RegistryOwnershipCoordinator::beginOperation(coordinator);
    if (begin != RegistryOwnershipStatus::Success)
        return begin;
    const auto admission = coordinator->makeOperationAdmission();
    const script_string::DatabaseSweepStatus status =
        script_string::TryTransferDatabaseUsers4To8(admission);
    RegistryOwnershipStatus mapped = RegistryOwnershipStatus::UnsafeFailure;
    switch (status)
    {
    case script_string::DatabaseSweepStatus::Success:
        mapped = RegistryOwnershipStatus::Success;
        break;
    case script_string::DatabaseSweepStatus::CapacityNoChange:
        mapped = RegistryOwnershipStatus::CapacityExceeded;
        break;
    case script_string::DatabaseSweepStatus::UnsafeFailure:
    default:
        break;
    }
    return RegistryOwnershipCoordinator::finishOperation(
        coordinator,
        mapped,
        mapped == RegistryOwnershipStatus::UnsafeFailure);
}

RegistryOwnershipStatus TryRegistryShutdownDatabaseUser8(
    RegistryOwnershipCoordinator *const coordinator) noexcept
{
    const RegistryOwnershipStatus begin =
        RegistryOwnershipCoordinator::beginOperation(coordinator);
    if (begin != RegistryOwnershipStatus::Success)
        return begin;
    const auto admission = coordinator->makeOperationAdmission();
    const script_string::DatabaseSweepStatus status =
        script_string::TryShutdownDatabaseUser8(admission);
    RegistryOwnershipStatus mapped = RegistryOwnershipStatus::UnsafeFailure;
    switch (status)
    {
    case script_string::DatabaseSweepStatus::Success:
        mapped = RegistryOwnershipStatus::Success;
        break;
    case script_string::DatabaseSweepStatus::CapacityNoChange:
        mapped = RegistryOwnershipStatus::CapacityExceeded;
        break;
    case script_string::DatabaseSweepStatus::UnsafeFailure:
    default:
        break;
    }
    return RegistryOwnershipCoordinator::finishOperation(
        coordinator,
        mapped,
        mapped == RegistryOwnershipStatus::UnsafeFailure);
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
    const TransactionBoundaryGuard boundaryGuard;
    if (coordinator)
        coordinator->reserved_[0] = reserved;
}

void RegistryOwnershipCoordinatorTestAccess::SetRepresentationMirrors(
    RegistryOwnershipCoordinator *const coordinator,
    const std::uint64_t serialMirror,
    const std::uintptr_t borrowedAddressMirror,
    const std::uint32_t borrowedTransactionSerialMirror,
    const std::uint32_t standaloneTransactionSerialMirror,
    const zone_load::ZoneLoadContextKey &borrowedKeyMirror,
    const std::uint8_t phaseMirror,
    const std::uint8_t modeMirror,
    const bool hashMirror) noexcept
{
    const TransactionBoundaryGuard boundaryGuard;
    if (!coordinator)
        return;
    coordinator->serialMirror_ = serialMirror;
    coordinator->borrowedControllerAddressMirror_ = borrowedAddressMirror;
    coordinator->borrowedTransactionSerialMirror_ =
        borrowedTransactionSerialMirror;
    coordinator->standaloneTransactionSerialMirror_ =
        standaloneTransactionSerialMirror;
    coordinator->borrowedKeyMirror_ = borrowedKeyMirror;
    coordinator->phaseMirror_ =
        static_cast<RegistryOwnershipCoordinatorPhase>(phaseMirror);
    coordinator->modeMirror_ =
        static_cast<RegistryOwnershipCoordinatorMode>(modeMirror);
    coordinator->hashLockRetainedMirror_ = hashMirror;
}

void RegistryOwnershipCoordinatorTestAccess::ResetStorageForTesting(
    RegistryOwnershipCoordinator *const coordinator) noexcept
{
    const TransactionBoundaryGuard boundaryGuard;
    if (coordinator)
        coordinator->resetAfterFinish();
}

void SetRegistryOwnershipCoordinatorBoundaryForTesting(
    const std::uintptr_t address,
    const std::uint64_t serial,
    const std::uintptr_t addressMirror,
    const std::uint64_t serialMirror,
    const std::uint8_t state,
    const std::uint8_t stateMirror) noexcept
{
    const TransactionBoundaryGuard boundaryGuard;
    if (address == 0 && serial == 0 && addressMirror == 0
        && serialMirror == 0 && state == 0 && stateMirror == 0)
    {
        ClearRegistryBoundary();
        s_nextCoordinatorSerialMirror = s_nextCoordinatorSerial;
        return;
    }
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
    const TransactionBoundaryGuard boundaryGuard;
    if (ClassifyRegistryBoundary() == RegistryBoundaryClassification::Idle)
    {
        s_nextCoordinatorSerial = serial;
        s_nextCoordinatorSerialMirror = serial;
    }
}

void SetRegistryOwnershipCoordinatorGlobalMirrorsForTesting(
    const std::uint64_t nextSerialMirror,
    const std::uintptr_t borrowedAddress,
    const std::uintptr_t borrowedAddressMirror,
    const std::uint32_t borrowedTransactionSerial,
    const std::uint32_t borrowedTransactionSerialMirror,
    const std::uint8_t phase,
    const std::uint8_t phaseMirror,
    const std::uint8_t mode,
    const std::uint8_t modeMirror,
    const bool hashRetained,
    const bool hashRetainedMirror) noexcept
{
    const TransactionBoundaryGuard boundaryGuard;
    s_nextCoordinatorSerialMirror = nextSerialMirror;
    s_borrowedControllerAddress = borrowedAddress;
    s_borrowedControllerAddressMirror = borrowedAddressMirror;
    s_outerTransactionSerial = borrowedTransactionSerial;
    s_outerTransactionSerialMirror = borrowedTransactionSerialMirror;
    s_activePhase =
        static_cast<RegistryOwnershipCoordinatorPhase>(phase);
    s_activePhaseMirror =
        static_cast<RegistryOwnershipCoordinatorPhase>(phaseMirror);
    s_activeMode = static_cast<RegistryOwnershipCoordinatorMode>(mode);
    s_activeModeMirror =
        static_cast<RegistryOwnershipCoordinatorMode>(modeMirror);
    s_hashLockRetained = hashRetained;
    s_hashLockRetainedMirror = hashRetainedMirror;
}
#endif

} // namespace db::registry_ownership
