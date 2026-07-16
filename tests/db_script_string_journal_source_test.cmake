cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_context_path
    "${SOURCE_ROOT}/src/database/db_zone_load_context.h")
set(_header_path
    "${SOURCE_ROOT}/src/database/db_script_string_journal.h")
set(_source_path
    "${SOURCE_ROOT}/src/database/db_script_string_journal.cpp")
set(_fixture_path
    "${SOURCE_ROOT}/tests/db_script_string_journal_tests.cpp")
set(_manifest_path "${SOURCE_ROOT}/scripts/common_files.cmake")
set(_tests_path "${SOURCE_ROOT}/tests/CMakeLists.txt")
set(_ci_path "${SOURCE_ROOT}/.github/workflows/ci.yml")

foreach(_path IN ITEMS
    "${_context_path}"
    "${_header_path}"
    "${_source_path}"
    "${_fixture_path}"
    "${_manifest_path}"
    "${_tests_path}"
    "${_ci_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR
            "Missing script-string journal source: ${_path}")
    endif()
endforeach()

file(READ "${_context_path}" _context)
file(READ "${_header_path}" _header)
file(READ "${_source_path}" _source)
file(READ "${_fixture_path}" _fixture)
file(READ "${_manifest_path}" _manifest)
file(READ "${_tests_path}" _tests)
file(READ "${_ci_path}" _ci)

foreach(_var IN ITEMS
    _context
    _header
    _source
    _fixture
    _manifest
    _tests
    _ci)
    string(REGEX REPLACE "[ \t\r\n]+" " " _normalized "${${_var}}")
    set(${_var} "${_normalized}")
endforeach()

function(require_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing script-string journal invariant (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(require_not_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden script-string journal regression (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(require_no_loop_tokens SOURCE_VAR DESCRIPTION)
    string(REGEX MATCH
        "(^|[^A-Za-z0-9_])(for|while|do)([^A-Za-z0-9_]|$)"
        _loop_token
        "${${SOURCE_VAR}}")
    if(NOT _loop_token STREQUAL "")
        message(FATAL_ERROR
            "Forbidden script-string journal loop (${DESCRIPTION}): "
            "'${_loop_token}'")
    endif()
endfunction()

function(require_ordered SOURCE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${FIRST}" _first)
    string(FIND "${${SOURCE_VAR}}" "${SECOND}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered script-string journal invariant "
            "(${DESCRIPTION})")
    endif()
endfunction()

function(require_literal_count SOURCE_VAR NEEDLE EXPECTED DESCRIPTION)
    set(_remaining "${${SOURCE_VAR}}")
    set(_count 0)
    string(LENGTH "${NEEDLE}" _needle_length)
    if(_needle_length EQUAL 0)
        message(FATAL_ERROR "Empty literal for ${DESCRIPTION}")
    endif()
    while(TRUE)
        string(FIND "${_remaining}" "${NEEDLE}" _position)
        if(_position EQUAL -1)
            break()
        endif()
        math(EXPR _count "${_count} + 1")
        math(EXPR _next "${_position} + ${_needle_length}")
        string(SUBSTRING "${_remaining}" ${_next} -1 _remaining)
    endwhile()
    if(NOT _count EQUAL EXPECTED)
        message(FATAL_ERROR
            "Wrong script-string journal invariant count (${DESCRIPTION}): "
            "expected ${EXPECTED}, found ${_count} for '${NEEDLE}'")
    endif()
endfunction()

function(extract_slice SOURCE_VAR START END OUT_VAR DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${START}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR "Missing start of ${DESCRIPTION}: '${START}'")
    endif()
    string(SUBSTRING "${${SOURCE_VAR}}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${END}" _end)
    if(_end LESS_EQUAL 0)
        message(FATAL_ERROR
            "Missing ordered end of ${DESCRIPTION}: '${END}'")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_end} _slice)
    set(${OUT_VAR} "${_slice}" PARENT_SCOPE)
endfunction()

# The journal may bind the already-issued lifecycle key, but it must remain a
# report-free callback primitive with no production script table, loader,
# registry, PMem, zone, or native asset representation dependency.
foreach(_marker IN ITEMS
    "struct alignas(8) ZoneLoadContextKey final"
    "std::uint64_t generation = 0;"
    "std::uint32_t slot = kInvalidZoneLoadSlot;"
    "std::uint32_t reserved = 0;")
    require_contains(_context "${_marker}" "canonical lifecycle key")
endforeach()
require_contains(
    _header
    "#include <database/db_zone_load_context.h>"
    "lifecycle-key binding")
foreach(_var IN ITEMS _header _source)
    foreach(_forbidden IN ITEMS
        "script/scr_stringlist.h"
        "scr_string_t"
        "RefString"
        "GetRefString"
        "SL_"
        "PMem"
        "XZone"
        "database/database.h"
        "database/db_load"
        "database/db_registry"
        "DB_Add"
        "DB_Resolve"
        "DB_Set"
        "varXAsset"
        "g_loadingZone"
        "const char **"
        "const char**"
        "std::vector"
        "std::unique_ptr"
        "std::shared_ptr"
        "malloc("
        "calloc("
        "realloc("
        "operator new"
        "new "
        "delete "
        "throw ")
        require_not_contains(
            ${_var} "${_forbidden}" "production-neutral boundary")
    endforeach()
endforeach()
foreach(_forbidden IN ITEMS
    "KISAK_DB_SCRIPT_STRING_JOURNAL_TESTING"
    "ScriptStringJournalTestAccess"
    "friend struct")
    require_not_contains(
        _source "${_forbidden}" "production implementation test boundary")
endforeach()
require_not_contains(
    _source
    "Com_Error"
    "implementation cannot report or longjmp")
require_not_contains(
    _header
    "reinterpret_cast"
    "public API cannot expose native pointer casts")
require_literal_count(
    _source
    "reinterpret_cast<std::uintptr_t>"
    2
    "the only pointer/integer casts are checked storage bounds")
extract_slice(
    _source
    "[[nodiscard]] bool IsUsedStorageSpanValid("
    "} // namespace"
    _storage_validation
    "checked storage-span validation")
require_literal_count(
    _storage_validation
    "reinterpret_cast<std::uintptr_t>"
    2
    "pointer casts stay inside storage-span validation")

# Entries preserve one full-width ID per acquisition. Persistent records and
# journal cursors are fixed-width; no host-sized count or long state may enter
# either ownership representation.
extract_slice(
    _header
    "struct ScriptStringJournalEntry final"
    "RUNTIME_SIZE(ScriptStringJournalEntry, 0x8, 0x8);"
    _entry
    "journal entry representation")
foreach(_marker IN ITEMS
    "std::uint32_t stringId = 0;"
    "ScriptStringJournalEntryState state = ScriptStringJournalEntryState::Released;"
    "std::uint8_t reserved[3]{};")
    require_contains(_entry "${_marker}" "fixed-width entry state")
endforeach()
foreach(_forbidden IN ITEMS
    "std::size_t"
    "size_t"
    " long "
    "std::int64_t"
    "std::uint16_t"
    "uint16_t")
    require_not_contains(
        _entry "${_forbidden}" "entry cannot narrow or inherit host width")
endforeach()

extract_slice(
    _header
    "class alignas(8) ScriptStringJournal final"
    "// Binds this journal to exactly one nonzero zone-load generation"
    _journal_class
    "journal persistent control state")
foreach(_marker IN ITEMS
    "zone_load::ZoneLoadContextKey key_{};"
    "ScriptStringJournalEntry *storage_ = nullptr;"
    "std::uint32_t capacity_ = 0;"
    "std::uint32_t expectedCount_ = 0;"
    "std::uint32_t entryCount_ = 0;"
    "std::uint32_t transferCursor_ = 0;"
    "std::uint32_t rollbackCursor_ = 0;"
    "std::uint8_t flags_ = 0;")
    require_contains(_journal_class "${_marker}" "fixed persistent state")
endforeach()
extract_slice(
    _journal_class
    "private:"
    "zone_load::ZoneLoadContextKey key_{};"
    _journal_private_methods
    "journal private method declarations")
require_contains(
    _journal_private_methods
    "void detachBacking() noexcept;"
    "rollback detach helper remains private")
foreach(_access_label IN ITEMS "public:" "protected:")
    require_not_contains(
        _journal_private_methods
        "${_access_label}"
        "detach helper cannot escape the private method section")
endforeach()
foreach(_forbidden IN ITEMS
    "std::size_t"
    "size_t"
    " long "
    "std::int64_t")
    require_not_contains(
        _journal_class "${_forbidden}" "no host-sized persistent state")
endforeach()
foreach(_marker IN ITEMS
    "~ScriptStringJournal() noexcept = default;"
    "ScriptStringJournal(const ScriptStringJournal &) = delete;"
    "ScriptStringJournal(ScriptStringJournal &&) = delete;"
    "class alignas(8) ScriptStringJournal final"
    "RUNTIME_SIZE(ScriptStringJournal, 0x30, 0x30);")
    require_contains(_header "${_marker}" "non-owning no-op lifetime")
endforeach()
require_not_contains(
    _source
    "ScriptStringJournal::~ScriptStringJournal"
    "destructor performs no cleanup")

# Freeze the exact state machines and callback vocabulary. Retry and rejection
# are explicitly no-change results; unsafe or unknown completion is poison.
foreach(_marker IN ITEMS
    "inline constexpr std::uint32_t kMaxScriptStringJournalEntries = UINT32_C(65536);"
    "OrdinaryStaged"
    "DatabaseUserClaimed"
    "DuplicateReleased"
    "Released"
    "Staging"
    "Sealed"
    "Transferring"
    "Transferred"
    "CommitReady"
    "Committed"
    "RollingBack"
    "RolledBack"
    "Acquired"
    "RetryNoChange"
    "RejectedNoChange"
    "UnsafeFailure"
    "ScriptStringJournalStatus::Busy"
    "ScriptStringJournalStatus::StaleKey")
    if(_marker MATCHES "^ScriptStringJournalStatus")
        require_contains(_source "${_marker}" "status-bearing fail-closed API")
    else()
        require_contains(_header "${_marker}" "fixed state vocabulary")
    endif()
endforeach()
foreach(_marker IN ITEMS
    "The journal never allocates, never deduplicates"
    "no destructor cleanup"
    "This object has no internal synchronization."
    "One global transaction serializer, not a"
    "per-zone or per-journal lock"
    "held continuously until terminal commit or rollback returns"
    "every other journal transaction"
    "every raw database-user add, transfer"
    "remove, or publication"
    "Overlapping journal transactions are forbidden"
    "future shared database-user claim-accounting layer"
    "Callbacks may not mutate the journal control object"
    "results must truthfully report the exact ownership outcome"
    "Callbacks must not throw, longjmp, call Com_Error"
    "Repeated IDs are distinct references and are never deduplicated."
    "status-bearing Transferred -> CommitReady step"
    "publish the matching lifecycle controller as Live"
    "invoke this unchecked finalizer"
    "It cannot validate, branch, report status"
    "Repeating it on the resulting Committed journal is harmless.")
    require_contains(_header "${_marker}" "public ownership contract")
endforeach()
foreach(_marker IN ITEMS
    "#ifdef KISAK_DB_SCRIPT_STRING_JOURNAL_TESTING struct ScriptStringJournalTestAccess; #endif class alignas(8) ScriptStringJournal final"
    "#ifdef KISAK_DB_SCRIPT_STRING_JOURNAL_TESTING friend struct ScriptStringJournalTestAccess; #endif"
    "#ifdef KISAK_DB_SCRIPT_STRING_JOURNAL_TESTING // Tests opt in before including this header."
    "struct ScriptStringJournalTestAccess final")
    require_contains(_header "${_marker}" "guarded test-only mutation access")
endforeach()
require_not_contains(
    _fixture
    "#define KISAK_DB_SCRIPT_STRING_JOURNAL_TESTING"
    "fixture cannot create a one-translation-unit class definition")

# Initialization binds one exact nonzero key and validates the complete
# expected count/storage span before any acquisition callback may run.
foreach(_marker IN ITEMS
    "capacity > kMaxScriptStringJournalEntries"
    "expectedCount > kMaxScriptStringJournalEntries"
    "if (expectedCount > capacity)"
    "if (expectedCount != 0 && !storage)"
    "IsUsedStorageSpanValid(journal, storage, expectedCount)"
    "journal->key_ = key;"
    "journal->expectedCount_ = expectedCount;"
    "journal->phase_ = ScriptStringJournalPhase::Staging;")
    require_contains(_source "${_marker}" "whole-count storage preflight")
endforeach()
require_ordered(
    _source
    "if (expectedCount > capacity)"
    "journal->key_ = key;"
    "capacity is proven before key/ownership publication")
require_ordered(
    _source
    "if (journal->entryCount_ >= journal->expectedCount_)"
    "callbacks.acquireOrdinary(callbacks.context, &stringId);"
    "staging capacity is checked before acquisition")

# Controller validation is O(1). The complete used-entry scan exists in one
# helper and runs only on the first Seal, BeginRollback, or Prepare transition;
# exact idempotent receipts return before it. One-entry transfer/rollback calls
# validate only the current entry immediately before acting.
extract_slice(
    _source
    "bool ScriptStringJournal::isCanonical() const noexcept"
    "ScriptStringJournalStatus ScriptStringJournal::validate() const noexcept"
    _canonical_controller
    "constant-time controller validation")
foreach(_forbidden IN ITEMS
    "for ("
    "while ("
    "storage_["
    "EntriesMatchPhase(")
    require_not_contains(
        _canonical_controller
        "${_forbidden}"
        "controller validation cannot scan entries")
endforeach()
foreach(_marker IN ITEMS
    "key_ == zone_load::ZoneLoadContextKey{}"
    "capacity_ > kMaxScriptStringJournalEntries"
    "expectedCount_ > kMaxScriptStringJournalEntries"
    "!IsUsedStorageSpanValid( this, storage_, expectedCount_)"
    "case ScriptStringJournalPhase::RollingBack: if (rollbackCursor_ == 0) return false;")
    require_contains(
        _canonical_controller
        "${_marker}"
        "complete constant-time controller checks")
endforeach()
extract_slice(
    _source
    "void ScriptStringJournal::detachBacking() noexcept"
    "void ScriptStringJournal::finishRollback() noexcept"
    _detach_backing
    "rollback detach helper")
string(STRIP "${_detach_backing}" _detach_backing_exact)
set(_expected_detach_backing
    "void ScriptStringJournal::detachBacking() noexcept { storage_ = nullptr; capacity_ = 0; }")
if(NOT _detach_backing_exact STREQUAL _expected_detach_backing)
    message(FATAL_ERROR
        "The private rollback detach helper must contain exactly its two "
        "backing-store assignments")
endif()
extract_slice(
    _source
    "void ScriptStringJournal::finishRollback() noexcept"
    "bool ScriptStringJournal::initialized() const noexcept"
    _finish_rollback
    "rollback terminal receipt")
string(STRIP "${_finish_rollback}" _finish_rollback_exact)
set(_expected_finish_rollback
    "void ScriptStringJournal::finishRollback() noexcept { rollbackCursor_ = 0; phase_ = ScriptStringJournalPhase::RolledBack; flags_ = kInitializedFlag; detachBacking(); }")
if(NOT _finish_rollback_exact STREQUAL _expected_finish_rollback)
    message(FATAL_ERROR
        "The rollback terminal must normalize its receipt and invoke the "
        "exact private detach helper")
endif()
extract_slice(
    _source
    "[[nodiscard]] bool EntriesMatchPhase("
    "[[nodiscard]] bool IsUsedStorageSpanValid("
    _entry_scan
    "single linear entry-scan helper")
require_literal_count(
    _entry_scan
    "for (std::uint32_t index = 0; index < entryCount; ++index)"
    1
    "one linear scan loop")
string(REGEX MATCHALL
    "(^|[^A-Za-z0-9_])(for|while|do)([^A-Za-z0-9_]|$)"
    _source_loop_tokens
    "${_source}")
list(LENGTH _source_loop_tokens _source_loop_count)
if(NOT _source_loop_count EQUAL 1)
    message(FATAL_ERROR
        "EntriesMatchPhase must own the journal implementation's sole loop; "
        "found ${_source_loop_count} loop tokens")
endif()
require_literal_count(
    _source
    "EntriesMatchPhase("
    4
    "definition plus Seal/Prepare/BeginRollback boundary scans")
require_literal_count(
    _source
    "EntryMatchesPhase("
    4
    "definition, scan delegation, and two current-entry checks")

extract_slice(
    _source
    "ScriptStringJournalStatus TrySealScriptStringJournal("
    "ScriptStringJournalStatus TryBeginScriptStringTransfer("
    _seal
    "first seal boundary")
require_ordered(
    _seal
    "if (journal->phase_ == ScriptStringJournalPhase::Sealed)"
    "if (!EntriesMatchPhase("
    "idempotent sealed receipt precedes its one-time scan")
extract_slice(
    _source
    "ScriptStringJournalStatus TryBeginScriptStringTransfer("
    "ScriptStringJournalStatus TryTransferNextScriptString("
    _begin_transfer
    "callback-free transfer boundary")
require_no_loop_tokens(
    _begin_transfer
    "begin-transfer must remain constant-time")
extract_slice(
    _source
    "ScriptStringJournalStatus TryPrepareScriptStringJournalCommit("
    "void FinalizeScriptStringJournalCommit("
    _prepare_boundary
    "pre-Live commit-prepare boundary")
require_ordered(
    _prepare_boundary
    "journal->phase_ == ScriptStringJournalPhase::CommitReady"
    "if (!EntriesMatchPhase("
    "idempotent prepare receipt precedes its one-time scan")
require_ordered(
    _prepare_boundary
    "if (!EntriesMatchPhase("
    "journal->phase_ = ScriptStringJournalPhase::CommitReady;"
    "complete scan precedes CommitReady publication")
extract_slice(
    _source
    "ScriptStringJournalStatus TryBeginScriptStringRollback("
    "ScriptStringJournalStatus TryRollbackNextScriptString("
    _begin_rollback
    "rollback boundary")
require_ordered(
    _begin_rollback
    "journal->phase_ == ScriptStringJournalPhase::RollingBack"
    "if (!EntriesMatchPhase("
    "idempotent rollback receipt precedes its one-time scan")
require_no_loop_tokens(
    _begin_rollback
    "begin-rollback delegates its sole scan to EntriesMatchPhase")

# Every mutating operation validates the exact key. Acquisition publishes one
# entry only after a durable nonzero ID, preserving multiplicity and output
# atomicity for retry/rejection.
require_literal_count(
    _source
    "status = journal->validateKey(key);"
    7
    "all seven status-bearing post-initialization mutators bind the exact key")
foreach(_marker IN ITEMS
    "if (!(key == key_))"
    "return ScriptStringJournalStatus::StaleKey;"
    "std::uint32_t stringId = 0;"
    "callbacks.acquireOrdinary(callbacks.context, &stringId);"
    "ScriptStringAcquireCallbackStatus::RetryNoChange"
    "ScriptStringAcquireCallbackStatus::RejectedNoChange"
    "if (stringId == 0)"
    "journal->poison();"
    "ScriptStringJournalEntryState::OrdinaryStaged"
    "++journal->entryCount_;")
    require_contains(_source "${_marker}" "failure-atomic acquisition")
endforeach()
require_ordered(
    _source
    "if (stringId == 0)"
    "journal->storage_[journal->entryCount_] = {"
    "zero/indeterminate IDs fail before entry publication")
require_ordered(
    _source
    "journal->storage_[journal->entryCount_] = {"
    "++journal->entryCount_;"
    "complete entry publishes before count")

# Transfer processes exactly one occurrence, records claimed-vs-duplicate
# ownership before advancing, and leaves the forward cursor stable on retry.
extract_slice(
    _source
    "ScriptStringJournalStatus TryTransferNextScriptString("
    "ScriptStringJournalStatus TryPrepareScriptStringJournalCommit("
    _transfer
    "one-entry transfer operation")
require_no_loop_tokens(
    _transfer
    "one-entry transfer operation")
foreach(_marker IN ITEMS
    "journal->storage_[journal->transferCursor_]"
    "if (!EntryMatchesPhase("
    "callbacks.transferToDatabaseUser("
    "ScriptStringTransferCallbackStatus::RetryNoChange"
    "ScriptStringJournalEntryState::DatabaseUserClaimed"
    "ScriptStringJournalEntryState::DuplicateReleased"
    "++journal->transferCursor_;"
    "ScriptStringJournalPhase::Transferred")
    require_contains(_transfer "${_marker}" "exact transfer outcome")
endforeach()
require_ordered(
    _transfer
    "if (!EntryMatchesPhase("
    "if (!callbacks.transferToDatabaseUser)"
    "current transfer entry corruption wins over callback validation")
require_ordered(
    _transfer
    "ScriptStringTransferCallbackStatus::RetryNoChange"
    "entry.state = callbackStatus"
    "retry returns before entry mutation")
require_ordered(
    _transfer
    "entry.state = callbackStatus"
    "++journal->transferCursor_;"
    "durable outcome publishes before cursor")

# Transferred prepares to CommitReady before Live while remaining reversible.
# The post-Live finalizer is an unconditional no-fail publication/detach step.
# Rollback walks in reverse and selects the exact release operation from each
# recorded outcome.
extract_slice(
    _source
    "ScriptStringJournalStatus TryPrepareScriptStringJournalCommit("
    "void FinalizeScriptStringJournalCommit("
    _prepare
    "status-bearing pre-Live commit preparation")
require_no_loop_tokens(
    _prepare
    "prepare delegates its sole scan to EntriesMatchPhase")
foreach(_forbidden IN ITEMS
    "callbacks"
    "callbackStatus"
    "acquireOrdinary"
    "transferToDatabaseUser"
    "removeOrdinary"
    "removeDatabaseUser")
    require_not_contains(
        _prepare "${_forbidden}" "prepare cannot invoke callbacks")
endforeach()
foreach(_marker IN ITEMS
    "if (!EntriesMatchPhase("
    "journal->phase_ = ScriptStringJournalPhase::CommitReady;")
    require_contains(_prepare "${_marker}" "rollback-capable commit preparation")
endforeach()

extract_slice(
    _source
    "void FinalizeScriptStringJournalCommit("
    "ScriptStringJournalStatus TryBeginScriptStringRollback("
    _finalize
    "unconditional post-Live final commit")
string(STRIP "${_finalize}" _finalize_exact)
set(_expected_finalize
    "void FinalizeScriptStringJournalCommit( ScriptStringJournal &journal) noexcept { journal.phase_ = ScriptStringJournalPhase::Committed; journal.flags_ = kInitializedFlag; journal.storage_ = nullptr; journal.capacity_ = 0; }")
if(NOT _finalize_exact STREQUAL _expected_finalize)
    message(FATAL_ERROR
        "The post-Live script-string finalizer must contain exactly its four "
        "unconditional publication/detach assignments")
endif()
foreach(_forbidden IN ITEMS
    "if ("
    "switch ("
    "for ("
    "while ("
    "validate"
    "EntriesMatchPhase"
    "EntryMatchesPhase"
    "callbacks"
    "callbackStatus"
    "acquireOrdinary"
    "transferToDatabaseUser"
    "removeOrdinary"
    "removeDatabaseUser"
    "Com_Error"
    "detachBacking("
    "return "
    "status"
    "throw ")
    require_not_contains(
        _finalize "${_forbidden}" "finalizer must remain unconditional/no-fail")
endforeach()
require_contains(
    _finalize
    "void FinalizeScriptStringJournalCommit( ScriptStringJournal &journal) noexcept { journal.phase_ = ScriptStringJournalPhase::Committed; journal.flags_ = kInitializedFlag; journal.storage_ = nullptr; journal.capacity_ = 0; }"
    "exact no-fail finalizer body")
require_ordered(
    _finalize
    "journal.phase_ = ScriptStringJournalPhase::Committed;"
    "journal.flags_ = kInitializedFlag;"
    "Committed publishes before flag normalization")
require_ordered(
    _finalize
    "journal.flags_ = kInitializedFlag;"
    "journal.storage_ = nullptr;"
    "flags normalize before backing detach")
require_ordered(
    _finalize
    "journal.storage_ = nullptr;"
    "journal.capacity_ = 0;"
    "backing pointer clears before capacity")

extract_slice(
    _source
    "ScriptStringJournalStatus TryBeginScriptStringRollback("
    "bool ScriptStringJournalKeyMatches("
    _rollback
    "reverse rollback protocol")
foreach(_marker IN ITEMS
    "case ScriptStringJournalPhase::Transferred:"
    "case ScriptStringJournalPhase::CommitReady:"
    "journal->rollbackCursor_ = journal->entryCount_;"
    "journal->rollbackCursor_ - 1"
    "if (!EntryMatchesPhase("
    "ScriptStringJournalEntryState::DuplicateReleased"
    "ScriptStringJournalEntryState::OrdinaryStaged"
    "callbacks.removeOrdinary("
    "ScriptStringJournalEntryState::DatabaseUserClaimed"
    "callbacks.removeDatabaseUser("
    "ScriptStringReleaseCallbackStatus::RetryNoChange"
    "entry.state = ScriptStringJournalEntryState::Released;"
    "--journal->rollbackCursor_;"
    "journal->finishRollback();")
    require_contains(_rollback "${_marker}" "outcome-specific reverse cleanup")
endforeach()
require_ordered(
    _rollback
    "journal->rollbackCursor_ - 1"
    "entry.state = ScriptStringJournalEntryState::Released;"
    "reverse entry selected before release publication")
extract_slice(
    _source
    "ScriptStringJournalStatus TryRollbackNextScriptString("
    "bool ScriptStringJournalKeyMatches("
    _rollback_step
    "one-entry rollback operation")
require_no_loop_tokens(
    _rollback_step
    "one-entry rollback operation")
extract_slice(
    _source
    "ScriptStringReleaseCallbackStatus callbackStatus ="
    "bool ScriptStringJournalKeyMatches("
    _rollback_callback
    "callback-backed rollback tail")
require_ordered(
    _rollback_callback
    "ScriptStringReleaseCallbackStatus::RetryNoChange"
    "entry.state = ScriptStringJournalEntryState::Released;"
    "release retry returns before ownership mutation")
require_ordered(
    _rollback
    "entry.state = ScriptStringJournalEntryState::Released;"
    "--journal->rollbackCursor_;"
    "released state publishes before reverse cursor")
require_literal_count(
    _source
    "detachBacking();"
    1
    "rollback is the only terminal that calls the detach helper")

# Callback reentry is Busy, unknown or unsafe results poison without cursor
# advancement, and complete representation validation rejects corrupt entries.
foreach(_marker IN ITEMS
    "if ((flags_ & kCallbackActiveFlag) != 0)"
    "return ScriptStringJournalStatus::Busy;"
    "if ((flags_ & kPoisonedFlag) != 0)"
    "return ScriptStringJournalStatus::UnsafeFailure;"
    "!IsKnownAcquireStatus(callbackStatus)"
    "!IsKnownTransferStatus(callbackStatus)"
    "!IsKnownReleaseStatus(callbackStatus)"
    "entry.stringId != 0"
    "entry.reserved[0] == 0")
    require_contains(_source "${_marker}" "canonical fail-closed state")
endforeach()

# Exercise the lifecycle and journal as one transaction without adding
# production wiring. Success prepares while Loading, publishes Live through
# the real controller, and only then finalizes. A stale-key controller failure
# leaves the canonical controller Loading so the CommitReady journal can be
# rolled back with its real key.
extract_slice(
    _fixture
    "void TestComposedZoneCommitFinalizationOrdering()"
    "void TestComposedZoneCommitFailureRollback()"
    _composed_commit_success
    "composed zone/journal commit success")
foreach(_marker IN ITEMS
    "ScriptStringJournalPhase::CommitReady"
    "ZoneLoadContextPhase::Loading"
    "ZoneLoadContextPhase::Live"
    "ScriptStringJournalPhase::Committed"
    "value.storage() == nullptr"
    "value.capacity() == 0")
    require_contains(
        _composed_commit_success
        "${_marker}"
        "composed commit success state")
endforeach()
require_ordered(
    _composed_commit_success
    "TryPrepareScriptStringJournalCommit(&value, key)"
    "TryCommitZoneLoadContext(&slot, key)"
    "journal prepare precedes real lifecycle publication")
require_ordered(
    _composed_commit_success
    "TryCommitZoneLoadContext(&slot, key)"
    "FinalizeScriptStringJournalCommit(value)"
    "real lifecycle publication precedes journal finalization")
extract_slice(
    _composed_commit_success
    "TryPrepareScriptStringJournalCommit(&value, key)"
    "TryCommitZoneLoadContext(&slot, key)"
    _prepared_controller_window
    "post-prepare pre-commit controller state")
require_contains(
    _prepared_controller_window
    "slot.phase() == ZoneLoadContextPhase::Loading"
    "successful prepare leaves the controller Loading")
extract_slice(
    _composed_commit_success
    "if (commitStatus == ZoneLoadContextStatus::Success)"
    "CHECK(commitStatus == ZoneLoadContextStatus::Success);"
    _successful_finalize_guard
    "successful lifecycle commit finalizer guard")
string(STRIP "${_successful_finalize_guard}" _successful_finalize_guard_exact)
set(_expected_successful_finalize_guard
    "if (commitStatus == ZoneLoadContextStatus::Success) FinalizeScriptStringJournalCommit(value);")
if(NOT _successful_finalize_guard_exact
    STREQUAL _expected_successful_finalize_guard)
    message(FATAL_ERROR
        "The composed fixture must finalize only after the real lifecycle "
        "commit reports Success")
endif()
foreach(_marker IN ITEMS
    "TryPrepareScriptStringJournalCommit(&value, key)"
    "TryCommitZoneLoadContext(&slot, key)"
    "FinalizeScriptStringJournalCommit(value)")
    require_literal_count(
        _composed_commit_success
        "${_marker}"
        1
        "one composed success transition")
endforeach()
require_not_contains(
    _composed_commit_success
    "TryBeginScriptStringRollback("
    "successful composed commit cannot roll back")

extract_slice(
    _fixture
    "void TestComposedZoneCommitFailureRollback()"
    "void TestMaximumCountLinearLifecycle()"
    _composed_commit_failure
    "composed zone/journal commit failure")
foreach(_marker IN ITEMS
    "ScriptStringJournalPhase::CommitReady"
    "ZoneLoadContextStatus::StaleKey"
    "ZoneLoadContextPhase::Loading"
    "ScriptStringJournalPhase::RollingBack"
    "ScriptStringJournalPhase::RolledBack"
    "EventKind::RemoveDatabaseUser")
    require_contains(
        _composed_commit_failure
        "${_marker}"
        "constrained pre-Live rollback state")
endforeach()
require_ordered(
    _composed_commit_failure
    "TryPrepareScriptStringJournalCommit(&value, key)"
    "TryCommitZoneLoadContext(&slot, staleCommitKey)"
    "prepare precedes forced lifecycle commit failure")
require_ordered(
    _composed_commit_failure
    "TryCommitZoneLoadContext(&slot, staleCommitKey)"
    "TryBeginScriptStringRollback(&value, key)"
    "failed pre-Live lifecycle commit precedes journal rollback")
require_ordered(
    _composed_commit_failure
    "TryBeginScriptStringRollback(&value, key)"
    "TryRollbackNextScriptString("
    "rollback begins before its targeted release")
extract_slice(
    _composed_commit_failure
    "TryCommitZoneLoadContext(&slot, staleCommitKey)"
    "TryBeginScriptStringRollback(&value, key)"
    _failed_lifecycle_commit
    "failed lifecycle commit receipt")
foreach(_marker IN ITEMS
    "ZoneLoadContextStatus::StaleKey"
    "slot.phase() == ZoneLoadContextPhase::Loading"
    "value.phase() == ScriptStringJournalPhase::CommitReady"
    "value.storage() == backing")
    require_contains(
        _failed_lifecycle_commit
        "${_marker}"
        "stale commit cannot publish Live or detach backing")
endforeach()
foreach(_marker IN ITEMS
    "TryPrepareScriptStringJournalCommit(&value, key)"
    "TryCommitZoneLoadContext(&slot, staleCommitKey)"
    "TryBeginScriptStringRollback(&value, key)")
    require_literal_count(
        _composed_commit_failure
        "${_marker}"
        1
        "one composed failure transition")
endforeach()
require_not_contains(
    _composed_commit_failure
    "FinalizeScriptStringJournalCommit("
    "failed lifecycle commit cannot finalize the journal")

extract_slice(
    _fixture
    "void TestMaximumCountLinearLifecycle()"
    "bool SequenceContains("
    _maximum_count_lifecycle
    "maximum-count journal lifecycle")
require_ordered(
    _maximum_count_lifecycle
    "TryPrepareScriptStringJournalCommit(&value, key)"
    "value.phase() == ScriptStringJournalPhase::CommitReady"
    "maximum-count scan publishes CommitReady")
require_ordered(
    _maximum_count_lifecycle
    "value.phase() == ScriptStringJournalPhase::CommitReady"
    "TryBeginScriptStringRollback(&value, key)"
    "maximum-count CommitReady remains rollback-capable")
foreach(_test_name IN ITEMS
    "TestComposedZoneCommitFinalizationOrdering"
    "TestComposedZoneCommitFailureRollback")
    require_literal_count(
        _fixture
        "${_test_name}()"
        2
        "composed fixture definition and main invocation")
endforeach()

# The focused fixture pins full-width and duplicate IDs, no-callback capacity
# rejection, retries/reentry, claimed-vs-duplicate outcomes, reverse partial
# rollback, reversible Transferred/CommitReady, empty receipts, and poison paths.
foreach(_marker IN ITEMS
    "void TestLayoutNoexceptAndInitialization()"
    "void TestAcquirePreflightNoDedupAndFailureAtomicity()"
    "void TestTransferRetryReentryPrepareFinalizeAndReceipt()"
    "void TestReverseRollbackFromStaging()"
    "void TestPartialTransferRollbackOwnershipSelection()"
    "void TestTransferredAndCommitReadyRemainRollbackCapable()"
    "void TestComposedZoneCommitFinalizationOrdering()"
    "void TestComposedZoneCommitFailureRollback()"
    "void TestUnsafeAndUnknownResultsPoison()"
    "journal::kMaxScriptStringJournalEntries"
    "(std::numeric_limits<std::uint32_t>::max)()"
    "UINT32_C(0x10203040)"
    "recorder.eventCount == 0"
    "ScriptStringJournalStatus::StaleKey"
    "ScriptStringJournalStatus::Busy"
    "ScriptStringJournalStatus::RetryNoChange"
    "ScriptStringJournalEntryState::DatabaseUserClaimed"
    "ScriptStringJournalEntryState::DuplicateReleased"
    "EventKind::RemoveOrdinary"
    "EventKind::RemoveDatabaseUser"
    "ScriptStringJournalPhase::Transferred"
    "ScriptStringJournalPhase::CommitReady"
    "ScriptStringJournalPhase::Committed"
    "ScriptStringJournalPhase::RolledBack"
    "static_cast<ScriptStringTransferCallbackStatus>( UINT8_C(0xFF))")
    require_contains(_fixture "${_marker}" "adversarial runtime coverage")
endforeach()
foreach(_marker IN ITEMS
    "static_assert(sizeof(ScriptStringJournal) == 0x30);"
    "static_assert(alignof(ScriptStringJournal) == 8);"
    "std::is_standard_layout_v<ScriptStringJournal>"
    "static_assert(noexcept(TryCommitZoneLoadContext("
    "void TestMaximumCountLinearLifecycle()"
    "journal::kMaxScriptStringJournalEntries"
    "ScaleCallbackRecorder"
    "void TestSequentialRepeatedIdOwnershipModel()"
    "RunSequentialOwnershipScenario("
    "const std::array<std::uint32_t, 3> repeatedA"
    "const std::array<std::uint32_t, 3> interleaved"
    "for (const bool preexisting : {false, true})"
    "for (const bool rollback : {false, true})"
    "void TestControllerAndEntryCorruptionFailClosed()"
    "ScriptStringJournalTestAccess::SetCapacity("
    "ScriptStringJournalTestAccess::SetExpectedCount("
    "ScriptStringJournalTestAccess::SetStorage("
    "ScriptStringJournalTestAccess::SetRollbackCursor("
    "void TestDatabaseUserReleaseStatuses()"
    "ScriptStringReleaseCallbackStatus::RetryNoChange"
    "unsafeRecorder.databaseReleaseStatus"
    "unknownRecorder.databaseReleaseStatus"
    "void TestPlacementReconstructionRejectsOldKey()"
    "::new (bytes.data()) ScriptStringJournal{}"
    "TrySealScriptStringJournal(value, oldKey)"
    "TryPrepareScriptStringJournalCommit(value, oldKey)"
    "TryPrepareScriptStringJournalCommit(value, newKey)"
    "FinalizeScriptStringJournalCommit(*value)"
    "TryPrepareScriptStringJournalCommit( &corruptCommitEntry, commitKey)"
    "corruptCommitEntry.phase() == ScriptStringJournalPhase::Transferred"
    "TryBeginScriptStringRollback( &corruptCommitEntry, commitKey)"
    "entry = { 0, ScriptStringJournalEntryState::Released"
    "entry = { 0, ScriptStringJournalEntryState::OrdinaryStaged")
    require_contains(_fixture "${_marker}" "hardening regression coverage")
endforeach()

# Compile the production-neutral primitive in normal source manifests. All
# five portable hosts build/run the complete test set; measured Windows x86
# explicitly names and executes this transaction fixture.
foreach(_marker IN ITEMS
    "\${SRC_DIR}/database/db_script_string_journal.cpp"
    "\${SRC_DIR}/database/db_script_string_journal.h")
    require_contains(_manifest "${_marker}" "production manifest coverage")
endforeach()
extract_slice(
    _tests
    "add_executable(kisakcod-db-script-string-journal-tests"
    "target_include_directories("
    _journal_test_target
    "journal runtime test target")
foreach(_marker IN ITEMS
    "db_script_string_journal_tests.cpp"
    "\${SRC_DIR}/database/db_script_string_journal.cpp"
    "\${SRC_DIR}/database/db_zone_load_context.cpp")
    require_contains(
        _journal_test_target
        "${_marker}"
        "composed runtime target linkage")
endforeach()
foreach(_marker IN ITEMS
    "add_executable(kisakcod-db-script-string-journal-tests"
    "db_script_string_journal_tests.cpp"
    "\${SRC_DIR}/database/db_script_string_journal.cpp"
    "KISAK_DB_SCRIPT_STRING_JOURNAL_TESTING=1"
    "NAME database-script-string-transaction-journal"
    "COMMAND kisakcod-db-script-string-journal-tests"
    "NAME database-script-string-journal-source-invariants"
    "db_script_string_journal_source_test.cmake")
    require_contains(_tests "${_marker}" "CMake runtime/source integration")
endforeach()
foreach(_marker IN ITEMS
    "platform: Linux amd64"
    "platform: Linux arm64"
    "platform: Windows amd64"
    "platform: Windows arm64"
    "platform: macOS arm64"
    "cmake --build build-tests --config Release --parallel"
    "ctest --test-dir build-tests -C Release --output-on-failure"
    "kisakcod-db-script-string-journal-tests"
    "database-script-string-transaction-journal")
    require_contains(_ci "${_marker}" "portable and measured CI integration")
endforeach()

message(STATUS "Script-string journal source invariants passed")
