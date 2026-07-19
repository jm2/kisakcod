cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_header_path "${SOURCE_ROOT}/src/database/db_zone_runtime_storage.h")
set(_source_path "${SOURCE_ROOT}/src/database/db_zone_runtime_storage.cpp")
set(_bridge_header_path
    "${SOURCE_ROOT}/src/database/db_zone_runtime_storage_fx_bridge.h")
set(_bridge_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_zone_runtime_storage_bridge.cpp")
set(_fixture_path "${SOURCE_ROOT}/tests/db_zone_runtime_storage_tests.cpp")
set(_manifest_path "${SOURCE_ROOT}/scripts/common_files.cmake")
set(_tests_path "${SOURCE_ROOT}/tests/CMakeLists.txt")
set(_registry_path "${SOURCE_ROOT}/src/database/db_registry.cpp")
set(_load_path "${SOURCE_ROOT}/src/database/db_load.cpp")

foreach(_path IN ITEMS
    "${_header_path}"
    "${_source_path}"
    "${_bridge_header_path}"
    "${_bridge_source_path}"
    "${_fixture_path}"
    "${_manifest_path}"
    "${_tests_path}"
    "${_registry_path}"
    "${_load_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing zone runtime storage source: ${_path}")
    endif()
endforeach()

file(READ "${_header_path}" _header)
file(READ "${_source_path}" _source)
file(READ "${_bridge_header_path}" _bridge_header)
file(READ "${_bridge_source_path}" _bridge_source)
file(READ "${_fixture_path}" _fixture)
file(READ "${_manifest_path}" _manifest)
file(READ "${_tests_path}" _tests)
file(READ "${_registry_path}" _registry)
file(READ "${_load_path}" _load)
foreach(_var IN ITEMS
    _header _source _bridge_header _bridge_source _fixture _manifest _tests
    _registry _load)
    string(REGEX REPLACE "[ \t\r\n]+" " " _normalized "${${_var}}")
    set(${_var} "${_normalized}")
endforeach()

function(require_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing runtime-storage invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_not_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden runtime-storage regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_ordered SOURCE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${FIRST}" _first)
    string(FIND "${${SOURCE_VAR}}" "${SECOND}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered runtime-storage invariant (${DESCRIPTION})")
    endif()
endfunction()

# The public layout remains fixed-width and has no generation or allocation
# receipt authority. Those capabilities belong to the future outer controller.
foreach(_marker IN ITEMS
    "std::uint32_t offset = 0;"
    "std::uint32_t byteCount = 0;"
    "std::uint32_t totalBytes = 0;"
    "std::uint64_t arenaBudget"
    "class ZoneRuntimeStorageBinding final"
    "ZoneRuntimeStorageBinding(const ZoneRuntimeStorageBinding &) = delete;"
    "ZoneRuntimeStorageBinding(ZoneRuntimeStorageBinding &&) = delete;"
    "~ZoneRuntimeStorageBinding() noexcept;"
    "const ZoneRuntimeStorageBinding *self_ = nullptr;"
    "TryPlanZoneRuntimeStorage("
    "TryBindZoneRuntimeStorage("
    "TryDestroyZoneRuntimeStorage(")
    require_contains(_header "${_marker}" "public exact-layout contract")
endforeach()
foreach(_forbidden IN ITEMS
    "physicalmemory_checked"
    "PhysicalMemoryAllocationReceipt"
    "generation_"
    "zoneIdentity_")
    require_not_contains(_header "${_forbidden}" "no duplicate authority")
    require_not_contains(_source "${_forbidden}" "no duplicate authority")
endforeach()
require_not_contains(
    _header "ZoneLoadContextKey" "no generation-key field or public parameter")

# Planning checks count, budget width, every aligned extent, and commits the
# local complete plan only on success.
foreach(_marker IN ITEMS
    "kMaxScriptStringJournalEntries"
    "arenaBudget == 0"
    "arenaBudget > UINT32_MAX"
    "IsRangeRepresentable(outPlan, sizeof(*outPlan))"
    "TryAppendExtent(&cursor, sizeof(Journal)"
    "TryAppendExtent(&cursor, entryBytes"
    "fxLayout.arenaBytes"
    "fxLayout.arenaAlignment"
    "fxLayout.workspaceBytes"
    "fxLayout.workspaceAlignment"
    "fxLayout.backingAlignment"
    "plan.totalBytes = cursor;"
    "*outPlan = plan;")
    require_contains(_source "${_marker}" "checked canonical planning")
endforeach()
require_ordered(
    _source "ZoneRuntimeStoragePlan plan{};" "*outPlan = plan;"
    "plan output commits last")

# Binding snapshots an aliasing plan, validates the complete slab and stable
# external output before placement, constructs in ownership order, then
# publishes the handle only after all no-throw construction.
foreach(_marker IN ITEMS
    "const ZoneRuntimeStoragePlan planSnapshot = *plan;"
    "IsRangeRepresentable(slab, slabCapacity)"
    "slabCapacity < planSnapshot.totalBytes"
    "RangesOverlap( slab, slabCapacity, outBinding, sizeof(*outBinding))"
    "!outBinding->isPristine()"
    "Journal *const journal = ::new"
    "::new (entries + index) JournalEntry{};"
    "Arena *const arena = detail::ConstructFxRuntimeArena("
    "Workspace *const workspace = detail::ConstructFxRuntimeWorkspace("
    "outBinding->self_ = outBinding;"
    "hasCanonicalBoundMetadata()"
    "State::Bound")
    require_contains(_source "${_marker}" "failure-atomic placement binding")
endforeach()
require_ordered(
    _source
    "RangesOverlap( plan, sizeof(*plan), outBinding, sizeof(*outBinding))"
    "const ZoneRuntimeStoragePlan planSnapshot = *plan;"
    "plan/output overlap rejects before plan snapshot")
require_ordered(
    _source "const ZoneRuntimeStoragePlan planSnapshot = *plan;"
    "Journal *const journal = ::new"
    "plan snapshot precedes slab writes")
require_ordered(
    _source "!LayoutAddressesAreAligned(slab, planSnapshot, fxLayout)"
    "Journal *const journal = ::new"
    "all absolute alignments validate before slab writes")
require_ordered(
    _source "Workspace *const workspace = detail::ConstructFxRuntimeWorkspace("
    "outBinding->self_ = outBinding;"
    "binding publishes only after construction")
require_ordered(
    _source
    "IsRangeRepresentable(binding, sizeof(*binding))"
    "binding->destroyed()"
    "binding address is validated before terminal-state access")

# Teardown validates every observable lifetime gate before TryUnbind, makes it
# the sole fallible mutation, destroys in exact reverse order, and retains a
# self-authenticating terminal handle for idempotent repeats.
foreach(_marker IN ITEMS
    "binding->destroyed()"
    "return Status::AlreadyComplete;"
    "JournalCanBeDestroyed(*binding->journal_)"
    "detail::TryPrepareFxRuntimeStorageDestroy("
    "detail::DestroyFxRuntimeWorkspace(binding->workspace_);"
    "detail::DestroyFxRuntimeArena(binding->arena_);"
    "binding->entries_[index - 1].~ScriptStringJournalEntry();"
    "binding->journal_->~ScriptStringJournal();"
    "State::Destroyed")
    require_contains(_source "${_marker}" "ordered checked destruction")
endforeach()

# This foundation is compiled and tested, but no legacy loader may call it
# before the exact-key PMem-owning coordinator lands.
foreach(_marker IN ITEMS
    "db_zone_runtime_storage.cpp"
    "db_zone_runtime_storage.h"
    "db_zone_runtime_storage_fx_bridge.h"
    "fx_zone_runtime_storage_bridge.cpp")
    require_contains(_manifest "${_marker}" "production source enrollment")
endforeach()

# The bridge, not the database translation unit, owns complete EffectsCore
# types. It authenticates callback activity and exact arena backing before the
# sole fallible unbind mutation.
foreach(_forbidden IN ITEMS
    "#include <EffectsCore/"
    "#include \"EffectsCore/")
    require_not_contains(
        _source "${_forbidden}" "headless-neutral database implementation")
endforeach()
foreach(_marker IN ITEMS
    "readyForDestruction()"
    "arena->storage() != expectedBacking"
    "arena->openTransactionDepth() != 0"
    "arena->TryUnbind()"
    "DestroyFxRuntimeWorkspace"
    "DestroyFxRuntimeArena")
    require_contains(
        _bridge_source "${_marker}" "exact FX bridge teardown")
endforeach()
foreach(_legacy IN ITEMS _registry _load)
    foreach(_forbidden IN ITEMS
        "#include <database/db_zone_runtime_storage.h>"
        "TryPlanZoneRuntimeStorage("
        "TryBindZoneRuntimeStorage("
        "TryDestroyZoneRuntimeStorage(")
        require_not_contains(
            ${_legacy} "${_forbidden}" "no premature legacy callsite")
    endforeach()
endforeach()

file(GLOB_RECURSE _all_production_sources
    "${SOURCE_ROOT}/src/*.c"
    "${SOURCE_ROOT}/src/*.cc"
    "${SOURCE_ROOT}/src/*.cpp"
    "${SOURCE_ROOT}/src/*.h"
    "${SOURCE_ROOT}/src/*.hpp")
foreach(_path IN LISTS _all_production_sources)
    if("${_path}" STREQUAL "${_header_path}"
        OR "${_path}" STREQUAL "${_source_path}"
        OR "${_path}" STREQUAL "${_bridge_header_path}"
        OR "${_path}" STREQUAL "${_bridge_source_path}")
        continue()
    endif()
    file(READ "${_path}" _production_source)
    string(REGEX REPLACE "[ \t\r\n]+" " " _production_source
        "${_production_source}")
    foreach(_forbidden IN ITEMS
        "#include <database/db_zone_runtime_storage.h>"
        "TryPlanZoneRuntimeStorage("
        "TryBindZoneRuntimeStorage("
        "TryDestroyZoneRuntimeStorage("
        "#include <database/db_zone_runtime_storage_fx_bridge.h>"
        "GetFxRuntimeStorageLayout("
        "ConstructFxRuntimeArena("
        "ConstructFxRuntimeWorkspace("
        "TryPrepareFxRuntimeStorageDestroy("
        "DestroyFxRuntimeWorkspace("
        "DestroyFxRuntimeArena(")
        string(FIND "${_production_source}" "${_forbidden}" _position)
        if(NOT _position EQUAL -1)
            file(RELATIVE_PATH _relative "${SOURCE_ROOT}/src" "${_path}")
            message(FATAL_ERROR
                "Premature runtime-storage production caller in ${_relative}: "
                "'${_forbidden}'")
        endif()
    endforeach()
endforeach()

foreach(_marker IN ITEMS
    "kisakcod-db-zone-runtime-storage-tests"
    "db_zone_runtime_storage_tests.cpp"
    "NAME database-zone-runtime-storage-layout")
    require_contains(_tests "${_marker}" "runtime fixture registration")
endforeach()
foreach(_marker IN ITEMS
    "kMaxScriptStringJournalEntries"
    "0xBD048u"
    "0xC34C8u"
    "maxBudget + 1u"
    "Status::OverlappingStorage"
    "reinterpret_cast<const Plan *>(&output)"
    "+ alignof(Plan)"
    "Plan *const aliased"
    "Status::AlreadyComplete"
    "TryBeginTransaction"
    "nearMaximumPlan"
    "nearMaximumBinding"
    "ReenterDestroyFromSpanOracle"
    "foreign.data()"
    "ScriptStringJournalTestAccess::SetFlags"
    "SetFlags(value, 0x82)"
    "ScriptStringJournalPhase::Committed"
    "ScriptStringJournalPhase::RolledBack")
    require_contains(_fixture "${_marker}" "boundary and lifetime coverage")
endforeach()

foreach(_marker IN ITEMS
    "journal.readyForDestruction()"
    "LayoutAddressesAreAligned("
    "externally serialize planning, binding"
    "every published consumer must already be"
    "TryInitializeScriptStringJournal with exactly scriptStringEntries()")
    if(_marker MATCHES "^(journal|Layout)")
        require_contains(_source "${_marker}" "exact journal teardown gate")
    else()
        require_contains(_header "${_marker}" "external lifetime precondition")
    endif()
endforeach()
