#pragma once

#include <database/db_zone_load_context.h>
#include <universal/kisak_abi.h>

#include <cstdint>

namespace db::script_string_journal
{
inline constexpr std::uint32_t kMaxScriptStringJournalEntries =
    UINT32_C(65536);

enum class ScriptStringJournalEntryState : std::uint8_t
{
    OrdinaryStaged,
    DatabaseUserClaimed,
    DuplicateReleased,
    Released,
};

// One entry records one acquired ordinary reference. IDs are deliberately
// full-width: the journal must not inherit the legacy 16-bit script-string
// field width. Repeated IDs are distinct references and are never deduplicated.
struct ScriptStringJournalEntry final
{
    std::uint32_t stringId = 0;
    ScriptStringJournalEntryState state =
        ScriptStringJournalEntryState::Released;
    std::uint8_t reserved[3]{};
};

RUNTIME_SIZE(ScriptStringJournalEntry, 0x8, 0x8);

enum class ScriptStringJournalPhase : std::uint8_t
{
    Staging,
    Sealed,
    Transferring,
    Transferred,
    Committed,
    RollingBack,
    RolledBack,
};

enum class ScriptStringAcquireCallbackStatus : std::uint8_t
{
    Acquired,
    RetryNoChange,
    RejectedNoChange,
    UnsafeFailure,
};

enum class ScriptStringTransferCallbackStatus : std::uint8_t
{
    DatabaseUserClaimed,
    DuplicateReleased,
    RetryNoChange,
    UnsafeFailure,
};

enum class ScriptStringReleaseCallbackStatus : std::uint8_t
{
    Success,
    RetryNoChange,
    UnsafeFailure,
};

enum class ScriptStringJournalStatus : std::uint8_t
{
    Success,
    RetryNoChange,
    Rejected,
    Busy,
    InvalidArgument,
    InvalidState,
    InvalidKey,
    StaleKey,
    InvalidPhase,
    CountMismatch,
    CapacityExceeded,
    UnsafeFailure,
};

struct ScriptStringJournalCallbacks final
{
    void *context = nullptr;

    // Acquires one ordinary reference for the source selected by context and
    // writes its full-width nonzero runtime ID only when returning Acquired.
    // The journal ignores outStringId for RetryNoChange and
    // RejectedNoChange.
    ScriptStringAcquireCallbackStatus (*acquireOrdinary)(
        void *context,
        std::uint32_t *outStringId) noexcept = nullptr;

    // Transfers this entry's one ordinary reference to database-user
    // ownership. DatabaseUserClaimed means the callback claimed that user
    // without dropping a reference. DuplicateReleased means that user was
    // already present and the entry's duplicate ordinary reference was
    // released.
    ScriptStringTransferCallbackStatus (*transferToDatabaseUser)(
        void *context,
        std::uint32_t stringId) noexcept = nullptr;

    // Removes exactly the ordinary reference represented by this entry.
    ScriptStringReleaseCallbackStatus (*removeOrdinary)(
        void *context,
        std::uint32_t stringId) noexcept = nullptr;

    // Removes exactly the database-user ownership claimed by this entry. This
    // is a targeted user removal, not a global/system ownership transfer.
    ScriptStringReleaseCallbackStatus (*removeDatabaseUser)(
        void *context,
        std::uint32_t stringId) noexcept = nullptr;
};

// Every callback status describes the ownership mutation of that one callback.
// RetryNoChange and RejectedNoChange guarantee that callback made no ownership
// change; the journal therefore leaves its entry, cursor, and phase unchanged.
// UnsafeFailure, an unknown callback value, or Acquired with ID zero makes
// ownership indeterminate and permanently poisons the journal.
//
// Callback results must truthfully report the exact ownership outcome.
// Callbacks may not mutate the journal control object or any attached entry
// storage, including through a saved caller pointer. Violating either rule
// makes the ownership record untrustworthy.
//
// Callbacks must not throw, longjmp, call Com_Error, or otherwise leave
// nonlocally. A nonlocal exit deliberately leaves callbackActive set, causing
// every later transition to fail closed as Busy. The callback context and
// caller-owned entry storage must outlive the complete nonterminal journal.
//
// This object has no internal synchronization. The caller must externally
// serialize initialization, every transition, every accessor, callback
// execution, and destruction. One global transaction serializer, not a
// per-zone or per-journal lock, must be acquired before initialization and
// held continuously until terminal commit or rollback returns. It must exclude
// every other journal transaction; every raw database-user add, transfer,
// remove, or publication; and the global database-user ownership sweep/
// transfer (the legacy user 4 -> 8 GC path). It cannot be dropped between
// journal calls. Overlapping journal transactions are forbidden unless a
// future shared database-user claim-accounting layer replaces this exclusive
// contract. Busy detects callback reentry only; it is not a cross-thread
// synchronization mechanism.
//
// The journal never allocates, never deduplicates, is non-copyable, and performs
// no destructor cleanup. TryCommit and completed rollback detach the backing
// storage only after all ownership has reached a terminal state. While backing
// is attached, the journal exclusively owns the used entry span. The caller
// cannot have its original pointer revoked and must therefore neither read nor
// mutate that span except through externally serialized diagnostics.
#ifdef KISAK_DB_SCRIPT_STRING_JOURNAL_TESTING
struct ScriptStringJournalTestAccess;
#endif

class alignas(8) ScriptStringJournal final
{
public:
    ScriptStringJournal() noexcept = default;
    ~ScriptStringJournal() noexcept = default;

    ScriptStringJournal(const ScriptStringJournal &) = delete;
    ScriptStringJournal &operator=(const ScriptStringJournal &) = delete;
    ScriptStringJournal(ScriptStringJournal &&) = delete;
    ScriptStringJournal &operator=(ScriptStringJournal &&) = delete;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] const zone_load::ZoneLoadContextKey &
    key() const noexcept;
    [[nodiscard]] ScriptStringJournalPhase phase() const noexcept;
    [[nodiscard]] const ScriptStringJournalEntry *
    storage() const noexcept;
    [[nodiscard]] std::uint32_t capacity() const noexcept;
    [[nodiscard]] std::uint32_t expectedCount() const noexcept;
    [[nodiscard]] std::uint32_t entryCount() const noexcept;
    [[nodiscard]] std::uint32_t transferCursor() const noexcept;
    [[nodiscard]] std::uint32_t rollbackCursor() const noexcept;
    [[nodiscard]] bool callbackActive() const noexcept;
    [[nodiscard]] bool poisoned() const noexcept;

private:
    friend ScriptStringJournalStatus TryInitializeScriptStringJournal(
        ScriptStringJournal *journal,
        const zone_load::ZoneLoadContextKey &key,
        ScriptStringJournalEntry *storage,
        std::uint32_t capacity,
        std::uint32_t expectedCount) noexcept;
    friend ScriptStringJournalStatus TryStageScriptString(
        ScriptStringJournal *journal,
        const zone_load::ZoneLoadContextKey &key,
        const ScriptStringJournalCallbacks &callbacks) noexcept;
    friend ScriptStringJournalStatus TrySealScriptStringJournal(
        ScriptStringJournal *journal,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    friend ScriptStringJournalStatus TryBeginScriptStringTransfer(
        ScriptStringJournal *journal,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    friend ScriptStringJournalStatus TryTransferNextScriptString(
        ScriptStringJournal *journal,
        const zone_load::ZoneLoadContextKey &key,
        const ScriptStringJournalCallbacks &callbacks) noexcept;
    friend ScriptStringJournalStatus TryCommitScriptStringJournal(
        ScriptStringJournal *journal,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    friend ScriptStringJournalStatus TryBeginScriptStringRollback(
        ScriptStringJournal *journal,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    friend ScriptStringJournalStatus TryRollbackNextScriptString(
        ScriptStringJournal *journal,
        const zone_load::ZoneLoadContextKey &key,
        const ScriptStringJournalCallbacks &callbacks) noexcept;
    friend bool ScriptStringJournalKeyMatches(
        const ScriptStringJournal *journal,
        const zone_load::ZoneLoadContextKey &key) noexcept;
#ifdef KISAK_DB_SCRIPT_STRING_JOURNAL_TESTING
    friend struct ScriptStringJournalTestAccess;
#endif

    [[nodiscard]] bool isCanonical() const noexcept;
    [[nodiscard]] ScriptStringJournalStatus validate() const noexcept;
    [[nodiscard]] ScriptStringJournalStatus validateKey(
        const zone_load::ZoneLoadContextKey &key) const noexcept;
    void poison() noexcept;
    void detachBacking() noexcept;
    void finishRollback() noexcept;

    zone_load::ZoneLoadContextKey key_{};
    ScriptStringJournalEntry *storage_ = nullptr;
    std::uint32_t capacity_ = 0;
    std::uint32_t expectedCount_ = 0;
    std::uint32_t entryCount_ = 0;
    std::uint32_t transferCursor_ = 0;
    std::uint32_t rollbackCursor_ = 0;
    ScriptStringJournalPhase phase_ =
        ScriptStringJournalPhase::Staging;
    std::uint8_t flags_ = 0;
};

RUNTIME_SIZE(ScriptStringJournal, 0x30, 0x30);

#ifdef KISAK_DB_SCRIPT_STRING_JOURNAL_TESTING
// Tests opt in before including this header. Production code has no mutation
// escape hatch around the checked journal API.
struct ScriptStringJournalTestAccess final
{
    static void SetKey(
        ScriptStringJournal *const journal,
        const zone_load::ZoneLoadContextKey &key) noexcept
    {
        if (journal)
            journal->key_ = key;
    }

    static void SetStorage(
        ScriptStringJournal *const journal,
        ScriptStringJournalEntry *const storage) noexcept
    {
        if (journal)
            journal->storage_ = storage;
    }

    static void SetCapacity(
        ScriptStringJournal *const journal,
        const std::uint32_t capacity) noexcept
    {
        if (journal)
            journal->capacity_ = capacity;
    }

    static void SetExpectedCount(
        ScriptStringJournal *const journal,
        const std::uint32_t expectedCount) noexcept
    {
        if (journal)
            journal->expectedCount_ = expectedCount;
    }

    static void SetEntryCount(
        ScriptStringJournal *const journal,
        const std::uint32_t entryCount) noexcept
    {
        if (journal)
            journal->entryCount_ = entryCount;
    }

    static void SetTransferCursor(
        ScriptStringJournal *const journal,
        const std::uint32_t transferCursor) noexcept
    {
        if (journal)
            journal->transferCursor_ = transferCursor;
    }

    static void SetRollbackCursor(
        ScriptStringJournal *const journal,
        const std::uint32_t rollbackCursor) noexcept
    {
        if (journal)
            journal->rollbackCursor_ = rollbackCursor;
    }

    static void SetPhase(
        ScriptStringJournal *const journal,
        const ScriptStringJournalPhase phase) noexcept
    {
        if (journal)
            journal->phase_ = phase;
    }

    static void SetFlags(
        ScriptStringJournal *const journal,
        const std::uint8_t flags) noexcept
    {
        if (journal)
            journal->flags_ = flags;
    }
};
#endif

// Binds this journal to exactly one nonzero zone-load generation and preflights
// the complete expected entry count before any acquisition callback can run.
// expectedCount zero needs no storage; otherwise storage must be non-null and
// capacity must cover expectedCount. Both counts are capped at 65,536. The used
// span is checked for entry alignment, multiplication/address overflow, and
// overlap with the journal control object. Unused storage is never inspected.
[[nodiscard]] ScriptStringJournalStatus TryInitializeScriptStringJournal(
    ScriptStringJournal *journal,
    const zone_load::ZoneLoadContextKey &key,
    ScriptStringJournalEntry *storage,
    std::uint32_t capacity,
    std::uint32_t expectedCount) noexcept;

// Invokes acquireOrdinary once for the caller-selected source and appends one
// OrdinaryStaged entry only after Acquired returns a nonzero ID. Repeated IDs
// append repeated entries. The expected-count bound is checked before the
// callback. RetryNoChange, RejectedNoChange, and all validation failures leave
// journal storage/cursors/phase unchanged.
[[nodiscard]] ScriptStringJournalStatus TryStageScriptString(
    ScriptStringJournal *journal,
    const zone_load::ZoneLoadContextKey &key,
    const ScriptStringJournalCallbacks &callbacks) noexcept;

// Staging -> Sealed only after exactly expectedCount entries were acquired.
// Repeating the exact seal while Sealed is a no-op success.
[[nodiscard]] ScriptStringJournalStatus TrySealScriptStringJournal(
    ScriptStringJournal *journal,
    const zone_load::ZoneLoadContextKey &key) noexcept;

// Sealed -> Transferring. This callback-free step makes a later
// RetryNoChange result completely mutation-free. Exact retries while
// Transferring or Transferred are no-op successes.
[[nodiscard]] ScriptStringJournalStatus TryBeginScriptStringTransfer(
    ScriptStringJournal *journal,
    const zone_load::ZoneLoadContextKey &key) noexcept;

// Processes at most one entry, so RetryNoChange leaves the entire invocation
// mutation-free. The forward cursor advances only after the callback reports
// the durable ownership outcome. The final successful step enters Transferred;
// an empty journal enters Transferred without invoking a callback.
[[nodiscard]] ScriptStringJournalStatus TryTransferNextScriptString(
    ScriptStringJournal *journal,
    const zone_load::ZoneLoadContextKey &key,
    const ScriptStringJournalCallbacks &callbacks) noexcept;

// Transferred remains rollback-capable while any whole-zone operation can
// still fail. The caller may invoke this callback-free
// Transferred -> Committed step only after the lifecycle controller has
// published the whole zone as Live, still under the same serializer and before
// releasing admission. It detaches caller storage and preserves the exact
// key/count receipt. Repeating it with the exact key is a no-op success.
// Committed ownership cannot be rolled back.
[[nodiscard]] ScriptStringJournalStatus TryCommitScriptStringJournal(
    ScriptStringJournal *journal,
    const zone_load::ZoneLoadContextKey &key) noexcept;

// Staging/Sealed/Transferring/Transferred -> RollingBack, preserving the
// transfer cursor and setting the reverse cursor to entryCount. This
// callback-free step also makes a later RetryNoChange mutation-free. An empty
// journal completes and detaches immediately. Exact retries while RollingBack
// or RolledBack are no-op successes.
[[nodiscard]] ScriptStringJournalStatus TryBeginScriptStringRollback(
    ScriptStringJournal *journal,
    const zone_load::ZoneLoadContextKey &key) noexcept;

// Reverses at most one entry. OrdinaryStaged uses removeOrdinary;
// DatabaseUserClaimed uses the targeted removeDatabaseUser; DuplicateReleased
// is already balanced and advances without a callback. Success marks the entry
// Released and durably decrements the reverse cursor. The final step enters
// RolledBack and detaches storage. Exact terminal retries invoke no callbacks.
[[nodiscard]] ScriptStringJournalStatus TryRollbackNextScriptString(
    ScriptStringJournal *journal,
    const zone_load::ZoneLoadContextKey &key,
    const ScriptStringJournalCallbacks &callbacks) noexcept;

// Exact binding predicate, including Committed/RolledBack receipts.
[[nodiscard]] bool ScriptStringJournalKeyMatches(
    const ScriptStringJournal *journal,
    const zone_load::ZoneLoadContextKey &key) noexcept;
} // namespace db::script_string_journal
