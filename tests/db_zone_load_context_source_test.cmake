cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_header_path
    "${SOURCE_ROOT}/src/database/db_zone_load_context.h")
set(_source_path
    "${SOURCE_ROOT}/src/database/db_zone_load_context.cpp")
set(_fixture_path
    "${SOURCE_ROOT}/tests/db_zone_load_context_tests.cpp")
set(_manifest_path "${SOURCE_ROOT}/scripts/common_files.cmake")
set(_tests_path "${SOURCE_ROOT}/tests/CMakeLists.txt")
set(_ci_path "${SOURCE_ROOT}/.github/workflows/ci.yml")

foreach(_path IN ITEMS
    "${_header_path}"
    "${_source_path}"
    "${_fixture_path}"
    "${_manifest_path}"
    "${_tests_path}"
    "${_ci_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR
            "Missing zone-load context source: ${_path}")
    endif()
endforeach()

file(READ "${_header_path}" _header)
file(READ "${_source_path}" _source)
file(READ "${_fixture_path}" _fixture)
file(READ "${_manifest_path}" _manifest)
file(READ "${_tests_path}" _tests)
file(READ "${_ci_path}" _ci)

foreach(_var IN ITEMS
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
            "Missing zone-load context invariant (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(require_not_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden zone-load context regression (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(require_ordered SOURCE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${FIRST}" _first)
    string(FIND "${${SOURCE_VAR}}" "${SECOND}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered zone-load context invariant "
            "(${DESCRIPTION})")
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

# This batch is a pure, external primitive. It must not import or call the
# production registry, stream, PMem, reporting, XZone, FX adapter, or script
# string machinery, and it must not allocate or depend on exception unwinding.
foreach(_var IN ITEMS _header _source)
    foreach(_forbidden IN ITEMS
        "database/database.h"
        "database/db_file_load"
        "database/db_registry"
        "database/db_load"
        "qcommon/qcommon.h"
        "EffectsCore/"
        "#include <vector>"
        "#include <string>"
        "std::function"
        "Com_Error("
        "PMem_"
        "DB_PushStreamPos"
        "DB_PopStreamPos"
        "DB_IncStreamPos"
        "DB_AddXAsset"
        "DB_RemoveXAsset"
        "SL_"
        "malloc("
        "calloc("
        "realloc("
        "operator new"
        "throw "
        "catch (")
        require_not_contains(
            ${_var} "${_forbidden}" "production-neutral report-free boundary")
    endforeach()
endforeach()
foreach(_marker IN ITEMS
    "~ZoneLoadContextSlot() noexcept = default;"
    "ZoneLoadContextSlot(const ZoneLoadContextSlot &) = delete;"
    "ZoneLoadContextSlot(ZoneLoadContextSlot &&) = delete;"
    "struct ZoneLoadContextSlotTestAccess;"
    "#ifdef KISAK_DB_ZONE_LOAD_CONTEXT_TESTING"
    "Production callers have no mutation escape hatch"
    "must not throw, longjmp, call Com_Error"
    "a nonlocal exit leaves cleanupActive set"
    "retain that serialization until the controller publishes"
    "Empty and TryFinish/TryUnload returns")
    require_contains(_header "${_marker}" "explicit no-destructor cleanup")
endforeach()
require_ordered(
    _header
    "private:"
    "std::uint64_t generation_ = 0;"
    "controller representation remains private")
require_not_contains(
    _source
    "KISAK_DB_ZONE_LOAD_CONTEXT_TESTING"
    "production source cannot enable test mutation access")

# Freeze the full-width external key, slot representation, and four explicit
# lifecycle phases without embedding any native XZone field.
foreach(_marker IN ITEMS
    "inline constexpr std::uint32_t kInvalidZoneLoadSlot = UINT32_MAX;"
    "struct alignas(8) ZoneLoadContextKey final"
    "std::uint64_t generation = 0;"
    "std::uint32_t slot = kInvalidZoneLoadSlot;"
    "std::uint32_t reserved = 0;"
    "RUNTIME_SIZE(ZoneLoadContextKey, 0x10, 0x10);"
    "enum class ZoneLoadContextPhase : std::uint8_t"
    "Empty,"
    "Loading,"
    "Live,"
    "Abandoning,"
    "class alignas(8) ZoneLoadContextSlot final"
    "RUNTIME_SIZE(ZoneLoadContextSlot, 0x10, 0x10);")
    require_contains(_header "${_marker}" "external generation-keyed schema")
endforeach()
require_not_contains(
    _header
    "XZone *"
    "slot lifecycle cannot embed native XZone")

# Generation zero is never issued. The UINT64_MAX boundary fails closed before
# addition, and a defensive post-addition zero check remains.
foreach(_marker IN ITEMS
    "== (std::numeric_limits<std::uint64_t>::max)()"
    "return ZoneLoadContextStatus::GenerationExhausted;"
    "const std::uint64_t nextGeneration = currentGeneration + 1;"
    "if (nextGeneration == 0)"
    "*outGeneration = nextGeneration;")
    require_contains(_source "${_marker}" "nonzero wrap-safe generation")
endforeach()
require_ordered(
    _source
    "== (std::numeric_limits<std::uint64_t>::max)()"
    "const std::uint64_t nextGeneration = currentGeneration + 1;"
    "wrap check precedes generation addition")
require_ordered(
    _source
    "if (nextGeneration == 0)"
    "*outGeneration = nextGeneration;"
    "zero generation cannot publish")

# Keys bind both slot and generation. Empty terminal receipts are accepted only
# by their matching final operation; the general ownership predicate excludes
# Empty so it cannot become an admission oracle for released state.
foreach(_marker IN ITEMS
    "if (!IsValidKey(key))"
    "return ZoneLoadContextStatus::InvalidKey;"
    "if (key.slot != slotIndex_ || key.generation != generation_)"
    "return ZoneLoadContextStatus::StaleKey;"
    "&& slot->phase_ != ZoneLoadContextPhase::Empty"
    "&& key.slot == slot->slotIndex_"
    "&& key.generation == slot->generation_")
    require_contains(_source "${_marker}" "stale and ABA key rejection")
endforeach()

# Claim/commit/begin/finish/unload each retain an explicit retry-safe phase or
# terminal receipt. Claim publishes the assembled key only after every check.
foreach(_marker IN ITEMS
    "ZoneLoadContextStatus TryClaimZoneLoadContext("
    "if (IsValidKey(*inOutKey))"
    "if (slot->phase_ == ZoneLoadContextPhase::Loading) return ZoneLoadContextStatus::Success;"
    "return slot->phase_ == ZoneLoadContextPhase::Empty ? ZoneLoadContextStatus::StaleKey"
    "const ZoneLoadContextKey candidate{ nextGeneration, slot->slotIndex_, 0};"
    "*inOutKey = candidate;"
    "if (slot->phase_ == ZoneLoadContextPhase::Loading)"
    "if (slot->phase_ == ZoneLoadContextPhase::Live) return ZoneLoadContextStatus::Success;"
    "slot->nextCleanupOperation_ = ZoneLoadCleanupOperation:: RemoveLiveAssetsAndReferences;"
    "slot->terminalKind_ == ZoneLoadTerminalKind::Abandoned"
    "slot->terminalKind_ == ZoneLoadTerminalKind::Unloaded"
    "if (slot->phase_ != ZoneLoadContextPhase::Loading) return ZoneLoadContextStatus::InvalidPhase;"
    "slot->phase_ = ZoneLoadContextPhase::Abandoning;"
    "slot->phase_ = ZoneLoadContextPhase::Empty;")
    require_contains(_source "${_marker}" "idempotent lifecycle transition")
endforeach()
require_ordered(
    _source
    "const ZoneLoadContextKey candidate{ nextGeneration, slot->slotIndex_, 0};"
    "*inOutKey = candidate;"
    "claim output publishes last")
require_not_contains(
    _source
    "slot->phase_ != ZoneLoadContextPhase::Loading && slot->phase_ != ZoneLoadContextPhase::Live"
    "committed Live state cannot enter load-abandonment recipe")

# Loading abandonment and committed Live unload have deliberately different
# no-drop recipes. Live teardown must never replay load-only cancellation,
# adapter abort, PMem EndAlloc, or loading-gate/signal work.
foreach(_marker IN ITEMS
    "CancelLoadInputAndInflate,"
    "AbortNativeAdapterTransactions,"
    "MakePartialAssetsAndStagedReferencesUnreachable,"
    "RemoveLiveAssetsAndReferences,"
    "InvalidateAliasDirectStreamAndDelayState,"
    "ReleaseGeometry,"
    "TearDownNativeArenaWorkspaceAndSidecars,"
    "EndPhysicalMemoryAllocation,"
    "FreePhysicalMemory,"
    "ClearRegistryLoadingQueueGateAndSignal,"
    "RemoveLiveRegistryAndHandles,"
    "ReleaseSlot,")
    require_contains(_header "${_marker}" "mandatory cleanup vocabulary")
endforeach()
foreach(_marker IN ITEMS
    "all load-only work: input/inflate cancellation or completion"
    "A committed Live slot must use TryUnload instead"
    "never replay load-only cancel/abort, PMem EndAlloc"
    "loading-gate/signal")
    require_contains(_header "${_marker}" "phase-specific cleanup contract")
endforeach()
foreach(_marker IN ITEMS
    "case ZoneLoadCleanupOperation::CancelLoadInputAndInflate:"
    "*operation = ZoneLoadCleanupOperation::AbortNativeAdapterTransactions;"
    "case ZoneLoadCleanupOperation::AbortNativeAdapterTransactions:"
    "*operation = ZoneLoadCleanupOperation::MakePartialAssetsAndStagedReferencesUnreachable;"
    "case ZoneLoadCleanupOperation::MakePartialAssetsAndStagedReferencesUnreachable:"
    "*operation = ZoneLoadCleanupOperation::InvalidateAliasDirectStreamAndDelayState;"
    "case ZoneLoadCleanupOperation::InvalidateAliasDirectStreamAndDelayState:"
    "*operation = ZoneLoadCleanupOperation::ReleaseGeometry;"
    "case ZoneLoadCleanupOperation::ReleaseGeometry:"
    "*operation = ZoneLoadCleanupOperation::TearDownNativeArenaWorkspaceAndSidecars;"
    "case ZoneLoadCleanupOperation::TearDownNativeArenaWorkspaceAndSidecars:"
    "*operation = ZoneLoadCleanupOperation::EndPhysicalMemoryAllocation;"
    "case ZoneLoadCleanupOperation::EndPhysicalMemoryAllocation:"
    "*operation = ZoneLoadCleanupOperation::FreePhysicalMemory;"
    "case ZoneLoadCleanupOperation::FreePhysicalMemory:"
    "*operation = ZoneLoadCleanupOperation::ClearRegistryLoadingQueueGateAndSignal;"
    "case ZoneLoadCleanupOperation::ClearRegistryLoadingQueueGateAndSignal:"
    "*operation = ZoneLoadCleanupOperation::ReleaseSlot;"
    "case ZoneLoadCleanupOperation::RemoveLiveAssetsAndReferences:"
    "*operation = ZoneLoadCleanupOperation::RemoveLiveRegistryAndHandles;"
    "case ZoneLoadCleanupOperation::RemoveLiveRegistryAndHandles:"
    "while (nextCleanupOperation_ != ZoneLoadCleanupOperation::ReleaseSlot)"
    "callbacks.perform(callbacks.context, operation);"
    "if (callbackStatus == ZoneLoadCleanupCallbackStatus::Retry)"
    "poisonCleanup();"
    "phase_ = ZoneLoadContextPhase::Empty;")
    require_contains(_source "${_marker}" "phase-specific cleanup recipes")
endforeach()

extract_slice(
    _source
    "[[nodiscard]] bool AdvanceAbandonmentCleanupOperation("
    "[[nodiscard]] bool AdvanceLiveUnloadCleanupOperation("
    _abandonment_advance_slice
    "loading-abandonment operation advancement")
foreach(_first_second IN ITEMS
    "CancelLoadInputAndInflate|AbortNativeAdapterTransactions"
    "AbortNativeAdapterTransactions|MakePartialAssetsAndStagedReferencesUnreachable"
    "MakePartialAssetsAndStagedReferencesUnreachable|InvalidateAliasDirectStreamAndDelayState"
    "InvalidateAliasDirectStreamAndDelayState|ReleaseGeometry"
    "ReleaseGeometry|TearDownNativeArenaWorkspaceAndSidecars"
    "TearDownNativeArenaWorkspaceAndSidecars|EndPhysicalMemoryAllocation"
    "EndPhysicalMemoryAllocation|FreePhysicalMemory"
    "FreePhysicalMemory|ClearRegistryLoadingQueueGateAndSignal"
    "ClearRegistryLoadingQueueGateAndSignal|ReleaseSlot")
    string(REPLACE "|" ";" _pair "${_first_second}")
    list(GET _pair 0 _first)
    list(GET _pair 1 _second)
    require_ordered(
        _abandonment_advance_slice
        "case ZoneLoadCleanupOperation::${_first}:"
        "*operation = ZoneLoadCleanupOperation::${_second};"
        "abandonment transition ${_first} -> ${_second}")
endforeach()

extract_slice(
    _source
    "[[nodiscard]] bool AdvanceLiveUnloadCleanupOperation("
    "[[nodiscard]] bool AdvanceCleanupOperation("
    _unload_advance_slice
    "live-unload operation advancement")
foreach(_first_second IN ITEMS
    "RemoveLiveAssetsAndReferences|InvalidateAliasDirectStreamAndDelayState"
    "InvalidateAliasDirectStreamAndDelayState|ReleaseGeometry"
    "ReleaseGeometry|TearDownNativeArenaWorkspaceAndSidecars"
    "TearDownNativeArenaWorkspaceAndSidecars|FreePhysicalMemory"
    "FreePhysicalMemory|RemoveLiveRegistryAndHandles"
    "RemoveLiveRegistryAndHandles|ReleaseSlot")
    string(REPLACE "|" ";" _pair "${_first_second}")
    list(GET _pair 0 _first)
    list(GET _pair 1 _second)
    require_ordered(
        _unload_advance_slice
        "case ZoneLoadCleanupOperation::${_first}:"
        "*operation = ZoneLoadCleanupOperation::${_second};"
        "live-unload transition ${_first} -> ${_second}")
endforeach()
foreach(_load_only IN ITEMS
    "AbortNativeAdapterTransactions;"
    "MakePartialAssetsAndStagedReferencesUnreachable;"
    "EndPhysicalMemoryAllocation;"
    "ClearRegistryLoadingQueueGateAndSignal;")
    require_not_contains(
        _unload_advance_slice
        "*operation = ZoneLoadCleanupOperation::${_load_only}"
        "live unload cannot advance into load-only work")
endforeach()

extract_slice(
    _source
    "ZoneLoadContextStatus ZoneLoadContextSlot::runCleanup("
    "bool ZoneLoadContextSlot::initialized() const noexcept"
    _cleanup_slice
    "cleanup controller")
require_ordered(
    _cleanup_slice
    "if ((flags_ & kCleanupPoisonedFlag) != 0)"
    "if (!HasCallbacks(callbacks))"
    "poisoned state remains permanently unsafe")
require_ordered(
    _cleanup_slice
    "callbacks.perform(callbacks.context, operation);"
    "if (!IsKnownCallbackStatus(callbackStatus)"
    "callback completes before status validation")
require_ordered(
    _cleanup_slice
    "if (callbackStatus == ZoneLoadCleanupCallbackStatus::Retry)"
    "if (!AdvanceCleanupOperation("
    "Retry cannot advance cleanup")
require_ordered(
    _cleanup_slice
    "if (!AdvanceCleanupOperation("
    "phase_ = ZoneLoadContextPhase::Empty;"
    "all external cleanup completes before slot release")
require_not_contains(
    _cleanup_slice
    "callbacks.perform(callbacks.context, ZoneLoadCleanupOperation::ReleaseSlot)"
    "slot release is controller-owned")
foreach(_marker IN ITEMS
    "case ZoneLoadTerminalKind::Abandoned: return IsAbandonmentCleanupOperation(operation);"
    "case ZoneLoadTerminalKind::Unloaded: return IsLiveUnloadCleanupOperation(operation);"
    "&& IsCleanupOperationForTerminal( terminalKind_, nextCleanupOperation_);"
    "isCleanupActive && isCleanupPoisoned")
    require_contains(_source "${_marker}" "recipe-correlated canonical state")
endforeach()

# The runtime fixture exercises every phase, every retry/unsafe boundary,
# callback reentry, output atomicity, terminal receipts, cross-slot/ABA
# rejection, wrap exhaustion, and corruption.
foreach(_marker IN ITEMS
    "void TestLayoutNoexceptAndInitialization()"
    "void TestClaimCommitAndFailureAtomicity()"
    "void TestLoadingAbandonmentOrderAndTerminalIdempotency()"
    "void TestLiveUnloadOrderAndIdempotency()"
    "void TestRetryAtEveryCleanupBoundary()"
    "void TestLiveUnloadRetryAtEveryCleanupBoundary()"
    "void TestUnsafeCleanupPoisonsEveryBoundary()"
    "void TestLiveUnloadUnsafeCleanupPoisonsEveryBoundary()"
    "void TestInvalidCallbacksDoNotChangeOwnership()"
    "void TestReentrantCleanupFailsBusyWithoutReordering()"
    "void TestKeyMatchesAcrossActiveAndTerminalStates()"
    "void TestStaleCrossSlotAndAbaRejection()"
    "void TestGenerationWrapFailsClosed()"
    "void TestCorruptionAndMalformedKeysFailClosed()"
    "kAbandonmentCleanupOrder"
    "kLiveUnloadCleanupOrder"
    "ZoneLoadContextKey allZero{0, 0, 0};"
    "ZoneLoadContextSlotTestAccess::SetCleanupOperation("
    "ZoneLoadCleanupOperation::ReleaseSlot"
    "kCleanupActiveFlag | kCleanupPoisonedFlag"
    "maximumGeneration - 1"
    "ZoneLoadContextStatus::GenerationExhausted"
    "ZoneLoadContextStatus::StaleKey"
    "ZoneLoadContextStatus::Busy"
    "cleanupPoisoned()"
    "Zone-load context lifecycle tests passed")
    require_contains(_fixture "${_marker}" "exhaustive runtime coverage")
endforeach()

# Keep the primitive in the production source manifest for compile coverage,
# while executing its standalone runtime/source contracts on all portable
# targets and the runtime fixture in measured Windows x86 Debug/Release.
foreach(_marker IN ITEMS
    "\${SRC_DIR}/database/db_zone_load_context.cpp"
    "\${SRC_DIR}/database/db_zone_load_context.h")
    require_contains(_manifest "${_marker}" "production manifest coverage")
endforeach()
foreach(_marker IN ITEMS
    "add_executable(kisakcod-db-zone-load-context-tests"
    "db_zone_load_context_tests.cpp"
    "\${SRC_DIR}/database/db_zone_load_context.cpp"
    "NAME database-zone-load-context-lifecycle"
    "COMMAND kisakcod-db-zone-load-context-tests"
    "NAME database-zone-load-context-source-invariants"
    "db_zone_load_context_source_test.cmake")
    require_contains(_tests "${_marker}" "CMake test integration")
endforeach()
foreach(_marker IN ITEMS
    "kisakcod-db-zone-load-context-tests"
    "database-zone-load-context-lifecycle")
    require_contains(_ci "${_marker}" "measured Windows x86 CI integration")
endforeach()

message(STATUS "Zone-load context source invariants passed")
