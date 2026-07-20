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
set(_headless_bridge_source_path
    "${SOURCE_ROOT}/src/database/db_zone_runtime_storage_fx_bridge_headless.cpp")
set(_arena_header_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_fastfile_native_arena.h")
set(_fixture_path "${SOURCE_ROOT}/tests/db_zone_runtime_storage_tests.cpp")
set(_runtime_table_seal_path
    "${SOURCE_ROOT}/tests/db_zone_runtime_table_source_test.cmake")
set(_manifest_path "${SOURCE_ROOT}/scripts/common_files.cmake")
set(_dedi_sources_path "${SOURCE_ROOT}/scripts/dedi/dedi_sources.cmake")
set(_headless_profile_path "${SOURCE_ROOT}/tests/headless_profile_test.cmake")
set(_tests_path "${SOURCE_ROOT}/tests/CMakeLists.txt")
set(_ci_path "${SOURCE_ROOT}/.github/workflows/ci.yml")
set(_registry_path "${SOURCE_ROOT}/src/database/db_registry.cpp")
set(_load_path "${SOURCE_ROOT}/src/database/db_load.cpp")
set(_integer_suffix_token_paste_path
    "${SOURCE_ROOT}/src/universal/q_shared.h")
set(_server_token_literal_path
    "${SOURCE_ROOT}/src/server_mp/sv_client_mp.cpp")
set(_ui_component_token_literal_path
    "${SOURCE_ROOT}/src/ui/ui_component.cpp")
set(_ui_parser_token_literal_path
    "${SOURCE_ROOT}/src/ui/ui_shared_obj.cpp")

foreach(_path IN ITEMS
    "${_header_path}"
    "${_source_path}"
    "${_bridge_header_path}"
    "${_bridge_source_path}"
    "${_headless_bridge_source_path}"
    "${_arena_header_path}"
    "${_fixture_path}"
    "${_runtime_table_seal_path}"
    "${_manifest_path}"
    "${_dedi_sources_path}"
    "${_headless_profile_path}"
    "${_tests_path}"
    "${_ci_path}"
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
file(READ "${_headless_bridge_source_path}" _headless_bridge_source)
file(READ "${_arena_header_path}" _arena_header)
file(READ "${_fixture_path}" _fixture)
file(READ "${_runtime_table_seal_path}" _runtime_table_seal)
file(READ "${_manifest_path}" _manifest)
file(READ "${_dedi_sources_path}" _dedi_sources)
file(READ "${_headless_profile_path}" _headless_profile)
file(READ "${_tests_path}" _tests)
file(READ "${_ci_path}" _ci)
file(READ "${_registry_path}" _registry)
file(READ "${_load_path}" _load)
foreach(_var IN ITEMS
    _header _source _bridge_header _bridge_source _headless_bridge_source
    _arena_header _fixture _runtime_table_seal _manifest _dedi_sources
    _headless_profile _tests _ci _registry _load)
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

foreach(_marker IN ITEMS
    "bool readyForCompositionAuthentication() const noexcept"
    "operating_ || nextSerial_ == 0 || transactionDepth_ != 0"
    "depth < kFxFastFileNativeArenaMaxTransactionDepth"
    "transactionSerials_[depth] != 0"
    "transactionBases_[depth] != 0")
    require_contains(
        _arena_header "${_marker}" "stable native-arena authentication gate")
endforeach()

# Headless links the database-neutral storage implementation but deliberately
# cannot construct EffectsCore objects yet. Its private bridge must make
# planning and every direct lifecycle operation fail closed, and both the
# source profile and its profile test pin the complete pair.
foreach(_marker IN ITEMS
    "#if !defined(KISAK_DEDI_HEADLESS)"
    "return {};"
    "return FxRuntimeStorageBindStatus::InvalidPhase;"
    "return false;"
    "return FxRuntimeStorageDestroyStatus::InvalidBinding;")
    require_contains(
        _headless_bridge_source "${_marker}" "fail-closed headless bridge")
endforeach()
foreach(_marker IN ITEMS
    "\${SRC_DIR}/database/db_zone_runtime_storage.cpp"
    "\${SRC_DIR}/database/db_zone_runtime_storage_fx_bridge_headless.cpp")
    require_contains(
        _dedi_sources "${_marker}" "headless storage link closure")
    require_contains(
        _headless_profile "${_marker}" "headless storage profile seal")
endforeach()

# Runtime-table production enrollment is intentionally owned by its more
# specific exact-controller seal. Keep this delegation explicit rather than
# maintaining a second, inevitably divergent callsite allowlist here.
foreach(_marker IN ITEMS
    "set(_storage_receipt_header_path"
    "class ZoneRuntimeStorageBinding final"
    "zone_runtime_storage::TryBindZoneRuntimeStorage("
    "database-zone-runtime-table-source-invariants")
    if(_marker MATCHES "^database-")
        require_contains(
            _tests "${_marker}" "dedicated runtime-table seal registration")
    else()
        require_contains(
            _runtime_table_seal "${_marker}"
            "dedicated runtime-table storage enrollment seal")
    endif()
endforeach()

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

function(require_substring_count SOURCE_VAR NEEDLE EXPECTED DESCRIPTION)
    set(_remaining "${${SOURCE_VAR}}")
    set(_count 0)
    while(TRUE)
        string(FIND "${_remaining}" "${NEEDLE}" _position)
        if(_position EQUAL -1)
            break()
        endif()
        math(EXPR _count "${_count} + 1")
        string(LENGTH "${NEEDLE}" _length)
        math(EXPR _next "${_position} + ${_length}")
        string(SUBSTRING "${_remaining}" ${_next} -1 _remaining)
    endwhile()
    if(NOT _count EQUAL EXPECTED)
        message(FATAL_ERROR
            "Wrong runtime-storage invariant count (${DESCRIPTION}): "
            "'${NEEDLE}' appeared ${_count} times, expected ${EXPECTED}")
    endif()
endfunction()

string(ASCII 92 _runtime_storage_backslash)
string(ASCII 13 _runtime_storage_carriage_return)
string(ASCII 10 _runtime_storage_line_feed)
set(_runtime_storage_block_comment "/\\*([^*]|\\*+[^*/])*\\*+/")
set(_runtime_storage_comment_atom
    "([ \t\r\n]|${_runtime_storage_block_comment}|//[^\r\n]*)")
set(_runtime_storage_comment_gap "${_runtime_storage_comment_atom}*")
set(_runtime_storage_comment_separator "${_runtime_storage_comment_atom}+")

set(_runtime_storage_protected_tokens
    db_zone_runtime_storage
    db_zone_runtime_storage_fx_bridge
    zone_runtime_storage
    ZoneRuntimeStorageExtent
    ZoneRuntimeStoragePlan
    ZoneRuntimeStorageStatus
    ZoneRuntimeStorageBindingPhase
    ZoneRuntimeStorageCompositionExpectation
    ZoneRuntimeStorageCompositionMode
    ZoneRuntimeStorageBinding
    FxRuntimeStorageLayout
    FxRuntimeStorageBindStatus
    FxRuntimeStorageDestroyStatus
    TryPlanZoneRuntimeStorage
    TryBindZoneRuntimeStorage
    TryDestroyZoneRuntimeStorage
    AuthenticateZoneRuntimeStorageBinding
    AuthenticateZoneRuntimeStorageComposition
    GetFxRuntimeStorageLayout
    ConstructFxRuntimeArena
    ConstructFxRuntimeWorkspace
    TryBindFxRuntimeStorage
    AuthenticateStableFxRuntimeStorage
    TryPrepareFxRuntimeStorageDestroy
    DestroyFxRuntimeWorkspace
    DestroyFxRuntimeArena)

function(normalize_runtime_storage_phase2 SOURCE_VAR OUT_VAR)
    set(_candidate "${${SOURCE_VAR}}")
    string(REPLACE
        "${_runtime_storage_backslash}${_runtime_storage_carriage_return}${_runtime_storage_line_feed}"
        "" _candidate "${_candidate}")
    string(REPLACE
        "${_runtime_storage_backslash}${_runtime_storage_line_feed}"
        "" _candidate "${_candidate}")
    string(REPLACE
        "${_runtime_storage_backslash}${_runtime_storage_carriage_return}"
        "" _candidate "${_candidate}")
    set(${OUT_VAR} "${_candidate}" PARENT_SCOPE)
endfunction()

function(runtime_storage_source_has_identifier SOURCE_VAR IDENTIFIER OUT_VAR)
    string(REGEX MATCH
        "(^|[^A-Za-z0-9_])${IDENTIFIER}([^A-Za-z0-9_]|$)"
        _match "${${SOURCE_VAR}}")
    if(_match STREQUAL "")
        set(${OUT_VAR} FALSE PARENT_SCOPE)
    else()
        set(${OUT_VAR} TRUE PARENT_SCOPE)
    endif()
endfunction()

function(runtime_storage_source_has_namespace_declaration SOURCE_VAR OUT_VAR)
    string(REGEX MATCH
        "(^|[^A-Za-z0-9_])namespace${_runtime_storage_comment_separator}((db${_runtime_storage_comment_gap}::${_runtime_storage_comment_gap})?zone_runtime_storage)(${_runtime_storage_comment_gap}::|${_runtime_storage_comment_gap}\\{)"
        _match "${${SOURCE_VAR}}")
    if(_match STREQUAL "")
        set(${OUT_VAR} FALSE PARENT_SCOPE)
    else()
        set(${OUT_VAR} TRUE PARENT_SCOPE)
    endif()
endfunction()

function(remove_reviewed_runtime_storage_token_text PATH SOURCE_VAR OUT_VAR)
    set(_candidate "${${SOURCE_VAR}}")
    if(PATH STREQUAL _integer_suffix_token_paste_path)
        string(REPLACE "num ## LL" "" _candidate "${_candidate}")
        string(REPLACE "num ## i64" "" _candidate "${_candidate}")
    elseif(PATH STREQUAL _server_token_literal_path)
        string(REPLACE
            "\"###!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!###\\n\""
            "" _candidate "${_candidate}")
        string(REPLACE
            "\"########################################\\n\""
            "" _candidate "${_candidate}")
    elseif(PATH STREQUAL _ui_component_token_literal_path)
        string(REPLACE "\"##\"" "" _candidate "${_candidate}")
    elseif(PATH STREQUAL _ui_parser_token_literal_path)
        string(REPLACE "\"##\"" "" _candidate "${_candidate}")
        string(REPLACE
            "\"define with misplaced ##\"" "" _candidate "${_candidate}")
    endif()
    set(${OUT_VAR} "${_candidate}" PARENT_SCOPE)
endfunction()

function(runtime_storage_source_has_token_paste SOURCE_VAR OUT_VAR)
    foreach(_operator IN ITEMS "##" "%:%:" "??/" "??=")
        string(FIND "${${SOURCE_VAR}}" "${_operator}" _position)
        if(NOT _position EQUAL -1)
            set(${OUT_VAR} TRUE PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${OUT_VAR} FALSE PARENT_SCOPE)
endfunction()

function(remove_one_reviewed_storage_construct
    SOURCE_VAR CONSTRUCT OUT_VAR DESCRIPTION)
    set(_candidate "${${SOURCE_VAR}}")
    set(_remaining "${_candidate}")
    set(_count 0)
    while(TRUE)
        string(FIND "${_remaining}" "${CONSTRUCT}" _position)
        if(_position EQUAL -1)
            break()
        endif()
        math(EXPR _count "${_count} + 1")
        string(LENGTH "${CONSTRUCT}" _length)
        math(EXPR _next "${_position} + ${_length}")
        string(SUBSTRING "${_remaining}" ${_next} -1 _remaining)
    endwhile()
    if(NOT _count EQUAL 1)
        message(FATAL_ERROR
            "Expected exactly one reviewed runtime-storage construct "
            "(${DESCRIPTION}); found ${_count}")
    endif()
    string(REPLACE "${CONSTRUCT}" "" _candidate "${_candidate}")
    set(${OUT_VAR} "${_candidate}" PARENT_SCOPE)
endfunction()

function(remove_reviewed_runtime_table_storage_tokens
    PATH SOURCE_VAR OUT_VAR)
    set(_candidate "${${SOURCE_VAR}}")
    string(REGEX REPLACE "[ \t\r\n]+" " " _candidate "${_candidate}")
    if(PATH MATCHES "database/db_zone_runtime_table\\.h$")
        remove_one_reviewed_storage_construct(
            _candidate
            "#include <database/db_zone_runtime_storage.h>"
            _candidate "runtime-table include")
        remove_one_reviewed_storage_construct(
            _candidate
            "zone_runtime_storage::ZoneRuntimeStorageBinding storageBinding_{};"
            _candidate "per-entry storage binding")
        remove_one_reviewed_storage_construct(
            _candidate
            "static zone_runtime_storage::ZoneRuntimeStorageBinding *StorageBinding("
            _candidate "test-gated storage accessor")
    elseif(PATH MATCHES "database/db_zone_runtime_table\\.cpp$")
        remove_one_reviewed_storage_construct(
            _candidate
            "bool IsPristineRuntimeReceipt( const zone_runtime_storage::ZoneRuntimeStorageBinding &binding) noexcept"
            _candidate "storage pristine predicate")
    else()
        message(FATAL_ERROR
            "Runtime-storage passive allowlist used for unexpected path: ${PATH}")
    endif()
    set(${OUT_VAR} "${_candidate}" PARENT_SCOPE)
endfunction()

function(detect_runtime_storage_enrollment SOURCE_VAR OUT_VAR)
    set(_found FALSE)
    foreach(_token IN LISTS _runtime_storage_protected_tokens)
        runtime_storage_source_has_identifier(
            ${SOURCE_VAR} "${_token}" _token_found)
        if(_token_found)
            set(_found TRUE)
        endif()
    endforeach()
    runtime_storage_source_has_token_paste(${SOURCE_VAR} _paste_found)
    if(_paste_found)
        set(_found TRUE)
    endif()
    set(${OUT_VAR} ${_found} PARENT_SCOPE)
endfunction()

function(require_runtime_table_storage_detector_fixture SOURCE_VAR DESCRIPTION)
    normalize_runtime_storage_phase2(${SOURCE_VAR} _candidate)
    runtime_storage_source_has_namespace_declaration(
        _candidate _namespace_declaration)
    remove_reviewed_runtime_table_storage_tokens(
        "${SOURCE_ROOT}/src/database/db_zone_runtime_table.h"
        _candidate _candidate)
    detect_runtime_storage_enrollment(_candidate _detected)
    if(NOT _namespace_declaration AND NOT _detected)
        message(FATAL_ERROR
            "Runtime-storage runtime-table seal missed ${DESCRIPTION}")
    endif()
endfunction()

# The database-facing bridge remains definition-free and exposes one narrow,
# fixed-width status translation instead of leaking the EffectsCore arena type
# into the headless database dependency surface.
foreach(_marker IN ITEMS
    "class FxFastFileNativeArena;"
    "class FxFastFileZoneAdapterDisk32Workspace;"
    "enum class FxRuntimeStorageBindStatus : std::uint8_t { Success, Busy, InvalidArgument, InvalidPhase, MisalignedStorage, SizeOverflow, InsufficientCapacity, TransactionLimit, InvalidTransaction, };"
    "[[nodiscard]] FxRuntimeStorageBindStatus TryBindFxRuntimeStorage( fx::fastfile::FxFastFileNativeArena *arena, void *backing, std::uint32_t budget, std::uint64_t zoneIdentity) noexcept;")
    require_contains(
        _bridge_header "${_marker}" "headless-neutral FX bind bridge contract")
endforeach()
foreach(_forbidden IN ITEMS
    "#include <EffectsCore/"
    "#include \"EffectsCore/"
    "fx_fastfile_native_arena.h"
    "fx_fastfile_zone_adapter_disk32.h")
    require_not_contains(
        _bridge_header "${_forbidden}"
        "database bridge header cannot expose complete EffectsCore types")
endforeach()
require_substring_count(
    _bridge_header "TryBindFxRuntimeStorage(" 1
    "single private FX bind declaration")
require_substring_count(
    _bridge_source "TryBindFxRuntimeStorage(" 1
    "single private FX bind definition")
require_substring_count(
    _bridge_source "arena->TryBind(" 1
    "single native-arena bind mutation")

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
    "~ZoneRuntimeStorageBinding() noexcept {}"
    "const ZoneRuntimeStorageBinding *self_ = nullptr;"
    "enum class ZoneRuntimeStorageBindingPhase : std::uint8_t"
    "struct alignas(8) ZoneRuntimeStorageCompositionExpectation final"
    "RUNTIME_SIZE(ZoneRuntimeStorageCompositionExpectation, 0x28, 0x30);"
    "enum class ZoneRuntimeStorageCompositionMode : std::uint8_t"
    "RUNTIME_SIZE(ZoneRuntimeStorageBinding, 0x58, 0x80);"
    "friend bool AuthenticateZoneRuntimeStorageBinding("
    "friend bool AuthenticateZoneRuntimeStorageComposition("
    "AuthenticateZoneRuntimeStorageBinding("
    "AuthenticateZoneRuntimeStorageComposition("
    "TryPlanZoneRuntimeStorage("
    "TryBindZoneRuntimeStorage("
    "TryDestroyZoneRuntimeStorage(")
    require_contains(_header "${_marker}" "public exact-layout contract")
endforeach()

# Composite expectations are values owned by the exact-key outer controller;
# the placement handle stores no generation key or arena-generation authority.
foreach(_marker IN ITEMS
    "zone_load::ZoneLoadContextKey key{};"
    "std::uint64_t arenaZoneIdentity = 0;"
    "const script_string_journal::ScriptStringJournal *journal = nullptr;"
    "const script_string_journal::ScriptStringJournalEntry *entries = nullptr;"
    "std::uint32_t capacity = 0;"
    "std::uint32_t expectedCount = 0;")
    require_contains(
        _header "${_marker}" "const external composition expectation")
endforeach()

# The public phase vocabulary maps through one const-only friend to the exact
# private topology. Unknown values fail closed and no binding pointer escapes.
foreach(_marker IN ITEMS
    "bool AuthenticateZoneRuntimeStorageBinding("
    "const ZoneRuntimeStorageBinding &binding"
    "const ZoneRuntimeStorageBindingPhase expectedPhase"
    "case ZoneRuntimeStorageBindingPhase::Pristine:"
    "return binding.isPristine();"
    "case ZoneRuntimeStorageBindingPhase::Bound:"
    "return binding.hasCanonicalBoundMetadata();"
    "case ZoneRuntimeStorageBindingPhase::Destroyed:"
    "return binding.destroyed();"
    "default: return false;")
    require_contains(
        _source "${_marker}" "exact const storage-binding authentication")
endforeach()

# Exact-key composition authentication stays read-only, requires the journal
# and arena to agree on the externally retained identity, and fails closed on
# unknown modes. Destroyed compares retained addresses without dereferencing
# the ended placement objects.
foreach(_marker IN ITEMS
    "bool AuthenticateZoneRuntimeStorageComposition("
    "const ZoneRuntimeStorageCompositionExpectation &expectation"
    "const ZoneRuntimeStorageCompositionMode mode"
    "expectation.arenaZoneIdentity != expectation.key.generation"
    "expectation.journal != binding.journal_"
    "expectation.entries != binding.entries_"
    "expectation.capacity != binding.plan_.expectedStringCount"
    "expectation.expectedCount != binding.plan_.expectedStringCount"
    "detail::AuthenticateStableFxRuntimeStorage("
    "case ZoneRuntimeStorageCompositionMode::Placed:"
    "return JournalIsPristine(*binding.journal_);"
    "case ZoneRuntimeStorageCompositionMode::Active:"
    "return JournalIsStableActive("
    "case ZoneRuntimeStorageCompositionMode::Detached:"
    "return JournalIsDetached("
    "mode == ZoneRuntimeStorageCompositionMode::Destroyed"
    "expectation.journal == binding.journal_"
    "default: return false;")
    require_contains(
        _source "${_marker}" "exact read-only storage composition")
endforeach()

foreach(_marker IN ITEMS
    "kisakcod-db-zone-runtime-storage-tests"
    "database-zone-runtime-storage-(layout|source-invariants)")
    require_contains(_ci "${_marker}" "measured Windows x86 test selection")
endforeach()
foreach(_forbidden IN ITEMS
    "physicalmemory_checked"
    "PhysicalMemoryAllocationReceipt"
    "generation_"
    "zoneIdentity_")
    require_not_contains(_header "${_forbidden}" "no duplicate authority")
    require_not_contains(_source "${_forbidden}" "no duplicate authority")
endforeach()

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
# self-authenticating terminal handle and complete placement identity for
# idempotent repeats and exact terminal composition checks.
foreach(_marker IN ITEMS
    "binding->destroyed()"
    "return Status::AlreadyComplete;"
    "JournalCanBeDestroyed(*binding->journal_)"
    "detail::TryPrepareFxRuntimeStorageDestroy("
    "detail::DestroyFxRuntimeWorkspace(binding->workspace_);"
    "detail::DestroyFxRuntimeArena(binding->arena_);"
    "binding->entries_[index - 1].~ScriptStringJournalEntry();"
    "binding->journal_->~ScriptStringJournal();"
    "terminal composition authentication compares addresses and plan fields"
    "State::Destroyed")
    require_contains(_source "${_marker}" "ordered checked destruction")
endforeach()
foreach(_forbidden IN ITEMS
    "binding->slab_ = nullptr;"
    "binding->slabCapacity_ = 0;"
    "binding->plan_ = {};"
    "binding->journal_ = nullptr;"
    "binding->entries_ = nullptr;"
    "binding->arena_ = nullptr;"
    "binding->workspace_ = nullptr;"
    "binding->arenaBacking_ = nullptr;")
    require_not_contains(
        _source "${_forbidden}" "destroyed identity remains retained")
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

# The bridge, not a database translation unit, owns complete EffectsCore types.
# Its bind adapter performs one complete status translation, then the stable
# authenticator and destroy adapter retain the exact arena/lifetime gates.
foreach(_forbidden IN ITEMS
    "#include <EffectsCore/"
    "#include \"EffectsCore/")
    require_not_contains(
        _source "${_forbidden}" "headless-neutral database implementation")
endforeach()
foreach(_marker IN ITEMS
    "AuthenticateStableFxRuntimeStorage("
    "workspace->readyForCompositionAuthentication()"
    "workspace->frameDepth() != 0"
    "workspace->readyForDestruction()"
    "workspace->phase() != Phase::Idle"
    "arena->zoneIdentity() == expectedZoneIdentity"
    "arena->readyForCompositionAuthentication()"
    "arena->openTransactionDepth() == 0"
    "readyForDestruction()"
    "arena->storage() != expectedBacking"
    "arena->openTransactionDepth() != 0"
    "arena->TryUnbind()"
    "DestroyFxRuntimeWorkspace"
    "DestroyFxRuntimeArena")
    require_contains(
        _bridge_source "${_marker}" "exact FX bridge teardown")
endforeach()
require_substring_count(
    _bridge_source "workspace->readyForCompositionAuthentication()" 1
    "single complete workspace composition gate")
foreach(_marker IN ITEMS
    "FxRuntimeStorageBindStatus TryBindFxRuntimeStorage("
    "if (!arena) return FxRuntimeStorageBindStatus::InvalidArgument;"
    "switch (arena->TryBind(backing, budget, zoneIdentity))"
    "case ArenaStatus::Success: return FxRuntimeStorageBindStatus::Success;"
    "case ArenaStatus::Busy: return FxRuntimeStorageBindStatus::Busy;"
    "case ArenaStatus::InvalidArgument: return FxRuntimeStorageBindStatus::InvalidArgument;"
    "case ArenaStatus::InvalidPhase: return FxRuntimeStorageBindStatus::InvalidPhase;"
    "case ArenaStatus::MisalignedStorage: return FxRuntimeStorageBindStatus::MisalignedStorage;"
    "case ArenaStatus::SizeOverflow: return FxRuntimeStorageBindStatus::SizeOverflow;"
    "case ArenaStatus::InsufficientCapacity: return FxRuntimeStorageBindStatus::InsufficientCapacity;"
    "case ArenaStatus::TransactionLimit: return FxRuntimeStorageBindStatus::TransactionLimit;"
    "case ArenaStatus::InvalidTransaction: default: return FxRuntimeStorageBindStatus::InvalidTransaction;")
    require_contains(
        _bridge_source "${_marker}" "exact native-arena bind translation")
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

set(_runtime_table_passive_storage_fixture
    "#include <database/db_zone_runtime_storage.h>\n"
    "zone_runtime_storage::ZoneRuntimeStorageBinding storageBinding_{};\n"
    "static zone_runtime_storage::ZoneRuntimeStorageBinding *StorageBinding(")
remove_reviewed_runtime_table_storage_tokens(
    "${SOURCE_ROOT}/src/database/db_zone_runtime_table.h"
    _runtime_table_passive_storage_fixture
    _runtime_table_passive_storage_reviewed)
detect_runtime_storage_enrollment(
    _runtime_table_passive_storage_reviewed
    _runtime_table_passive_storage_enrolled)
if(_runtime_table_passive_storage_enrolled)
    message(FATAL_ERROR
        "Runtime-storage seal rejected reviewed passive table storage")
endif()
set(_runtime_table_storage_pointer_bypass
    "${_runtime_table_passive_storage_fixture}\n"
    "auto bind = &db::zone_runtime_storage::TryBindZoneRuntimeStorage;")
require_runtime_table_storage_detector_fixture(
    _runtime_table_storage_pointer_bypass "a qualified function pointer")
set(_runtime_table_storage_using_bypass
    "${_runtime_table_passive_storage_fixture}\n"
    "using db/**/::/**/zone_runtime_storage/**/::/**/TryDestroyZoneRuntimeStorage;")
require_runtime_table_storage_detector_fixture(
    _runtime_table_storage_using_bypass "a commented using declaration")
string(CONCAT _runtime_table_storage_splice_bypass
    "${_runtime_table_passive_storage_fixture}\n"
    "auto plan = &TryPlanZoneRuntime"
    "${_runtime_storage_backslash}${_runtime_storage_line_feed}Storage;")
require_runtime_table_storage_detector_fixture(
    _runtime_table_storage_splice_bypass "a phase-2-spliced API")
set(_runtime_table_storage_alias_bypass
    "${_runtime_table_passive_storage_fixture}\n"
    "namespace storage = db::zone_runtime_storage; auto destroy = &storage::TryDestroyZoneRuntimeStorage;")
require_runtime_table_storage_detector_fixture(
    _runtime_table_storage_alias_bypass "a namespace-alias API")
set(_runtime_table_storage_paste_bypass
    "${_runtime_table_passive_storage_fixture}\n"
    "#define KISAK_STORAGE_CAT(a,b) a %:%: b\n"
    "auto bind = &KISAK_STORAGE_CAT(TryBindZoneRuntime,Storage);")
require_runtime_table_storage_detector_fixture(
    _runtime_table_storage_paste_bypass "a token-pasted API")
set(_runtime_table_storage_bridge_bypass
    "${_runtime_table_passive_storage_fixture}\n"
    "auto construct = &ConstructFxRuntimeArena;")
require_runtime_table_storage_detector_fixture(
    _runtime_table_storage_bridge_bypass "a private bridge reference")
set(_runtime_table_storage_bind_bridge_bypass
    "${_runtime_table_passive_storage_fixture}\n"
    "auto bindArena = &TryBindFxRuntimeStorage;")
require_runtime_table_storage_detector_fixture(
    _runtime_table_storage_bind_bridge_bypass
    "a private FX bind bridge reference")
set(_runtime_table_storage_alias_only_bypass
    "${_runtime_table_passive_storage_fixture}\n"
    "namespace storage = db::zone_runtime_storage;")
require_runtime_table_storage_detector_fixture(
    _runtime_table_storage_alias_only_bypass
    "a namespace alias without an API")
set(_runtime_table_storage_type_alias_bypass
    "${_runtime_table_passive_storage_fixture}\n"
    "using BindingAlias = zone_runtime_storage::ZoneRuntimeStorageBinding;")
require_runtime_table_storage_detector_fixture(
    _runtime_table_storage_type_alias_bypass
    "an unreviewed passive type alias")
set(_runtime_table_storage_namespace_macro_bypass
    "${_runtime_table_passive_storage_fixture}\n"
    "#define KISAK_STORAGE_NAMESPACE zone_runtime_storage")
require_runtime_table_storage_detector_fixture(
    _runtime_table_storage_namespace_macro_bypass "a namespace macro")

file(GLOB_RECURSE _all_production_sources
    LIST_DIRECTORIES FALSE "${SOURCE_ROOT}/src/*")
foreach(_non_extension_sentinel IN ITEMS
    "${SOURCE_ROOT}/src/groupvoice/speex/Makefile.am"
    "${SOURCE_ROOT}/src/groupvoice/speex/Makefile.in")
    list(FIND _all_production_sources
        "${_non_extension_sentinel}" _sentinel_index)
    if(_sentinel_index EQUAL -1)
        message(FATAL_ERROR
            "Runtime-storage production seal lost extension-independent "
            "traversal: ${_non_extension_sentinel}")
    endif()
endforeach()
foreach(_path IN LISTS _all_production_sources)
    if("${_path}" STREQUAL "${_header_path}"
        OR "${_path}" STREQUAL "${_source_path}"
        OR "${_path}" STREQUAL "${_bridge_header_path}"
        OR "${_path}" STREQUAL "${_bridge_source_path}"
        OR "${_path}" STREQUAL "${_headless_bridge_source_path}")
        continue()
    endif()
    if("${_path}" MATCHES
        "database/db_zone_runtime_table\.(h|cpp)$")
        # The dedicated exact-controller source seal owns every allowed
        # runtime-storage use in these two files.
        continue()
    endif()
    file(READ "${_path}" _production_raw)
    normalize_runtime_storage_phase2(_production_raw _production_source)
    remove_reviewed_runtime_storage_token_text(
        "${_path}" _production_source _production_source)
    detect_runtime_storage_enrollment(_production_source _enrolled)
    if(_enrolled)
        file(RELATIVE_PATH _relative "${SOURCE_ROOT}/src" "${_path}")
        message(FATAL_ERROR
            "Unreviewed runtime-storage production enrollment in ${_relative}")
    endif()
endforeach()

foreach(_marker IN ITEMS
    "kisakcod-db-zone-runtime-storage-tests"
    "db_zone_runtime_storage_tests.cpp"
    "NAME database-zone-runtime-storage-layout")
    require_contains(_tests "${_marker}" "runtime fixture registration")
endforeach()
foreach(_marker IN ITEMS
    "using BindingPhase = runtime_storage::ZoneRuntimeStorageBindingPhase;"
    "using CompositionExpectation = runtime_storage::ZoneRuntimeStorageCompositionExpectation;"
    "using CompositionMode = runtime_storage::ZoneRuntimeStorageCompositionMode;"
    "using BindingAuthenticator = bool (*)("
    "using CompositionAuthenticator = bool (*)("
    "decltype(&runtime_storage::AuthenticateZoneRuntimeStorageBinding)"
    "decltype(&runtime_storage::AuthenticateZoneRuntimeStorageComposition)"
    "BindingPhase::Pristine"
    "BindingPhase::Bound"
    "BindingPhase::Destroyed"
    "static_cast<BindingPhase>(UINT8_C(0xFF))"
    "static_cast<CompositionMode>(UINT8_C(0xFF))"
    "sizeof(Binding) == (sizeof(void *) == 4 ? 0x58u : 0x80u)"
    "sizeof(CompositionExpectation) == (sizeof(void *) == 4 ? 0x28u : 0x30u)"
    "CompositionMode::Placed"
    "CompositionMode::Active"
    "CompositionMode::Detached"
    "CompositionMode::Destroyed"
    "TestCompositionAuthentication()"
    "arenaZoneIdentity"
    "ScriptStringJournalTestAccess::SetCapacity"
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
    "KISAK_FX_FASTFILE_ZONE_ADAPTER_TESTING"
    "using WorkspaceAuthenticator = bool ("
    "workspace->readyForCompositionAuthentication()"
    "WorkspaceAccess::SetArena"
    "WorkspaceAccess::SetCursor"
    "WorkspaceAccess::SetRecordingCounts"
    "WorkspaceAccess::SetHints"
    "WorkspaceAccess::CorruptDormantFrame"
    "WorkspaceAccess::SetOperating"
    "workspaceCompositionRejected()"
    "restoreWorkspace()"
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
