#include <database/db_script_string_journal.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{
namespace journal = db::script_string_journal;
namespace zone_load = db::zone_load;

using journal::ScriptStringAcquireCallbackStatus;
using journal::ScriptStringJournal;
using journal::ScriptStringJournalCallbacks;
using journal::ScriptStringJournalEntry;
using journal::ScriptStringJournalEntryState;
using journal::ScriptStringJournalPhase;
using journal::ScriptStringJournalStatus;
using journal::ScriptStringReleaseCallbackStatus;
using journal::ScriptStringTransferCallbackStatus;
using journal::TryBeginScriptStringRollback;
using journal::TryBeginScriptStringTransfer;
using journal::TryCommitScriptStringJournal;
using journal::TryInitializeScriptStringJournal;
using journal::TryRollbackNextScriptString;
using journal::TrySealScriptStringJournal;
using journal::TryStageScriptString;
using journal::TryTransferNextScriptString;
using zone_load::ZoneLoadContextKey;

int failures = 0;

void Check(
    const bool condition,
    const char *const expression,
    const int line)
{
    if (condition)
        return;
    std::fprintf(
        stderr, "line %d: check failed: %s\n", line, expression);
    ++failures;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

enum class EventKind : std::uint8_t
{
    Acquire,
    Transfer,
    RemoveOrdinary,
    RemoveDatabaseUser,
};

struct Event final
{
    EventKind kind = EventKind::Acquire;
    std::uint32_t stringId = 0;
};

enum class ReentryKind : std::uint8_t
{
    None,
    Stage,
    Transfer,
    Rollback,
};

struct CallbackRecorder final
{
    std::array<Event, 64> events{};
    std::size_t eventCount = 0;

    std::uint32_t acquireId = 1;
    ScriptStringAcquireCallbackStatus acquireStatus =
        ScriptStringAcquireCallbackStatus::Acquired;
    ScriptStringTransferCallbackStatus transferStatus =
        ScriptStringTransferCallbackStatus::DatabaseUserClaimed;
    ScriptStringReleaseCallbackStatus ordinaryReleaseStatus =
        ScriptStringReleaseCallbackStatus::Success;
    ScriptStringReleaseCallbackStatus databaseReleaseStatus =
        ScriptStringReleaseCallbackStatus::Success;

    ScriptStringJournal *reentrantJournal = nullptr;
    ZoneLoadContextKey reentrantKey{};
    ReentryKind reentryKind = ReentryKind::None;
    ScriptStringJournalStatus reentryStatus =
        ScriptStringJournalStatus::InvalidState;
    bool observedCallbackActive = false;
};

bool RecordEvent(
    CallbackRecorder *const recorder,
    const EventKind kind,
    const std::uint32_t stringId) noexcept
{
    if (!recorder
        || recorder->eventCount >= recorder->events.size())
    {
        return false;
    }
    recorder->events[recorder->eventCount++] = {kind, stringId};
    return true;
}

void AttemptReentry(CallbackRecorder *const recorder) noexcept
{
    if (!recorder
        || recorder->reentryKind == ReentryKind::None)
    {
        return;
    }

    const ReentryKind reentryKind = recorder->reentryKind;
    recorder->reentryKind = ReentryKind::None;
    recorder->observedCallbackActive =
        recorder->reentrantJournal
        && recorder->reentrantJournal->callbackActive();
    switch (reentryKind)
    {
    case ReentryKind::Stage:
        recorder->reentryStatus = TryStageScriptString(
            recorder->reentrantJournal,
            recorder->reentrantKey,
            {});
        break;
    case ReentryKind::Transfer:
        recorder->reentryStatus = TryTransferNextScriptString(
            recorder->reentrantJournal,
            recorder->reentrantKey,
            {});
        break;
    case ReentryKind::Rollback:
        recorder->reentryStatus = TryRollbackNextScriptString(
            recorder->reentrantJournal,
            recorder->reentrantKey,
            {});
        break;
    case ReentryKind::None:
    default:
        break;
    }
}

ScriptStringAcquireCallbackStatus AcquireOrdinary(
    void *const context,
    std::uint32_t *const outStringId) noexcept
{
    CallbackRecorder *const recorder =
        static_cast<CallbackRecorder *>(context);
    if (!recorder
        || !RecordEvent(
            recorder, EventKind::Acquire, recorder->acquireId))
    {
        return ScriptStringAcquireCallbackStatus::UnsafeFailure;
    }
    AttemptReentry(recorder);
    if (outStringId)
        *outStringId = recorder->acquireId;
    return recorder->acquireStatus;
}

ScriptStringTransferCallbackStatus TransferToDatabaseUser(
    void *const context,
    const std::uint32_t stringId) noexcept
{
    CallbackRecorder *const recorder =
        static_cast<CallbackRecorder *>(context);
    if (!RecordEvent(recorder, EventKind::Transfer, stringId))
        return ScriptStringTransferCallbackStatus::UnsafeFailure;
    AttemptReentry(recorder);
    return recorder->transferStatus;
}

ScriptStringReleaseCallbackStatus RemoveOrdinary(
    void *const context,
    const std::uint32_t stringId) noexcept
{
    CallbackRecorder *const recorder =
        static_cast<CallbackRecorder *>(context);
    if (!RecordEvent(
            recorder, EventKind::RemoveOrdinary, stringId))
    {
        return ScriptStringReleaseCallbackStatus::UnsafeFailure;
    }
    AttemptReentry(recorder);
    return recorder->ordinaryReleaseStatus;
}

ScriptStringReleaseCallbackStatus RemoveDatabaseUser(
    void *const context,
    const std::uint32_t stringId) noexcept
{
    CallbackRecorder *const recorder =
        static_cast<CallbackRecorder *>(context);
    if (!RecordEvent(
            recorder, EventKind::RemoveDatabaseUser, stringId))
    {
        return ScriptStringReleaseCallbackStatus::UnsafeFailure;
    }
    AttemptReentry(recorder);
    return recorder->databaseReleaseStatus;
}

ScriptStringJournalCallbacks MakeCallbacks(
    CallbackRecorder *const recorder) noexcept
{
    return {
        recorder,
        AcquireOrdinary,
        TransferToDatabaseUser,
        RemoveOrdinary,
        RemoveDatabaseUser};
}

constexpr ZoneLoadContextKey MakeKey(
    const std::uint64_t generation = UINT64_C(17),
    const std::uint32_t slot = 3) noexcept
{
    return {generation, slot, 0};
}

void Initialize(
    ScriptStringJournal *const value,
    const ZoneLoadContextKey &key,
    ScriptStringJournalEntry *const storage,
    const std::size_t capacity,
    const std::size_t expectedCount)
{
    CHECK(capacity <= journal::kMaxScriptStringJournalEntries);
    CHECK(expectedCount <= journal::kMaxScriptStringJournalEntries);
    CHECK(
        TryInitializeScriptStringJournal(
            value,
            key,
            storage,
            static_cast<std::uint32_t>(capacity),
            static_cast<std::uint32_t>(expectedCount))
        == ScriptStringJournalStatus::Success);
}

void Stage(
    ScriptStringJournal *const value,
    const ZoneLoadContextKey &key,
    CallbackRecorder *const recorder,
    const std::uint32_t stringId)
{
    recorder->acquireId = stringId;
    recorder->acquireStatus =
        ScriptStringAcquireCallbackStatus::Acquired;
    CHECK(
        TryStageScriptString(value, key, MakeCallbacks(recorder))
        == ScriptStringJournalStatus::Success);
}

void SealAndBeginTransfer(
    ScriptStringJournal *const value,
    const ZoneLoadContextKey &key)
{
    CHECK(
        TrySealScriptStringJournal(value, key)
        == ScriptStringJournalStatus::Success);
    CHECK(
        TryBeginScriptStringTransfer(value, key)
        == ScriptStringJournalStatus::Success);
}

void TestLayoutNoexceptAndInitialization()
{
    static_assert(sizeof(ScriptStringJournalEntry) == 0x8);
    static_assert(alignof(ScriptStringJournalEntry) == 4);
    static_assert(
        std::is_standard_layout_v<ScriptStringJournalEntry>);
    static_assert(
        std::is_trivially_copyable_v<ScriptStringJournalEntry>);
    static_assert(
        std::is_trivially_destructible_v<ScriptStringJournal>);
    static_assert(
        !std::is_copy_constructible_v<ScriptStringJournal>);
    static_assert(
        !std::is_move_constructible_v<ScriptStringJournal>);
    static_assert(sizeof(ScriptStringJournalEntryState) == 1);
    static_assert(sizeof(ScriptStringJournalPhase) == 1);
    static_assert(
        sizeof(ScriptStringAcquireCallbackStatus) == 1);
    static_assert(
        sizeof(ScriptStringTransferCallbackStatus) == 1);
    static_assert(
        sizeof(ScriptStringReleaseCallbackStatus) == 1);
    static_assert(sizeof(ScriptStringJournalStatus) == 1);
    static_assert(noexcept(TryInitializeScriptStringJournal(
        nullptr,
        std::declval<const ZoneLoadContextKey &>(),
        nullptr,
        0,
        0)));
    static_assert(noexcept(TryStageScriptString(
        nullptr,
        std::declval<const ZoneLoadContextKey &>(),
        std::declval<const ScriptStringJournalCallbacks &>())));
    static_assert(noexcept(TrySealScriptStringJournal(
        nullptr, std::declval<const ZoneLoadContextKey &>())));
    static_assert(noexcept(TryBeginScriptStringTransfer(
        nullptr, std::declval<const ZoneLoadContextKey &>())));
    static_assert(noexcept(TryTransferNextScriptString(
        nullptr,
        std::declval<const ZoneLoadContextKey &>(),
        std::declval<const ScriptStringJournalCallbacks &>())));
    static_assert(noexcept(TryCommitScriptStringJournal(
        nullptr, std::declval<const ZoneLoadContextKey &>())));
    static_assert(noexcept(TryBeginScriptStringRollback(
        nullptr, std::declval<const ZoneLoadContextKey &>())));
    static_assert(noexcept(TryRollbackNextScriptString(
        nullptr,
        std::declval<const ZoneLoadContextKey &>(),
        std::declval<const ScriptStringJournalCallbacks &>())));
    static_assert(noexcept(journal::ScriptStringJournalKeyMatches(
        nullptr, std::declval<const ZoneLoadContextKey &>())));

    ScriptStringJournal value{};
    CHECK(!value.initialized());
    CHECK(!value.key());
    CHECK(value.phase() == ScriptStringJournalPhase::Staging);
    CHECK(value.storage() == nullptr);
    CHECK(value.capacity() == 0);
    CHECK(value.expectedCount() == 0);
    CHECK(value.entryCount() == 0);
    CHECK(value.transferCursor() == 0);
    CHECK(value.rollbackCursor() == 0);
    CHECK(!value.callbackActive());
    CHECK(!value.poisoned());

    std::array<ScriptStringJournalEntry, 2> storage{};
    const std::uint32_t storageCapacity =
        static_cast<std::uint32_t>(storage.size());
    const ZoneLoadContextKey key = MakeKey();
    CHECK(
        TryInitializeScriptStringJournal(
            nullptr, key, storage.data(), storageCapacity, 2)
        == ScriptStringJournalStatus::InvalidArgument);
    CHECK(
        TryInitializeScriptStringJournal(
            &value, {}, storage.data(), storageCapacity, 2)
        == ScriptStringJournalStatus::InvalidKey);
    ZoneLoadContextKey malformed = key;
    malformed.reserved = 1;
    CHECK(
        TryInitializeScriptStringJournal(
            &value,
            malformed,
            storage.data(),
            storageCapacity,
            2)
        == ScriptStringJournalStatus::InvalidKey);
    CHECK(
        TryInitializeScriptStringJournal(
            &value, key, storage.data(), 1, 2)
        == ScriptStringJournalStatus::CapacityExceeded);
    CHECK(!value.initialized());
    CHECK(
        TryInitializeScriptStringJournal(
            &value,
            key,
            nullptr,
            journal::kMaxScriptStringJournalEntries + 1,
            0)
        == ScriptStringJournalStatus::CapacityExceeded);
    CHECK(
        TryInitializeScriptStringJournal(
            &value,
            key,
            storage.data(),
            journal::kMaxScriptStringJournalEntries + 1,
            journal::kMaxScriptStringJournalEntries + 1)
        == ScriptStringJournalStatus::CapacityExceeded);
    CHECK(!value.initialized());
    CHECK(
        TryInitializeScriptStringJournal(
            &value, key, nullptr, 2, 2)
        == ScriptStringJournalStatus::InvalidArgument);
    CHECK(!value.initialized());

    alignas(ScriptStringJournalEntry)
        std::array<std::uint8_t, sizeof(ScriptStringJournalEntry) + 1>
            misalignedBytes{};
    ScriptStringJournalEntry *const misalignedStorage =
        reinterpret_cast<ScriptStringJournalEntry *>(
            misalignedBytes.data() + 1);
    CHECK(
        TryInitializeScriptStringJournal(
            &value, key, misalignedStorage, 1, 1)
        == ScriptStringJournalStatus::InvalidArgument);
    CHECK(!value.initialized());

    ScriptStringJournalEntry *const overlappingStorage =
        reinterpret_cast<ScriptStringJournalEntry *>(&value);
    CHECK(
        TryInitializeScriptStringJournal(
            &value, key, overlappingStorage, 1, 1)
        == ScriptStringJournalStatus::InvalidArgument);
    CHECK(!value.initialized());

    Initialize(
        &value, key, storage.data(), storage.size(), 2);
    CHECK(value.initialized());
    CHECK(value.key() == key);
    CHECK(value.storage() == storage.data());
    CHECK(value.capacity() == storageCapacity);
    CHECK(value.expectedCount() == 2);
    CHECK(journal::ScriptStringJournalKeyMatches(&value, key));
    CHECK(
        TryInitializeScriptStringJournal(
            &value, key, storage.data(), storageCapacity, 2)
        == ScriptStringJournalStatus::InvalidPhase);

    ZoneLoadContextKey stale = key;
    ++stale.generation;
    CHECK(!journal::ScriptStringJournalKeyMatches(&value, stale));
    CHECK(
        TrySealScriptStringJournal(&value, stale)
        == ScriptStringJournalStatus::StaleKey);
    CHECK(
        TryBeginScriptStringRollback(&value, malformed)
        == ScriptStringJournalStatus::InvalidKey);
    ZoneLoadContextKey crossSlot = key;
    ++crossSlot.slot;
    CHECK(
        TryBeginScriptStringTransfer(&value, crossSlot)
        == ScriptStringJournalStatus::StaleKey);
    CHECK(value.phase() == ScriptStringJournalPhase::Staging);
    CHECK(value.entryCount() == 0);

    static std::array<
        ScriptStringJournalEntry,
        journal::kMaxScriptStringJournalEntries>
        maxStorage{};
    ScriptStringJournal maxJournal{};
    const ZoneLoadContextKey maxKey = MakeKey(18, 3);
    CHECK(
        TryInitializeScriptStringJournal(
            &maxJournal,
            maxKey,
            maxStorage.data(),
            journal::kMaxScriptStringJournalEntries,
            journal::kMaxScriptStringJournalEntries)
        == ScriptStringJournalStatus::Success);
    CHECK(
        maxJournal.capacity()
        == journal::kMaxScriptStringJournalEntries);
    CHECK(
        maxJournal.expectedCount()
        == journal::kMaxScriptStringJournalEntries);
    CHECK(
        TryBeginScriptStringRollback(&maxJournal, maxKey)
        == ScriptStringJournalStatus::Success);
    CHECK(
        maxJournal.phase()
        == ScriptStringJournalPhase::RolledBack);
}

void TestAcquirePreflightNoDedupAndFailureAtomicity()
{
    constexpr std::uint32_t kSentinelId =
        UINT32_C(0x13572468);
    const ZoneLoadContextKey key = MakeKey(23, 7);
    std::array<ScriptStringJournalEntry, 3> storage{};
    for (ScriptStringJournalEntry &entry : storage)
    {
        entry.stringId = kSentinelId;
        entry.state = ScriptStringJournalEntryState::Released;
    }

    ScriptStringJournal value{};
    Initialize(
        &value, key, storage.data(), storage.size(), 3);
    CallbackRecorder recorder{};
    ScriptStringJournalCallbacks callbacks =
        MakeCallbacks(&recorder);

    ZoneLoadContextKey stale = key;
    ++stale.slot;
    CHECK(
        TryStageScriptString(&value, stale, callbacks)
        == ScriptStringJournalStatus::StaleKey);
    CHECK(recorder.eventCount == 0);
    CHECK(value.entryCount() == 0);
    CHECK(storage[0].stringId == kSentinelId);
    CHECK(
        TryStageScriptString(&value, key, {})
        == ScriptStringJournalStatus::InvalidArgument);
    CHECK(recorder.eventCount == 0);

    recorder.acquireId = UINT32_C(0xAABBCCDD);
    recorder.acquireStatus =
        ScriptStringAcquireCallbackStatus::RetryNoChange;
    CHECK(
        TryStageScriptString(&value, key, callbacks)
        == ScriptStringJournalStatus::RetryNoChange);
    CHECK(value.phase() == ScriptStringJournalPhase::Staging);
    CHECK(value.entryCount() == 0);
    CHECK(storage[0].stringId == kSentinelId);
    CHECK(!value.poisoned());

    recorder.acquireStatus =
        ScriptStringAcquireCallbackStatus::RejectedNoChange;
    CHECK(
        TryStageScriptString(&value, key, callbacks)
        == ScriptStringJournalStatus::Rejected);
    CHECK(value.entryCount() == 0);
    CHECK(storage[0].stringId == kSentinelId);

    recorder.reentrantJournal = &value;
    recorder.reentrantKey = key;
    recorder.reentryKind = ReentryKind::Stage;
    Stage(
        &value,
        key,
        &recorder,
        (std::numeric_limits<std::uint32_t>::max)());
    CHECK(recorder.observedCallbackActive);
    CHECK(
        recorder.reentryStatus
        == ScriptStringJournalStatus::Busy);
    Stage(&value, key, &recorder, UINT32_C(0x10203040));
    Stage(&value, key, &recorder, UINT32_C(0x10203040));
    CHECK(value.entryCount() == 3);
    CHECK(
        storage[0].stringId
        == (std::numeric_limits<std::uint32_t>::max)());
    CHECK(storage[1].stringId == UINT32_C(0x10203040));
    CHECK(storage[2].stringId == UINT32_C(0x10203040));
    for (const ScriptStringJournalEntry &entry : storage)
    {
        CHECK(
            entry.state
            == ScriptStringJournalEntryState::OrdinaryStaged);
        CHECK(entry.reserved[0] == 0);
        CHECK(entry.reserved[1] == 0);
        CHECK(entry.reserved[2] == 0);
    }

    const std::size_t callsBeforeCapacity = recorder.eventCount;
    CHECK(
        TryStageScriptString(&value, key, callbacks)
        == ScriptStringJournalStatus::CapacityExceeded);
    CHECK(recorder.eventCount == callsBeforeCapacity);
    CHECK(
        TrySealScriptStringJournal(&value, key)
        == ScriptStringJournalStatus::Success);
    CHECK(
        TrySealScriptStringJournal(&value, key)
        == ScriptStringJournalStatus::Success);
    CHECK(
        TryStageScriptString(&value, key, callbacks)
        == ScriptStringJournalStatus::InvalidPhase);

    ScriptStringJournal partial{};
    std::array<ScriptStringJournalEntry, 2> partialStorage{};
    Initialize(
        &partial,
        MakeKey(24, 7),
        partialStorage.data(),
        partialStorage.size(),
        2);
    CHECK(
        TrySealScriptStringJournal(&partial, MakeKey(24, 7))
        == ScriptStringJournalStatus::CountMismatch);
    CHECK(partial.phase() == ScriptStringJournalPhase::Staging);
}

void TestTransferRetryReentryCommitAndReceipt()
{
    const ZoneLoadContextKey key = MakeKey(31, 9);
    std::array<ScriptStringJournalEntry, 2> storage{};
    ScriptStringJournal value{};
    CallbackRecorder recorder{};
    Initialize(
        &value, key, storage.data(), storage.size(), 2);
    Stage(&value, key, &recorder, UINT32_C(0xF0000001));
    Stage(&value, key, &recorder, UINT32_C(0xF0000002));
    SealAndBeginTransfer(&value, key);
    CHECK(
        TryBeginScriptStringTransfer(&value, key)
        == ScriptStringJournalStatus::Success);

    recorder.transferStatus =
        ScriptStringTransferCallbackStatus::RetryNoChange;
    const ScriptStringJournalEntry firstBefore = storage[0];
    CHECK(
        TryTransferNextScriptString(
            &value, key, MakeCallbacks(&recorder))
        == ScriptStringJournalStatus::RetryNoChange);
    CHECK(value.phase() == ScriptStringJournalPhase::Transferring);
    CHECK(value.transferCursor() == 0);
    CHECK(storage[0].stringId == firstBefore.stringId);
    CHECK(storage[0].state == firstBefore.state);
    CHECK(!value.poisoned());

    recorder.transferStatus =
        ScriptStringTransferCallbackStatus::DatabaseUserClaimed;
    recorder.reentrantJournal = &value;
    recorder.reentrantKey = key;
    recorder.reentryKind = ReentryKind::Transfer;
    CHECK(
        TryTransferNextScriptString(
            &value, key, MakeCallbacks(&recorder))
        == ScriptStringJournalStatus::Success);
    CHECK(recorder.observedCallbackActive);
    CHECK(
        recorder.reentryStatus
        == ScriptStringJournalStatus::Busy);
    CHECK(value.transferCursor() == 1);
    CHECK(
        storage[0].state
        == ScriptStringJournalEntryState::DatabaseUserClaimed);

    recorder.transferStatus =
        ScriptStringTransferCallbackStatus::DuplicateReleased;
    CHECK(
        TryTransferNextScriptString(
            &value, key, MakeCallbacks(&recorder))
        == ScriptStringJournalStatus::Success);
    CHECK(value.phase() == ScriptStringJournalPhase::Transferred);
    CHECK(value.transferCursor() == 2);
    CHECK(
        storage[1].state
        == ScriptStringJournalEntryState::DuplicateReleased);

    const std::size_t eventsBeforeReceipt = recorder.eventCount;
    CHECK(
        TryTransferNextScriptString(&value, key, {})
        == ScriptStringJournalStatus::Success);
    CHECK(recorder.eventCount == eventsBeforeReceipt);
    CHECK(
        TryBeginScriptStringTransfer(&value, key)
        == ScriptStringJournalStatus::Success);

    const ScriptStringJournalEntry *const originalStorage =
        value.storage();
    CHECK(
        TryCommitScriptStringJournal(&value, key)
        == ScriptStringJournalStatus::Success);
    CHECK(value.phase() == ScriptStringJournalPhase::Committed);
    CHECK(value.storage() == nullptr);
    CHECK(value.capacity() == 0);
    CHECK(value.expectedCount() == 2);
    CHECK(value.entryCount() == 2);
    CHECK(value.transferCursor() == 2);
    CHECK(originalStorage == storage.data());
    CHECK(journal::ScriptStringJournalKeyMatches(&value, key));
    CHECK(
        TryCommitScriptStringJournal(&value, key)
        == ScriptStringJournalStatus::Success);
    CHECK(
        TryBeginScriptStringRollback(&value, key)
        == ScriptStringJournalStatus::InvalidPhase);
}

void TestReverseRollbackFromStaging()
{
    const ZoneLoadContextKey key = MakeKey(41, 11);
    std::array<ScriptStringJournalEntry, 3> storage{};
    ScriptStringJournal value{};
    CallbackRecorder recorder{};
    Initialize(
        &value, key, storage.data(), storage.size(), 3);
    Stage(&value, key, &recorder, 101);
    Stage(&value, key, &recorder, 202);
    Stage(&value, key, &recorder, 303);

    CHECK(
        TryBeginScriptStringRollback(&value, key)
        == ScriptStringJournalStatus::Success);
    CHECK(value.phase() == ScriptStringJournalPhase::RollingBack);
    CHECK(value.rollbackCursor() == 3);
    CHECK(
        TryBeginScriptStringRollback(&value, key)
        == ScriptStringJournalStatus::Success);
    CHECK(
        TryRollbackNextScriptString(&value, key, {})
        == ScriptStringJournalStatus::InvalidArgument);
    CHECK(!value.callbackActive());
    CHECK(value.rollbackCursor() == 3);

    recorder.ordinaryReleaseStatus =
        ScriptStringReleaseCallbackStatus::RetryNoChange;
    const std::size_t eventsBeforeRetry = recorder.eventCount;
    CHECK(
        TryRollbackNextScriptString(
            &value, key, MakeCallbacks(&recorder))
        == ScriptStringJournalStatus::RetryNoChange);
    CHECK(recorder.eventCount == eventsBeforeRetry + 1);
    CHECK(value.rollbackCursor() == 3);
    CHECK(
        storage[2].state
        == ScriptStringJournalEntryState::OrdinaryStaged);

    recorder.ordinaryReleaseStatus =
        ScriptStringReleaseCallbackStatus::Success;
    recorder.reentrantJournal = &value;
    recorder.reentrantKey = key;
    recorder.reentryKind = ReentryKind::Rollback;
    CHECK(
        TryRollbackNextScriptString(
            &value, key, MakeCallbacks(&recorder))
        == ScriptStringJournalStatus::Success);
    CHECK(recorder.observedCallbackActive);
    CHECK(
        recorder.reentryStatus
        == ScriptStringJournalStatus::Busy);
    CHECK(value.rollbackCursor() == 2);
    CHECK(
        storage[2].state
        == ScriptStringJournalEntryState::Released);

    CHECK(
        TryRollbackNextScriptString(
            &value, key, MakeCallbacks(&recorder))
        == ScriptStringJournalStatus::Success);
    CHECK(value.rollbackCursor() == 1);
    CHECK(
        TryRollbackNextScriptString(
            &value, key, MakeCallbacks(&recorder))
        == ScriptStringJournalStatus::Success);
    CHECK(value.phase() == ScriptStringJournalPhase::RolledBack);
    CHECK(value.rollbackCursor() == 0);
    CHECK(value.storage() == nullptr);
    for (const ScriptStringJournalEntry &entry : storage)
    {
        CHECK(
            entry.state
            == ScriptStringJournalEntryState::Released);
    }

    const std::size_t last = recorder.eventCount;
    CHECK(last >= 3);
    CHECK(
        recorder.events[last - 3].kind
        == EventKind::RemoveOrdinary);
    CHECK(recorder.events[last - 3].stringId == 303);
    CHECK(recorder.events[last - 2].stringId == 202);
    CHECK(recorder.events[last - 1].stringId == 101);

    const std::size_t eventsBeforeReceipt = recorder.eventCount;
    CHECK(
        TryBeginScriptStringRollback(&value, key)
        == ScriptStringJournalStatus::Success);
    CHECK(
        TryRollbackNextScriptString(&value, key, {})
        == ScriptStringJournalStatus::Success);
    CHECK(recorder.eventCount == eventsBeforeReceipt);
}

void TestPartialTransferRollbackOwnershipSelection()
{
    const ZoneLoadContextKey key = MakeKey(51, 13);
    std::array<ScriptStringJournalEntry, 3> storage{};
    ScriptStringJournal value{};
    CallbackRecorder recorder{};
    Initialize(
        &value, key, storage.data(), storage.size(), 3);
    Stage(&value, key, &recorder, 1001);
    Stage(&value, key, &recorder, 2002);
    Stage(&value, key, &recorder, 3003);
    SealAndBeginTransfer(&value, key);

    recorder.transferStatus =
        ScriptStringTransferCallbackStatus::DatabaseUserClaimed;
    CHECK(
        TryTransferNextScriptString(
            &value, key, MakeCallbacks(&recorder))
        == ScriptStringJournalStatus::Success);
    recorder.transferStatus =
        ScriptStringTransferCallbackStatus::DuplicateReleased;
    CHECK(
        TryTransferNextScriptString(
            &value, key, MakeCallbacks(&recorder))
        == ScriptStringJournalStatus::Success);
    CHECK(value.transferCursor() == 2);
    CHECK(value.phase() == ScriptStringJournalPhase::Transferring);

    CHECK(
        TryBeginScriptStringRollback(&value, key)
        == ScriptStringJournalStatus::Success);
    const std::size_t eventsBeforeRollback = recorder.eventCount;

    CHECK(
        TryRollbackNextScriptString(
            &value, key, MakeCallbacks(&recorder))
        == ScriptStringJournalStatus::Success);
    CHECK(
        recorder.events[eventsBeforeRollback].kind
        == EventKind::RemoveOrdinary);
    CHECK(
        recorder.events[eventsBeforeRollback].stringId
        == 3003);
    CHECK(value.rollbackCursor() == 2);

    const std::size_t eventsBeforeDuplicate = recorder.eventCount;
    CHECK(
        TryRollbackNextScriptString(&value, key, {})
        == ScriptStringJournalStatus::Success);
    CHECK(recorder.eventCount == eventsBeforeDuplicate);
    CHECK(value.rollbackCursor() == 1);
    CHECK(
        storage[1].state
        == ScriptStringJournalEntryState::Released);

    CHECK(
        TryRollbackNextScriptString(
            &value, key, MakeCallbacks(&recorder))
        == ScriptStringJournalStatus::Success);
    CHECK(
        recorder.events[recorder.eventCount - 1].kind
        == EventKind::RemoveDatabaseUser);
    CHECK(
        recorder.events[recorder.eventCount - 1].stringId
        == 1001);
    CHECK(value.phase() == ScriptStringJournalPhase::RolledBack);
    CHECK(value.storage() == nullptr);
}

void TestTransferredRemainsRollbackCapableAndEmptyJournal()
{
    const ZoneLoadContextKey key = MakeKey(61, 15);
    std::array<ScriptStringJournalEntry, 2> storage{};
    ScriptStringJournal value{};
    CallbackRecorder recorder{};
    Initialize(
        &value, key, storage.data(), storage.size(), 2);
    Stage(&value, key, &recorder, 7007);
    Stage(&value, key, &recorder, 8008);
    SealAndBeginTransfer(&value, key);
    recorder.transferStatus =
        ScriptStringTransferCallbackStatus::DatabaseUserClaimed;
    CHECK(
        TryTransferNextScriptString(
            &value, key, MakeCallbacks(&recorder))
        == ScriptStringJournalStatus::Success);
    recorder.transferStatus =
        ScriptStringTransferCallbackStatus::DuplicateReleased;
    CHECK(
        TryTransferNextScriptString(
            &value, key, MakeCallbacks(&recorder))
        == ScriptStringJournalStatus::Success);
    CHECK(value.phase() == ScriptStringJournalPhase::Transferred);

    CHECK(
        TryBeginScriptStringRollback(&value, key)
        == ScriptStringJournalStatus::Success);
    const std::size_t eventsBeforeDuplicate = recorder.eventCount;
    CHECK(
        TryRollbackNextScriptString(&value, key, {})
        == ScriptStringJournalStatus::Success);
    CHECK(recorder.eventCount == eventsBeforeDuplicate);
    CHECK(
        TryRollbackNextScriptString(
            &value, key, MakeCallbacks(&recorder))
        == ScriptStringJournalStatus::Success);
    CHECK(value.phase() == ScriptStringJournalPhase::RolledBack);

    const ZoneLoadContextKey emptyKey = MakeKey(62, 15);
    ScriptStringJournal empty{};
    Initialize(&empty, emptyKey, nullptr, 0, 0);
    CHECK(
        TrySealScriptStringJournal(&empty, emptyKey)
        == ScriptStringJournalStatus::Success);
    CHECK(
        TryBeginScriptStringTransfer(&empty, emptyKey)
        == ScriptStringJournalStatus::Success);
    CHECK(
        TryTransferNextScriptString(&empty, emptyKey, {})
        == ScriptStringJournalStatus::Success);
    CHECK(empty.phase() == ScriptStringJournalPhase::Transferred);
    CHECK(
        TryBeginScriptStringRollback(&empty, emptyKey)
        == ScriptStringJournalStatus::Success);
    CHECK(empty.phase() == ScriptStringJournalPhase::RolledBack);
    CHECK(
        TryRollbackNextScriptString(&empty, emptyKey, {})
        == ScriptStringJournalStatus::Success);
}

void TestUnsafeAndUnknownResultsPoison()
{
    const ZoneLoadContextKey unsafeKey = MakeKey(70, 17);
    std::array<ScriptStringJournalEntry, 1> unsafeStorage{};
    ScriptStringJournal unsafeJournal{};
    CallbackRecorder unsafeRecorder{};
    Initialize(
        &unsafeJournal,
        unsafeKey,
        unsafeStorage.data(),
        unsafeStorage.size(),
        1);
    unsafeRecorder.acquireId = 7070;
    unsafeRecorder.acquireStatus =
        ScriptStringAcquireCallbackStatus::UnsafeFailure;
    CHECK(
        TryStageScriptString(
            &unsafeJournal,
            unsafeKey,
            MakeCallbacks(&unsafeRecorder))
        == ScriptStringJournalStatus::UnsafeFailure);
    CHECK(unsafeJournal.poisoned());
    CHECK(unsafeJournal.entryCount() == 0);

    const ZoneLoadContextKey acquireKey = MakeKey(71, 17);
    std::array<ScriptStringJournalEntry, 1> acquireStorage{};
    ScriptStringJournal acquireJournal{};
    CallbackRecorder acquireRecorder{};
    Initialize(
        &acquireJournal,
        acquireKey,
        acquireStorage.data(),
        acquireStorage.size(),
        1);
    acquireRecorder.acquireId = 0;
    CHECK(
        TryStageScriptString(
            &acquireJournal,
            acquireKey,
            MakeCallbacks(&acquireRecorder))
        == ScriptStringJournalStatus::UnsafeFailure);
    CHECK(acquireJournal.poisoned());
    CHECK(acquireJournal.entryCount() == 0);
    CHECK(
        TryBeginScriptStringRollback(
            &acquireJournal, acquireKey)
        == ScriptStringJournalStatus::UnsafeFailure);

    const ZoneLoadContextKey transferKey = MakeKey(72, 17);
    std::array<ScriptStringJournalEntry, 1> transferStorage{};
    ScriptStringJournal transferJournal{};
    CallbackRecorder transferRecorder{};
    Initialize(
        &transferJournal,
        transferKey,
        transferStorage.data(),
        transferStorage.size(),
        1);
    Stage(
        &transferJournal,
        transferKey,
        &transferRecorder,
        UINT32_C(0xCAFEBABE));
    SealAndBeginTransfer(&transferJournal, transferKey);
    transferRecorder.transferStatus =
        static_cast<ScriptStringTransferCallbackStatus>(
            UINT8_C(0xFF));
    CHECK(
        TryTransferNextScriptString(
            &transferJournal,
            transferKey,
            MakeCallbacks(&transferRecorder))
        == ScriptStringJournalStatus::UnsafeFailure);
    CHECK(transferJournal.poisoned());
    CHECK(transferJournal.transferCursor() == 0);
    CHECK(
        transferStorage[0].state
        == ScriptStringJournalEntryState::OrdinaryStaged);

    const ZoneLoadContextKey releaseKey = MakeKey(73, 17);
    std::array<ScriptStringJournalEntry, 1> releaseStorage{};
    ScriptStringJournal releaseJournal{};
    CallbackRecorder releaseRecorder{};
    Initialize(
        &releaseJournal,
        releaseKey,
        releaseStorage.data(),
        releaseStorage.size(),
        1);
    Stage(
        &releaseJournal, releaseKey, &releaseRecorder, 9090);
    CHECK(
        TryBeginScriptStringRollback(
            &releaseJournal, releaseKey)
        == ScriptStringJournalStatus::Success);
    releaseRecorder.ordinaryReleaseStatus =
        static_cast<ScriptStringReleaseCallbackStatus>(
            UINT8_C(0xFF));
    CHECK(
        TryRollbackNextScriptString(
            &releaseJournal,
            releaseKey,
            MakeCallbacks(&releaseRecorder))
        == ScriptStringJournalStatus::UnsafeFailure);
    CHECK(releaseJournal.poisoned());
    CHECK(releaseJournal.rollbackCursor() == 1);
    CHECK(
        releaseStorage[0].state
        == ScriptStringJournalEntryState::OrdinaryStaged);
}

} // namespace

int main()
{
    TestLayoutNoexceptAndInitialization();
    TestAcquirePreflightNoDedupAndFailureAtomicity();
    TestTransferRetryReentryCommitAndReceipt();
    TestReverseRollbackFromStaging();
    TestPartialTransferRollbackOwnershipSelection();
    TestTransferredRemainsRollbackCapableAndEmptyJournal();
    TestUnsafeAndUnknownResultsPoison();

    if (failures != 0)
    {
        std::fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
