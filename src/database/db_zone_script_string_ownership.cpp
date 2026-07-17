#include <database/db_zone_script_string_ownership.h>

namespace db::zone_script_string_ownership
{
namespace
{
namespace adapter = script_string_adapter;
namespace journal = script_string_journal;
namespace transaction = script_string_transaction;
namespace lifecycle = zone_load;

[[nodiscard]] constexpr bool IsKnownPhase(
    const ZoneScriptStringOwnershipPhase phase) noexcept
{
    switch (phase)
    {
    case ZoneScriptStringOwnershipPhase::Empty:
    case ZoneScriptStringOwnershipPhase::Staging:
    case ZoneScriptStringOwnershipPhase::Sealed:
    case ZoneScriptStringOwnershipPhase::Transferring:
    case ZoneScriptStringOwnershipPhase::Transferred:
    case ZoneScriptStringOwnershipPhase::CommitReady:
    case ZoneScriptStringOwnershipPhase::Unpublishing:
    case ZoneScriptStringOwnershipPhase::UnpublishingCallback:
    case ZoneScriptStringOwnershipPhase::RollingBack:
    case ZoneScriptStringOwnershipPhase::OwnershipRolledBack:
    case ZoneScriptStringOwnershipPhase::Cleaning:
    case ZoneScriptStringOwnershipPhase::Admitting:
    case ZoneScriptStringOwnershipPhase::Live:
    case ZoneScriptStringOwnershipPhase::Abandoned:
    case ZoneScriptStringOwnershipPhase::UnsafeFailure:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool IsCallbackPhase(
    const ZoneScriptStringOwnershipPhase phase) noexcept
{
    return phase == ZoneScriptStringOwnershipPhase::UnpublishingCallback
        || phase == ZoneScriptStringOwnershipPhase::Cleaning
        || phase == ZoneScriptStringOwnershipPhase::Admitting;
}

[[nodiscard]] constexpr bool IsRollbackSourcePhase(
    const ZoneScriptStringOwnershipPhase phase) noexcept
{
    switch (phase)
    {
    case ZoneScriptStringOwnershipPhase::Staging:
    case ZoneScriptStringOwnershipPhase::Sealed:
    case ZoneScriptStringOwnershipPhase::Transferring:
    case ZoneScriptStringOwnershipPhase::Transferred:
    case ZoneScriptStringOwnershipPhase::CommitReady:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr journal::ScriptStringJournalPhase JournalPhaseFor(
    const ZoneScriptStringOwnershipPhase phase) noexcept
{
    switch (phase)
    {
    case ZoneScriptStringOwnershipPhase::Staging:
        return journal::ScriptStringJournalPhase::Staging;
    case ZoneScriptStringOwnershipPhase::Sealed:
        return journal::ScriptStringJournalPhase::Sealed;
    case ZoneScriptStringOwnershipPhase::Transferring:
        return journal::ScriptStringJournalPhase::Transferring;
    case ZoneScriptStringOwnershipPhase::Transferred:
        return journal::ScriptStringJournalPhase::Transferred;
    case ZoneScriptStringOwnershipPhase::CommitReady:
        return journal::ScriptStringJournalPhase::CommitReady;
    case ZoneScriptStringOwnershipPhase::RollingBack:
        return journal::ScriptStringJournalPhase::RollingBack;
    case ZoneScriptStringOwnershipPhase::OwnershipRolledBack:
    case ZoneScriptStringOwnershipPhase::Cleaning:
        return journal::ScriptStringJournalPhase::RolledBack;
    case ZoneScriptStringOwnershipPhase::Admitting:
    case ZoneScriptStringOwnershipPhase::Live:
        return journal::ScriptStringJournalPhase::Committed;
    case ZoneScriptStringOwnershipPhase::Empty:
    case ZoneScriptStringOwnershipPhase::Unpublishing:
    case ZoneScriptStringOwnershipPhase::UnpublishingCallback:
    case ZoneScriptStringOwnershipPhase::Abandoned:
    case ZoneScriptStringOwnershipPhase::UnsafeFailure:
    default:
        return journal::ScriptStringJournalPhase::Staging;
    }
}

[[nodiscard]] ZoneScriptStringOwnershipStatus MapJournalStatus(
    const journal::ScriptStringJournalStatus status) noexcept
{
    switch (status)
    {
    case journal::ScriptStringJournalStatus::Success:
        return ZoneScriptStringOwnershipStatus::Success;
    case journal::ScriptStringJournalStatus::RetryNoChange:
        return ZoneScriptStringOwnershipStatus::Retry;
    case journal::ScriptStringJournalStatus::Rejected:
        return ZoneScriptStringOwnershipStatus::Rejected;
    case journal::ScriptStringJournalStatus::Busy:
        return ZoneScriptStringOwnershipStatus::Busy;
    case journal::ScriptStringJournalStatus::InvalidArgument:
        return ZoneScriptStringOwnershipStatus::InvalidArgument;
    case journal::ScriptStringJournalStatus::InvalidState:
        return ZoneScriptStringOwnershipStatus::InvalidState;
    case journal::ScriptStringJournalStatus::InvalidKey:
        return ZoneScriptStringOwnershipStatus::InvalidKey;
    case journal::ScriptStringJournalStatus::StaleKey:
        return ZoneScriptStringOwnershipStatus::StaleKey;
    case journal::ScriptStringJournalStatus::InvalidPhase:
        return ZoneScriptStringOwnershipStatus::InvalidPhase;
    case journal::ScriptStringJournalStatus::CountMismatch:
        return ZoneScriptStringOwnershipStatus::CountMismatch;
    case journal::ScriptStringJournalStatus::CapacityExceeded:
        return ZoneScriptStringOwnershipStatus::CapacityExceeded;
    case journal::ScriptStringJournalStatus::UnsafeFailure:
    default:
        return ZoneScriptStringOwnershipStatus::UnsafeFailure;
    }
}

[[nodiscard]] ZoneScriptStringOwnershipStatus MapLifecycleStatus(
    const lifecycle::ZoneLoadContextStatus status) noexcept
{
    switch (status)
    {
    case lifecycle::ZoneLoadContextStatus::Success:
        return ZoneScriptStringOwnershipStatus::Success;
    case lifecycle::ZoneLoadContextStatus::Retry:
        return ZoneScriptStringOwnershipStatus::Retry;
    case lifecycle::ZoneLoadContextStatus::Busy:
        return ZoneScriptStringOwnershipStatus::Busy;
    case lifecycle::ZoneLoadContextStatus::InvalidArgument:
        return ZoneScriptStringOwnershipStatus::InvalidArgument;
    case lifecycle::ZoneLoadContextStatus::InvalidState:
        return ZoneScriptStringOwnershipStatus::InvalidState;
    case lifecycle::ZoneLoadContextStatus::InvalidKey:
        return ZoneScriptStringOwnershipStatus::InvalidKey;
    case lifecycle::ZoneLoadContextStatus::StaleKey:
        return ZoneScriptStringOwnershipStatus::StaleKey;
    case lifecycle::ZoneLoadContextStatus::InvalidPhase:
        return ZoneScriptStringOwnershipStatus::InvalidPhase;
    case lifecycle::ZoneLoadContextStatus::GenerationExhausted:
        return ZoneScriptStringOwnershipStatus::InvalidState;
    case lifecycle::ZoneLoadContextStatus::UnsafeFailure:
    default:
        return ZoneScriptStringOwnershipStatus::UnsafeFailure;
    }
}

[[nodiscard]] ZoneScriptStringOwnershipStatus MapTransactionStatus(
    const transaction::ScriptStringTransactionStatus status) noexcept
{
    switch (status)
    {
    case transaction::ScriptStringTransactionStatus::Success:
        return ZoneScriptStringOwnershipStatus::Success;
    case transaction::ScriptStringTransactionStatus::Busy:
        return ZoneScriptStringOwnershipStatus::Busy;
    case transaction::ScriptStringTransactionStatus::InvalidArgument:
        return ZoneScriptStringOwnershipStatus::InvalidArgument;
    case transaction::ScriptStringTransactionStatus::InvalidToken:
    default:
        return ZoneScriptStringOwnershipStatus::InvalidState;
    }
}

} // namespace

ZoneScriptStringOwnershipPhase
ZoneScriptStringOwnershipController::phase() const noexcept
{
    return phase_;
}

const lifecycle::ZoneLoadContextKey &
ZoneScriptStringOwnershipController::key() const noexcept
{
    return key_;
}

bool ZoneScriptStringOwnershipController::serializerRetained() const noexcept
{
    return transactionSerial_ != 0;
}

bool ZoneScriptStringOwnershipController::poisoned() const noexcept
{
    return phase_ == ZoneScriptStringOwnershipPhase::UnsafeFailure;
}

bool ZoneScriptStringOwnershipController::isEmptyCanonical() const noexcept
{
    return phase_ == ZoneScriptStringOwnershipPhase::Empty
        && key_ == lifecycle::ZoneLoadContextKey{}
        && lifecycle_ == nullptr && journal_ == nullptr && storage_ == nullptr
        && rollbackContext_ == nullptr && ensureUnreachable_ == nullptr
        && performCleanup_ == nullptr && storageCapacity_ == 0
        && expectedCount_ == 0 && transactionSerial_ == 0
        && transaction_.canonicalInactive()
        && resumePhase_ == ZoneScriptStringOwnershipPhase::Empty
        && reserved_[0] == 0 && reserved_[1] == 0;
}

lifecycle::ZoneLoadCleanupCallbackStatus
ZoneScriptStringOwnershipController::PerformBoundCleanup(
    void *const context,
    const lifecycle::ZoneLoadCleanupOperation operation) noexcept
{
    auto *const controller =
        static_cast<ZoneScriptStringOwnershipController *>(context);
    if (!controller)
        return lifecycle::ZoneLoadCleanupCallbackStatus::UnsafeFailure;

    if (operation
        == lifecycle::ZoneLoadCleanupOperation::
            MakePartialAssetsAndStagedReferencesUnreachable)
    {
        if (!controller->ensureUnreachable_)
            return lifecycle::ZoneLoadCleanupCallbackStatus::UnsafeFailure;
        switch (controller->ensureUnreachable_(controller->rollbackContext_))
        {
        case ZoneScriptStringUnpublishStatus::Success:
            return lifecycle::ZoneLoadCleanupCallbackStatus::Success;
        case ZoneScriptStringUnpublishStatus::Retry:
            return lifecycle::ZoneLoadCleanupCallbackStatus::Retry;
        case ZoneScriptStringUnpublishStatus::UnsafeFailure:
        default:
            return lifecycle::ZoneLoadCleanupCallbackStatus::UnsafeFailure;
        }
    }

    if (!controller->performCleanup_)
        return lifecycle::ZoneLoadCleanupCallbackStatus::UnsafeFailure;
    return controller->performCleanup_(
        controller->rollbackContext_, operation);
}

bool ZoneScriptStringOwnershipController::bindingMatchesCurrentPhase() const noexcept
{
    if (!IsKnownPhase(phase_) || reserved_[0] != 0 || reserved_[1] != 0
        || !static_cast<bool>(key_) || !lifecycle_)
    {
        return false;
    }

    if (!lifecycle::ZoneLoadContextKeyMatches(lifecycle_, key_))
        return false;

    ZoneScriptStringOwnershipPhase bindingPhase = phase_;
    if (bindingPhase == ZoneScriptStringOwnershipPhase::Unpublishing
        || bindingPhase
            == ZoneScriptStringOwnershipPhase::UnpublishingCallback)
        bindingPhase = resumePhase_;

    if (bindingPhase == ZoneScriptStringOwnershipPhase::Admitting)
    {
        return lifecycle_->phase() == lifecycle::ZoneLoadContextPhase::Live
            && journal_ == nullptr && storage_ == nullptr;
    }

    if (bindingPhase == ZoneScriptStringOwnershipPhase::RollingBack
        || bindingPhase
            == ZoneScriptStringOwnershipPhase::OwnershipRolledBack
        || bindingPhase == ZoneScriptStringOwnershipPhase::Cleaning)
    {
        if (lifecycle_->phase()
                != lifecycle::ZoneLoadContextPhase::Abandoning
            || lifecycle_->terminalKind()
                != lifecycle::ZoneLoadTerminalKind::Abandoned)
        {
            return false;
        }
    }
    else if (lifecycle_->phase()
        != lifecycle::ZoneLoadContextPhase::Loading)
    {
        return false;
    }

    if (bindingPhase
            == ZoneScriptStringOwnershipPhase::OwnershipRolledBack
        || bindingPhase == ZoneScriptStringOwnershipPhase::Cleaning)
    {
        return journal_ == nullptr && storage_ == nullptr
            && storageCapacity_ == 0 && expectedCount_ == 0;
    }

    if (!journal_
        || !journal::ScriptStringJournalKeyMatches(journal_, key_)
        || journal_->capacity() != storageCapacity_
        || journal_->expectedCount() != expectedCount_
        || journal_->storage() != storage_)
    {
        return false;
    }

    return journal_->phase() == JournalPhaseFor(bindingPhase);
}

ZoneScriptStringOwnershipStatus
ZoneScriptStringOwnershipController::validateOwned() const noexcept
{
    // Authenticate first. A foreign thread blocks here until the owner has
    // published terminal controller state and released the serializer.
    if (!transaction::OwnsScriptStringTransaction(transaction_))
    {
        return phase_ == ZoneScriptStringOwnershipPhase::Empty
                || phase_ == ZoneScriptStringOwnershipPhase::Live
                || phase_ == ZoneScriptStringOwnershipPhase::Abandoned
            ? ZoneScriptStringOwnershipStatus::InvalidPhase
            : ZoneScriptStringOwnershipStatus::InvalidState;
    }
    if (phase_ == ZoneScriptStringOwnershipPhase::UnsafeFailure)
        return ZoneScriptStringOwnershipStatus::UnsafeFailure;
    if (phase_ == ZoneScriptStringOwnershipPhase::Empty
        || phase_ == ZoneScriptStringOwnershipPhase::Live
        || phase_ == ZoneScriptStringOwnershipPhase::Abandoned)
    {
        return ZoneScriptStringOwnershipStatus::InvalidPhase;
    }
    if (transactionSerial_ == 0
        || transaction_.serial() != transactionSerial_)
    {
        return ZoneScriptStringOwnershipStatus::InvalidState;
    }
    if (IsCallbackPhase(phase_))
        return ZoneScriptStringOwnershipStatus::Busy;
    if (!bindingMatchesCurrentPhase())
    {
        return static_cast<bool>(key_)
                && lifecycle_ && lifecycle_->slotIndex() == key_.slot
                && lifecycle_->generation() != key_.generation
            ? ZoneScriptStringOwnershipStatus::StaleKey
            : ZoneScriptStringOwnershipStatus::InvalidState;
    }
    return ZoneScriptStringOwnershipStatus::Success;
}

ZoneScriptStringOwnershipStatus
ZoneScriptStringOwnershipController::validateAbandonedReceipt() const noexcept
{
    if (phase_ != ZoneScriptStringOwnershipPhase::Abandoned)
        return ZoneScriptStringOwnershipStatus::InvalidPhase;
    if (journal_ != nullptr || storage_ != nullptr
        || rollbackContext_ != nullptr || ensureUnreachable_ != nullptr
        || performCleanup_ != nullptr || storageCapacity_ != 0
        || expectedCount_ != 0 || transactionSerial_ != 0
        || !transaction_.canonicalInactive()
        || resumePhase_ != ZoneScriptStringOwnershipPhase::Empty
        || reserved_[0] != 0 || reserved_[1] != 0)
    {
        return ZoneScriptStringOwnershipStatus::InvalidState;
    }
    if (!static_cast<bool>(key_))
        return ZoneScriptStringOwnershipStatus::InvalidKey;
    if (!lifecycle_ || !lifecycle_->initialized())
        return ZoneScriptStringOwnershipStatus::InvalidState;
    if (lifecycle_->slotIndex() != key_.slot)
        return ZoneScriptStringOwnershipStatus::InvalidKey;
    if (lifecycle_->generation() != key_.generation)
        return ZoneScriptStringOwnershipStatus::StaleKey;
    if (lifecycle_->phase() != lifecycle::ZoneLoadContextPhase::Empty
        || lifecycle_->terminalKind()
            != lifecycle::ZoneLoadTerminalKind::Abandoned)
    {
        return ZoneScriptStringOwnershipStatus::InvalidState;
    }
    return ZoneScriptStringOwnershipStatus::Success;
}

void ZoneScriptStringOwnershipController::detachJournalBacking() noexcept
{
    journal_ = nullptr;
    storage_ = nullptr;
    storageCapacity_ = 0;
    expectedCount_ = 0;
}

void ZoneScriptStringOwnershipController::poison() noexcept
{
    phase_ = ZoneScriptStringOwnershipPhase::UnsafeFailure;
}

void ZoneScriptStringOwnershipController::reset() noexcept
{
    key_ = {};
    lifecycle_ = nullptr;
    journal_ = nullptr;
    storage_ = nullptr;
    rollbackContext_ = nullptr;
    ensureUnreachable_ = nullptr;
    performCleanup_ = nullptr;
    storageCapacity_ = 0;
    expectedCount_ = 0;
    transactionSerial_ = 0;
    phase_ = ZoneScriptStringOwnershipPhase::Empty;
    resumePhase_ = ZoneScriptStringOwnershipPhase::Empty;
    reserved_[0] = 0;
    reserved_[1] = 0;
}

ZoneScriptStringOwnershipStatus TryBeginZoneScriptStringOwnership(
    ZoneScriptStringOwnershipController *const controller,
    lifecycle::ZoneLoadContextSlot *const lifecycleSlot,
    const lifecycle::ZoneLoadContextKey &key,
    journal::ScriptStringJournal *const stringJournal,
    journal::ScriptStringJournalEntry *const storage,
    const std::uint32_t storageCapacity,
    const std::uint32_t expectedCount) noexcept
{
    if (!controller || !lifecycleSlot || !stringJournal
        || !static_cast<bool>(key))
    {
        return ZoneScriptStringOwnershipStatus::InvalidArgument;
    }
    if (!controller->isEmptyCanonical())
        return ZoneScriptStringOwnershipStatus::InvalidState;

    const transaction::ScriptStringTransactionStatus beginStatus =
        transaction::TryBeginScriptStringTransaction(
            &controller->transaction_);
    if (beginStatus != transaction::ScriptStringTransactionStatus::Success)
        return MapTransactionStatus(beginStatus);

    ZoneScriptStringOwnershipStatus result =
        ZoneScriptStringOwnershipStatus::Success;
    if (!lifecycle::ZoneLoadContextKeyMatches(lifecycleSlot, key))
    {
        result = lifecycleSlot->slotIndex() == key.slot
                && lifecycleSlot->generation() != key.generation
            ? ZoneScriptStringOwnershipStatus::StaleKey
            : ZoneScriptStringOwnershipStatus::InvalidKey;
    }
    else if (lifecycleSlot->phase()
        != lifecycle::ZoneLoadContextPhase::Loading)
    {
        result = ZoneScriptStringOwnershipStatus::InvalidPhase;
    }

    if (result == ZoneScriptStringOwnershipStatus::Success)
    {
        result = MapJournalStatus(
            journal::TryInitializeScriptStringJournal(
                stringJournal,
                key,
                storage,
                storageCapacity,
                expectedCount));
    }

    if (result != ZoneScriptStringOwnershipStatus::Success)
    {
        const transaction::ScriptStringTransactionStatus finishStatus =
            transaction::FinishScriptStringTransaction(
                &controller->transaction_);
        return finishStatus == transaction::ScriptStringTransactionStatus::Success
            ? result
            : ZoneScriptStringOwnershipStatus::UnsafeFailure;
    }

    controller->key_ = key;
    controller->lifecycle_ = lifecycleSlot;
    controller->journal_ = stringJournal;
    controller->storage_ = storage;
    controller->storageCapacity_ = storageCapacity;
    controller->expectedCount_ = expectedCount;
    controller->transactionSerial_ = controller->transaction_.serial();
    controller->phase_ = ZoneScriptStringOwnershipPhase::Staging;
    if (controller->transactionSerial_ == 0)
    {
        controller->poison();
        return ZoneScriptStringOwnershipStatus::UnsafeFailure;
    }
    return ZoneScriptStringOwnershipStatus::Success;
}

ZoneScriptStringOwnershipStatus TryStageZoneScriptString(
    ZoneScriptStringOwnershipController *const controller,
    const adapter::ScriptStringSourceView &source,
    std::uint32_t *const outStringId) noexcept
{
    if (!controller || !outStringId)
        return ZoneScriptStringOwnershipStatus::InvalidArgument;
    const ZoneScriptStringOwnershipStatus validation =
        controller->validateOwned();
    if (validation != ZoneScriptStringOwnershipStatus::Success)
        return validation;
    if (controller->phase_ != ZoneScriptStringOwnershipPhase::Staging)
        return ZoneScriptStringOwnershipStatus::InvalidPhase;

    const journal::ScriptStringJournalStatus status =
        adapter::TryStageScriptStringFromSource(
            controller->journal_,
            controller->key_,
            controller->transaction_,
            source,
            outStringId);
    if (status == journal::ScriptStringJournalStatus::UnsafeFailure)
        controller->poison();
    return MapJournalStatus(status);
}

ZoneScriptStringOwnershipStatus TrySealZoneScriptStrings(
    ZoneScriptStringOwnershipController *const controller) noexcept
{
    if (!controller)
        return ZoneScriptStringOwnershipStatus::InvalidArgument;
    const ZoneScriptStringOwnershipStatus validation =
        controller->validateOwned();
    if (validation != ZoneScriptStringOwnershipStatus::Success)
        return validation;
    if (controller->phase_ == ZoneScriptStringOwnershipPhase::Sealed)
        return ZoneScriptStringOwnershipStatus::Success;
    if (controller->phase_ != ZoneScriptStringOwnershipPhase::Staging)
        return ZoneScriptStringOwnershipStatus::InvalidPhase;

    const journal::ScriptStringJournalStatus status =
        journal::TrySealScriptStringJournal(
            controller->journal_, controller->key_);
    if (status == journal::ScriptStringJournalStatus::Success)
        controller->phase_ = ZoneScriptStringOwnershipPhase::Sealed;
    else if (status == journal::ScriptStringJournalStatus::UnsafeFailure)
        controller->poison();
    return MapJournalStatus(status);
}

ZoneScriptStringOwnershipStatus TryBeginZoneScriptStringTransfer(
    ZoneScriptStringOwnershipController *const controller) noexcept
{
    if (!controller)
        return ZoneScriptStringOwnershipStatus::InvalidArgument;
    const ZoneScriptStringOwnershipStatus validation =
        controller->validateOwned();
    if (validation != ZoneScriptStringOwnershipStatus::Success)
        return validation;
    if (controller->phase_ == ZoneScriptStringOwnershipPhase::Transferring
        || controller->phase_ == ZoneScriptStringOwnershipPhase::Transferred)
    {
        return ZoneScriptStringOwnershipStatus::Success;
    }
    if (controller->phase_ != ZoneScriptStringOwnershipPhase::Sealed)
        return ZoneScriptStringOwnershipStatus::InvalidPhase;

    const journal::ScriptStringJournalStatus status =
        journal::TryBeginScriptStringTransfer(
            controller->journal_, controller->key_);
    if (status == journal::ScriptStringJournalStatus::Success)
        controller->phase_ = ZoneScriptStringOwnershipPhase::Transferring;
    else if (status == journal::ScriptStringJournalStatus::UnsafeFailure)
        controller->poison();
    return MapJournalStatus(status);
}

ZoneScriptStringOwnershipStatus TryTransferNextZoneScriptString(
    ZoneScriptStringOwnershipController *const controller) noexcept
{
    if (!controller)
        return ZoneScriptStringOwnershipStatus::InvalidArgument;
    const ZoneScriptStringOwnershipStatus validation =
        controller->validateOwned();
    if (validation != ZoneScriptStringOwnershipStatus::Success)
        return validation;
    if (controller->phase_ == ZoneScriptStringOwnershipPhase::Transferred)
        return ZoneScriptStringOwnershipStatus::Success;
    if (controller->phase_ != ZoneScriptStringOwnershipPhase::Transferring)
        return ZoneScriptStringOwnershipStatus::InvalidPhase;

    const journal::ScriptStringJournalStatus status =
        adapter::TryTransferNextScriptStringToDatabaseUser(
            controller->journal_,
            controller->key_,
            controller->transaction_);
    if (status == journal::ScriptStringJournalStatus::Success
        && controller->journal_->phase()
            == journal::ScriptStringJournalPhase::Transferred)
    {
        controller->phase_ = ZoneScriptStringOwnershipPhase::Transferred;
    }
    else if (status == journal::ScriptStringJournalStatus::UnsafeFailure)
    {
        controller->poison();
    }
    return MapJournalStatus(status);
}

ZoneScriptStringOwnershipStatus TryPrepareZoneScriptStringCommit(
    ZoneScriptStringOwnershipController *const controller) noexcept
{
    if (!controller)
        return ZoneScriptStringOwnershipStatus::InvalidArgument;
    const ZoneScriptStringOwnershipStatus validation =
        controller->validateOwned();
    if (validation != ZoneScriptStringOwnershipStatus::Success)
        return validation;
    if (controller->phase_ == ZoneScriptStringOwnershipPhase::CommitReady)
        return ZoneScriptStringOwnershipStatus::Success;
    if (controller->phase_ != ZoneScriptStringOwnershipPhase::Transferred)
        return ZoneScriptStringOwnershipStatus::InvalidPhase;

    const journal::ScriptStringJournalStatus status =
        journal::TryPrepareScriptStringJournalCommit(
            controller->journal_, controller->key_);
    if (status == journal::ScriptStringJournalStatus::Success)
        controller->phase_ = ZoneScriptStringOwnershipPhase::CommitReady;
    else if (status == journal::ScriptStringJournalStatus::UnsafeFailure)
        controller->poison();
    return MapJournalStatus(status);
}

ZoneScriptStringOwnershipStatus TryCommitZoneScriptStringsAndAdmit(
    ZoneScriptStringOwnershipController *const controller,
    const ZoneScriptStringAdmissionCallback &admission) noexcept
{
    if (!controller || !admission.admitLive)
        return ZoneScriptStringOwnershipStatus::InvalidArgument;
    const ZoneScriptStringOwnershipStatus validation =
        controller->validateOwned();
    if (validation != ZoneScriptStringOwnershipStatus::Success)
        return validation;
    if (controller->phase_ != ZoneScriptStringOwnershipPhase::CommitReady)
        return ZoneScriptStringOwnershipStatus::InvalidPhase;

    const lifecycle::ZoneLoadContextStatus lifecycleStatus =
        lifecycle::TryCommitZoneLoadContext(
            controller->lifecycle_, controller->key_);
    if (lifecycleStatus != lifecycle::ZoneLoadContextStatus::Success)
        return MapLifecycleStatus(lifecycleStatus);

    // No status-bearing or fallible operation may occur between Live and this
    // unconditional finalizer.
    journal::FinalizeScriptStringJournalCommit(*controller->journal_);
    controller->detachJournalBacking();
    controller->phase_ = ZoneScriptStringOwnershipPhase::Admitting;
    admission.admitLive(admission.context);

    const std::uint32_t retainedSerial = controller->transactionSerial_;
    controller->rollbackContext_ = nullptr;
    controller->ensureUnreachable_ = nullptr;
    controller->performCleanup_ = nullptr;
    controller->resumePhase_ = ZoneScriptStringOwnershipPhase::Empty;
    controller->transactionSerial_ = 0;
    controller->phase_ = ZoneScriptStringOwnershipPhase::Live;
    const transaction::ScriptStringTransactionStatus finishStatus =
        transaction::FinishScriptStringTransaction(
            &controller->transaction_);
    if (finishStatus != transaction::ScriptStringTransactionStatus::Success)
    {
        controller->transactionSerial_ = retainedSerial;
        controller->poison();
        return ZoneScriptStringOwnershipStatus::UnsafeFailure;
    }
    return ZoneScriptStringOwnershipStatus::Success;
}

ZoneScriptStringOwnershipStatus TryBeginZoneScriptStringRollback(
    ZoneScriptStringOwnershipController *const controller,
    const ZoneScriptStringRollbackCallbacks &callbacks) noexcept
{
    if (!controller || !callbacks.ensureUnreachable
        || !callbacks.performCleanup)
    {
        return ZoneScriptStringOwnershipStatus::InvalidArgument;
    }

    const ZoneScriptStringOwnershipStatus validation =
        controller->validateOwned();
    if (validation != ZoneScriptStringOwnershipStatus::Success)
        return validation;

    // A completed callback retry remains in Unpublishing. The distinct
    // UnpublishingCallback phase makes same-thread callback reentry Busy.
    if (controller->phase_ == ZoneScriptStringOwnershipPhase::Unpublishing)
    {
        if (controller->rollbackContext_ != callbacks.context
            || controller->ensureUnreachable_
                != callbacks.ensureUnreachable
            || controller->performCleanup_ != callbacks.performCleanup)
        {
            return ZoneScriptStringOwnershipStatus::InvalidState;
        }
        if (!controller->bindingMatchesCurrentPhase())
            return ZoneScriptStringOwnershipStatus::InvalidState;
    }
    else
    {
        if (controller->phase_ == ZoneScriptStringOwnershipPhase::RollingBack
            || controller->phase_
                == ZoneScriptStringOwnershipPhase::OwnershipRolledBack)
        {
            return controller->rollbackContext_ == callbacks.context
                    && controller->ensureUnreachable_
                        == callbacks.ensureUnreachable
                    && controller->performCleanup_
                        == callbacks.performCleanup
                ? ZoneScriptStringOwnershipStatus::Success
                : ZoneScriptStringOwnershipStatus::InvalidState;
        }
        if (!IsRollbackSourcePhase(controller->phase_))
            return ZoneScriptStringOwnershipStatus::InvalidPhase;

        controller->rollbackContext_ = callbacks.context;
        controller->ensureUnreachable_ = callbacks.ensureUnreachable;
        controller->performCleanup_ = callbacks.performCleanup;
        controller->resumePhase_ = controller->phase_;
        controller->phase_ = ZoneScriptStringOwnershipPhase::Unpublishing;
    }

    controller->phase_ =
        ZoneScriptStringOwnershipPhase::UnpublishingCallback;
    switch (controller->ensureUnreachable_(controller->rollbackContext_))
    {
    case ZoneScriptStringUnpublishStatus::Retry:
        controller->phase_ = ZoneScriptStringOwnershipPhase::Unpublishing;
        return ZoneScriptStringOwnershipStatus::Retry;
    case ZoneScriptStringUnpublishStatus::UnsafeFailure:
        controller->poison();
        return ZoneScriptStringOwnershipStatus::UnsafeFailure;
    case ZoneScriptStringUnpublishStatus::Success:
        controller->phase_ = ZoneScriptStringOwnershipPhase::Unpublishing;
        break;
    default:
        controller->poison();
        return ZoneScriptStringOwnershipStatus::UnsafeFailure;
    }

    const lifecycle::ZoneLoadContextStatus lifecycleStatus =
        lifecycle::TryBeginZoneLoadContextAbandonment(
            controller->lifecycle_, controller->key_);
    if (lifecycleStatus != lifecycle::ZoneLoadContextStatus::Success)
    {
        if (lifecycleStatus == lifecycle::ZoneLoadContextStatus::UnsafeFailure)
            controller->poison();
        return MapLifecycleStatus(lifecycleStatus);
    }

    const journal::ScriptStringJournalStatus journalStatus =
        journal::TryBeginScriptStringRollback(
            controller->journal_, controller->key_);
    if (journalStatus != journal::ScriptStringJournalStatus::Success)
    {
        controller->poison();
        return journalStatus == journal::ScriptStringJournalStatus::UnsafeFailure
            ? ZoneScriptStringOwnershipStatus::UnsafeFailure
            : ZoneScriptStringOwnershipStatus::InvalidState;
    }

    controller->resumePhase_ = ZoneScriptStringOwnershipPhase::Empty;
    if (controller->journal_->phase()
        == journal::ScriptStringJournalPhase::RolledBack)
    {
        controller->detachJournalBacking();
        controller->phase_ =
            ZoneScriptStringOwnershipPhase::OwnershipRolledBack;
    }
    else
    {
        controller->phase_ = ZoneScriptStringOwnershipPhase::RollingBack;
    }
    return ZoneScriptStringOwnershipStatus::Success;
}

ZoneScriptStringOwnershipStatus TryRollbackNextZoneScriptString(
    ZoneScriptStringOwnershipController *const controller) noexcept
{
    if (!controller)
        return ZoneScriptStringOwnershipStatus::InvalidArgument;
    const ZoneScriptStringOwnershipStatus validation =
        controller->validateOwned();
    if (validation != ZoneScriptStringOwnershipStatus::Success)
        return validation;
    if (controller->phase_
        == ZoneScriptStringOwnershipPhase::OwnershipRolledBack)
    {
        return ZoneScriptStringOwnershipStatus::Success;
    }
    if (controller->phase_ != ZoneScriptStringOwnershipPhase::RollingBack)
        return ZoneScriptStringOwnershipStatus::InvalidPhase;

    const journal::ScriptStringJournalStatus status =
        adapter::TryRollbackNextScriptStringOwnership(
            controller->journal_,
            controller->key_,
            controller->transaction_);
    if (status == journal::ScriptStringJournalStatus::Success
        && controller->journal_->phase()
            == journal::ScriptStringJournalPhase::RolledBack)
    {
        controller->detachJournalBacking();
        controller->phase_ =
            ZoneScriptStringOwnershipPhase::OwnershipRolledBack;
    }
    else if (status == journal::ScriptStringJournalStatus::UnsafeFailure)
    {
        controller->poison();
    }
    return MapJournalStatus(status);
}

ZoneScriptStringOwnershipStatus TryFinishZoneScriptStringAbandonment(
    ZoneScriptStringOwnershipController *const controller) noexcept
{
    if (!controller)
        return ZoneScriptStringOwnershipStatus::InvalidArgument;
    const ZoneScriptStringOwnershipStatus validation =
        controller->validateOwned();
    if (validation == ZoneScriptStringOwnershipStatus::InvalidPhase
        && controller->phase_ == ZoneScriptStringOwnershipPhase::Abandoned)
    {
        return controller->validateAbandonedReceipt();
    }
    if (validation != ZoneScriptStringOwnershipStatus::Success)
        return validation;
    if (controller->phase_
        != ZoneScriptStringOwnershipPhase::OwnershipRolledBack)
    {
        return ZoneScriptStringOwnershipStatus::InvalidPhase;
    }
    if (!controller->ensureUnreachable_ || !controller->performCleanup_)
    {
        controller->poison();
        return ZoneScriptStringOwnershipStatus::UnsafeFailure;
    }

    controller->phase_ = ZoneScriptStringOwnershipPhase::Cleaning;
    const lifecycle::ZoneLoadCleanupCallbacks cleanupCallbacks{
        controller,
        ZoneScriptStringOwnershipController::PerformBoundCleanup};
    const lifecycle::ZoneLoadContextStatus cleanupStatus =
        lifecycle::TryFinishZoneLoadContextAbandonment(
            controller->lifecycle_,
            controller->key_,
            cleanupCallbacks);
    if (cleanupStatus == lifecycle::ZoneLoadContextStatus::Retry)
    {
        controller->phase_ =
            ZoneScriptStringOwnershipPhase::OwnershipRolledBack;
        return ZoneScriptStringOwnershipStatus::Retry;
    }
    if (cleanupStatus != lifecycle::ZoneLoadContextStatus::Success)
    {
        controller->poison();
        return cleanupStatus == lifecycle::ZoneLoadContextStatus::UnsafeFailure
            ? ZoneScriptStringOwnershipStatus::UnsafeFailure
            : MapLifecycleStatus(cleanupStatus);
    }

    const std::uint32_t retainedSerial = controller->transactionSerial_;
    controller->rollbackContext_ = nullptr;
    controller->ensureUnreachable_ = nullptr;
    controller->performCleanup_ = nullptr;
    controller->resumePhase_ = ZoneScriptStringOwnershipPhase::Empty;
    controller->transactionSerial_ = 0;
    controller->phase_ = ZoneScriptStringOwnershipPhase::Abandoned;
    const transaction::ScriptStringTransactionStatus finishStatus =
        transaction::FinishScriptStringTransaction(
            &controller->transaction_);
    if (finishStatus != transaction::ScriptStringTransactionStatus::Success)
    {
        controller->transactionSerial_ = retainedSerial;
        controller->poison();
        return ZoneScriptStringOwnershipStatus::UnsafeFailure;
    }
    return ZoneScriptStringOwnershipStatus::Success;
}

ZoneScriptStringOwnershipStatus TryResetAbandonedZoneScriptStringOwnership(
    ZoneScriptStringOwnershipController *const controller) noexcept
{
    if (!controller)
        return ZoneScriptStringOwnershipStatus::InvalidArgument;
    const ZoneScriptStringOwnershipStatus validation =
        controller->validateAbandonedReceipt();
    if (validation != ZoneScriptStringOwnershipStatus::Success)
        return validation;

    controller->reset();
    return ZoneScriptStringOwnershipStatus::Success;
}

} // namespace db::zone_script_string_ownership
