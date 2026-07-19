#include <database/db_script_string_journal.h>

#include <cstdint>
#include <limits>

namespace db::script_string_journal
{
namespace
{
constexpr std::uint8_t kInitializedFlag = UINT8_C(1) << 0;
constexpr std::uint8_t kCallbackActiveFlag = UINT8_C(1) << 1;
constexpr std::uint8_t kPoisonedFlag = UINT8_C(1) << 2;
constexpr std::uint8_t kKnownFlags =
    kInitializedFlag | kCallbackActiveFlag | kPoisonedFlag;

[[nodiscard]] constexpr bool IsValidKey(
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    return static_cast<bool>(key);
}

[[nodiscard]] constexpr bool IsKnownEntryState(
    const ScriptStringJournalEntryState state) noexcept
{
    switch (state)
    {
    case ScriptStringJournalEntryState::OrdinaryStaged:
    case ScriptStringJournalEntryState::DatabaseUserClaimed:
    case ScriptStringJournalEntryState::DuplicateReleased:
    case ScriptStringJournalEntryState::Released:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool IsTransferredState(
    const ScriptStringJournalEntryState state) noexcept
{
    return state
            == ScriptStringJournalEntryState::DatabaseUserClaimed
        || state
            == ScriptStringJournalEntryState::DuplicateReleased;
}

[[nodiscard]] constexpr bool IsKnownPhase(
    const ScriptStringJournalPhase phase) noexcept
{
    switch (phase)
    {
    case ScriptStringJournalPhase::Staging:
    case ScriptStringJournalPhase::Sealed:
    case ScriptStringJournalPhase::Transferring:
    case ScriptStringJournalPhase::Transferred:
    case ScriptStringJournalPhase::CommitReady:
    case ScriptStringJournalPhase::Committed:
    case ScriptStringJournalPhase::RollingBack:
    case ScriptStringJournalPhase::RolledBack:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool IsTerminalPhase(
    const ScriptStringJournalPhase phase) noexcept
{
    return phase == ScriptStringJournalPhase::Committed
        || phase == ScriptStringJournalPhase::RolledBack;
}

[[nodiscard]] constexpr bool IsKnownAcquireStatus(
    const ScriptStringAcquireCallbackStatus status) noexcept
{
    switch (status)
    {
    case ScriptStringAcquireCallbackStatus::Acquired:
    case ScriptStringAcquireCallbackStatus::RetryNoChange:
    case ScriptStringAcquireCallbackStatus::RejectedNoChange:
    case ScriptStringAcquireCallbackStatus::UnsafeFailure:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool IsKnownTransferStatus(
    const ScriptStringTransferCallbackStatus status) noexcept
{
    switch (status)
    {
    case ScriptStringTransferCallbackStatus::DatabaseUserClaimed:
    case ScriptStringTransferCallbackStatus::DuplicateReleased:
    case ScriptStringTransferCallbackStatus::RetryNoChange:
    case ScriptStringTransferCallbackStatus::UnsafeFailure:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool IsKnownReleaseStatus(
    const ScriptStringReleaseCallbackStatus status) noexcept
{
    switch (status)
    {
    case ScriptStringReleaseCallbackStatus::Success:
    case ScriptStringReleaseCallbackStatus::RetryNoChange:
    case ScriptStringReleaseCallbackStatus::UnsafeFailure:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool EntryIsCanonical(
    const ScriptStringJournalEntry &entry) noexcept
{
    return entry.stringId != 0
        && IsKnownEntryState(entry.state)
        && entry.reserved[0] == 0
        && entry.reserved[1] == 0
        && entry.reserved[2] == 0;
}

[[nodiscard]] bool EntryMatchesPhase(
    const ScriptStringJournalEntry &entry,
    const ScriptStringJournalPhase phase,
    const std::uint32_t index,
    const std::uint32_t transferCursor,
    const std::uint32_t rollbackCursor) noexcept
{
    if (!EntryIsCanonical(entry))
        return false;

    switch (phase)
    {
    case ScriptStringJournalPhase::Staging:
    case ScriptStringJournalPhase::Sealed:
        return entry.state
            == ScriptStringJournalEntryState::OrdinaryStaged;
    case ScriptStringJournalPhase::Transferring:
    case ScriptStringJournalPhase::Transferred:
    case ScriptStringJournalPhase::CommitReady:
        return index < transferCursor
            ? IsTransferredState(entry.state)
            : entry.state
                == ScriptStringJournalEntryState::OrdinaryStaged;
    case ScriptStringJournalPhase::RollingBack:
        if (index >= rollbackCursor)
        {
            return entry.state
                == ScriptStringJournalEntryState::Released;
        }
        return index < transferCursor
            ? IsTransferredState(entry.state)
            : entry.state
                == ScriptStringJournalEntryState::OrdinaryStaged;
    case ScriptStringJournalPhase::Committed:
    case ScriptStringJournalPhase::RolledBack:
    default:
        return false;
    }
}

[[nodiscard]] bool EntriesMatchPhase(
    const ScriptStringJournalEntry *const storage,
    const std::uint32_t entryCount,
    const ScriptStringJournalPhase phase,
    const std::uint32_t transferCursor,
    const std::uint32_t rollbackCursor) noexcept
{
    for (std::uint32_t index = 0; index < entryCount; ++index)
    {
        if (!EntryMatchesPhase(
                storage[index],
                phase,
                index,
                transferCursor,
                rollbackCursor))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsUsedStorageSpanValid(
    const ScriptStringJournal *const journal,
    ScriptStringJournalEntry *const storage,
    const std::uint32_t expectedCount) noexcept
{
    if (expectedCount == 0)
        return true;
    if (!journal || !storage)
        return false;

    const std::uintptr_t storageBegin =
        reinterpret_cast<std::uintptr_t>(storage);
    if ((storageBegin % alignof(ScriptStringJournalEntry)) != 0)
        return false;
    const std::uintptr_t storageBytes =
        static_cast<std::uintptr_t>(expectedCount)
        * static_cast<std::uintptr_t>(
            sizeof(ScriptStringJournalEntry));
    if (storageBegin
        > (std::numeric_limits<std::uintptr_t>::max)()
            - storageBytes)
    {
        return false;
    }
    const std::uintptr_t storageEnd =
        storageBegin + storageBytes;

    const std::uintptr_t journalBegin =
        reinterpret_cast<std::uintptr_t>(journal);
    if (journalBegin
        > (std::numeric_limits<std::uintptr_t>::max)()
            - sizeof(ScriptStringJournal))
    {
        return false;
    }
    const std::uintptr_t journalEnd =
        journalBegin + sizeof(ScriptStringJournal);
    return storageBegin >= journalEnd
        || journalBegin >= storageEnd;
}

} // namespace

bool ScriptStringJournal::isCanonical() const noexcept
{
    if ((flags_ & ~kKnownFlags) != 0
        || !IsKnownPhase(phase_))
    {
        return false;
    }

    const bool isInitialized =
        (flags_ & kInitializedFlag) != 0;
    const bool isCallbackActive =
        (flags_ & kCallbackActiveFlag) != 0;
    const bool isPoisoned =
        (flags_ & kPoisonedFlag) != 0;
    if (!isInitialized)
    {
        return key_ == zone_load::ZoneLoadContextKey{}
            && storage_ == nullptr
            && capacity_ == 0
            && expectedCount_ == 0
            && entryCount_ == 0
            && transferCursor_ == 0
            && rollbackCursor_ == 0
            && phase_ == ScriptStringJournalPhase::Staging
            && !isCallbackActive
            && !isPoisoned;
    }

    if (!IsValidKey(key_)
        || capacity_ > kMaxScriptStringJournalEntries
        || expectedCount_ > kMaxScriptStringJournalEntries
        || (isCallbackActive && isPoisoned))
    {
        return false;
    }

    if (IsTerminalPhase(phase_))
    {
        if (storage_ != nullptr
            || capacity_ != 0
            || isCallbackActive
            || isPoisoned
            || entryCount_ > expectedCount_
            || transferCursor_ > entryCount_
            || rollbackCursor_ != 0)
        {
            return false;
        }
        if (phase_ == ScriptStringJournalPhase::Committed)
        {
            return entryCount_ == expectedCount_
                && transferCursor_ == entryCount_;
        }
        return true;
    }

    if (expectedCount_ > capacity_
        || entryCount_ > expectedCount_
        || transferCursor_ > entryCount_
        || rollbackCursor_ > entryCount_
        || (expectedCount_ != 0 && storage_ == nullptr)
        || !IsUsedStorageSpanValid(
            this, storage_, expectedCount_))
    {
        return false;
    }

    switch (phase_)
    {
    case ScriptStringJournalPhase::Staging:
        if (transferCursor_ != 0 || rollbackCursor_ != 0)
            return false;
        break;
    case ScriptStringJournalPhase::Sealed:
        if (entryCount_ != expectedCount_
            || transferCursor_ != 0
            || rollbackCursor_ != 0)
        {
            return false;
        }
        break;
    case ScriptStringJournalPhase::Transferring:
        if (entryCount_ != expectedCount_
            || rollbackCursor_ != 0)
        {
            return false;
        }
        break;
    case ScriptStringJournalPhase::Transferred:
    case ScriptStringJournalPhase::CommitReady:
        if (entryCount_ != expectedCount_
            || transferCursor_ != entryCount_
            || rollbackCursor_ != 0)
        {
            return false;
        }
        break;
    case ScriptStringJournalPhase::RollingBack:
        if (rollbackCursor_ == 0)
            return false;
        break;
    case ScriptStringJournalPhase::Committed:
    case ScriptStringJournalPhase::RolledBack:
    default:
        return false;
    }

    return true;
}

ScriptStringJournalStatus ScriptStringJournal::validate() const noexcept
{
    if (!isCanonical()
        || (flags_ & kInitializedFlag) == 0)
    {
        return ScriptStringJournalStatus::InvalidState;
    }
    if ((flags_ & kCallbackActiveFlag) != 0)
        return ScriptStringJournalStatus::Busy;
    if ((flags_ & kPoisonedFlag) != 0)
        return ScriptStringJournalStatus::UnsafeFailure;
    return ScriptStringJournalStatus::Success;
}

ScriptStringJournalStatus ScriptStringJournal::validateKey(
    const zone_load::ZoneLoadContextKey &key) const noexcept
{
    if (!IsValidKey(key))
        return ScriptStringJournalStatus::InvalidKey;
    if (!(key == key_))
        return ScriptStringJournalStatus::StaleKey;
    return ScriptStringJournalStatus::Success;
}

void ScriptStringJournal::poison() noexcept
{
    flags_ = static_cast<std::uint8_t>(
        (flags_ & ~kCallbackActiveFlag) | kPoisonedFlag);
}

void ScriptStringJournal::detachBacking() noexcept
{
    storage_ = nullptr;
    capacity_ = 0;
}

void ScriptStringJournal::finishRollback() noexcept
{
    rollbackCursor_ = 0;
    phase_ = ScriptStringJournalPhase::RolledBack;
    flags_ = kInitializedFlag;
    detachBacking();
}

bool ScriptStringJournal::initialized() const noexcept
{
    return isCanonical()
        && (flags_ & kInitializedFlag) != 0;
}

const zone_load::ZoneLoadContextKey &
ScriptStringJournal::key() const noexcept
{
    return key_;
}

ScriptStringJournalPhase ScriptStringJournal::phase() const noexcept
{
    return phase_;
}

const ScriptStringJournalEntry *
ScriptStringJournal::storage() const noexcept
{
    return storage_;
}

std::uint32_t ScriptStringJournal::capacity() const noexcept
{
    return capacity_;
}

std::uint32_t ScriptStringJournal::expectedCount() const noexcept
{
    return expectedCount_;
}

std::uint32_t ScriptStringJournal::entryCount() const noexcept
{
    return entryCount_;
}

std::uint32_t ScriptStringJournal::transferCursor() const noexcept
{
    return transferCursor_;
}

std::uint32_t ScriptStringJournal::rollbackCursor() const noexcept
{
    return rollbackCursor_;
}

bool ScriptStringJournal::callbackActive() const noexcept
{
    return (flags_ & kCallbackActiveFlag) != 0;
}

bool ScriptStringJournal::poisoned() const noexcept
{
    return (flags_ & kPoisonedFlag) != 0;
}

bool ScriptStringJournal::readyForDestruction() const noexcept
{
    if (!isCanonical() || callbackActive() || poisoned()
        || storage_ != nullptr || capacity_ != 0)
    {
        return false;
    }
    if ((flags_ & kInitializedFlag) == 0)
        return true;
    return phase_ == ScriptStringJournalPhase::Committed
        || phase_ == ScriptStringJournalPhase::RolledBack;
}

ScriptStringJournalStatus TryInitializeScriptStringJournal(
    ScriptStringJournal *const journal,
    const zone_load::ZoneLoadContextKey &key,
    ScriptStringJournalEntry *const storage,
    const std::uint32_t capacity,
    const std::uint32_t expectedCount) noexcept
{
    if (!journal)
        return ScriptStringJournalStatus::InvalidArgument;
    if (!IsValidKey(key))
        return ScriptStringJournalStatus::InvalidKey;
    if (capacity > kMaxScriptStringJournalEntries
        || expectedCount > kMaxScriptStringJournalEntries)
    {
        return ScriptStringJournalStatus::CapacityExceeded;
    }
    if (expectedCount > capacity)
        return ScriptStringJournalStatus::CapacityExceeded;
    if (expectedCount != 0 && !storage)
        return ScriptStringJournalStatus::InvalidArgument;
    if (!IsUsedStorageSpanValid(journal, storage, expectedCount))
        return ScriptStringJournalStatus::InvalidArgument;
    if (!journal->isCanonical())
        return ScriptStringJournalStatus::InvalidState;
    if ((journal->flags_ & kInitializedFlag) != 0)
        return ScriptStringJournalStatus::InvalidPhase;

    journal->key_ = key;
    journal->storage_ = storage;
    journal->capacity_ = capacity;
    journal->expectedCount_ = expectedCount;
    journal->entryCount_ = 0;
    journal->transferCursor_ = 0;
    journal->rollbackCursor_ = 0;
    journal->phase_ = ScriptStringJournalPhase::Staging;
    journal->flags_ = kInitializedFlag;
    return ScriptStringJournalStatus::Success;
}

ScriptStringJournalStatus TryStageScriptString(
    ScriptStringJournal *const journal,
    const zone_load::ZoneLoadContextKey &key,
    const ScriptStringJournalCallbacks &callbacks) noexcept
{
    if (!journal)
        return ScriptStringJournalStatus::InvalidArgument;
    ScriptStringJournalStatus status = journal->validate();
    if (status == ScriptStringJournalStatus::Success)
        status = journal->validateKey(key);
    if (status != ScriptStringJournalStatus::Success)
        return status;
    if (journal->phase_ != ScriptStringJournalPhase::Staging)
        return ScriptStringJournalStatus::InvalidPhase;
    if (journal->entryCount_ >= journal->expectedCount_)
        return ScriptStringJournalStatus::CapacityExceeded;
    if (!callbacks.acquireOrdinary)
        return ScriptStringJournalStatus::InvalidArgument;

    std::uint32_t stringId = 0;
    journal->flags_ = static_cast<std::uint8_t>(
        journal->flags_ | kCallbackActiveFlag);
    const ScriptStringAcquireCallbackStatus callbackStatus =
        callbacks.acquireOrdinary(callbacks.context, &stringId);
    journal->flags_ = static_cast<std::uint8_t>(
        journal->flags_ & ~kCallbackActiveFlag);

    if (!IsKnownAcquireStatus(callbackStatus)
        || callbackStatus
            == ScriptStringAcquireCallbackStatus::UnsafeFailure)
    {
        journal->poison();
        return ScriptStringJournalStatus::UnsafeFailure;
    }
    if (callbackStatus
        == ScriptStringAcquireCallbackStatus::RetryNoChange)
    {
        return ScriptStringJournalStatus::RetryNoChange;
    }
    if (callbackStatus
        == ScriptStringAcquireCallbackStatus::RejectedNoChange)
    {
        return ScriptStringJournalStatus::Rejected;
    }
    if (stringId == 0)
    {
        journal->poison();
        return ScriptStringJournalStatus::UnsafeFailure;
    }

    journal->storage_[journal->entryCount_] = {
        stringId,
        ScriptStringJournalEntryState::OrdinaryStaged,
        {0, 0, 0}};
    ++journal->entryCount_;
    return ScriptStringJournalStatus::Success;
}

ScriptStringJournalStatus TrySealScriptStringJournal(
    ScriptStringJournal *const journal,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    if (!journal)
        return ScriptStringJournalStatus::InvalidArgument;
    ScriptStringJournalStatus status = journal->validate();
    if (status == ScriptStringJournalStatus::Success)
        status = journal->validateKey(key);
    if (status != ScriptStringJournalStatus::Success)
        return status;
    if (journal->phase_ == ScriptStringJournalPhase::Sealed)
        return ScriptStringJournalStatus::Success;
    if (journal->phase_ != ScriptStringJournalPhase::Staging)
        return ScriptStringJournalStatus::InvalidPhase;
    if (journal->entryCount_ != journal->expectedCount_)
        return ScriptStringJournalStatus::CountMismatch;
    if (!EntriesMatchPhase(
            journal->storage_,
            journal->entryCount_,
            journal->phase_,
            journal->transferCursor_,
            journal->rollbackCursor_))
    {
        return ScriptStringJournalStatus::InvalidState;
    }

    journal->phase_ = ScriptStringJournalPhase::Sealed;
    return ScriptStringJournalStatus::Success;
}

ScriptStringJournalStatus TryBeginScriptStringTransfer(
    ScriptStringJournal *const journal,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    if (!journal)
        return ScriptStringJournalStatus::InvalidArgument;
    ScriptStringJournalStatus status = journal->validate();
    if (status == ScriptStringJournalStatus::Success)
        status = journal->validateKey(key);
    if (status != ScriptStringJournalStatus::Success)
        return status;
    if (journal->phase_ == ScriptStringJournalPhase::Transferring
        || journal->phase_
            == ScriptStringJournalPhase::Transferred)
    {
        return ScriptStringJournalStatus::Success;
    }
    if (journal->phase_ != ScriptStringJournalPhase::Sealed)
        return ScriptStringJournalStatus::InvalidPhase;

    journal->phase_ = ScriptStringJournalPhase::Transferring;
    return ScriptStringJournalStatus::Success;
}

ScriptStringJournalStatus TryTransferNextScriptString(
    ScriptStringJournal *const journal,
    const zone_load::ZoneLoadContextKey &key,
    const ScriptStringJournalCallbacks &callbacks) noexcept
{
    if (!journal)
        return ScriptStringJournalStatus::InvalidArgument;
    ScriptStringJournalStatus status = journal->validate();
    if (status == ScriptStringJournalStatus::Success)
        status = journal->validateKey(key);
    if (status != ScriptStringJournalStatus::Success)
        return status;
    if (journal->phase_
        == ScriptStringJournalPhase::Transferred)
    {
        return ScriptStringJournalStatus::Success;
    }
    if (journal->phase_
        != ScriptStringJournalPhase::Transferring)
    {
        return ScriptStringJournalStatus::InvalidPhase;
    }
    if (journal->transferCursor_ == journal->entryCount_)
    {
        journal->phase_ =
            ScriptStringJournalPhase::Transferred;
        return ScriptStringJournalStatus::Success;
    }
    ScriptStringJournalEntry &entry =
        journal->storage_[journal->transferCursor_];
    if (!EntryMatchesPhase(
            entry,
            journal->phase_,
            journal->transferCursor_,
            journal->transferCursor_,
            journal->rollbackCursor_))
    {
        return ScriptStringJournalStatus::InvalidState;
    }
    if (!callbacks.transferToDatabaseUser)
        return ScriptStringJournalStatus::InvalidArgument;
    journal->flags_ = static_cast<std::uint8_t>(
        journal->flags_ | kCallbackActiveFlag);
    const ScriptStringTransferCallbackStatus callbackStatus =
        callbacks.transferToDatabaseUser(
            callbacks.context, entry.stringId);
    journal->flags_ = static_cast<std::uint8_t>(
        journal->flags_ & ~kCallbackActiveFlag);

    if (!IsKnownTransferStatus(callbackStatus)
        || callbackStatus
            == ScriptStringTransferCallbackStatus::UnsafeFailure)
    {
        journal->poison();
        return ScriptStringJournalStatus::UnsafeFailure;
    }
    if (callbackStatus
        == ScriptStringTransferCallbackStatus::RetryNoChange)
    {
        return ScriptStringJournalStatus::RetryNoChange;
    }

    entry.state = callbackStatus
            == ScriptStringTransferCallbackStatus::
                DatabaseUserClaimed
        ? ScriptStringJournalEntryState::DatabaseUserClaimed
        : ScriptStringJournalEntryState::DuplicateReleased;
    ++journal->transferCursor_;
    if (journal->transferCursor_ == journal->entryCount_)
    {
        journal->phase_ =
            ScriptStringJournalPhase::Transferred;
    }
    return ScriptStringJournalStatus::Success;
}

ScriptStringJournalStatus TryPrepareScriptStringJournalCommit(
    ScriptStringJournal *const journal,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    if (!journal)
        return ScriptStringJournalStatus::InvalidArgument;
    ScriptStringJournalStatus status = journal->validate();
    if (status == ScriptStringJournalStatus::Success)
        status = journal->validateKey(key);
    if (status != ScriptStringJournalStatus::Success)
        return status;
    if (journal->phase_ == ScriptStringJournalPhase::CommitReady
        || journal->phase_ == ScriptStringJournalPhase::Committed)
    {
        return ScriptStringJournalStatus::Success;
    }
    if (journal->phase_
        != ScriptStringJournalPhase::Transferred)
    {
        return ScriptStringJournalStatus::InvalidPhase;
    }
    if (!EntriesMatchPhase(
            journal->storage_,
            journal->entryCount_,
            journal->phase_,
            journal->transferCursor_,
            journal->rollbackCursor_))
    {
        return ScriptStringJournalStatus::InvalidState;
    }

    journal->phase_ = ScriptStringJournalPhase::CommitReady;
    return ScriptStringJournalStatus::Success;
}

void FinalizeScriptStringJournalCommit(
    ScriptStringJournal &journal) noexcept
{
    journal.phase_ = ScriptStringJournalPhase::Committed;
    journal.flags_ = kInitializedFlag;
    journal.storage_ = nullptr;
    journal.capacity_ = 0;
}

ScriptStringJournalStatus TryBeginScriptStringRollback(
    ScriptStringJournal *const journal,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    if (!journal)
        return ScriptStringJournalStatus::InvalidArgument;
    ScriptStringJournalStatus status = journal->validate();
    if (status == ScriptStringJournalStatus::Success)
        status = journal->validateKey(key);
    if (status != ScriptStringJournalStatus::Success)
        return status;
    if (journal->phase_ == ScriptStringJournalPhase::RolledBack
        || journal->phase_
            == ScriptStringJournalPhase::RollingBack)
    {
        return ScriptStringJournalStatus::Success;
    }
    switch (journal->phase_)
    {
    case ScriptStringJournalPhase::Staging:
    case ScriptStringJournalPhase::Sealed:
    case ScriptStringJournalPhase::Transferring:
    case ScriptStringJournalPhase::Transferred:
    case ScriptStringJournalPhase::CommitReady:
        break;
    case ScriptStringJournalPhase::Committed:
    case ScriptStringJournalPhase::RollingBack:
    case ScriptStringJournalPhase::RolledBack:
    default:
        return ScriptStringJournalStatus::InvalidPhase;
    }
    if (!EntriesMatchPhase(
            journal->storage_,
            journal->entryCount_,
            journal->phase_,
            journal->transferCursor_,
            journal->rollbackCursor_))
    {
        return ScriptStringJournalStatus::InvalidState;
    }

    journal->phase_ = ScriptStringJournalPhase::RollingBack;
    journal->rollbackCursor_ = journal->entryCount_;
    if (journal->rollbackCursor_ == 0)
        journal->finishRollback();
    return ScriptStringJournalStatus::Success;
}

ScriptStringJournalStatus TryRollbackNextScriptString(
    ScriptStringJournal *const journal,
    const zone_load::ZoneLoadContextKey &key,
    const ScriptStringJournalCallbacks &callbacks) noexcept
{
    if (!journal)
        return ScriptStringJournalStatus::InvalidArgument;
    ScriptStringJournalStatus status = journal->validate();
    if (status == ScriptStringJournalStatus::Success)
        status = journal->validateKey(key);
    if (status != ScriptStringJournalStatus::Success)
        return status;
    if (journal->phase_ == ScriptStringJournalPhase::RolledBack)
        return ScriptStringJournalStatus::Success;
    if (journal->phase_
        != ScriptStringJournalPhase::RollingBack)
    {
        return ScriptStringJournalStatus::InvalidPhase;
    }
    if (journal->rollbackCursor_ == 0)
    {
        journal->finishRollback();
        return ScriptStringJournalStatus::Success;
    }

    const std::uint32_t entryIndex =
        journal->rollbackCursor_ - 1;
    ScriptStringJournalEntry &entry =
        journal->storage_[entryIndex];
    if (!EntryMatchesPhase(
            entry,
            journal->phase_,
            entryIndex,
            journal->transferCursor_,
            journal->rollbackCursor_))
    {
        return ScriptStringJournalStatus::InvalidState;
    }
    if (entry.state
        == ScriptStringJournalEntryState::DuplicateReleased)
    {
        entry.state = ScriptStringJournalEntryState::Released;
        --journal->rollbackCursor_;
        if (journal->rollbackCursor_ == 0)
            journal->finishRollback();
        return ScriptStringJournalStatus::Success;
    }

    ScriptStringReleaseCallbackStatus callbackStatus =
        ScriptStringReleaseCallbackStatus::UnsafeFailure;
    if (entry.state
        == ScriptStringJournalEntryState::OrdinaryStaged)
    {
        if (!callbacks.removeOrdinary)
            return ScriptStringJournalStatus::InvalidArgument;
        journal->flags_ = static_cast<std::uint8_t>(
            journal->flags_ | kCallbackActiveFlag);
        callbackStatus = callbacks.removeOrdinary(
            callbacks.context, entry.stringId);
    }
    else if (entry.state
        == ScriptStringJournalEntryState::DatabaseUserClaimed)
    {
        if (!callbacks.removeDatabaseUser)
            return ScriptStringJournalStatus::InvalidArgument;
        journal->flags_ = static_cast<std::uint8_t>(
            journal->flags_ | kCallbackActiveFlag);
        callbackStatus = callbacks.removeDatabaseUser(
            callbacks.context, entry.stringId);
    }
    else
    {
        journal->poison();
        return ScriptStringJournalStatus::UnsafeFailure;
    }
    journal->flags_ = static_cast<std::uint8_t>(
        journal->flags_ & ~kCallbackActiveFlag);

    if (!IsKnownReleaseStatus(callbackStatus)
        || callbackStatus
            == ScriptStringReleaseCallbackStatus::UnsafeFailure)
    {
        journal->poison();
        return ScriptStringJournalStatus::UnsafeFailure;
    }
    if (callbackStatus
        == ScriptStringReleaseCallbackStatus::RetryNoChange)
    {
        return ScriptStringJournalStatus::RetryNoChange;
    }

    entry.state = ScriptStringJournalEntryState::Released;
    --journal->rollbackCursor_;
    if (journal->rollbackCursor_ == 0)
        journal->finishRollback();
    return ScriptStringJournalStatus::Success;
}

bool ScriptStringJournalKeyMatches(
    const ScriptStringJournal *const journal,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    return journal && journal->isCanonical()
        && (journal->flags_ & kInitializedFlag) != 0
        && IsValidKey(key)
        && key == journal->key_;
}
} // namespace db::script_string_journal
