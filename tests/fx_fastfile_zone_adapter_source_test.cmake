cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_arena_header_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_fastfile_native_arena.h")
set(_arena_impl_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_fastfile_native_arena.cpp")
set(_adapter_header_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_fastfile_zone_adapter_disk32.h")
set(_adapter_impl_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_fastfile_zone_adapter_disk32.cpp")
set(_db_load_path "${SOURCE_ROOT}/src/database/db_load.cpp")
set(_manifest_path "${SOURCE_ROOT}/scripts/common_files.cmake")
set(_tests_path "${SOURCE_ROOT}/tests/CMakeLists.txt")
set(_ci_path "${SOURCE_ROOT}/.github/workflows/ci.yml")

foreach(_path IN ITEMS
    "${_arena_header_path}"
    "${_arena_impl_path}"
    "${_adapter_header_path}"
    "${_adapter_impl_path}"
    "${_db_load_path}"
    "${_manifest_path}"
    "${_tests_path}"
    "${_ci_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing FX zone adapter source: ${_path}")
    endif()
endforeach()

file(READ "${_arena_header_path}" _arena_header)
file(READ "${_arena_impl_path}" _arena_impl)
file(READ "${_adapter_header_path}" _adapter_header)
file(READ "${_adapter_impl_path}" _adapter_impl)
file(READ "${_db_load_path}" _db_load)
file(READ "${_manifest_path}" _manifest)
file(READ "${_tests_path}" _tests)
file(READ "${_ci_path}" _ci)

foreach(_var IN ITEMS
    _arena_header
    _arena_impl
    _adapter_header
    _adapter_impl
    _db_load
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
            "Missing FX zone adapter invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_not_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden FX zone adapter regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_occurrence_count SOURCE_VAR NEEDLE EXPECTED DESCRIPTION)
    string(LENGTH "${NEEDLE}" _needle_length)
    if(_needle_length EQUAL 0)
        message(FATAL_ERROR "Cannot count an empty invariant: ${DESCRIPTION}")
    endif()
    string(LENGTH "${${SOURCE_VAR}}" _source_length)
    string(REPLACE "${NEEDLE}" "" _without "${${SOURCE_VAR}}")
    string(LENGTH "${_without}" _without_length)
    math(EXPR _removed_length "${_source_length} - ${_without_length}")
    math(EXPR _count "${_removed_length} / ${_needle_length}")
    if(NOT _count EQUAL EXPECTED)
        message(FATAL_ERROR
            "Wrong FX zone adapter invariant count (${DESCRIPTION}): "
            "expected ${EXPECTED}, found ${_count} for '${NEEDLE}'")
    endif()
endfunction()

function(require_ordered SOURCE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${FIRST}" _first)
    if(_first EQUAL -1)
        message(FATAL_ERROR
            "Missing first FX zone adapter invariant (${DESCRIPTION}): "
            "'${FIRST}'")
    endif()
    string(FIND "${${SOURCE_VAR}}" "${SECOND}" _second)
    if(_second EQUAL -1)
        message(FATAL_ERROR
            "Missing second FX zone adapter invariant (${DESCRIPTION}): "
            "'${SECOND}'")
    endif()
    if(NOT _first LESS _second)
        message(FATAL_ERROR
            "Reordered FX zone adapter invariant (${DESCRIPTION}): "
            "'${FIRST}' must precede '${SECOND}'")
    endif()
endfunction()

# --- Zone-owned aligned native arena -----------------------------------------

require_contains(_arena_header
    "RUNTIME_SIZE(FxFastFileNativeArenaTransaction, 0x18, 0x18);"
    "exact arena transaction token layout")
require_contains(_arena_header
    "RUNTIME_SIZE(FxFastFileNativeArena, 0x58, 0x58);"
    "exact arena layout")
require_contains(_arena_header
    "FxFastFileNativeArena(const FxFastFileNativeArena &) = delete;"
    "the arena is noncopyable")
require_contains(_arena_header
    "inline constexpr std::uint32_t kFxFastFileNativeArenaMaxTransactionDepth = 2;"
    "exact LIFO transaction depth for impact-nested effects")
require_contains(_arena_impl
    "transactionBase > committed_ ? transactionBase : committed_"
    "abandonment reclaims only above the committed watermark")
require_ordered(_arena_impl
    "committed_ = cursor_;"
    "const std::uint64_t transactionBase ="
    "commit ratchets the watermark; abandon honors it afterwards")
require_contains(_arena_impl
    "return value != 0 && (value & (value - 1u)) == 0;"
    "power-of-two alignment validation")
require_contains(_arena_impl
    "if (storage_ || transactionDepth_ != 0) return Status::InvalidPhase;"
    "rebinding requires the explicit lifetime-checked unbind boundary")

# --- Guarded stateful zone adapter --------------------------------------------

require_contains(_adapter_header
    "RUNTIME_SIZE(FxFastFileZoneAdapterDisk32Workspace, 0xBD048, 0xC34C8);"
    "exact adapter workspace layout")
require_contains(_adapter_header
    "std::is_nothrow_default_constructible_v< FxFastFileZoneAdapterDisk32Workspace>"
    "heap-only workspace stays nothrow-constructible")
require_contains(_adapter_header
    "kFxFastFileDisk32MaxResolvedReferences + kFxFastFileImpactDisk32JournalCount;"
    "recording capacity covers one impact plus one nested effect")

# A depth-zero workspace is composable only at ResetRecordingState's complete
# stable boundary. In particular, stale journal counts cannot become base
# indices for the next top-level transaction, and dormant frames cannot retain
# an arena token or source topology while phase() reports Idle.
foreach(_marker IN ITEMS
    "readyForCompositionAuthentication() const noexcept"
    "operating_ || frameDepth_ != 0 || arena_ != nullptr"
    "cursor_.context != nullptr || cursor_.validateWireSpan != nullptr"
    "referenceCount_ != 0 || spanCount_ != 0 || resolveHint_ != 0"
    "spanHint_ != 0"
    "effectConverter_.phase() != FxFastFileNativeDisk32Phase::Empty"
    "impactConverter_.phase() != FxFastFileNativeDisk32Phase::Empty"
    "for (const Frame &frame : frames_)"
    "frame.kind != FrameKind::None"
    "frame.state != FxFastFileZoneAdapterDisk32Phase::Idle"
    "frame.stage != ElementStage::Velocity"
    "frame.effectHeader != nullptr || frame.impactHeader != nullptr"
    "frame.elements != nullptr || frame.entries != nullptr"
    "frame.elementCount != 0 || frame.elementIndex != 0"
    "frame.visualSlot != 0 || frame.effectRefSlot != 0"
    "frame.impactSlot != 0 || frame.referenceBase != 0"
    "frame.spanBase != 0"
    "frame.arenaTransaction != FxFastFileNativeArenaTransaction{}")
    require_contains(
        _adapter_header "${_marker}"
        "exact stable workspace composition authentication")
endforeach()

# Publication ordering: materialize, commit the arena reservation, then (and
# only then) invoke the publication sink, for both asset families.
require_ordered(_adapter_impl
    "converterStatus = TryMaterializeFxEffectDefDisk32("
    "publication.publishEffect( publication.context, effect, &publishedEffect)"
    "effects publish only after complete materialization")
require_ordered(_adapter_impl
    "arenaStatus = workspace->arena_->TryCommit(frame.arenaTransaction);"
    "publication.publishEffect( publication.context, effect, &publishedEffect)"
    "a rejected effect publication strands only retired storage")
require_ordered(_adapter_impl
    "converterStatus = TryMaterializeFxImpactTableDisk32("
    "publication.publishImpact( publication.context, table, &publishedTable)"
    "impact tables publish only after complete materialization")
require_contains(_adapter_impl
    "handle.resolution.pointer = publishedEffect;"
    "nested impact handles bind the canonical effect identity")
require_contains(_adapter_impl
    "*outEffect = publishedEffect;"
    "effect callers receive the canonical publication identity")
require_contains(_adapter_impl
    "*outTable = publishedTable;"
    "impact callers receive the canonical publication identity")

# Failure teardown must structurally reset both pure converter workspaces so
# a poisoned Planned phase can never leak into the next transaction.
require_contains(_adapter_impl
    "workspace.effectConverter_.~FxFastFileNativeDisk32Workspace();"
    "teardown reconstructs the effect converter workspace")
require_contains(_adapter_impl
    "workspace.impactConverter_.~FxFastFileImpactNativeDisk32Workspace();"
    "teardown reconstructs the impact converter workspace")

# Every recorded resolution must be consumed exactly once by the frozen
# resolver schedule; leftovers mean expectation drift and must fail closed.
require_contains(_adapter_impl
    "if (!workspace->references_[index].consumed)"
    "unconsumed recorded resolutions fail closed")

# Nested effect transactions require a legacy inline sentinel in the pending
# impact slot; alias tokens must never open a nested conversion.
require_contains(_adapter_impl
    "!(pendingField->isInline() || pendingField->isSharedInline())"
    "nested effects require an inline sentinel slot")
require_contains(_adapter_impl
    "if (!sourceField->isOffset())"
    "alias handle reports require a real offset token")

# Every reported wire string validates its extent against the cursor oracle
# before any byte of it is inspected, in all three string recorders.
require_occurrence_count(_adapter_impl
    "if (!OracleValidates(workspace->cursor_, name, nameBytes)) return Workspace::FailTransaction(*workspace, Status::InvalidSpan); if (!IsExactWireCString(name, nameBytes)) return Workspace::FailTransaction(*workspace, Status::InvalidString);"
    3
    "string extents validate through the oracle before content inspection")

# Sound names are retained (not copied) by the converter, so the adapter must
# copy them into the zone-owned arena inside the open transaction.
require_ordered(_adapter_impl
    "workspace->arena_->TryReserve( frame.arenaTransaction, static_cast<std::size_t>(nameBytes), 1, &copy);"
    "std::memcpy(copy, name, static_cast<std::size_t>(nameBytes));"
    "sound names become arena-owned before resolution")

# The adapter and arena stay report-free.
foreach(_needle IN ITEMS "Com_Error" "Com_Printf" "printf" "assert(")
    require_not_contains(_arena_impl "${_needle}"
        "the arena is report-free")
    require_not_contains(_adapter_impl "${_needle}"
        "the adapter is report-free")
endforeach()

# The legacy x86 in-place fixup path remains the untouched compatibility
# boundary in this batch: db_load.cpp still owns the sentinel walk and gains
# no adapter wiring until the whole-zone ownership/rollback batch lands.
require_contains(_db_load "Load_FxEffectDefHandle"
    "the legacy loader keeps the FX sentinel walk")
require_contains(_db_load "Load_FxImpactTablePtr"
    "the legacy loader keeps the impact sentinel walk")
require_not_contains(_db_load "ZoneDisk32"
    "production wiring belongs to the later ownership/rollback batch")

# --- Build/test/CI wiring ------------------------------------------------------

foreach(_entry IN ITEMS
    "EffectsCore/fx_fastfile_native_arena.cpp"
    "EffectsCore/fx_fastfile_native_arena.h"
    "EffectsCore/fx_fastfile_zone_adapter_disk32.cpp"
    "EffectsCore/fx_fastfile_zone_adapter_disk32.h")
    require_contains(_manifest "${_entry}"
        "the production manifest owns the new sources")
endforeach()

foreach(_entry IN ITEMS
    "kisakcod-fx-fastfile-native-arena-subject"
    "kisakcod-fx-fastfile-native-arena-tests"
    "kisakcod-fx-fastfile-zone-adapter-disk32-subject"
    "kisakcod-fx-fastfile-zone-adapter-disk32-tests"
    "effectscore-fastfile-native-arena"
    "effectscore-fastfile-zone-adapter-disk32"
    "fx_fastfile_zone_adapter_source_test.cmake")
    require_contains(_tests "${_entry}"
        "the portable test wiring registers the new suites")
endforeach()

require_contains(_ci "effectscore-fastfile-native-arena"
    "measured Windows x86 CI runs the arena suite")
require_contains(_ci "effectscore-fastfile-zone-adapter-disk32"
    "measured Windows x86 CI runs the adapter suite")
require_contains(_ci "kisakcod-fx-fastfile-native-arena-tests"
    "the measured Windows x86 build step compiles the arena suite")
require_contains(_ci "kisakcod-fx-fastfile-zone-adapter-disk32-tests"
    "the measured Windows x86 build step compiles the adapter suite")

message(STATUS "FX fast-file zone adapter source invariants hold")
