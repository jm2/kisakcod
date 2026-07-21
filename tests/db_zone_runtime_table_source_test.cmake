cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_header_path
    "${SOURCE_ROOT}/src/database/db_zone_runtime_table.h")
set(_source_path
    "${SOURCE_ROOT}/src/database/db_zone_runtime_table.cpp")
set(_fixture_path
    "${SOURCE_ROOT}/tests/db_zone_runtime_table_tests.cpp")
set(_production_seal_path
    "${SOURCE_ROOT}/tests/db_zone_runtime_table_production_seal_tests.cpp")
set(_physical_receipt_header_path
    "${SOURCE_ROOT}/src/universal/physicalmemory_checked.h")
set(_stream_receipt_header_path
    "${SOURCE_ROOT}/src/database/db_zone_stream_ownership.h")
set(_pending_receipt_header_path
    "${SOURCE_ROOT}/src/database/db_zone_pending_copy_ledger.h")
set(_storage_receipt_header_path
    "${SOURCE_ROOT}/src/database/db_zone_runtime_storage.h")
set(_manifest_path "${SOURCE_ROOT}/scripts/common_files.cmake")
set(_tests_path "${SOURCE_ROOT}/tests/CMakeLists.txt")
set(_ci_path "${SOURCE_ROOT}/.github/workflows/ci.yml")
set(_registry_path "${SOURCE_ROOT}/src/database/db_registry.cpp")
set(_file_load_path "${SOURCE_ROOT}/src/database/db_file_load.cpp")
set(_load_path "${SOURCE_ROOT}/src/database/db_load.cpp")
set(_stream_path "${SOURCE_ROOT}/src/database/db_stream_load.cpp")
set(_stringtable_path
    "${SOURCE_ROOT}/src/database/db_stringtable_load.cpp")
set(_xanim_path "${SOURCE_ROOT}/src/xanim/xanim.h")

foreach(_path IN ITEMS
    "${_header_path}"
    "${_source_path}"
    "${_fixture_path}"
    "${_production_seal_path}"
    "${_physical_receipt_header_path}"
    "${_stream_receipt_header_path}"
    "${_pending_receipt_header_path}"
    "${_storage_receipt_header_path}"
    "${_manifest_path}"
    "${_tests_path}"
    "${_ci_path}"
    "${_registry_path}"
    "${_file_load_path}"
    "${_load_path}"
    "${_stream_path}"
    "${_stringtable_path}"
    "${_xanim_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing zone runtime table source: ${_path}")
    endif()
endforeach()

file(READ "${_header_path}" _header)
file(READ "${_source_path}" _source)
file(READ "${_fixture_path}" _fixture)
file(READ "${_production_seal_path}" _production_seal)
file(READ "${_physical_receipt_header_path}" _physical_receipt_header)
file(READ "${_stream_receipt_header_path}" _stream_receipt_header)
file(READ "${_pending_receipt_header_path}" _pending_receipt_header)
file(READ "${_storage_receipt_header_path}" _storage_receipt_header)
file(READ "${_manifest_path}" _manifest)
file(READ "${_tests_path}" _tests)
file(READ "${_ci_path}" _ci)
file(READ "${_registry_path}" _registry)
file(READ "${_file_load_path}" _file_load)
file(READ "${_load_path}" _load)
file(READ "${_stream_path}" _stream)
file(READ "${_stringtable_path}" _stringtable)
file(READ "${_xanim_path}" _xanim)

# Preserve the original line structure for preprocessing-directive checks.
# The normalized copies below are used for exact C++ declaration matching.
set(_runtime_receipt_header_raw "${_header}")
set(_runtime_source_raw "${_source}")
set(_physical_receipt_header_raw "${_physical_receipt_header}")
set(_stream_receipt_header_raw "${_stream_receipt_header}")
set(_pending_receipt_header_raw "${_pending_receipt_header}")
set(_storage_receipt_header_raw "${_storage_receipt_header}")

foreach(_var IN ITEMS
    _header
    _source
    _fixture
    _production_seal
    _physical_receipt_header
    _stream_receipt_header
    _pending_receipt_header
    _storage_receipt_header
    _manifest
    _tests
    _ci
    _registry
    _file_load
    _load
    _stream
    _stringtable
    _xanim)
    string(REGEX REPLACE "[ \t\r\n]+" " " _normalized "${${_var}}")
    set(${_var} "${_normalized}")
endforeach()

function(require_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing zone runtime table invariant (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(require_not_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden zone runtime table regression (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(require_ordered SOURCE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${FIRST}" _first)
    string(FIND "${${SOURCE_VAR}}" "${SECOND}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered zone runtime table invariant "
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
        message(FATAL_ERROR "Missing ordered end of ${DESCRIPTION}: '${END}'")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_end} _slice)
    set(${OUT_VAR} "${_slice}" PARENT_SCOPE)
endfunction()

function(require_exact_pristine_overload START END OBJECT DESCRIPTION)
    extract_slice(_source "${START}" "${END}" _slice "${DESCRIPTION}")
    string(STRIP "${_slice}" _slice)
    set(_expected "${START} { return ${OBJECT}.isPristine(); }")
    if(NOT _slice STREQUAL _expected)
        message(FATAL_ERROR
            "Pristine friend body is not exact (${DESCRIPTION}): '${_slice}'")
    endif()
endfunction()

function(require_exact_shared_authentication_overload
    START END OBJECT AUTHENTICATOR DESCRIPTION)
    extract_slice(_source "${START}" "${END}" _slice "${DESCRIPTION}")
    string(STRIP "${_slice}" _slice)
    set(_expected
        "${START} { return ${AUTHENTICATOR}(${OBJECT}); }")
    if(NOT _slice STREQUAL _expected)
        message(FATAL_ERROR
            "Shared authenticator body is not exact (${DESCRIPTION}): "
            "'${_slice}'")
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
            "Unexpected zone runtime table count (${DESCRIPTION}): expected "
            "${EXPECTED}, found ${_count} for '${NEEDLE}'")
    endif()
endfunction()

function(require_regex_count SOURCE_VAR PATTERN EXPECTED DESCRIPTION)
    string(REGEX MATCHALL "${PATTERN}" _matches "${${SOURCE_VAR}}")
    list(LENGTH _matches _count)
    if(NOT _count EQUAL EXPECTED)
        message(FATAL_ERROR
            "Unexpected zone runtime table regex count (${DESCRIPTION}): "
            "expected ${EXPECTED}, found ${_count}")
    endif()
endfunction()

function(friend_surface_matches_exact
    SOURCE_VAR EXPECTED_FRIENDS_VAR OUT_VAR)
    string(REGEX MATCHALL
        "friend[ ]+[^;]*"
        _actual_friends
        "${${SOURCE_VAR}}")
    if("${_actual_friends}" STREQUAL
        "${${EXPECTED_FRIENDS_VAR}}")
        set(${OUT_VAR} TRUE PARENT_SCOPE)
    else()
        set(${OUT_VAR} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(require_exact_friend_surface
    SOURCE_VAR EXPECTED_FRIENDS_VAR DESCRIPTION)
    friend_surface_matches_exact(
        ${SOURCE_VAR} ${EXPECTED_FRIENDS_VAR} _surface_matches)
    if(NOT _surface_matches)
        string(REGEX MATCHALL
            "friend[ ]+[^;]*"
            _actual_friends
            "${${SOURCE_VAR}}")
        list(LENGTH _actual_friends _actual_count)
        list(LENGTH ${EXPECTED_FRIENDS_VAR} _expected_count)
        message(FATAL_ERROR
            "Exact friend surface drifted (${DESCRIPTION}): expected "
            "${_expected_count} declarations '${${EXPECTED_FRIENDS_VAR}}', "
            "found ${_actual_count} '${_actual_friends}'")
    endif()
endfunction()

function(class_digest_matches_exact SOURCE_VAR EXPECTED OUT_VAR)
    string(SHA256 _actual_digest "${${SOURCE_VAR}}")
    if(_actual_digest STREQUAL EXPECTED)
        set(${OUT_VAR} TRUE PARENT_SCOPE)
    else()
        set(${OUT_VAR} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(require_exact_class_digest SOURCE_VAR EXPECTED DESCRIPTION)
    class_digest_matches_exact(${SOURCE_VAR} ${EXPECTED} _digest_matches)
    if(NOT _digest_matches)
        string(SHA256 _actual_digest "${${SOURCE_VAR}}")
        message(FATAL_ERROR
            "Authority class body drifted (${DESCRIPTION}): expected "
            "${EXPECTED}, found ${_actual_digest}")
    endif()
endfunction()

string(ASCII 92 _friend_surface_backslash)
string(ASCII 13 _friend_surface_carriage_return)
string(ASCII 10 _friend_surface_line_feed)
string(ASCII 11 _friend_surface_vertical_tab)
string(ASCII 12 _friend_surface_form_feed)

function(source_has_define_directive SOURCE_VAR OUT_VAR)
    set(_candidate "${${SOURCE_VAR}}")
    # Translation phase 1 historically maps trigraphs before phase 2 removes
    # escaped newlines. Preserve that order so a trigraph-spelled backslash
    # cannot split the directive name around this source-level seal.
    string(REPLACE "??/" "${_friend_surface_backslash}"
        _candidate "${_candidate}")
    string(REPLACE "??=" "#" _candidate "${_candidate}")
    string(REPLACE
        "${_friend_surface_backslash}${_friend_surface_carriage_return}${_friend_surface_line_feed}"
        "" _candidate "${_candidate}")
    string(REPLACE
        "${_friend_surface_backslash}${_friend_surface_line_feed}"
        "" _candidate "${_candidate}")
    string(REPLACE
        "${_friend_surface_backslash}${_friend_surface_carriage_return}"
        "" _candidate "${_candidate}")
    # Normalize the standard alternative preprocessing tokens before checking
    # line-leading directives. This keeps a digraph or historical trigraph
    # spelling from bypassing the same zero-macro contract.
    string(REPLACE "%:" "#" _candidate "${_candidate}")
    # Comments are whitespace during preprocessing and can legally separate
    # the directive introducer from its name.
    string(REGEX REPLACE
        "/[*]([^*]|[*]+[^*/])*[*]+/" " " _candidate "${_candidate}")
    string(REGEX REPLACE
        "//[^\r\n]*" "" _candidate "${_candidate}")
    string(REPLACE "${_friend_surface_vertical_tab}" " "
        _candidate "${_candidate}")
    string(REPLACE "${_friend_surface_form_feed}" " "
        _candidate "${_candidate}")
    string(REGEX MATCH
        "(^|[\r\n])[ \t]*#[ \t]*define([ \t]|$)"
        _define_directive
        "${_candidate}")
    if(_define_directive STREQUAL "")
        set(${OUT_VAR} FALSE PARENT_SCOPE)
    else()
        set(${OUT_VAR} TRUE PARENT_SCOPE)
    endif()
endfunction()

function(require_no_define_directive SOURCE_VAR DESCRIPTION)
    source_has_define_directive(${SOURCE_VAR} _has_define_directive)
    if(_has_define_directive)
        message(FATAL_ERROR
            "Authority-bearing header gained a macro definition "
            "(${DESCRIPTION}); macros can synthesize hidden friendship")
    endif()
endfunction()

# A macro-off translation unit recreates the helper's public name and probes
# every private mutable capability that the test-only implementation exposes.
# These are normal positive-build checks, so unrelated compiler failures cannot
# satisfy the production authority seal.
foreach(_marker IN ITEMS
    "struct ZoneRuntimeTableTestAccess"
    "sizeof(ZoneRuntimeGenerationCallbacks) == (sizeof(void *) == 4 ? 0x14u : 0x28u)"
    "sizeof(ZoneRuntimePendingCopyView) == 0x18u"
    "sizeof(ZoneRuntimeGenerationBinding) == (sizeof(void *) == 4 ? 0x50u : 0x78u)"
    "sizeof(ZoneRuntimeReceiptCapsule) == (sizeof(void *) == 4 ? 0xD0u : 0x120u)"
    "sizeof(ZoneRuntimeEntry) == (sizeof(void *) == 4 ? 0x190u : 0x228u)"
    "sizeof(ZoneRuntimeTable) == (sizeof(void *) == 4 ? 0xF568u : 0x109A0u)"
    "using PendingCopyViewOperation ="
    "using PendingCopyReadOperation ="
    "decltype(&TryGetZoneRuntimePendingCopyView)"
    "decltype(&TryReadZoneRuntimePendingCopy)"
    "CanReachEntries"
    "CanMutateState"
    "CanMutateSharedState"
    "CanReachMutableLifecycle"
    "CanReachMutableOwnership"
    "CanMutateKey"
    "CanReachReceiptCapsule"
    "CanReachGenerationBinding"
    "CanReachActiveStreamBinding"
    "CanReachPendingCopyLedger"
    "CanReachPendingDrainCallback"
    "CanReachGenerationCallbacks"
    "CanMutateGenerationStage"
    "CanReachCallbackMarker"
    "CanAuthenticateRegistryLifecycleCallback"
    "CanRestoreRegistryLifecycleCallback"
    "CanSetPendingCopyReadHook"
    "CanAuthenticateLifecycleCallbackMarker"
    "CanAuthenticatePendingCopyRead"
    "CanAuthenticatePendingCopyOutput"
    "CanReachPendingCopyAdmissionReceipt"
    "CanReachAllocationReceipt"
    "CanReachStreamGenerationReceipt"
    "CanReachPendingAdmissionReceipt"
    "CanReachStorageBinding"
    "&table->entries_;"
    "&table->activeZoneStreamBinding_;"
    "&table->pendingCopyLedger_;"
    "table->state_ = 1u;"
    "table->sharedState_ = 1u;"
    "&entry->lifecycle_;"
    "&entry->scriptStringOwnership_;"
    "&entry->receiptCapsule_;"
    "&entry->generationBinding_;"
    "entry->key_ = zone_load::ZoneLoadContextKey{};"
    "&capsule->allocationReceipt_;"
    "&capsule->streamGenerationReceipt_;"
    "&capsule->pendingCopyAdmissionReceipt_;"
    "&capsule->storageBinding_;"
    "&table->pendingDrainCallback_;"
    "&binding->callbacks_;"
    "binding->setupStage_ = ZoneRuntimeSetupStage::CallbacksBound;"
    "&binding->callbackMarker_;"
    "table->authenticateExactRegistryLifecycleCallback(1u, key);"
    "table->restoreExactRegistryLifecycleCallback(1u, key);"
    "table->authenticateExactLifecycleCallbackMarker(1u, key, marker);"
    "table->authenticateExactPendingCopyRead(1u, key, outEntry);"
    "table->authenticateExactPendingCopyOutput( 1u, key, output, sizeof(std::uint32_t), alignof(std::uint32_t));"
    "Table::pendingCopyAdmissionReceipt(entry);"
    "!ZoneRuntimeTableTestAccess::CanReachEntries<ZoneRuntimeTable>"
    "!ZoneRuntimeTableTestAccess::CanSetPendingCopyReadHook< ZoneRuntimeTableTestAccess>"
    "!ZoneRuntimeTableTestAccess::CanMutateState<ZoneRuntimeTable>"
    "!ZoneRuntimeTableTestAccess::CanMutateSharedState<ZoneRuntimeTable>"
    "!ZoneRuntimeTableTestAccess::CanReachMutableLifecycle<ZoneRuntimeEntry>"
    "!ZoneRuntimeTableTestAccess::CanReachMutableOwnership<ZoneRuntimeEntry>"
    "!ZoneRuntimeTableTestAccess::CanMutateKey<ZoneRuntimeEntry>"
    "!ZoneRuntimeTableTestAccess::CanReachReceiptCapsule<ZoneRuntimeEntry>"
    "!ZoneRuntimeTableTestAccess::CanReachGenerationBinding<ZoneRuntimeEntry>"
    "!ZoneRuntimeTableTestAccess::CanReachActiveStreamBinding<ZoneRuntimeTable>"
    "!ZoneRuntimeTableTestAccess::CanReachPendingCopyLedger<ZoneRuntimeTable>"
    "!ZoneRuntimeTableTestAccess::CanReachPendingDrainCallback<ZoneRuntimeTable>"
    "!ZoneRuntimeTableTestAccess::CanReachGenerationCallbacks< ZoneRuntimeGenerationBinding>"
    "!ZoneRuntimeTableTestAccess::CanMutateGenerationStage< ZoneRuntimeGenerationBinding>"
    "!ZoneRuntimeTableTestAccess::CanReachCallbackMarker< ZoneRuntimeGenerationBinding>"
    "!ZoneRuntimeTableTestAccess::CanAuthenticateRegistryLifecycleCallback< ZoneRuntimeTable>"
    "!ZoneRuntimeTableTestAccess::CanRestoreRegistryLifecycleCallback< ZoneRuntimeTable>"
    "!ZoneRuntimeTableTestAccess::CanAuthenticateLifecycleCallbackMarker< ZoneRuntimeTable, ZoneRuntimeGenerationBinding>"
    "!ZoneRuntimeTableTestAccess::CanAuthenticatePendingCopyRead< ZoneRuntimeTable, ZoneRuntimeEntry>"
    "!ZoneRuntimeTableTestAccess::CanAuthenticatePendingCopyOutput< ZoneRuntimeTable>"
    "!ZoneRuntimeTableTestAccess::CanReachPendingCopyAdmissionReceipt< ZoneRuntimeTable, ZoneRuntimeEntry>"
    "!ZoneRuntimeTableTestAccess::CanReachAllocationReceipt< ZoneRuntimeReceiptCapsule>"
    "!ZoneRuntimeTableTestAccess::CanReachStreamGenerationReceipt< ZoneRuntimeReceiptCapsule>"
    "!ZoneRuntimeTableTestAccess::CanReachPendingAdmissionReceipt< ZoneRuntimeReceiptCapsule>"
    "!ZoneRuntimeTableTestAccess::CanReachStorageBinding< ZoneRuntimeReceiptCapsule>")
    require_contains(
        _production_seal "${_marker}" "independent production authority seal")
endforeach()
require_not_contains(
    _production_seal
    "#define KISAK_DB_ZONE_RUNTIME_TABLE_TESTING"
    "production authority seal cannot opt into test access")

# The staged mutation hooks exist only in the private runtime-table fixture.
# Production compiles the same source with all hook storage/calls removed, and
# the one-shot test hook clears before invoking adversarial mutation.
require_substring_count(
    _runtime_source_raw
    "#ifdef KISAK_DB_ZONE_RUNTIME_TABLE_TESTING" 4
    "four exact source-local runtime-table test-hook gates")
foreach(_marker IN ITEMS
    "struct PendingCopyReadTestHook final"
    "PendingCopyReadTestHook g_pendingCopyReadTestHook{};"
    "void InvokePendingCopyReadTestHook("
    "const PendingCopyReadTestHook hook = g_pendingCopyReadTestHook;"
    "g_pendingCopyReadTestHook = {};"
    "void ZoneRuntimeTableTestAccess::SetPendingCopyReadHook("
    "PendingCopyReadHookStage::BeforeLowerRead"
    "PendingCopyReadHookStage::AfterLowerRead")
    require_contains(
        _source "${_marker}"
        "test-gated one-shot pending-copy mutation seam")
endforeach()
require_substring_count(
    _source "InvokePendingCopyReadTestHook(" 3
    "one hook helper plus exact pre/post lower-read calls")
require_ordered(
    _source
    "const PendingCopyReadTestHook hook = g_pendingCopyReadTestHook;"
    "g_pendingCopyReadTestHook = {};"
    "test hook snapshots before one-shot revocation")
require_ordered(
    _source
    "g_pendingCopyReadTestHook = {};"
    "hook.callback(hook.context, table, physicalSlot, key);"
    "test hook revokes before adversarial callback")

extract_slice(
    _tests
    "# Compile production's runtime-table header without its test-access opt-in."
    "add_executable(kisakcod-fx-archive-disk32-tests"
    _production_seal_registration
    "production authority-seal registration")
foreach(_marker IN ITEMS
    "add_executable( kisakcod-db-zone-runtime-table-production-seal-tests"
    "db_zone_runtime_table_production_seal_tests.cpp"
    "NAME database-zone-runtime-table-production-test-access-sealed")
    require_contains(
        _production_seal_registration
        "${_marker}"
        "normal positive-build production authority seal")
endforeach()
foreach(_forbidden IN ITEMS
    "WILL_FAIL"
    "EXCLUDE_FROM_ALL"
    "KISAK_DB_ZONE_RUNTIME_TABLE_TESTING")
    require_not_contains(
        _production_seal_registration
        "${_forbidden}"
        "production authority seal cannot accept a vacuous failure")
endforeach()

# All four owning classes independently gate their friendship, and the helper's
# forward declaration and implementation remain absent from production TUs.
foreach(_marker IN ITEMS
    "#ifdef KISAK_DB_ZONE_RUNTIME_TABLE_TESTING struct ZoneRuntimeTableTestAccess; #endif"
    "#ifdef KISAK_DB_ZONE_RUNTIME_TABLE_TESTING friend struct ZoneRuntimeTableTestAccess; #endif"
    "#ifdef KISAK_DB_ZONE_RUNTIME_TABLE_TESTING // Tests opt in before including this header.")
    require_contains(_header "${_marker}" "macro-gated test authority")
endforeach()

# The fixed table owns durable metadata and coordinates only the report-free,
# serialized PMem bridge through its opaque receipt. It remains external to
# XZone and cannot report, publish assets, call raw legacy PMem, or mutate
# script strings outside the exact ownership controller.
foreach(_var IN ITEMS _header _source)
    foreach(_forbidden IN ITEMS
        "#include <xanim/"
        "#include <EffectsCore/"
        "#include \"EffectsCore/"
        "fx_fastfile_native_arena.h"
        "fx_fastfile_zone_adapter_disk32.h"
        "#include <vector>"
        "#include <string>"
        "std::function"
        "Com_Error("
        "PMem_"
        "DB_LoadXFile"
        "DB_AddXAsset"
        "DB_LinkXAssetEntry"
        "g_zones"
        "g_zoneHandles"
        "SL_"
        "malloc("
        "calloc("
        "realloc("
        "operator new"
        "throw "
        "catch (")
        require_not_contains(
            ${_var} "${_forbidden}" "metadata-only report-free table")
    endforeach()
endforeach()

extract_slice(
    _header
    "struct ZoneRuntimePendingCopyView final"
    "RUNTIME_SIZE(ZoneRuntimePendingCopyView, 0x18, 0x18);"
    _pending_copy_view_declaration
    "pointer-free pending-copy view declaration")
string(STRIP "${_pending_copy_view_declaration}"
    _pending_copy_view_declaration)
set(_expected_pending_copy_view_declaration
    "struct ZoneRuntimePendingCopyView final { zone_load::ZoneLoadContextKey key{}; std::uint32_t recordCount = 0; std::uint32_t reserved = 0; [[nodiscard]] constexpr explicit operator bool() const noexcept { return static_cast<bool>(key) && recordCount <= zone_pending_copy::kPendingCopyRecordCapacity && reserved == 0; } };")
if(NOT _pending_copy_view_declaration STREQUAL
    _expected_pending_copy_view_declaration)
    message(FATAL_ERROR
        "Pending-copy view must remain an exact pointer-free 0x18 value")
endif()
foreach(_pending_copy_api IN ITEMS
    "[[nodiscard]] ZoneRuntimeTableStatus TryGetZoneRuntimePendingCopyView( ZoneRuntimeTable *table, std::uint32_t physicalSlot, const zone_load::ZoneLoadContextKey &key, ZoneRuntimePendingCopyView *outView) noexcept;"
    "[[nodiscard]] ZoneRuntimeTableStatus TryReadZoneRuntimePendingCopy( ZoneRuntimeTable *table, std::uint32_t physicalSlot, const zone_load::ZoneLoadContextKey &key, std::uint32_t expectedRecordCount, std::uint32_t ordinal, zone_pending_copy::PendingCopyRecord *outRecord) noexcept;")
    require_substring_count(
        _header "${_pending_copy_api}" 1
        "single exact public pending-copy inspection declaration")
endforeach()
require_substring_count(
    _source "#include <database/db_zone_runtime_storage_fx_bridge.h>" 1
    "single headless-neutral private storage bridge include")
require_not_contains(
    _xanim "ZoneRuntimeTable" "runtime ownership cannot enter XZone")

# Freeze the external 33-entry schema, default-slot reservation, full-width
# generation key, stable control objects, and portable layouts.
foreach(_marker IN ITEMS
    "Physical slot zero remains the engine's reserved/default slot."
    "Only slots 1..32 are usable."
    "class alignas(8) ZoneRuntimeReceiptCapsule final"
    "class alignas(8) ZoneRuntimeEntry final"
    "physical_memory::AllocationReceipt allocationReceipt_{};"
    "ZoneStreamGenerationReceipt streamGenerationReceipt_{};"
    "PendingCopyAdmissionReceipt pendingCopyAdmissionReceipt_{};"
    "ZoneRuntimeStorageBinding storageBinding_{};"
    "RUNTIME_SIZE(ZoneRuntimeReceiptCapsule, 0xD0, 0x120);"
    "zone_load::ZoneLoadContextSlot lifecycle_{};"
    "ZoneScriptStringOwnershipController scriptStringOwnership_{};"
    "zone_load::ZoneLoadContextKey key_{};"
    "ZoneRuntimeReceiptCapsule receiptCapsule_{};"
    "ZoneRuntimeGenerationBinding generationBinding_{};"
    "enum class CallbackMarker : std::uint8_t { Idle, ActiveNoRegistry, ActiveRegistryBorrow, };"
    "CallbackMarker callbackMarker_ = CallbackMarker::Idle;"
    "const ZoneRuntimeEntry *entry = nullptr;"
    "RUNTIME_SIZE(ZoneRuntimeEntry, 0x190, 0x228);"
    "std::array<ZoneRuntimeEntry, zone_slots::kPhysicalZoneSlotCount> entries_{};"
    "ActiveZoneStreamBinding activeZoneStreamBinding_{};"
    "zone_pending_copy::PendingCopyLedger pendingCopyLedger_{};"
    "RUNTIME_SIZE(ZoneRuntimeTable, 0xF568, 0x109A0);"
    "RUNTIME_SIZE(ZoneRuntimeGenerationView, 0x18, 0x18);"
    "struct ZoneRuntimePendingCopyView final"
    "std::uint32_t recordCount = 0;"
    "std::uint32_t reserved = 0;"
    "RUNTIME_SIZE(ZoneRuntimePendingCopyView, 0x18, 0x18);"
    "ZoneRuntimeEntry(const ZoneRuntimeEntry &) = delete;"
    "ZoneRuntimeReceiptCapsule(const ZoneRuntimeReceiptCapsule &) = delete;"
    "ZoneRuntimeTable(const ZoneRuntimeTable &) = delete;"
    "KISAK_DB_ZONE_RUNTIME_TABLE_TESTING"
    "friend class ZoneRuntimeTable;"
    "authenticateExactMutableEntry("
    "authenticateExactRegistryLifecycleCallback("
    "restoreExactRegistryLifecycleCallback("
    "authenticateExactLifecycleCallbackMarker("
    "authenticateExactPendingCopyRead("
    "authenticateExactPendingCopyOutput("
    "pendingCopyAdmissionReceipt("
    "exactRegistryLifecycleCallbackPhaseMatches("
    "completeMutableOperation("
    "mutableScriptStringOwnership("
    "TryBeginZoneRuntimeScriptStringOwnership("
    "TryStageZoneRuntimeScriptString("
    "TrySealZoneRuntimeScriptStrings("
    "TryBeginZoneRuntimeScriptStringTransfer("
    "TryTransferNextZoneRuntimeScriptString("
    "TryPrepareZoneRuntimeScriptStringCommit("
    "TryCommitZoneRuntimeScriptStringsAndAdmit("
    "TryBeginZoneRuntimeScriptStringRollback("
    "TryRollbackNextZoneRuntimeScriptString("
    "TryFinishZoneRuntimeScriptStringAbandonment("
    "TryGetZoneRuntimePendingCopyView("
    "TryReadZoneRuntimePendingCopy("
    "TryUnloadZoneRuntimeGeneration("
    "TryResetZoneRuntimeTerminalReceipt("
    "Rejected,"
    "CountMismatch,"
    "CapacityExceeded,"
    "authenticates table state, physical slot, durable key, lifecycle generation,"
    "unchanged on every non-Success result"
    "retains exact callback/controller ownership"
    "The lifecycle terminal kind,"
    "generation, and durable table key remain as a receipt"
    "external serialization.")
    require_contains(_header "${_marker}" "durable external table schema")
endforeach()
foreach(_var IN ITEMS _header _source)
    require_not_contains(
        ${_var} "callbackActive_"
        "generation callback byte must retain three-state semantics")
endforeach()

# Private component state is authenticated through exact const-reference
# predicates, never a broadly friended class that another production
# translation unit could recreate to recover mutable authority.
foreach(_receipt_header IN ITEMS
    _physical_receipt_header
    _stream_receipt_header
    _pending_receipt_header
    _storage_receipt_header)
    require_contains(
        ${_receipt_header}
        "friend bool db::zone_runtime::detail::IsPristineRuntimeReceipt("
        "narrow const-only pristine friendship")
    require_not_contains(
        ${_receipt_header}
        "friend class db::zone_runtime::"
        "no broadly friended runtime-table authentication class")
endforeach()

# Freeze each authority class's complete declaration surface. Counting only
# friends that spell db::zone_runtime literally is insufficient: a namespace
# or type alias can grant the same private access without repeating that text.
extract_slice(
    _physical_receipt_header
    "class AllocationReceipt final"
    "[[nodiscard]] AllocationScopeStatus TryBegin("
    _physical_allocation_receipt_class
    "checked-PMem allocation receipt class")
set(_physical_allocation_receipt_friends
    "friend AllocationScopeStatus TryBegin( PhysicalMemory *memory, std::uint32_t allocType, const char *stableName, AllocationReceipt *receipt) noexcept"
    "friend AllocationScopeStatus TryEnd( AllocationReceipt *receipt) noexcept"
    "friend AllocationScopeStatus TryFree( AllocationReceipt *receipt) noexcept"
    "friend bool db::zone_runtime::detail::IsPristineRuntimeReceipt( const AllocationReceipt &receipt) noexcept"
    "friend bool pmem_runtime::detail::AuthenticateAllocationReceiptNoLock( const AllocationReceipt &receipt, const PhysicalMemory &owner, std::uint32_t allocType, std::uint32_t index, const char *stableName, pmem_runtime::AllocationReceiptPhase expectedPhase) noexcept")
require_exact_friend_surface(
    _physical_allocation_receipt_class
    _physical_allocation_receipt_friends
    "checked-PMem AllocationReceipt")

extract_slice(
    _storage_receipt_header
    "class ZoneRuntimeStorageBinding final"
    "[[nodiscard]] ZoneRuntimeStorageStatus TryPlanZoneRuntimeStorage("
    _runtime_storage_binding_class
    "runtime-storage binding class")
set(_runtime_storage_binding_friends
    "friend bool AuthenticateZoneRuntimeStorageBinding( const ZoneRuntimeStorageBinding &, ZoneRuntimeStorageBindingPhase) noexcept"
    "friend bool AuthenticateZoneRuntimeStorageComposition( const ZoneRuntimeStorageBinding &, const ZoneRuntimeStorageCompositionExpectation &, ZoneRuntimeStorageCompositionMode) noexcept"
    "friend ZoneRuntimeStorageStatus TryBindZoneRuntimeStorage( void *, std::size_t, const ZoneRuntimeStoragePlan *, ZoneRuntimeStorageBinding *) noexcept"
    "friend ZoneRuntimeStorageStatus TryDestroyZoneRuntimeStorage( ZoneRuntimeStorageBinding *) noexcept"
    "friend bool db::zone_runtime::detail::IsPristineRuntimeReceipt( const ZoneRuntimeStorageBinding &binding) noexcept")
require_exact_friend_surface(
    _runtime_storage_binding_class
    _runtime_storage_binding_friends
    "ZoneRuntimeStorageBinding")

extract_slice(
    _stream_receipt_header
    "class alignas(8) ZoneStreamGenerationReceipt final"
    "RUNTIME_SIZE(ZoneStreamGenerationReceipt, 0x20, 0x28);"
    _stream_generation_receipt_class
    "zone-stream generation receipt class")
set(_stream_generation_receipt_friends
    "friend ZoneStreamOwnershipStatus TryBeginZoneStreamGeneration( ZoneStreamGenerationReceipt *, zone_load::ZoneLoadContextSlot *, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend ZoneStreamOwnershipStatus TryBindZoneStreams( ActiveZoneStreamBinding *, ZoneStreamGenerationReceipt *, const zone_load::ZoneLoadContextKey &, const XZoneMemory *, const relocation::BlockView *, std::size_t) noexcept"
    "friend ZoneStreamOwnershipStatus TryInvalidateZoneStreams( ActiveZoneStreamBinding *, ZoneStreamGenerationReceipt *, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend class ActiveZoneStreamBinding"
    "friend bool db::zone_runtime::detail::IsPristineRuntimeReceipt( const ZoneStreamGenerationReceipt &receipt) noexcept")
require_exact_friend_surface(
    _stream_generation_receipt_class
    _stream_generation_receipt_friends
    "ZoneStreamGenerationReceipt")

extract_slice(
    _stream_receipt_header
    "class alignas(8) ActiveZoneStreamBinding final"
    "RUNTIME_SIZE(ActiveZoneStreamBinding, 0x68, 0xC0);"
    _active_stream_binding_class
    "active zone-stream binding class")
set(_active_stream_binding_friends
    "friend ZoneStreamOwnershipStatus TryBindZoneStreams( ActiveZoneStreamBinding *, ZoneStreamGenerationReceipt *, const zone_load::ZoneLoadContextKey &, const XZoneMemory *, const relocation::BlockView *, std::size_t) noexcept"
    "friend ZoneStreamOwnershipStatus TryInvalidateZoneStreams( ActiveZoneStreamBinding *, ZoneStreamGenerationReceipt *, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend bool AuthenticatePassiveZoneStreamSingleton( const ActiveZoneStreamBinding &binding) noexcept")
require_exact_friend_surface(
    _active_stream_binding_class
    _active_stream_binding_friends
    "ActiveZoneStreamBinding")

extract_slice(
    _pending_receipt_header
    "class alignas(8) PendingCopyAdmissionReceipt final"
    "// Process-lifetime ordered storage"
    _pending_admission_receipt_class
    "pending-copy admission receipt class")
set(_pending_admission_receipt_friends
    "friend class PendingCopyLedger"
    "friend bool AuthenticatePendingCopyAdmissionReceipt( const PendingCopyAdmissionReceipt &, const PendingCopyLedger *, const zone_load::ZoneLoadContextSlot *, const zone_load::ZoneLoadContextKey &, PendingCopyAdmissionPhase, const PendingCopyAdmissionCompletion &) noexcept"
    "friend PendingCopyStatus TryInitializePendingCopyLedger( PendingCopyLedger *) noexcept"
    "friend PendingCopyStatus TryBeginPendingCopyAdmission( PendingCopyLedger *, PendingCopyAdmissionReceipt *, zone_load::ZoneLoadContextSlot *, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend PendingCopyStatus TryAppendPendingCopyRecord( PendingCopyAdmissionReceipt *, const zone_load::ZoneLoadContextKey &, std::uint32_t) noexcept"
    "friend PendingCopyStatus TryReadPendingCopyRecord( const PendingCopyAdmissionReceipt *, const zone_load::ZoneLoadContextKey &, std::uint32_t, PendingCopyRecord *) noexcept"
    "friend PendingCopyStatus TryPreparePendingCopyAdmission( PendingCopyAdmissionReceipt *, const zone_load::ZoneLoadContextKey &, const PendingCopyAdmissionCompletion &) noexcept"
    "friend void FinalizePreparedPendingCopyAdmission( PendingCopyAdmissionReceipt &) noexcept"
    "friend PendingCopyStatus TryDiscardPendingCopyAdmission( PendingCopyAdmissionReceipt *, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend PendingCopyStatus TryFinishPendingCopyDrain( PendingCopyLedger *) noexcept"
    "friend PendingCopyStatus TryResetPendingCopyAdmissionReceipt( PendingCopyAdmissionReceipt *, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend bool db::zone_runtime::detail::IsPristineRuntimeReceipt( const PendingCopyAdmissionReceipt &receipt) noexcept"
    "friend struct PendingCopyLedgerTestAccess")
require_exact_friend_surface(
    _pending_admission_receipt_class
    _pending_admission_receipt_friends
    "PendingCopyAdmissionReceipt")

extract_slice(
    _pending_receipt_header
    "class alignas(8) PendingCopyLedger final"
    "struct PendingCopyLedgerTestAccess final"
    _pending_copy_ledger_class
    "pending-copy ledger class")
set(_pending_copy_ledger_friends
    "friend class PendingCopyAdmissionReceipt"
    "friend PendingCopyStatus TryInitializePendingCopyLedger( PendingCopyLedger *) noexcept"
    "friend PendingCopyStatus TryBeginPendingCopyAdmission( PendingCopyLedger *, PendingCopyAdmissionReceipt *, zone_load::ZoneLoadContextSlot *, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend PendingCopyStatus TryAppendPendingCopyRecord( PendingCopyAdmissionReceipt *, const zone_load::ZoneLoadContextKey &, std::uint32_t) noexcept"
    "friend PendingCopyStatus TryReadPendingCopyRecord( const PendingCopyAdmissionReceipt *, const zone_load::ZoneLoadContextKey &, std::uint32_t, PendingCopyRecord *) noexcept"
    "friend PendingCopyStatus TryPreparePendingCopyAdmission( PendingCopyAdmissionReceipt *, const zone_load::ZoneLoadContextKey &, const PendingCopyAdmissionCompletion &) noexcept"
    "friend void FinalizePreparedPendingCopyAdmission( PendingCopyAdmissionReceipt &) noexcept"
    "friend PendingCopyStatus TryDiscardPendingCopyAdmission( PendingCopyAdmissionReceipt *, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend PendingCopyStatus TryBeginPendingCopyDrain( PendingCopyLedger *) noexcept"
    "friend PendingCopyStatus TryDrainNextPendingCopy( PendingCopyLedger *, const PendingCopyDrainCallback &) noexcept"
    "friend PendingCopyStatus TryFinishPendingCopyDrain( PendingCopyLedger *) noexcept"
    "friend PendingCopyStatus TryResetPendingCopyAdmissionReceipt( PendingCopyAdmissionReceipt *, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend bool AuthenticatePassivePendingCopyLedger( const PendingCopyLedger &ledger) noexcept"
    "friend PendingCopyAuthenticationResult AuthenticatePendingCopyLedgerDescriptors( const PendingCopyLedger &, PendingCopyLedgerAuthenticationPhase, const PendingCopyDescriptorBinding *, std::size_t) noexcept"
    "friend struct PendingCopyLedgerTestAccess")
require_exact_friend_surface(
    _pending_copy_ledger_class
    _pending_copy_ledger_friends
    "PendingCopyLedger")

extract_slice(
    _header
    "class alignas(8) ZoneRuntimeGenerationBinding final"
    "RUNTIME_SIZE(ZoneRuntimeGenerationBinding, 0x50, 0x78);"
    _generation_binding_class
    "generation binding class")
set(_generation_binding_friends
    "friend class ZoneRuntimeTable"
    "friend struct ZoneRuntimeTableTestAccess")
require_exact_friend_surface(
    _generation_binding_class
    _generation_binding_friends
    "ZoneRuntimeGenerationBinding")

extract_slice(
    _header
    "class alignas(8) ZoneRuntimeReceiptCapsule final"
    "RUNTIME_SIZE(ZoneRuntimeReceiptCapsule, 0xD0, 0x120);"
    _receipt_capsule_class
    "receipt capsule class")
set(_receipt_capsule_friends
    "friend class ZoneRuntimeEntry"
    "friend class ZoneRuntimeTable"
    "friend bool detail::IsPristineRuntimeReceipt( const ZoneRuntimeReceiptCapsule &capsule) noexcept"
    "friend struct ZoneRuntimeTableTestAccess")
require_exact_friend_surface(
    _receipt_capsule_class
    _receipt_capsule_friends
    "ZoneRuntimeReceiptCapsule")

extract_slice(
    _header
    "class alignas(8) ZoneRuntimeEntry final"
    "RUNTIME_SIZE(ZoneRuntimeEntry, 0x190, 0x228);"
    _zone_runtime_entry_class
    "zone runtime entry class")
set(_zone_runtime_entry_friends
    "friend class ZoneRuntimeTable"
    "friend bool detail::EntryReceiptsArePristine( const ZoneRuntimeEntry &entry) noexcept"
    "friend ZoneRuntimeTableStatus TryInitializeZoneRuntimeTable( ZoneRuntimeTable *table) noexcept"
    "friend ZoneRuntimeTableStatus TryGetZoneRuntimeEntry( ZoneRuntimeTable *table, std::uint32_t physicalSlot, const ZoneRuntimeEntry **outEntry) noexcept"
    "friend ZoneRuntimeTableStatus TryClaimZoneRuntimeGeneration( ZoneRuntimeTable *table, std::uint32_t physicalSlot, zone_load::ZoneLoadContextKey *inOutKey) noexcept"
    "friend ZoneRuntimeTableStatus TryGetZoneRuntimeGeneration( ZoneRuntimeTable *table, std::uint32_t physicalSlot, const zone_load::ZoneLoadContextKey &key, ZoneRuntimeGenerationView *outView) noexcept"
    "friend ZoneRuntimeTableStatus TryBindZoneRuntimeGenerationCallbacks( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &, const ZoneRuntimeGenerationCallbacks &) noexcept"
    "friend ZoneRuntimeTableStatus TryBeginZoneRuntimePhysicalAllocation( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &, const char *, std::uint32_t) noexcept"
    "friend ZoneRuntimeTableStatus TryAllocateZoneRuntimeMemory( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &, std::uint32_t, std::uint32_t, std::uint32_t, pmem_runtime::AllocationResult *) noexcept"
    "friend ZoneRuntimeTableStatus TryBindZoneRuntimeStorage( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &, void *, std::size_t, const zone_runtime_storage::ZoneRuntimeStoragePlan *) noexcept"
    "friend ZoneRuntimeTableStatus TryBeginZoneRuntimeStreamGeneration( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend ZoneRuntimeTableStatus TryBindZoneRuntimeStreams( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &, const XZoneMemory *, const relocation::BlockView *, std::size_t) noexcept"
    "friend ZoneRuntimeTableStatus TryBeginZoneRuntimePendingCopies( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend ZoneRuntimeTableStatus TryAppendZoneRuntimePendingCopy( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &, std::uint32_t) noexcept"
    "friend ZoneRuntimeTableStatus TryPrepareZoneRuntimeAdmission( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend ZoneRuntimeTableStatus TryInvalidateZoneRuntimeStreams( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend ZoneRuntimeTableStatus TryEndZoneRuntimePhysicalAllocation( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend ZoneRuntimeTableStatus TryCommitZoneRuntimeGeneration( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend ZoneRuntimeTableStatus TryBeginZoneRuntimeGenerationAbandonment( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend ZoneRuntimeTableStatus TryContinueZoneRuntimeGenerationAbandonment( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend ZoneRuntimeTableStatus TryUnloadZoneRuntimeGeneration( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend ZoneRuntimeTableStatus TryUnloadZoneRuntimeGeneration( ZoneRuntimeTable *table, std::uint32_t physicalSlot, const zone_load::ZoneLoadContextKey &key, const zone_load::ZoneLoadCleanupCallbacks &callbacks) noexcept"
    "friend ZoneRuntimeTableStatus TryResetZoneRuntimeTerminalReceipt( ZoneRuntimeTable *table, std::uint32_t physicalSlot, const zone_load::ZoneLoadContextKey &key) noexcept"
    "friend struct ZoneRuntimeTableTestAccess")
require_exact_friend_surface(
    _zone_runtime_entry_class
    _zone_runtime_entry_friends
    "ZoneRuntimeEntry")

extract_slice(
    _header
    "class alignas(8) ZoneRuntimeTable final"
    "RUNTIME_SIZE(ZoneRuntimeTable, 0xF568, 0x109A0);"
    _zone_runtime_table_class
    "zone runtime table class")
set(_zone_runtime_table_friends
    "friend class ZoneRuntimeFacade"
    "friend ZoneRuntimeTableStatus TryInitializeZoneRuntimeTable( ZoneRuntimeTable *table) noexcept"
    "friend ZoneRuntimeTableStatus TryGetZoneRuntimeEntry( ZoneRuntimeTable *table, std::uint32_t physicalSlot, const ZoneRuntimeEntry **outEntry) noexcept"
    "friend ZoneRuntimeTableStatus TryClaimZoneRuntimeGeneration( ZoneRuntimeTable *table, std::uint32_t physicalSlot, zone_load::ZoneLoadContextKey *inOutKey) noexcept"
    "friend ZoneRuntimeTableStatus TryGetZoneRuntimeGeneration( ZoneRuntimeTable *table, std::uint32_t physicalSlot, const zone_load::ZoneLoadContextKey &key, ZoneRuntimeGenerationView *outView) noexcept"
    "friend ZoneRuntimeTableStatus TryBindZoneRuntimeGenerationCallbacks( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &, const ZoneRuntimeGenerationCallbacks &) noexcept"
    "friend ZoneRuntimeTableStatus TryBeginZoneRuntimePhysicalAllocation( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &, const char *, std::uint32_t) noexcept"
    "friend ZoneRuntimeTableStatus TryAllocateZoneRuntimeMemory( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &, std::uint32_t, std::uint32_t, std::uint32_t, pmem_runtime::AllocationResult *) noexcept"
    "friend ZoneRuntimeTableStatus TryBindZoneRuntimeStorage( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &, void *, std::size_t, const zone_runtime_storage::ZoneRuntimeStoragePlan *) noexcept"
    "friend ZoneRuntimeTableStatus TryBeginZoneRuntimeStreamGeneration( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend ZoneRuntimeTableStatus TryBindZoneRuntimeStreams( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &, const XZoneMemory *, const relocation::BlockView *, std::size_t) noexcept"
    "friend ZoneRuntimeTableStatus TryBeginZoneRuntimePendingCopies( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend ZoneRuntimeTableStatus TryAppendZoneRuntimePendingCopy( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &, std::uint32_t) noexcept"
    "friend ZoneRuntimeTableStatus TryGetZoneRuntimePendingCopyView( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &, ZoneRuntimePendingCopyView *) noexcept"
    "friend ZoneRuntimeTableStatus TryReadZoneRuntimePendingCopy( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &, std::uint32_t, std::uint32_t, zone_pending_copy::PendingCopyRecord *) noexcept"
    "friend ZoneRuntimeTableStatus TryPrepareZoneRuntimeAdmission( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend ZoneRuntimeTableStatus TryInvalidateZoneRuntimeStreams( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend ZoneRuntimeTableStatus TryEndZoneRuntimePhysicalAllocation( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend ZoneRuntimeTableStatus TryCommitZoneRuntimeGeneration( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend ZoneRuntimeTableStatus TryBeginZoneRuntimeGenerationAbandonment( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend ZoneRuntimeTableStatus TryContinueZoneRuntimeGenerationAbandonment( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend ZoneRuntimeTableStatus TryUnloadZoneRuntimeGeneration( ZoneRuntimeTable *, std::uint32_t, const zone_load::ZoneLoadContextKey &) noexcept"
    "friend ZoneRuntimeTableStatus TryBeginZoneRuntimePendingCopyDrain( ZoneRuntimeTable *, const zone_pending_copy::PendingCopyDrainCallback &) noexcept"
    "friend ZoneRuntimeTableStatus TryDrainNextZoneRuntimePendingCopy( ZoneRuntimeTable *) noexcept"
    "friend ZoneRuntimeTableStatus TryFinishZoneRuntimePendingCopyDrain( ZoneRuntimeTable *) noexcept"
    "friend ZoneRuntimeTableStatus TryUnloadZoneRuntimeGeneration( ZoneRuntimeTable *table, std::uint32_t physicalSlot, const zone_load::ZoneLoadContextKey &key, const zone_load::ZoneLoadCleanupCallbacks &callbacks) noexcept"
    "friend ZoneRuntimeTableStatus TryResetZoneRuntimeTerminalReceipt( ZoneRuntimeTable *table, std::uint32_t physicalSlot, const zone_load::ZoneLoadContextKey &key) noexcept"
    "friend ZoneRuntimeTableStatus TryBeginZoneRuntimeScriptStringOwnership( ZoneRuntimeTable *table, std::uint32_t physicalSlot, const zone_load::ZoneLoadContextKey &key, script_string_journal::ScriptStringJournal *journal, script_string_journal::ScriptStringJournalEntry *storage, std::uint32_t storageCapacity, std::uint32_t expectedCount) noexcept"
    "friend ZoneRuntimeTableStatus TryStageZoneRuntimeScriptString( ZoneRuntimeTable *table, std::uint32_t physicalSlot, const zone_load::ZoneLoadContextKey &key, const script_string_adapter::ScriptStringSourceView &source, std::uint32_t *outStringId) noexcept"
    "friend ZoneRuntimeTableStatus TrySealZoneRuntimeScriptStrings( ZoneRuntimeTable *table, std::uint32_t physicalSlot, const zone_load::ZoneLoadContextKey &key) noexcept"
    "friend ZoneRuntimeTableStatus TryBeginZoneRuntimeScriptStringTransfer( ZoneRuntimeTable *table, std::uint32_t physicalSlot, const zone_load::ZoneLoadContextKey &key) noexcept"
    "friend ZoneRuntimeTableStatus TryTransferNextZoneRuntimeScriptString( ZoneRuntimeTable *table, std::uint32_t physicalSlot, const zone_load::ZoneLoadContextKey &key) noexcept"
    "friend ZoneRuntimeTableStatus TryPrepareZoneRuntimeScriptStringCommit( ZoneRuntimeTable *table, std::uint32_t physicalSlot, const zone_load::ZoneLoadContextKey &key) noexcept"
    "friend ZoneRuntimeTableStatus TryCommitZoneRuntimeScriptStringsAndAdmit( ZoneRuntimeTable *table, std::uint32_t physicalSlot, const zone_load::ZoneLoadContextKey &key, const zone_script_string_ownership:: ZoneScriptStringAdmissionCallback &admission) noexcept"
    "friend ZoneRuntimeTableStatus TryBeginZoneRuntimeScriptStringRollback( ZoneRuntimeTable *table, std::uint32_t physicalSlot, const zone_load::ZoneLoadContextKey &key, const zone_script_string_ownership:: ZoneScriptStringRollbackCallbacks &callbacks) noexcept"
    "friend ZoneRuntimeTableStatus TryRollbackNextZoneRuntimeScriptString( ZoneRuntimeTable *table, std::uint32_t physicalSlot, const zone_load::ZoneLoadContextKey &key) noexcept"
    "friend ZoneRuntimeTableStatus TryFinishZoneRuntimeScriptStringAbandonment( ZoneRuntimeTable *table, std::uint32_t physicalSlot, const zone_load::ZoneLoadContextKey &key) noexcept"
    "friend struct ZoneRuntimeTableTestAccess")
require_exact_friend_surface(
    _zone_runtime_table_class
    _zone_runtime_table_friends
    "ZoneRuntimeTable")

# The exact friend lists catch direct and aliased declarations. Freezing each
# normalized authority-class body also catches a neutral-name macro invocation
# whose definition arrives through an included header or build definition and
# expands only after this raw-source pass.
require_exact_class_digest(_physical_allocation_receipt_class
    75f83533a5d4b9988e78803421caa663b9e3e8050012560251fe5e870190bc0b
    "AllocationReceipt")
require_exact_class_digest(_runtime_storage_binding_class
    ea0ec938bd46b81eb0a02df459633751970c4911c67317a5ddfde915860cb907
    "ZoneRuntimeStorageBinding")
require_exact_class_digest(_stream_generation_receipt_class
    0e94ce08089afadd3198fa52a914d8de83380717869753d91fb64cd39247638b
    "ZoneStreamGenerationReceipt")
require_exact_class_digest(_active_stream_binding_class
    43068d2cdd450876889c053f7df29cf0011558ff85a9dd7a54a5282560f4719f
    "ActiveZoneStreamBinding")
require_exact_class_digest(_pending_admission_receipt_class
    b1aacac21746323366412a95be63f642927984e027a39f930d326173fe4a6a3a
    "PendingCopyAdmissionReceipt")
require_exact_class_digest(_pending_copy_ledger_class
    501f278a5fede51712aa454d786f5f225247fe4f79427df9c09c050ca2739065
    "PendingCopyLedger")
require_exact_class_digest(_generation_binding_class
    80071a79d85862bab72efee7fc388bbe82cb91bac07616ca9aa348cfd911784d
    "ZoneRuntimeGenerationBinding")
require_exact_class_digest(_receipt_capsule_class
    bf472ac7720169338debff211f4986e00e50233386054eccd0d2a96cd0b287f1
    "ZoneRuntimeReceiptCapsule")
require_exact_class_digest(_zone_runtime_entry_class
    4f2c0c8a116a52a6a291a8715254951e2ad25093196fcb34f706044641f87bd9
    "ZoneRuntimeEntry")
require_exact_class_digest(_zone_runtime_table_class
    b3337c16218f4f8d55ebea7ef617b4e4fc183ca318cbe9ce4256910d9c7f06be
    "ZoneRuntimeTable")

set(_external_macro_friend_invocation_fixture "${_receipt_capsule_class}")
string(REGEX REPLACE
    "};[ ]*$" "runtime_access_hook };"
    _external_macro_friend_invocation_fixture
    "${_external_macro_friend_invocation_fixture}")
class_digest_matches_exact(
    _external_macro_friend_invocation_fixture
    bf472ac7720169338debff211f4986e00e50233386054eccd0d2a96cd0b287f1
    _external_macro_friend_invocation_accepted)
if(_external_macro_friend_invocation_accepted)
    message(FATAL_ERROR
        "Authority class digest missed an externally defined macro invocation")
endif()

# Every authority-bearing header is deliberately macro-definition-free. A
# macro declared before a class could otherwise expand to a friend declaration
# inside the class while evading a raw-token friend census.
foreach(_authority_header_raw IN ITEMS
    _runtime_receipt_header_raw
    _physical_receipt_header_raw
    _stream_receipt_header_raw
    _pending_receipt_header_raw
    _storage_receipt_header_raw)
    require_no_define_directive(
        ${_authority_header_raw}
        "${_authority_header_raw}")
endforeach()
require_no_define_directive(
    _runtime_source_raw
    "runtime-table exact-controller implementation")

set(_runtime_friend_namespace_alias_bypass
    "${_receipt_capsule_class} namespace runtime_alias = db::zone_runtime::detail; friend bool runtime_alias::MutateReceipt(ZoneRuntimeReceiptCapsule &) noexcept;")
friend_surface_matches_exact(
    _runtime_friend_namespace_alias_bypass
    _receipt_capsule_friends
    _namespace_alias_surface_matches)
if(_namespace_alias_surface_matches)
    message(FATAL_ERROR
        "Exact friend seal missed a namespace-alias-qualified extra friend")
endif()

set(_runtime_friend_type_alias_bypass
    "${_receipt_capsule_class} using RuntimeReceiptBackdoor = ZoneRuntimeEntry; friend RuntimeReceiptBackdoor;")
friend_surface_matches_exact(
    _runtime_friend_type_alias_bypass
    _receipt_capsule_friends
    _type_alias_surface_matches)
if(_type_alias_surface_matches)
    message(FATAL_ERROR
        "Exact friend seal missed a type-alias-qualified extra friend")
endif()

set(_macro_generated_capsule_friend_fixture
    "#define KISAK_RUNTIME_CAPSULE_EXTRA_FRIEND friend struct HiddenRuntimeMutator;\nclass Capsule { KISAK_RUNTIME_CAPSULE_EXTRA_FRIEND };")
source_has_define_directive(
    _macro_generated_capsule_friend_fixture
    _macro_generated_friend_detected)
if(NOT _macro_generated_friend_detected)
    message(FATAL_ERROR
        "Friend seal missed a macro-generated capsule friend")
endif()
string(CONCAT _spliced_friend_macro_fixture
    "#def"
    "${_friend_surface_backslash}${_friend_surface_line_feed}"
    "ine KISAK_RUNTIME_CAPSULE_EXTRA_FRIEND friend struct HiddenRuntimeMutator;")
source_has_define_directive(
    _spliced_friend_macro_fixture
    _spliced_friend_macro_detected)
if(NOT _spliced_friend_macro_detected)
    message(FATAL_ERROR
        "Friend seal missed a phase-2-spliced macro definition")
endif()
set(_digraph_friend_macro_fixture
    "%:define KISAK_RUNTIME_CAPSULE_EXTRA_FRIEND friend struct HiddenRuntimeMutator;")
source_has_define_directive(
    _digraph_friend_macro_fixture
    _digraph_friend_macro_detected)
if(NOT _digraph_friend_macro_detected)
    message(FATAL_ERROR
        "Friend seal missed a digraph macro definition")
endif()
set(_trigraph_friend_macro_fixture
    "??=define KISAK_RUNTIME_CAPSULE_EXTRA_FRIEND friend struct HiddenRuntimeMutator;")
source_has_define_directive(
    _trigraph_friend_macro_fixture
    _trigraph_friend_macro_detected)
if(NOT _trigraph_friend_macro_detected)
    message(FATAL_ERROR
        "Friend seal missed a trigraph macro definition")
endif()
string(CONCAT _trigraph_spliced_friend_macro_fixture
    "??=def??/"
    "${_friend_surface_line_feed}"
    "ine KISAK_RUNTIME_CAPSULE_EXTRA_FRIEND friend struct HiddenRuntimeMutator;")
source_has_define_directive(
    _trigraph_spliced_friend_macro_fixture
    _trigraph_spliced_friend_macro_detected)
if(NOT _trigraph_spliced_friend_macro_detected)
    message(FATAL_ERROR
        "Friend seal missed a trigraph-spliced macro definition")
endif()
set(_comment_separated_friend_macro_fixture
    "#/**/define KISAK_RUNTIME_CAPSULE_EXTRA_FRIEND friend struct HiddenRuntimeMutator;")
source_has_define_directive(
    _comment_separated_friend_macro_fixture
    _comment_separated_friend_macro_detected)
if(NOT _comment_separated_friend_macro_detected)
    message(FATAL_ERROR
        "Friend seal missed a comment-separated macro definition")
endif()

foreach(_marker IN ITEMS
    "namespace detail"
    "bool IsPristineRuntimeReceipt( const physical_memory::AllocationReceipt &receipt) noexcept"
    "bool IsPristineRuntimeReceipt( const zone_stream_ownership::ZoneStreamGenerationReceipt &receipt) noexcept"
    "bool IsPristineRuntimeReceipt( const zone_pending_copy::PendingCopyAdmissionReceipt &receipt) noexcept"
    "bool IsPristineRuntimeReceipt( const zone_runtime_storage::ZoneRuntimeStorageBinding &binding) noexcept"
    "bool IsPristineRuntimeReceipt( const zone_stream_ownership::ActiveZoneStreamBinding &binding) noexcept"
    "bool IsPristineRuntimeReceipt( const zone_pending_copy::PendingCopyLedger &ledger) noexcept"
    "AuthenticatePassiveZoneStreamSingleton(binding)"
    "AuthenticatePassivePendingCopyLedger(ledger)"
    "bool ZoneRuntimeReceiptCapsule::isPristine() const noexcept"
    "detail::EntryReceiptsArePristine(entry)")
    require_contains(_source "${_marker}" "passive pristine authentication")
endforeach()

# A const-reference friend still has full private access. Freeze every friend
# overload to its one read-only predicate so later const_cast, address escape,
# assignment, or authority operation cannot hide behind the narrow signature.
require_exact_pristine_overload(
    "bool IsPristineRuntimeReceipt( const physical_memory::AllocationReceipt &receipt) noexcept"
    "bool IsPristineRuntimeReceipt( const zone_stream_ownership::ZoneStreamGenerationReceipt &receipt) noexcept"
    receipt
    "physical-memory receipt predicate")
require_exact_pristine_overload(
    "bool IsPristineRuntimeReceipt( const zone_stream_ownership::ZoneStreamGenerationReceipt &receipt) noexcept"
    "bool IsPristineRuntimeReceipt( const zone_pending_copy::PendingCopyAdmissionReceipt &receipt) noexcept"
    receipt
    "stream-generation receipt predicate")
require_exact_pristine_overload(
    "bool IsPristineRuntimeReceipt( const zone_pending_copy::PendingCopyAdmissionReceipt &receipt) noexcept"
    "bool IsPristineRuntimeReceipt( const zone_runtime_storage::ZoneRuntimeStorageBinding &binding) noexcept"
    receipt
    "pending-copy receipt predicate")
require_exact_pristine_overload(
    "bool IsPristineRuntimeReceipt( const zone_runtime_storage::ZoneRuntimeStorageBinding &binding) noexcept"
    "bool IsPristineRuntimeReceipt( const zone_stream_ownership::ActiveZoneStreamBinding &binding) noexcept"
    binding
    "runtime-storage binding predicate")
require_exact_shared_authentication_overload(
    "bool IsPristineRuntimeReceipt( const zone_stream_ownership::ActiveZoneStreamBinding &binding) noexcept"
    "bool IsPristineRuntimeReceipt( const zone_pending_copy::PendingCopyLedger &ledger) noexcept"
    binding
    "zone_stream_ownership::AuthenticatePassiveZoneStreamSingleton"
    "active-stream binding predicate")
require_exact_shared_authentication_overload(
    "bool IsPristineRuntimeReceipt( const zone_pending_copy::PendingCopyLedger &ledger) noexcept"
    "bool IsPristineRuntimeReceipt( const ZoneRuntimeReceiptCapsule &capsule) noexcept"
    ledger
    "zone_pending_copy::AuthenticatePassivePendingCopyLedger"
    "pending-copy ledger predicate")
require_exact_pristine_overload(
    "bool IsPristineRuntimeReceipt( const ZoneRuntimeReceiptCapsule &capsule) noexcept"
    "bool EntryReceiptsArePristine(const ZoneRuntimeEntry &entry) noexcept"
    capsule
    "receipt-capsule predicate")

extract_slice(
    _source
    "bool EntryReceiptsArePristine(const ZoneRuntimeEntry &entry) noexcept"
    "} // namespace detail"
    _entry_receipts_predicate
    "entry receipt predicate")
string(STRIP "${_entry_receipts_predicate}" _entry_receipts_predicate)
set(_expected_entry_receipts_predicate
    "bool EntryReceiptsArePristine(const ZoneRuntimeEntry &entry) noexcept { return IsPristineRuntimeReceipt(entry.receiptCapsule_); }")
if(NOT _entry_receipts_predicate STREQUAL
    _expected_entry_receipts_predicate)
    message(FATAL_ERROR
        "Entry receipt predicate gained mutable authority or side effects")
endif()

extract_slice(
    _source
    "bool ZoneRuntimeReceiptCapsule::isPristine() const noexcept"
    "namespace {"
    _capsule_pristine_predicate
    "receipt capsule pristine predicate")
string(STRIP "${_capsule_pristine_predicate}" _capsule_pristine_predicate)
set(_expected_capsule_pristine_predicate
    "bool ZoneRuntimeReceiptCapsule::isPristine() const noexcept { return detail::IsPristineRuntimeReceipt(allocationReceipt_) && detail::IsPristineRuntimeReceipt(streamGenerationReceipt_) && detail::IsPristineRuntimeReceipt(pendingCopyAdmissionReceipt_) && detail::IsPristineRuntimeReceipt(storageBinding_); }")
if(NOT _capsule_pristine_predicate STREQUAL
    _expected_capsule_pristine_predicate)
    message(FATAL_ERROR
        "Receipt capsule predicate must contain exactly four pristine checks")
endif()

# The component-global seals delegate only this translation unit to the exact
# controller seal. Freeze every enrolled qualified authority identifier and
# direct call independently so an extra call, import, pointer extraction,
# alias, or raw checked-PMem bypass cannot hide behind that delegation.
set(_reviewed_composite_calls
    "zone_stream_ownership::TryBeginZoneStreamGeneration|1"
    "zone_stream_ownership::TryBindZoneStreams|1"
    "zone_stream_ownership::TryInvalidateZoneStreams|2"
    "zone_stream_ownership::AuthenticateZoneStreamComposition|1"
    "zone_stream_ownership::AuthenticateZoneStreamOutputSpan|1"
    "zone_pending_copy::TryInitializePendingCopyLedger|1"
    "zone_pending_copy::TryBeginPendingCopyAdmission|1"
    "zone_pending_copy::TryAppendPendingCopyRecord|1"
    "zone_pending_copy::TryReadPendingCopyRecord|1"
    "zone_pending_copy::TryPreparePendingCopyAdmission|1"
    "zone_pending_copy::FinalizePreparedPendingCopyAdmission|1"
    "zone_pending_copy::TryDiscardPendingCopyAdmission|2"
    "zone_pending_copy::TryBeginPendingCopyDrain|1"
    "zone_pending_copy::TryDrainNextPendingCopy|1"
    "zone_pending_copy::TryFinishPendingCopyDrain|1"
    "zone_pending_copy::TryResetPendingCopyAdmissionReceipt|1"
    "zone_pending_copy::AuthenticatePendingCopyAdmissionReceipt|1"
    "zone_pending_copy::AuthenticatePendingCopyLedgerDescriptors|2"
    "zone_runtime_storage::TryBindZoneRuntimeStorage|1"
    "zone_runtime_storage::detail::TryBindFxRuntimeStorage|1"
    "zone_runtime_storage::TryDestroyZoneRuntimeStorage|2"
    "zone_runtime_storage::AuthenticateZoneRuntimeStorageComposition|1"
    "pmem_runtime::TryBeginAllocationReceipt|1"
    "pmem_runtime::TryAllocate|1"
    "pmem_runtime::TryAuthenticateAllocationReceipt|1"
    "pmem_runtime::TryAuthenticateAllocationRange|2"
    "pmem_runtime::TryEndAllocationReceipt|2"
    "pmem_runtime::TryFreeAllocationReceipt|1"
    "pmem_runtime::StorageIsOutsideManagedMemory|6")
foreach(_reviewed_call IN LISTS _reviewed_composite_calls)
    string(REPLACE "|" ";" _reviewed_call_fields "${_reviewed_call}")
    list(GET _reviewed_call_fields 0 _reviewed_call_name)
    list(GET _reviewed_call_fields 1 _reviewed_call_count)
    require_substring_count(
        _source "${_reviewed_call_name}" ${_reviewed_call_count}
        "exact composite authority identifier enrollment")
    require_substring_count(
        _source "${_reviewed_call_name}(" ${_reviewed_call_count}
        "exact direct composite authority call enrollment")
endforeach()
set(_authority_import_detector_fixture
    "zone_pending_copy::TryAppendPendingCopyRecord(...); "
    "using zone_pending_copy::TryAppendPendingCopyRecord; "
    "TryAppendPendingCopyRecord(...);")
require_substring_count(
    _authority_import_detector_fixture
    "zone_pending_copy::TryAppendPendingCopyRecord"
    2
    "qualified identifier counting catches imports and extraction")
require_substring_count(
    _authority_import_detector_fixture
    "zone_pending_copy::TryAppendPendingCopyRecord("
    1
    "qualified direct-call counting distinguishes imported calls")
set(_authority_extraction_detector_fixture
    "auto operation = &zone_pending_copy::TryAppendPendingCopyRecord; "
    "operation(...);")
require_substring_count(
    _authority_extraction_detector_fixture
    "zone_pending_copy::TryAppendPendingCopyRecord"
    1
    "qualified identifier counting observes authority extraction")
require_substring_count(
    _authority_extraction_detector_fixture
    "zone_pending_copy::TryAppendPendingCopyRecord("
    0
    "direct-call counting rejects authority extraction substitution")

set(_namespace_alias_pattern
    "namespace[ ]+[A-Za-z_][A-Za-z0-9_]*[ ]*=")
if(_source MATCHES "${_namespace_alias_pattern}")
    message(FATAL_ERROR
        "Runtime-table controller cannot use namespace aliases around its authority seal")
endif()
set(_namespace_alias_detector_fixture
    "namespace arbitrary = zone_pending_copy;")
if(NOT _namespace_alias_detector_fixture MATCHES
    "${_namespace_alias_pattern}")
    message(FATAL_ERROR
        "Runtime-table authority seal lost generic namespace-alias detection")
endif()
foreach(_forbidden_operation IN ITEMS
    "physical_memory::TryBegin("
    "physical_memory::TryEnd("
    "physical_memory::TryFree("
    "using namespace physical_memory"
    "using namespace zone_stream_ownership"
    "using namespace zone_pending_copy"
    "using namespace zone_runtime_storage"
    )
    require_not_contains(
        _source "${_forbidden_operation}"
        "no raw operation or alias bypass in exact controller")
endforeach()

# The storage adapter crosses into EffectsCore only through the private
# database bridge. Freeze the complete status mapping and require rollback of
# the just-placed binding before a recoverable bind failure is returned.
extract_slice(
    _source
    "ZoneRuntimeTableStatus TryBindZoneRuntimeStorage("
    "ZoneRuntimeTableStatus TryBeginZoneRuntimeStreamGeneration("
    _storage_bind_adapter
    "exact private FX storage-bind adapter")
foreach(_marker IN ITEMS
    "zone_runtime_storage::detail::TryBindFxRuntimeStorage("
    "zone_runtime_storage::detail::FxRuntimeStorageBindStatus"
    "case FxBindStatus::Busy:"
    "case FxBindStatus::InvalidArgument:"
    "case FxBindStatus::MisalignedStorage:"
    "case FxBindStatus::SizeOverflow:"
    "case FxBindStatus::InsufficientCapacity:"
    "case FxBindStatus::InvalidPhase:"
    "case FxBindStatus::TransactionLimit:"
    "case FxBindStatus::InvalidTransaction:"
    "zone_runtime_storage::TryDestroyZoneRuntimeStorage(&storage)"
    "ZoneRuntimeStorageBindingPhase::Destroyed"
    "storage.~ZoneRuntimeStorageBinding();"
    "new (&storage) zone_runtime_storage::ZoneRuntimeStorageBinding{};")
    require_contains(
        _storage_bind_adapter "${_marker}"
        "complete private FX bind status and rollback mapping")
endforeach()
foreach(_marker IN ITEMS
    "ZoneRuntimeTable::retainGenerationPlacement("
    "retainedPlan->scriptStringCapacity"
    "storage.scriptStringEntries(), retainedPlan->scriptStringCapacity, 0);"
    "ZoneRuntimeSetupStage::StorageBound")
    require_contains(
        _storage_bind_adapter "${_marker}"
        "capacity retention with zero pre-begin demand")
endforeach()
require_ordered(
    _storage_bind_adapter
    "zone_runtime_storage::TryBindZoneRuntimeStorage("
    "zone_runtime_storage::detail::TryBindFxRuntimeStorage("
    "placement precedes native-arena bind")
require_ordered(
    _storage_bind_adapter
    "zone_runtime_storage::detail::TryBindFxRuntimeStorage("
    "ZoneRuntimeTable::retainGenerationPlacement("
    "native-arena bind precedes placement retention")
require_ordered(
    _storage_bind_adapter
    "retainedPlan->scriptStringCapacity"
    "ZoneRuntimeSetupStage::StorageBound"
    "capacity and zero demand retain before storage-stage publication")
require_not_contains(
    _storage_bind_adapter "->TryBind("
    "runtime table cannot call the complete EffectsCore arena directly")

# Capacity is retained at storage bind, while expected acquisition demand must
# remain zero until the lower ownership controller successfully begins.  The
# generation witness rejects every other pre-begin shape, including a non-null
# zero-capacity entry pointer and expected demand greater than capacity.
extract_slice(
    _source
    "bool ZoneRuntimeGenerationBinding::canonicalFor("
    "bool ZoneRuntimeGenerationBinding::callbacksMatch("
    _generation_binding_canonical
    "capacity-aware generation binding authentication")
foreach(_marker IN ITEMS
    "placementExpectedCount_ > placementCapacity_"
    "(placementCapacity_ == 0) != (placementEntries_ == nullptr)"
    "setupStage_ < ZoneRuntimeSetupStage::ScriptStringsBegun"
    "placementExpectedCount_ != 0")
    require_contains(
        _generation_binding_canonical "${_marker}"
        "bounded capacity/demand generation witness")
endforeach()

extract_slice(
    _source
    "bool ZoneRuntimeTable::generationPlacementMatches("
    "void ZoneRuntimeTable::bindGeneration("
    _pre_begin_placement_match
    "pre-begin placement authentication")
foreach(_marker IN ITEMS
    "binding.placementJournal_ == journal"
    "binding.placementEntries_ == storage"
    "binding.placementCapacity_ == capacity"
    "binding.placementExpectedCount_ == 0"
    "expectedCount <= capacity")
    require_contains(
        _pre_begin_placement_match "${_marker}"
        "pre-begin zero-demand capacity match")
endforeach()

extract_slice(
    _source
    "bool ZoneRuntimeTable::exactRegistryLifecycleCallbackPhaseMatches("
    "void ZoneRuntimeTable::bindGeneration("
    _registry_callback_phase_matrix
    "exact registry lifecycle callback phase matrix")
foreach(_marker IN ITEMS
    "binding.canonicalFor( binding.table_, &lifecycle, entry.key_)"
    "controller.canonicalForBinding(&lifecycle, entry.key_)"
    "controller.serializerRetained()"
    "controller.poisoned()"
    "case OwnershipPhase::UnpublishingCallback:"
    "case OwnershipPhase::Cleaning:"
    "case OwnershipPhase::Admitting:"
    "case OwnershipPhase::UnloadingCallback:"
    "ZoneRuntimeExecutionMode::Abandoning"
    "ZoneRuntimeExecutionMode::Admitting"
    "ZoneRuntimeExecutionMode::Unloading"
    "ZoneRuntimeSetupStage::ScriptStringsBegun"
    "ZoneRuntimeSetupStage::AllocationEnded"
    "ZoneLoadTerminalKind::Abandoned"
    "ZoneLoadTerminalKind::Unloaded"
    "lifecycle.cleanupActive()"
    "!lifecycle.cleanupActive()")
    require_contains(
        _registry_callback_phase_matrix "${_marker}"
        "closed registry lifecycle callback phase matrix")
endforeach()
require_regex_count(
    _registry_callback_phase_matrix
    "case[ ]+OwnershipPhase::[A-Za-z]+Callback:|case[ ]+OwnershipPhase::(Cleaning|Admitting):"
    4
    "exact four registry-authorized ownership callback phases")

extract_slice(
    _source
    "ZoneRuntimeTableStatus ZoneRuntimeTable::authenticateExactRegistryLifecycleCallback("
    "ZoneRuntimeTableStatus ZoneRuntimeTable::restoreExactRegistryLifecycleCallback("
    _registry_callback_consumption
    "exact registry lifecycle callback consumption")
foreach(_marker IN ITEMS
    "authenticateExactLifecycleCallbackMarker( physicalSlot, key, Marker::ActiveRegistryBorrow)"
    "if (status == ZoneRuntimeTableStatus::Success)"
    "entries_[physicalSlot].generationBinding_.callbackMarker_ = Marker::ActiveNoRegistry;"
    "return status;")
    require_contains(
        _registry_callback_consumption "${_marker}"
        "success-only callback registry authority consumption")
endforeach()
require_ordered(
    _registry_callback_consumption
    "authenticateExactLifecycleCallbackMarker("
    "callbackMarker_ = Marker::ActiveNoRegistry;"
    "callback marker authenticates before consumption")

extract_slice(
    _source
    "ZoneRuntimeTableStatus ZoneRuntimeTable::restoreExactRegistryLifecycleCallback("
    "ZoneRuntimeTableStatus ZoneRuntimeTable::authenticateExactLifecycleCallbackMarker("
    _registry_callback_restoration
    "exact registry lifecycle callback restoration")
foreach(_marker IN ITEMS
    "authenticateExactLifecycleCallbackMarker( physicalSlot, key, Marker::ActiveNoRegistry)"
    "if (status == ZoneRuntimeTableStatus::Success)"
    "entries_[physicalSlot].generationBinding_.callbackMarker_ = Marker::ActiveRegistryBorrow;"
    "return status;")
    require_contains(
        _registry_callback_restoration "${_marker}"
        "success-only callback registry authority restoration")
endforeach()
require_ordered(
    _registry_callback_restoration
    "authenticateExactLifecycleCallbackMarker("
    "callbackMarker_ = Marker::ActiveRegistryBorrow;"
    "callback marker authenticates before restoration")

extract_slice(
    _source
    "ZoneRuntimeTableStatus ZoneRuntimeTable::authenticateExactLifecycleCallbackMarker("
    "ZoneRuntimeTableStatus ZoneRuntimeTable::authenticateExactPendingCopyRead("
    _registry_callback_authentication
    "non-consuming exact registry lifecycle callback authentication")
foreach(_marker IN ITEMS
    "expectedMarker == Marker::Idle"
    "expectedMarker > Marker::ActiveRegistryBorrow"
    "HasKnownState(state_)"
    "HasKnownSharedState(sharedState_)"
    "ValidateUsableSlot(physicalSlot)"
    "if (key.slot != physicalSlot)"
    "if (entry.key_ != key) return ZoneRuntimeTableStatus::StaleKey;"
    "if (entry.lifecycle_.slotIndex() != physicalSlot || entry.lifecycle_.generation() != key.generation) { poison(); return ZoneRuntimeTableStatus::UnsafeFailure; }"
    "validateSharedComposition()"
    "binding.canonicalFor(this, &entry.lifecycle_, key)"
    "std::size_t activeMarkerCount = 0;"
    "const ZoneRuntimeEntry *activeMarkerEntry = nullptr;"
    "marker > Marker::ActiveRegistryBorrow"
    "if (activeMarkerCount != 1)"
    "activeMarkerEntry = &candidate;"
    "if (activeMarkerEntry && activeMarkerEntry != &entry)"
    "binding.callbackMarker_ == Marker::Idle"
    "binding.callbackMarker_ != expectedMarker"
    "return ZoneRuntimeTableStatus::Busy;"
    "activeMarkerCount != 1"
    "tableStatus != ZoneRuntimeTableStatus::Busy"
    "entry.scriptStringOwnership_.canonicalForBinding( &entry.lifecycle_, key)"
    "exactRegistryLifecycleCallbackPhaseMatches(entry)"
    "This helper only authenticates the marker. Mutable registry admission"
    "pending-copy inspection deliberately leaves the borrow intact."
    "return ZoneRuntimeTableStatus::Success;")
    require_contains(
        _registry_callback_authentication "${_marker}"
        "whole-table sole-binding callback authentication")
endforeach()
require_ordered(
    _registry_callback_authentication
    "if (entry.key_ != key)"
    "if (entry.lifecycle_.slotIndex() != physicalSlot"
    "requested callback key mismatch precedes internal lifecycle corruption")
require_ordered(
    _registry_callback_authentication
    "if (entry.lifecycle_.slotIndex() != physicalSlot"
    "validateSharedComposition()"
    "callback lifecycle contradiction poisons before shared traversal")
require_ordered(
    _registry_callback_authentication
    "validateSharedComposition()"
    "std::size_t activeMarkerCount = 0;"
    "whole-table validation precedes sole callback-marker census")
require_ordered(
    _registry_callback_authentication
    "binding.callbackMarker_ != expectedMarker"
    "exactRegistryLifecycleCallbackPhaseMatches(entry)"
    "mismatched callback transitions classify Busy before phase checks")
require_not_contains(
    _registry_callback_authentication
    "callbackMarker_ = Marker::"
    "callback marker authentication must remain non-consuming")

extract_slice(
    _source
    "ZoneRuntimeTableStatus ZoneRuntimeTable::authenticateExactPendingCopyRead("
    "ZoneRuntimeTableStatus ZoneRuntimeTable::authenticateExactPendingCopyOutput("
    _pending_copy_read_authentication
    "exact pending-copy read authentication")
foreach(_marker IN ITEMS
    "if (!outEntry)"
    "HasKnownState(state_)"
    "HasKnownSharedState(sharedState_)"
    "ValidateUsableSlot(physicalSlot)"
    "if (key.slot != physicalSlot)"
    "if (entry.key_ != key) return ZoneRuntimeTableStatus::StaleKey;"
    "if (entry.lifecycle_.slotIndex() != physicalSlot || entry.lifecycle_.generation() != key.generation) { poison(); return ZoneRuntimeTableStatus::UnsafeFailure; }"
    "candidateMarker > Marker::ActiveRegistryBorrow"
    "if (marker != Marker::Idle)"
    "authenticateExactLifecycleCallbackMarker( physicalSlot, key, Marker::ActiveRegistryBorrow)"
    "The authentication above is non-mutating."
    "validateInitializedHeader()"
    "authenticateExactEntry(physicalSlot, key)"
    "static_cast<SharedState>(sharedState_) == SharedState::Draining"
    "entry.setupStage() < ZoneRuntimeSetupStage::PendingCopyBegun"
    "const auto *const receipt = pendingCopyAdmissionReceipt(&entry)"
    "case PendingPhase::Collecting:"
    "case PendingPhase::Prepared:"
    "case PendingPhase::Admitted:"
    "case PendingPhase::Drained:"
    "case PendingPhase::Discarded:"
    "*outEntry = &entry;"
    "case PendingPhase::Admitting:"
    "case PendingPhase::Pristine:"
    "case PendingPhase::UnsafeFailure:"
    "poison();")
    require_contains(
        _pending_copy_read_authentication "${_marker}"
        "quiescent-or-exact-callback pending-copy read gate")
endforeach()
require_ordered(
    _pending_copy_read_authentication
    "if (entry.key_ != key)"
    "if (entry.lifecycle_.slotIndex() != physicalSlot"
    "requested read key mismatch precedes internal lifecycle corruption")
require_ordered(
    _pending_copy_read_authentication
    "if (entry.lifecycle_.slotIndex() != physicalSlot"
    "const Marker marker ="
    "pending-read lifecycle contradiction poisons before marker traversal")
require_ordered(
    _pending_copy_read_authentication
    "if (marker != Marker::Idle)"
    "authenticateExactLifecycleCallbackMarker("
    "non-idle pending-copy reads require callback authentication")
require_ordered(
    _pending_copy_read_authentication
    "case PendingPhase::Discarded:"
    "*outEntry = &entry;"
    "entry pointer publishes only for the closed readable phase set")
require_not_contains(
    _pending_copy_read_authentication
    "callbackMarker_ = Marker::"
    "pending-copy inspection cannot consume callback registry authority")

extract_slice(
    _source
    "ZoneRuntimeTableStatus ZoneRuntimeTable::authenticateExactPendingCopyOutput("
    "zone_load::ZoneLoadContextSlot *ZoneRuntimeTable::mutableLifecycle("
    _pending_copy_output_authentication
    "exact pending-copy output authentication")
foreach(_marker IN ITEMS
    "const ZoneRuntimeEntry *entry = nullptr;"
    "authenticateExactPendingCopyRead(physicalSlot, key, &entry)"
    "if (authentication != ZoneRuntimeTableStatus::Success) return authentication;"
    "if (!entry || !zone_slots::IsUsableZoneSlot(physicalSlot) || entry != &entries_[physicalSlot])"
    "poison();"
    "return ZoneRuntimeTableStatus::UnsafeFailure;"
    "WritableOutputIsSeparatedFromRetainedRuntime( activeZoneStreamBinding_, static_cast<SharedState>(sharedState_), output, outputSize, outputAlignment)"
    "? ZoneRuntimeTableStatus::Success : ZoneRuntimeTableStatus::InvalidArgument;")
    require_contains(
        _pending_copy_output_authentication "${_marker}"
        "table-owned exact pending-copy caller-output authentication")
endforeach()
require_ordered(
    _pending_copy_output_authentication
    "authenticateExactPendingCopyRead("
    "if (authentication != ZoneRuntimeTableStatus::Success)"
    "pending-copy output exact-key authentication precedes status handling")
require_ordered(
    _pending_copy_output_authentication
    "if (authentication != ZoneRuntimeTableStatus::Success)"
    "entry != &entries_[physicalSlot]"
    "pending-copy output status handling precedes canonical-entry validation")
require_ordered(
    _pending_copy_output_authentication
    "entry != &entries_[physicalSlot]"
    "WritableOutputIsSeparatedFromRetainedRuntime("
    "pending-copy output canonical entry validation precedes retained-span authentication")
require_substring_count(
    _source "authenticateExactPendingCopyOutput(" 3
    "one private definition and two reviewed pending-copy output gates")

extract_slice(
    _source
    "ZoneRuntimeTable::EnsureBoundGenerationUnreachable("
    "ZoneRuntimeTable::PerformBoundGenerationCleanup("
    _registry_unpublish_callback_window
    "registry-authorized unpublish callback window")
foreach(_marker IN ITEMS
    "CallbackMarker::Idle"
    "exactRegistryLifecycleCallbackPhaseMatches( *entry)"
    "CallbackMarker::ActiveRegistryBorrow"
    "CallbackMarker::ActiveNoRegistry"
    "binding.callbacks_.ensureUnreachable( binding.callbacks_.context)"
    "TryDiscardPendingCopyAdmission(")
    require_contains(
        _registry_unpublish_callback_window "${_marker}"
        "unpublish callback registry boundary")
endforeach()
require_ordered(
    _registry_unpublish_callback_window
    "CallbackMarker::ActiveRegistryBorrow"
    "binding.callbacks_.ensureUnreachable( binding.callbacks_.context)"
    "unpublish callback publishes authority only before invocation")
require_ordered(
    _registry_unpublish_callback_window
    "binding.callbacks_.ensureUnreachable( binding.callbacks_.context)"
    "The external callback authority ends before any table-owned pending"
    "unpublish callback revokes authority before internal pending work")
require_ordered(
    _registry_unpublish_callback_window
    "The external callback authority ends before any table-owned pending"
    "TryDiscardPendingCopyAdmission("
    "unpublish callback remains no-registry through internal pending work")

extract_slice(
    _source
    "ZoneRuntimeTable::PerformBoundGenerationCleanup("
    "void ZoneRuntimeTable::CompleteBoundPendingAdmission("
    _registry_cleanup_callback_window
    "registry-authorized external cleanup callback window")
foreach(_marker IN ITEMS
    "CallbackMarker::ActiveNoRegistry"
    "exactRegistryLifecycleCallbackPhaseMatches( *entry)"
    "CallbackMarker::ActiveRegistryBorrow"
    "binding.callbacks_.performExternalCleanup( binding.callbacks_.context, operation)"
    "TryDiscardPendingCopyAdmission("
    "TryInvalidateZoneStreams("
    "TryDestroyZoneRuntimeStorage("
    "TryEndAllocationReceipt("
    "TryFreeAllocationReceipt(")
    require_contains(
        _registry_cleanup_callback_window "${_marker}"
        "external cleanup callback registry boundary")
endforeach()
require_ordered(
    _registry_cleanup_callback_window
    "CallbackMarker::ActiveRegistryBorrow"
    "binding.callbacks_.performExternalCleanup( binding.callbacks_.context, operation)"
    "external cleanup publishes registry authority only for invocation")
require_ordered(
    _registry_cleanup_callback_window
    "binding.callbacks_.performExternalCleanup( binding.callbacks_.context, operation)"
    "if (status == CleanupResult::Success) { binding.callbackMarker_ = ZoneRuntimeGenerationBinding:: CallbackMarker::ActiveNoRegistry;"
    "external cleanup revokes registry authority before internal work")

extract_slice(
    _source
    "void ZoneRuntimeTable::CompleteBoundPendingAdmission("
    "void ZoneRuntimeTable::AdmitBoundGeneration("
    _registry_pending_completion_callback_window
    "registry-authorized pending completion callback window")
foreach(_marker IN ITEMS
    "ZoneRuntimeTable *const table = binding.table_"
    "const zone_load::ZoneLoadContextKey callbackKey = entry->key_"
    "const ZoneRuntimeGenerationBinding::CallbackMarker resumeMarker"
    "const ZoneRuntimeGenerationCallbacks callbackSnapshot = binding.callbacks_"
    "!table->initialized()"
    "CallbackMarker::ActiveNoRegistry"
    "exactRegistryLifecycleCallbackPhaseMatches(*entry)"
    "entry->scriptStringOwnership_.tryBeginRegistryCallbackWindow( zone_script_string_ownership:: ZoneScriptStringOwnershipPhase::Admitting)"
    "table->poison();"
    "CallbackMarker::ActiveRegistryBorrow"
    "callbackSnapshot.completePendingAdmission(callbackSnapshot.context)"
    "entry->key_ != callbackKey"
    "!binding.callbacksMatch(callbackSnapshot)"
    "binding.callbackMarker_ = resumeMarker;")
    require_contains(
        _registry_pending_completion_callback_window "${_marker}"
        "nested pending completion registry boundary")
endforeach()
require_ordered(
    _registry_pending_completion_callback_window
    "exactRegistryLifecycleCallbackPhaseMatches(*entry)"
    "entry->scriptStringOwnership_.tryBeginRegistryCallbackWindow( zone_script_string_ownership:: ZoneScriptStringOwnershipPhase::Admitting)"
    "pending completion authenticates the exact phase before advancing its window")
require_ordered(
    _registry_pending_completion_callback_window
    "entry->scriptStringOwnership_.tryBeginRegistryCallbackWindow( zone_script_string_ownership:: ZoneScriptStringOwnershipPhase::Admitting)"
    "CallbackMarker::ActiveRegistryBorrow"
    "pending completion advances the exact controller window before publishing table authority")
require_ordered(
    _registry_pending_completion_callback_window
    "CallbackMarker::ActiveRegistryBorrow"
    "callbackSnapshot.completePendingAdmission(callbackSnapshot.context)"
    "pending completion publishes authority only for invocation")
require_ordered(
    _registry_pending_completion_callback_window
    "callbackSnapshot.completePendingAdmission(callbackSnapshot.context)"
    "binding.callbackMarker_ = resumeMarker;"
    "pending completion post-authenticates before restoring the caller marker")
require_substring_count(
    _registry_pending_completion_callback_window
    "exactRegistryLifecycleCallbackPhaseMatches(*entry)" 2
    "pending completion has exact pre- and post-callback authentication")
require_not_contains(
    _registry_pending_completion_callback_window
    "const bool registryWindow"
    "pending completion cannot silently downgrade to a no-registry callback")

extract_slice(
    _source
    "void ZoneRuntimeTable::AdmitBoundGeneration("
    "ZoneRuntimeTableStatus ZoneRuntimeTable::completeMutableOperation("
    _registry_admission_callback_window
    "registry-authorized live admission callback window")
foreach(_marker IN ITEMS
    "ZoneRuntimeTable *const table = binding.table_"
    "const zone_load::ZoneLoadContextKey callbackKey = entry->key_"
    "const ZoneRuntimeGenerationCallbacks callbackSnapshot = binding.callbacks_"
    "CallbackMarker::ActiveNoRegistry"
    "FinalizePreparedPendingCopyAdmission("
    "const auto pendingPhase"
    "exactRegistryLifecycleCallbackPhaseMatches(*entry)"
    "entry->scriptStringOwnership_.tryBeginRegistryCallbackWindow( zone_script_string_ownership:: ZoneScriptStringOwnershipPhase::Admitting)"
    "table->poison();"
    "CallbackMarker::ActiveRegistryBorrow"
    "callbackSnapshot.admitLive(callbackSnapshot.context)"
    "entry->key_ != callbackKey"
    "!binding.callbacksMatch(callbackSnapshot)"
    "CallbackMarker::Idle")
    require_contains(
        _registry_admission_callback_window "${_marker}"
        "live admission callback registry boundary")
endforeach()
require_ordered(
    _registry_admission_callback_window
    "CallbackMarker::ActiveNoRegistry"
    "FinalizePreparedPendingCopyAdmission("
    "pending finalization remains internal no-registry work")
require_ordered(
    _registry_admission_callback_window
    "FinalizePreparedPendingCopyAdmission("
    "const auto pendingPhase"
    "pending callback completes before its fail-closed post-authentication")
require_ordered(
    _registry_admission_callback_window
    "exactRegistryLifecycleCallbackPhaseMatches(*entry)"
    "entry->scriptStringOwnership_.tryBeginRegistryCallbackWindow( zone_script_string_ownership:: ZoneScriptStringOwnershipPhase::Admitting)"
    "live admission authenticates the exact phase before advancing its window")
require_ordered(
    _registry_admission_callback_window
    "entry->scriptStringOwnership_.tryBeginRegistryCallbackWindow( zone_script_string_ownership:: ZoneScriptStringOwnershipPhase::Admitting)"
    "CallbackMarker::ActiveRegistryBorrow"
    "live admission advances the exact controller window before publishing table authority")
require_ordered(
    _registry_admission_callback_window
    "CallbackMarker::ActiveRegistryBorrow"
    "callbackSnapshot.admitLive(callbackSnapshot.context)"
    "live admission publishes authority only for invocation")
require_ordered(
    _registry_admission_callback_window
    "callbackSnapshot.admitLive(callbackSnapshot.context)"
    "binding.callbackMarker_ = ZoneRuntimeGenerationBinding::CallbackMarker::Idle;"
    "live admission post-authenticates before revoking authority on return")
require_substring_count(
    _registry_admission_callback_window
    "exactRegistryLifecycleCallbackPhaseMatches(*entry)" 2
    "live admission has exact post-pending and post-live authentication")
require_not_contains(
    _registry_admission_callback_window
    "const bool registryWindow"
    "live admission cannot silently downgrade to a no-registry callback")
foreach(_marker IN ITEMS
    "void TestCompositeAdmissionStopsAfterPendingCallbackFailure()"
    "ownership, UINT8_C(254), UINT8_C(254)"
    "fixture.driver.pendingCompletionCalls == 0"
    "fixture.driver.admitCalls == 0"
    "!fixture.table->initialized()"
    "ZoneScriptStringOwnershipPhase::UnsafeFailure"
    "ownership->serializerRetained()")
    require_contains(
        _fixture "${_marker}"
        "pending admission callback failure blocks the live side effect")
endforeach()
require_substring_count(
    _source "Marker::ActiveRegistryBorrow" 13
    "closed callback registry marker reference surface")
require_substring_count(
    _source "tryBeginRegistryCallbackWindow(" 2
    "only the two nested admission callbacks advance controller windows from the table")

# Writable outputs, retained callback descriptors, and stream descriptors are
# attacker-controlled aliases at this API boundary. Freeze the shared overflow-
# safe range primitive and every preflight/snapshot before table authentication
# or authority mutation, then require publication only after post-authentication.
extract_slice(
    _source
    "bool AddressRangesAreDisjoint("
    "} // namespace"
    _separation_helpers
    "control-state separation helpers")
foreach(_marker IN ITEMS
    "if (!leftStorage || leftSize == 0 || !rightStorage || rightSize == 0)"
    "leftSize > maximum - left || rightSize > maximum - right"
    "return leftEnd <= right || rightEnd <= left;"
    "bool WritableOutputIsSeparated("
    "reinterpret_cast<std::uintptr_t>(output) % outputAlignment != 0"
    "!AddressRangesAreDisjoint( table, sizeof(*table), output, outputSize)"
    "AddressRangesAreDisjoint( inputKey, sizeof(*inputKey), output, outputSize)"
    "bool WritableOutputIsSeparatedFromRetainedRuntime("
    "zone_stream_ownership::AuthenticateZoneStreamOutputSpan( binding, output, outputSize, outputAlignment)"
    "if (sharedState == SharedState::Pristine) return true;"
    "sharedState != SharedState::Ready && sharedState != SharedState::Draining"
    "pmem_runtime::StorageIsOutsideManagedMemory( output, outputSize)")
    require_contains(
        _separation_helpers "${_marker}"
        "overflow-safe exact control-state separation")
endforeach()
require_substring_count(
    _source "WritableOutputIsSeparated(" 8
    "one helper plus seven reviewed writable-output preflights")
require_substring_count(
    _source "WritableOutputIsSeparatedFromRetainedRuntime(" 7
    "one retained-runtime helper plus six reviewed output gates")
require_substring_count(
    _source "zone_stream_ownership::AuthenticateZoneStreamOutputSpan" 1
    "single exact stream-authority output authenticator")
require_substring_count(
    _source "const_cast<ZoneRuntimeEntry *>(&entry)" 1
    "single read-only callback-context identity reconstruction")
require_contains(
    _source
    "descriptor.completion.context = const_cast<ZoneRuntimeEntry *>(&entry);"
    "const cast only reconstructs the retained completion context identity")

function(require_retained_output_gate
    SOURCE_VAR AUTHENTICATION OUTPUT MUTATION DESCRIPTION)
    require_ordered(
        ${SOURCE_VAR}
        "${AUTHENTICATION}"
        "WritableOutputIsSeparatedFromRetainedRuntime("
        "${DESCRIPTION} authenticates retained state before output gating")
    require_contains(
        ${SOURCE_VAR}
        "static_cast<SharedState>(table->sharedState_), ${OUTPUT}, sizeof(*${OUTPUT}), alignof("
        "${DESCRIPTION} passes exact shared state, output span, and alignment")
    require_ordered(
        ${SOURCE_VAR}
        "WritableOutputIsSeparatedFromRetainedRuntime("
        "${MUTATION}"
        "${DESCRIPTION} rejects retained aliases before authority mutation")
endfunction()

function(require_output_preflight
    START END OUTPUT MUTATION POST DESCRIPTION)
    extract_slice(_source "${START}" "${END}" _output_adapter
        "${DESCRIPTION}")
    require_ordered(
        _output_adapter
        "WritableOutputIsSeparated("
        "authenticateExactMutableEntry("
        "${DESCRIPTION} separates output before keyed authentication")
    require_retained_output_gate(
        _output_adapter
        "authenticateExactMutableEntry("
        "${OUTPUT}"
        "${MUTATION}"
        "${DESCRIPTION}")
    require_ordered(
        _output_adapter
        "${POST}"
        "*${OUTPUT} ="
        "${DESCRIPTION} publishes only after post-authentication")
endfunction()

extract_slice(
    _source
    "ZoneRuntimeTableStatus TryGetZoneRuntimeEntry("
    "ZoneRuntimeTableStatus TryClaimZoneRuntimeGeneration("
    _entry_lookup_output
    "entry lookup output preflight")
require_ordered(
    _entry_lookup_output "WritableOutputIsSeparated(" "validateInitializedHeader()"
    "entry lookup separates output before table authentication")
require_retained_output_gate(
    _entry_lookup_output
    "validateEntryBinding(physicalSlot)"
    "outEntry"
    "*outEntry = &entry;"
    "entry lookup output")
require_ordered(
    _entry_lookup_output "validateEntryBinding(physicalSlot)" "*outEntry = &entry;"
    "entry lookup publishes only after binding authentication")

extract_slice(
    _source
    "ZoneRuntimeTableStatus TryClaimZoneRuntimeGeneration("
    "ZoneRuntimeTableStatus TryGetZoneRuntimeGeneration("
    _claim_output
    "claim output preflight")
require_ordered(
    _claim_output "WritableOutputIsSeparated(" "validateInitializedHeader()"
    "claim separates in/out key before table authentication")
require_retained_output_gate(
    _claim_output
    "validateEntryBinding(physicalSlot)"
    "inOutKey"
    "zone_load::TryClaimZoneLoadContext("
    "generation claim output")
require_ordered(
    _claim_output "table->authenticateExactEntry(physicalSlot, candidate)" "*inOutKey = candidate;"
    "claim publishes only after exact post-authentication")

extract_slice(
    _source
    "ZoneRuntimeTableStatus TryGetZoneRuntimeGeneration("
    "ZoneRuntimeTableStatus TryBindZoneRuntimeGenerationCallbacks("
    _generation_lookup_output
    "generation lookup output")
require_ordered(
    _generation_lookup_output
    "WritableOutputIsSeparated("
    "validateEntryBinding(physicalSlot)"
    "generation lookup separates output before entry authentication")
require_retained_output_gate(
    _generation_lookup_output
    "validateEntryBinding(physicalSlot)"
    "outView"
    "ZoneLoadContextKeyMatches("
    "generation lookup output")
require_ordered(
    _generation_lookup_output
    "ZoneLoadContextKeyMatches("
    "*outView = candidate;"
    "generation lookup publishes only after exact key authentication")

extract_slice(
    _source
    "ZoneRuntimeTableStatus TryGetZoneRuntimePendingCopyView("
    "ZoneRuntimeTableStatus TryReadZoneRuntimePendingCopy("
    _pending_copy_view_adapter
    "exact pending-copy view adapter")
foreach(_marker IN ITEMS
    "const zone_load::ZoneLoadContextKey key = keyArgument;"
    "WritableOutputIsSeparated( table, outView, sizeof(*outView), alignof(ZoneRuntimePendingCopyView), &keyArgument)"
    "table->authenticateExactPendingCopyOutput( physicalSlot, key, outView, sizeof(*outView), alignof(ZoneRuntimePendingCopyView))"
    "if (authentication != ZoneRuntimeTableStatus::Success) return authentication;"
    "const ZoneRuntimeEntry *const entry = &table->entries_[physicalSlot];"
    "ZoneRuntimeTable::pendingCopyAdmissionReceipt(entry)"
    "const auto phase = receipt->phase();"
    "const std::uint32_t recordCount = receipt->recordCount();"
    "const ZoneRuntimePendingCopyView candidate{key, recordCount, 0};"
    "if (!candidate)"
    "table->authenticateExactPendingCopyRead( physicalSlot, key, &postEntry)"
    "postEntry != entry"
    "postReceipt != receipt"
    "postReceipt->phase() != phase"
    "postReceipt->recordCount() != recordCount"
    "table->poison();"
    "*outView = candidate;")
    require_contains(
        _pending_copy_view_adapter "${_marker}"
        "failure-atomic exact pending-copy view")
endforeach()
require_substring_count(
    _pending_copy_view_adapter
    "authenticateExactPendingCopyRead(" 1
    "pending-copy view exact post-authentication")
require_substring_count(
    _pending_copy_view_adapter
    "authenticateExactPendingCopyOutput(" 1
    "pending-copy view exact output authentication")
require_substring_count(
    _pending_copy_view_adapter "*outView = candidate;" 1
    "sole pending-copy view publication")
require_not_contains(
    _pending_copy_view_adapter
    "TryReadPendingCopyRecord("
    "count-only view cannot read or expose ledger records")
require_ordered(
    _pending_copy_view_adapter
    "const zone_load::ZoneLoadContextKey key = keyArgument;"
    "WritableOutputIsSeparated("
    "input key snapshots before caller-output separation")
require_ordered(
    _pending_copy_view_adapter
    "WritableOutputIsSeparated("
    "authenticateExactPendingCopyOutput("
    "view output preflights before table authentication")
require_ordered(
    _pending_copy_view_adapter
    "authenticateExactPendingCopyOutput("
    "const auto *const receipt ="
    "exact output topology authenticates before receipt inspection")
require_ordered(
    _pending_copy_view_adapter
    "const ZoneRuntimeTableStatus postAuthentication ="
    "*outView = candidate;"
    "view post-authenticates before sole caller write")

extract_slice(
    _source
    "ZoneRuntimeTableStatus TryReadZoneRuntimePendingCopy("
    "ZoneRuntimeTableStatus TryPrepareZoneRuntimeAdmission("
    _pending_copy_record_adapter
    "exact pending-copy record adapter")
foreach(_marker IN ITEMS
    "expectedRecordCount > zone_pending_copy::kPendingCopyRecordCapacity"
    "ordinal >= expectedRecordCount"
    "const zone_load::ZoneLoadContextKey key = keyArgument;"
    "WritableOutputIsSeparated( table, outRecord, sizeof(*outRecord), alignof(zone_pending_copy::PendingCopyRecord), &keyArgument)"
    "table->authenticateExactPendingCopyOutput( physicalSlot, key, outRecord, sizeof(*outRecord), alignof(zone_pending_copy::PendingCopyRecord))"
    "if (authentication != ZoneRuntimeTableStatus::Success) return authentication;"
    "const ZoneRuntimeEntry *const entry = &table->entries_[physicalSlot];"
    "ZoneRuntimeTable::pendingCopyAdmissionReceipt(entry)"
    "const auto phase = receipt->phase();"
    "receipt->recordCount() != expectedRecordCount"
    "return ZoneRuntimeTableStatus::CountMismatch;"
    "zone_pending_copy::PendingCopyRecord candidate{};"
    "zone_pending_copy::TryReadPendingCopyRecord( receipt, key, ordinal, &candidate)"
    "pendingStatus != zone_pending_copy::PendingCopyStatus::Success"
    "candidate.key != key"
    "candidate.assetEntryIndex < zone_pending_copy::kFirstAssetEntryIndex"
    "candidate.assetEntryIndex > zone_pending_copy::kLastAssetEntryIndex"
    "candidate.reserved != 0"
    "table->authenticateExactPendingCopyRead( physicalSlot, key, &postEntry)"
    "postEntry != entry"
    "postReceipt != receipt"
    "postReceipt->phase() != phase"
    "postReceipt->recordCount() != expectedRecordCount"
    "table->poison();"
    "*outRecord = candidate;")
    require_contains(
        _pending_copy_record_adapter "${_marker}"
        "failure-atomic exact pending-copy record read")
endforeach()
require_substring_count(
    _pending_copy_record_adapter
    "authenticateExactPendingCopyRead(" 1
    "pending-copy record exact post-authentication")
require_substring_count(
    _pending_copy_record_adapter
    "authenticateExactPendingCopyOutput(" 1
    "pending-copy record exact output authentication")
require_substring_count(
    _pending_copy_record_adapter
    "zone_pending_copy::TryReadPendingCopyRecord(" 1
    "one reviewed lower by-value record read")
require_substring_count(
    _pending_copy_record_adapter "*outRecord = candidate;" 1
    "sole pending-copy record publication")
require_ordered(
    _pending_copy_record_adapter
    "expectedRecordCount > zone_pending_copy::kPendingCopyRecordCapacity"
    "const zone_load::ZoneLoadContextKey key = keyArgument;"
    "count and ordinal preflight before table or caller-buffer inspection")
require_ordered(
    _pending_copy_record_adapter
    "WritableOutputIsSeparated("
    "authenticateExactPendingCopyOutput("
    "record output preflights before table authentication")
require_ordered(
    _pending_copy_record_adapter
    "receipt->recordCount() != expectedRecordCount"
    "zone_pending_copy::TryReadPendingCopyRecord("
    "snapshot count mismatch precedes lower record access")
require_ordered(
    _pending_copy_record_adapter
    "zone_pending_copy::TryReadPendingCopyRecord("
    "const ZoneRuntimeTableStatus postAuthentication ="
    "lower by-value read precedes exact post-authentication")
require_ordered(
    _pending_copy_record_adapter
    "const ZoneRuntimeTableStatus postAuthentication ="
    "*outRecord = candidate;"
    "record post-authenticates before sole caller write")
require_output_preflight(
    "ZoneRuntimeTableStatus TryAllocateZoneRuntimeMemory("
    "ZoneRuntimeTableStatus TryBindZoneRuntimeStorage("
    outResult
    "pmem_runtime::TryAllocate("
    "completeCompositeOperation("
    "PMem allocation output")
require_output_preflight(
    "ZoneRuntimeTableStatus TryStageZoneRuntimeScriptString("
    "ZoneRuntimeTableStatus TrySealZoneRuntimeScriptStrings("
    outStringId
    "TryStageZoneScriptString("
    "completeMutableOperation("
    "script-string stage output")

extract_slice(
    _source
    "ZoneRuntimeTableStatus TryBindZoneRuntimeGenerationCallbacks("
    "ZoneRuntimeTableStatus TryBeginZoneRuntimePhysicalAllocation("
    _generation_callback_binding
    "generation callback binding")
foreach(_marker IN ITEMS
    "AddressRangesAreDisjoint( table, sizeof(*table), &callbacks, sizeof(callbacks))"
    "ZoneRuntimeGenerationCallbacks callbackSnapshot = callbacks;"
    "CallbacksAreComplete(callbackSnapshot)"
    "StorageIsOutsideManagedMemory( callbackSnapshot.context, 1)"
    "AddressRangesAreDisjoint( table, sizeof(*table), callbackSnapshot.context, 1)"
    "generationCallbacksMatch( entry, callbackSnapshot)"
    "entry->scriptStringOwnership_.isEmptyCanonical()"
    "ZoneRuntimeTable::bindGeneration( table, entry, key, callbackSnapshot)")
    require_contains(
        _generation_callback_binding "${_marker}"
        "snapshotted separated generation callback identity")
endforeach()
require_ordered(
    _generation_callback_binding
    "ZoneRuntimeGenerationCallbacks callbackSnapshot = callbacks;"
    "authenticateExactMutableEntry("
    "generation callbacks snapshot before table authentication")
require_ordered(
    _generation_callback_binding
    "entry->scriptStringOwnership_.isEmptyCanonical()"
    "zone_pending_copy::TryInitializePendingCopyLedger("
    "legacy ownership rejection precedes shared-state mutation")

extract_slice(
    _source
    "ZoneRuntimeTableStatus TryBindZoneRuntimeStreams("
    "ZoneRuntimeTableStatus TryBeginZoneRuntimePendingCopies("
    _stream_binding
    "snapshotted stream binding")
foreach(_marker IN ITEMS
    "AddressRangesAreDisjoint( table, sizeof(*table), blocks, sizeof(*blocks) * relocation::kBlockCount)"
    "relocation::BlockView blockSnapshot[relocation::kBlockCount]{};"
    "blockSnapshot[index] = blocks[index];"
    "TryAuthenticateAllocationRange("
    "AddressRangesAreDisjoint( storageSlab, storageCapacity, reinterpret_cast<const void *>(blockSnapshot[index].base), blockSnapshot[index].size)"
    "blockSnapshot, blockCount)")
    require_contains(
        _stream_binding "${_marker}"
        "snapshotted separated stream descriptors")
endforeach()
require_ordered(
    _stream_binding
    "blockSnapshot[index] = blocks[index];"
    "authenticateExactMutableEntry("
    "stream descriptors snapshot before table authentication")
require_ordered(
    _stream_binding
    "AddressRangesAreDisjoint( storageSlab, storageCapacity"
    "TryBindZoneStreams("
    "cross-component overlap rejection before singleton publication")

extract_slice(
    _source
    "ZoneRuntimeTableStatus TryBeginZoneRuntimePendingCopyDrain("
    "ZoneRuntimeTableStatus TryDrainNextZoneRuntimePendingCopy("
    _pending_drain_begin
    "pending drain callback binding")
foreach(_marker IN ITEMS
    "AddressRangesAreDisjoint( table, sizeof(*table), &callback, sizeof(callback))"
    "PendingCopyDrainCallback callbackSnapshot = callback;"
    "StorageIsOutsideManagedMemory( callbackSnapshot.context, 1)"
    "AddressRangesAreDisjoint( table, sizeof(*table), callbackSnapshot.context, 1)"
    "pendingDrainCallback_ = callbackSnapshot;")
    require_contains(
        _pending_drain_begin "${_marker}"
        "snapshotted separated pending-drain callback identity")
endforeach()
require_ordered(
    _pending_drain_begin
    "PendingCopyDrainCallback callbackSnapshot = callback;"
    "validateInitializedHeader()"
    "pending-drain callback snapshot before table authentication")

# Passive compatibility adapters cannot smuggle table/control aliases into
# journal storage or into callbacks, even though enrolled generations reject
# those adapters later at their phase gate.
extract_slice(
    _source
    "ZoneRuntimeTableStatus TryBeginZoneRuntimeScriptStringOwnership("
    "ZoneRuntimeTableStatus TryStageZoneRuntimeScriptString("
    _ownership_begin
    "script-string storage preflight")
foreach(_marker IN ITEMS
    "reinterpret_cast<std::uintptr_t>(journal) % alignof(script_string_journal::ScriptStringJournal)"
    "AddressRangesAreDisjoint( table, sizeof(*table), journal, sizeof(*journal))"
    "if (expectedCount > storageCapacity) return ZoneRuntimeTableStatus::CapacityExceeded;"
    "if ((storageCapacity == 0) != (storage == nullptr)) return ZoneRuntimeTableStatus::InvalidArgument;"
    "static_cast<std::size_t>(storageCapacity) > (std::numeric_limits<std::size_t>::max)() / sizeof(*storage)"
    "static_cast<std::size_t>(storageCapacity) * sizeof(*storage)"
    "AddressRangesAreDisjoint( table, sizeof(*table), storage, storageBytes)")
    require_contains(
        _ownership_begin "${_marker}"
        "bounded separated passive ownership storage")
endforeach()
require_not_contains(
    _ownership_begin
    "static_cast<std::size_t>(expectedCount)"
    "storage separation must span planned capacity rather than demand")
require_ordered(
    _ownership_begin
    "AddressRangesAreDisjoint( table, sizeof(*table), storage, storageBytes)"
    "authenticateExactMutableEntry("
    "ownership storage preflight before keyed authentication")
foreach(_marker IN ITEMS
    "if (composite && ownershipStatus"
    "ZoneScriptStringOwnershipStatus::Success)"
    "ZoneRuntimeTable::retainGenerationPlacement("
    "storageCapacity, expectedCount);"
    "ZoneRuntimeSetupStage::ScriptStringsBegun")
    require_contains(
        _ownership_begin "${_marker}"
        "success-only expected-demand publication")
endforeach()
require_substring_count(
    _ownership_begin
    "ZoneRuntimeTable::retainGenerationPlacement("
    1
    "single success-only expected-demand publication")
require_ordered(
    _ownership_begin
    "TryBeginZoneScriptStringOwnership("
    "ZoneRuntimeTable::retainGenerationPlacement("
    "lower ownership begin succeeds before expected demand publication")
require_ordered(
    _ownership_begin
    "ZoneRuntimeTable::retainGenerationPlacement("
    "ZoneRuntimeSetupStage::ScriptStringsBegun"
    "expected demand publishes before script-string stage publication")

extract_slice(
    _source
    "ZoneRuntimeTableStatus TryCommitZoneRuntimeScriptStringsAndAdmit("
    "ZoneRuntimeTableStatus TryBeginZoneRuntimeScriptStringRollback("
    _legacy_admission_callback
    "legacy admission callback guard")
foreach(_marker IN ITEMS
    "AddressRangesAreDisjoint( table, sizeof(*table), &admission, sizeof(admission))"
    "ZoneScriptStringAdmissionCallback admissionSnapshot = admission;"
    "AddressRangesAreDisjoint( table, sizeof(*table), admissionSnapshot.context, 1)"
    "TryCommitZoneScriptStringsAndAdmit( ZoneRuntimeTable::mutableScriptStringOwnership(entry), admissionSnapshot)")
    require_contains(
        _legacy_admission_callback "${_marker}"
        "snapshotted separated legacy admission callback")
endforeach()
require_ordered(
    _legacy_admission_callback
    "admissionSnapshot = admission;"
    "authenticateExactMutableEntry("
    "legacy admission callback snapshot before authentication")

extract_slice(
    _source
    "ZoneRuntimeTableStatus TryBeginZoneRuntimeScriptStringRollback("
    "ZoneRuntimeTableStatus TryRollbackNextZoneRuntimeScriptString("
    _legacy_rollback_callback
    "legacy rollback callback guard")
foreach(_marker IN ITEMS
    "AddressRangesAreDisjoint( table, sizeof(*table), &callbacks, sizeof(callbacks))"
    "ZoneScriptStringRollbackCallbacks callbackSnapshot = callbacks;"
    "AddressRangesAreDisjoint( table, sizeof(*table), callbackSnapshot.context, 1)"
    "TryBeginZoneScriptStringRollback( ZoneRuntimeTable::mutableScriptStringOwnership(entry), callbackSnapshot)")
    require_contains(
        _legacy_rollback_callback "${_marker}"
        "snapshotted separated legacy rollback callback")
endforeach()
require_ordered(
    _legacy_rollback_callback
    "callbackSnapshot = callbacks;"
    "authenticateExactMutableEntry("
    "legacy rollback callback snapshot before authentication")

extract_slice(
    _source
    "ZoneRuntimeTableStatus TryUnloadZoneRuntimeGeneration( ZoneRuntimeTable *const table, const std::uint32_t physicalSlot, const zone_load::ZoneLoadContextKey &key, const zone_load::ZoneLoadCleanupCallbacks &callbacks) noexcept"
    "ZoneRuntimeTableStatus TryResetZoneRuntimeTerminalReceipt("
    _legacy_unload_callback
    "legacy unload callback guard")
foreach(_marker IN ITEMS
    "AddressRangesAreDisjoint( table, sizeof(*table), &callbacks, sizeof(callbacks))"
    "ZoneLoadCleanupCallbacks callbackSnapshot = callbacks;"
    "AddressRangesAreDisjoint( table, sizeof(*table), callbackSnapshot.context, 1)"
    "TryUnloadLiveZoneScriptStringOwnership( &entry.scriptStringOwnership_, &entry.lifecycle_, key, callbackSnapshot)")
    require_contains(
        _legacy_unload_callback "${_marker}"
        "snapshotted separated legacy unload callback")
endforeach()
require_ordered(
    _legacy_unload_callback
    "callbackSnapshot = callbacks;"
    "TryUnloadLiveZoneScriptStringOwnership("
    "legacy unload callback snapshot before invocation")

foreach(_marker IN ITEMS
    "zone_slots::kDefaultZoneSlot"
    "zone_slots::kFirstUsableZoneSlot"
    "zone_slots::kPhysicalZoneSlotCount"
    "ZoneRuntimeTable g_productionZoneRuntimeTable{};"
    "ZoneRuntimeTable &ProductionZoneRuntimeTable() noexcept"
    "TryInitializeZoneLoadContextSlot("
    "TryClaimZoneLoadContext("
    "ZoneLoadContextKeyMatches("
    "lifecycle.canonical()"
    "ownership.isEmptyCanonical()"
    "validateEntryBinding("
    "IsPristineLifecycle(lifecycle, true, physicalSlot)"
    "lifecycle.cleanupActive()"
    "lifecycle.cleanupPoisoned()"
    "ownership.poisoned()"
    "ownership.canonicalForBinding(&lifecycle, key)"
    "if (!ownership.serializerRetained())"
    "IsOwnershipCallbackPhase(ownershipPhase)"
    "case OwnershipPhase::UnpublishingCallback:"
    "case OwnershipPhase::Cleaning:"
    "case OwnershipPhase::Admitting:"
    "case OwnershipPhase::Unloading:"
    "case OwnershipPhase::UnloadingCallback:"
    "if (ownershipPhase == OwnershipPhase::Unloaded)"
    "ZoneRuntimeTableStatus::Retry"
    "ZoneRuntimeTableStatus::Rejected"
    "ZoneRuntimeTableStatus::CountMismatch"
    "ZoneRuntimeTableStatus::CapacityExceeded"
    "MapOwnershipStatus("
    "MapLiveUnloadOwnershipStatus("
    "ZoneRuntimeTable::authenticateExactMutableEntry("
    "ZoneRuntimeTable::completeMutableOperation("
    "ZoneRuntimeTable::mutableScriptStringOwnership("
    "lifecycle.phase() == zone_load::ZoneLoadContextPhase::Loading"
    "lifecycle.phase() == zone_load::ZoneLoadContextPhase::Abandoning"
    "lifecycle.phase() == zone_load::ZoneLoadContextPhase::Live"
    "TableState::Poisoned")
    require_contains(_source "${_marker}" "checked table implementation")
endforeach()

extract_slice(
    _source
    "ZoneRuntimeTableStatus MapLiveUnloadOwnershipStatus("
    "ZoneRuntimeTableStatus ZoneRuntimeTable::authenticateExactEntry("
    _live_unload_status_map
    "Live-unload status allowlist")
foreach(_marker IN ITEMS
    "case OwnershipStatus::Success:"
    "case OwnershipStatus::Retry:"
    "case OwnershipStatus::Busy:"
    "case OwnershipStatus::InvalidArgument:"
    "case OwnershipStatus::InvalidState:"
    "case OwnershipStatus::InvalidKey:"
    "case OwnershipStatus::StaleKey:"
    "case OwnershipStatus::InvalidPhase:"
    "case OwnershipStatus::Rejected:"
    "case OwnershipStatus::CountMismatch:"
    "case OwnershipStatus::CapacityExceeded:"
    "case OwnershipStatus::UnsafeFailure:"
    "return ZoneRuntimeTableStatus::UnsafeFailure;")
    require_contains(
        _live_unload_status_map
        "${_marker}"
        "terminal Live-unload must fail closed on journal-only statuses")
endforeach()
require_ordered(
    _live_unload_status_map
    "case OwnershipStatus::InvalidPhase:"
    "return MapOwnershipStatus(status);"
    "allowed terminal result must use the shared status mapper")
require_ordered(
    _live_unload_status_map
    "return MapOwnershipStatus(status);"
    "case OwnershipStatus::Rejected:"
    "journal-only results must follow the terminal allowlist")
require_ordered(
    _live_unload_status_map
    "case OwnershipStatus::Rejected:"
    "case OwnershipStatus::CountMismatch:"
    "Rejected must enter the journal-only fail-closed group")
require_ordered(
    _live_unload_status_map
    "case OwnershipStatus::CountMismatch:"
    "case OwnershipStatus::CapacityExceeded:"
    "CountMismatch must enter the journal-only fail-closed group")
require_ordered(
    _live_unload_status_map
    "case OwnershipStatus::CapacityExceeded:"
    "case OwnershipStatus::UnsafeFailure:"
    "CapacityExceeded must enter the terminal unsafe group")
require_ordered(
    _live_unload_status_map
    "case OwnershipStatus::UnsafeFailure:"
    "return ZoneRuntimeTableStatus::UnsafeFailure;"
    "terminal unsafe group must poison through UnsafeFailure")

# Initialization is idempotent only while every usable entry remains pristine.
# A rebound table is rejected without resetting its generation; malformed or
# partially initialized storage is poisoned.
extract_slice(
    _source
    "ZoneRuntimeTableStatus TryInitializeZoneRuntimeTable("
    "ZoneRuntimeTableStatus TryGetZoneRuntimeEntry("
    _initialize
    "table initialization")
require_ordered(
    _initialize
    "if (state == TableState::Initialized)"
    "return pristine ? ZoneRuntimeTableStatus::Success : ZoneRuntimeTableStatus::InvalidState;"
    "reinitialization accepts only pristine state")
require_ordered(
    _initialize
    "TryInitializeZoneLoadContextSlot("
    "table->state_ = static_cast<std::uint32_t>(TableState::Initialized);"
    "all lifecycle slots initialize before table publication")

# Claims and keyed lookup are failure-atomic.  The explicit physical-slot
# argument prevents a same-generation key from being replayed across slots.
extract_slice(
    _source
    "ZoneRuntimeTableStatus TryClaimZoneRuntimeGeneration("
    "ZoneRuntimeTableStatus TryGetZoneRuntimeGeneration("
    _claim
    "generation claim")
require_ordered(
    _claim
    "TryClaimZoneLoadContext("
    "entry.key_ = candidate;"
    "lifecycle claim before durable key publication")
require_ordered(
    _claim
    "entry.key_ = candidate;"
    "*inOutKey = candidate;"
    "durable key before caller output publication")
extract_slice(
    _source
    "ZoneRuntimeTableStatus TryGetZoneRuntimeGeneration("
    "ZoneRuntimeTableStatus TryUnloadZoneRuntimeGeneration("
    _lookup
    "keyed generation lookup")
require_ordered(
    _lookup
    "if (key.slot != physicalSlot)"
    "ZoneLoadContextKeyMatches("
    "cross-slot rejection before active-generation authentication")
require_ordered(
    _lookup
    "ZoneLoadContextKeyMatches("
    "*outView = candidate;"
    "authentication before output publication")

# Every public loading mutation routes through the same inaccessible raw-entry
# shim, authenticates the exact durable binding on both sides of the controller
# call, and preserves recoverable controller statuses.  The one scalar output
# remains local until successful post-authentication.
extract_slice(
    _source
    "ZoneRuntimeTableStatus TryBeginZoneRuntimeScriptStringOwnership("
    "ZoneRuntimeTableStatus TryUnloadZoneRuntimeGeneration("
    _mutable_adapters
    "exact-key mutable loading adapters")
foreach(_marker IN ITEMS
    "TryBeginZoneRuntimeScriptStringOwnership("
    "TryStageZoneRuntimeScriptString("
    "TrySealZoneRuntimeScriptStrings("
    "TryBeginZoneRuntimeScriptStringTransfer("
    "TryTransferNextZoneRuntimeScriptString("
    "TryPrepareZoneRuntimeScriptStringCommit("
    "TryCommitZoneRuntimeScriptStringsAndAdmit("
    "TryBeginZoneRuntimeScriptStringRollback("
    "TryRollbackNextZoneRuntimeScriptString("
    "TryFinishZoneRuntimeScriptStringAbandonment("
    "authenticateExactMutableEntry("
    "completeMutableOperation("
    "TryBeginZoneScriptStringOwnership("
    "TryStageZoneScriptString("
    "TrySealZoneScriptStrings("
    "TryBeginZoneScriptStringTransfer("
    "TryTransferNextZoneScriptString("
    "TryPrepareZoneScriptStringCommit("
    "TryCommitZoneScriptStringsAndAdmit("
    "TryBeginZoneScriptStringRollback("
    "TryRollbackNextZoneScriptString("
    "TryFinishZoneScriptStringAbandonment(")
    require_contains(
        _mutable_adapters "${_marker}" "complete mutable adapter surface")
endforeach()

function(require_mutable_adapter START END MUTATION DESCRIPTION)
    extract_slice(
        _source "${START}" "${END}" _adapter
        "${DESCRIPTION} adapter")
    require_ordered(
        _adapter
        "authenticateExactMutableEntry("
        "${MUTATION}"
        "${DESCRIPTION} pre-authentication")
    require_ordered(
        _adapter
        "${MUTATION}"
        "completeMutableOperation("
        "${DESCRIPTION} post-authentication")
endfunction()

require_mutable_adapter(
    "ZoneRuntimeTableStatus TryBeginZoneRuntimeScriptStringOwnership("
    "ZoneRuntimeTableStatus TryStageZoneRuntimeScriptString("
    "TryBeginZoneScriptStringOwnership("
    "ownership begin")
require_mutable_adapter(
    "ZoneRuntimeTableStatus TryStageZoneRuntimeScriptString("
    "ZoneRuntimeTableStatus TrySealZoneRuntimeScriptStrings("
    "TryStageZoneScriptString("
    "string stage")
require_mutable_adapter(
    "ZoneRuntimeTableStatus TrySealZoneRuntimeScriptStrings("
    "ZoneRuntimeTableStatus TryBeginZoneRuntimeScriptStringTransfer("
    "TrySealZoneScriptStrings("
    "journal seal")
require_mutable_adapter(
    "ZoneRuntimeTableStatus TryBeginZoneRuntimeScriptStringTransfer("
    "ZoneRuntimeTableStatus TryTransferNextZoneRuntimeScriptString("
    "TryBeginZoneScriptStringTransfer("
    "transfer begin")
require_mutable_adapter(
    "ZoneRuntimeTableStatus TryTransferNextZoneRuntimeScriptString("
    "ZoneRuntimeTableStatus TryPrepareZoneRuntimeScriptStringCommit("
    "TryTransferNextZoneScriptString("
    "one-entry transfer")
require_mutable_adapter(
    "ZoneRuntimeTableStatus TryPrepareZoneRuntimeScriptStringCommit("
    "ZoneRuntimeTableStatus TryCommitZoneRuntimeScriptStringsAndAdmit("
    "TryPrepareZoneScriptStringCommit("
    "commit prepare")
require_mutable_adapter(
    "ZoneRuntimeTableStatus TryCommitZoneRuntimeScriptStringsAndAdmit("
    "ZoneRuntimeTableStatus TryBeginZoneRuntimeScriptStringRollback("
    "TryCommitZoneScriptStringsAndAdmit("
    "live commit")
require_mutable_adapter(
    "ZoneRuntimeTableStatus TryBeginZoneRuntimeScriptStringRollback("
    "ZoneRuntimeTableStatus TryRollbackNextZoneRuntimeScriptString("
    "TryBeginZoneScriptStringRollback("
    "rollback begin")
require_mutable_adapter(
    "ZoneRuntimeTableStatus TryRollbackNextZoneRuntimeScriptString("
    "ZoneRuntimeTableStatus TryFinishZoneRuntimeScriptStringAbandonment("
    "TryRollbackNextZoneScriptString("
    "one-entry rollback")
require_mutable_adapter(
    "ZoneRuntimeTableStatus TryFinishZoneRuntimeScriptStringAbandonment("
    "ZoneRuntimeTableStatus TryUnloadZoneRuntimeGeneration("
    "TryFinishZoneScriptStringAbandonment("
    "abandonment finish")

extract_slice(
    _source
    "ZoneRuntimeTableStatus TryStageZoneRuntimeScriptString("
    "ZoneRuntimeTableStatus TrySealZoneRuntimeScriptStrings("
    _stage_adapter
    "failure-atomic keyed stage adapter")
require_ordered(
    _stage_adapter
    "std::uint32_t candidate = 0;"
    "TryStageZoneScriptString("
    "stage candidate precedes controller mutation")
require_ordered(
    _stage_adapter
    "TryStageZoneScriptString("
    "completeMutableOperation("
    "stage post-authentication follows mutation")
require_ordered(
    _stage_adapter
    "completeMutableOperation("
    "*outStringId = candidate;"
    "stage output publishes only after post-authentication")
require_not_contains(
    _header
    "ZoneRuntimeMutableAdapterAccess"
    "public header cannot expose raw mutable ownership")
require_not_contains(
    _source
    "ZoneRuntimeMutableAdapterAccess"
    "implementation cannot restore a forgeable friend shim")

extract_slice(
    _source
    "ZoneRuntimeTableStatus ZoneRuntimeTable::authenticateExactMutableEntry("
    "ZoneRuntimeTableStatus ZoneRuntimeTable::authenticateExactRegistryLifecycleCallback("
    _mutable_entry_authentication
    "private mutable entry authentication")
foreach(_marker IN ITEMS
    "validateInitializedHeader()"
    "if (!static_cast<bool>(key))"
    "ValidateUsableSlot(physicalSlot)"
    "if (key.slot != physicalSlot)"
    "authenticateExactEntry(physicalSlot, key)"
    "if (authentication == ZoneRuntimeTableStatus::UnsafeFailure)"
    "*outEntry = &entry;")
    require_contains(
        _mutable_entry_authentication "${_marker}"
        "exact mutable entry authentication")
endforeach()

extract_slice(
    _source
    "ZoneRuntimeTableStatus ZoneRuntimeTable::completeMutableOperation("
    "ZoneRuntimeTableStatus ZoneRuntimeTable::completeCompositeOperation("
    _mutable_access
    "private mutable operation completion")
foreach(_marker IN ITEMS
    "MapOwnershipStatus(ownershipStatus)"
    "const ZoneRuntimeTableStatus postAuthentication ="
    "authenticateExactMutableEntry(physicalSlot, key, &postEntry)"
    "status == ZoneRuntimeTableStatus::UnsafeFailure"
    "postAuthentication != ZoneRuntimeTableStatus::Success"
    "poison();")
    require_contains(
        _mutable_access "${_marker}" "pre/post mutation authentication")
endforeach()
require_ordered(
    _mutable_access
    "MapOwnershipStatus(ownershipStatus)"
    "const ZoneRuntimeTableStatus postAuthentication ="
    "map result before mandatory post-authentication")
require_ordered(
    _mutable_access
    "const ZoneRuntimeTableStatus postAuthentication ="
    "return status;"
    "post-authentication before recoverable status publication")

# Live unload is owned by the script-string controller so the outer
# transaction and exact callback identity survive Retry. Reset consumes only
# the matching controller receipt; lifecycle/key evidence remains until Claim
# advances the generation and publishes a new key.
extract_slice(
    _source
    "ZoneRuntimeTableStatus TryUnloadZoneRuntimeGeneration( ZoneRuntimeTable *const table, const std::uint32_t physicalSlot, const zone_load::ZoneLoadContextKey &key, const zone_load::ZoneLoadCleanupCallbacks &callbacks) noexcept"
    "ZoneRuntimeTableStatus TryResetZoneRuntimeTerminalReceipt("
    _unload
    "keyed live unload")
foreach(_marker IN ITEMS
    "table->authenticateExactEntry(physicalSlot, key)"
    "TryUnloadLiveZoneScriptStringOwnership("
    "MapLiveUnloadOwnershipStatus(ownershipStatus)"
    "OwnershipPhase::Unloaded"
    "ZoneLoadTerminalKind::Unloaded"
    "table->poison();")
    require_contains(_unload "${_marker}" "controller-owned live unload")
endforeach()
require_ordered(
    _unload
    "table->authenticateExactEntry(physicalSlot, key)"
    "TryUnloadLiveZoneScriptStringOwnership("
    "exact key authentication before unload mutation")
require_ordered(
    _unload
    "TryUnloadLiveZoneScriptStringOwnership("
    "const ZoneRuntimeTableStatus postAuthentication ="
    "post-callback authentication")

extract_slice(
    _source
    "ZoneRuntimeTableStatus TryResetZoneRuntimeTerminalReceipt("
    "} // namespace db::zone_runtime"
    _terminal_reset
    "keyed terminal reset")
foreach(_marker IN ITEMS
    "table->authenticateExactEntry(physicalSlot, key)"
    "entry.lifecycle_.phase() != zone_load::ZoneLoadContextPhase::Empty"
    "entry.lifecycle_.terminalKind()"
    "OwnershipPhase::Abandoned"
    "OwnershipPhase::Unloaded"
    "TryResetTerminalZoneScriptStringOwnership("
    "entry.scriptStringOwnership_.isEmptyCanonical()"
    "entry.lifecycle_.terminalKind() != terminalKind"
    "entry.key_ != key")
    require_contains(
        _terminal_reset "${_marker}" "exact receipt-preserving reset")
endforeach()
require_ordered(
    _terminal_reset
    "table->authenticateExactEntry(physicalSlot, key)"
    "TryResetTerminalZoneScriptStringOwnership("
    "exact key and receipt authentication before reset")
require_not_contains(
    _terminal_reset
    "entry.key_ = {}"
    "terminal reset cannot erase ABA evidence")

# DB_Init initializes the table before mutating the legacy asset pools and uses
# the established fatal initialization boundary.  The load, stream, and asset
# publication paths remain completely unenrolled in this independent batch.
extract_slice(
    _registry
    "void DB_Init()"
    "void __cdecl DB_InitPoolHeader("
    _db_init
    "production DB initialization")
foreach(_marker IN ITEMS
    "#include \"db_zone_runtime_table.h\""
    "TryInitializeZoneRuntimeTable("
    "ProductionZoneRuntimeTable()"
    "ZoneRuntimeTableStatus::Success"
    "Com_Error( ERR_FATAL, \"DB_Init: zone runtime table initialization failed (%u)\""
    "return;")
    require_contains(_registry "${_marker}" "production initialization wiring")
endforeach()
require_ordered(
    _db_init
    "TryInitializeZoneRuntimeTable("
    "for (XAssetType type"
    "table initialization before asset-pool mutation")
foreach(_var IN ITEMS _registry _file_load _load _stream _stringtable)
    require_not_contains(
        ${_var}
        "TryClaimZoneRuntimeGeneration("
        "legacy loader cannot claim the table in this batch")
    require_not_contains(
        ${_var}
        "TryGetZoneRuntimeGeneration("
        "legacy loader cannot consume keyed capabilities in this batch")
    require_not_contains(
        ${_var}
        "TryUnloadZoneRuntimeGeneration("
        "legacy loader cannot unload through unwired callbacks")
    require_not_contains(
        ${_var}
        "TryResetZoneRuntimeTerminalReceipt("
        "legacy loader cannot reset runtime receipts")
    foreach(_inspection_api IN ITEMS
        "TryGetZoneRuntimePendingCopyView("
        "TryReadZoneRuntimePendingCopy("
        "TryGetPendingCopyView("
        "TryReadPendingCopy(")
        require_not_contains(
            ${_var} "${_inspection_api}"
            "legacy loader cannot enroll pending-copy inspection")
    endforeach()
    foreach(_mutable_api IN ITEMS
        "TryBeginZoneRuntimeScriptStringOwnership("
        "TryStageZoneRuntimeScriptString("
        "TrySealZoneRuntimeScriptStrings("
        "TryBeginZoneRuntimeScriptStringTransfer("
        "TryTransferNextZoneRuntimeScriptString("
        "TryPrepareZoneRuntimeScriptStringCommit("
        "TryCommitZoneRuntimeScriptStringsAndAdmit("
        "TryBeginZoneRuntimeScriptStringRollback("
        "TryRollbackNextZoneRuntimeScriptString("
        "TryFinishZoneRuntimeScriptStringAbandonment(")
        require_not_contains(
            ${_var} "${_mutable_api}" "legacy loader remains unenrolled")
    endforeach()
endforeach()

# Runtime coverage spans every slot, ABI/noexcept traits, stable addresses,
# bounds, output atomicity, generation/ABA rejection, and corruption handling.
foreach(_marker IN ITEMS
    "void TestLayoutNoexceptAndDefaultState()"
    "void TestAllPhysicalSlotsAndStableAddresses()"
    "void TestLookupAndClaimOutputAliasPreflight()"
    "void TestClaimAuthenticationAndAdjacentIsolation()"
    "void TestGenerationAdvanceRejectsAba()"
    "void TestPartialInitializationAndCorruptionFailClosed()"
    "void TestHiddenCorruptionAndCleanupReentryFailClosed()"
    "void TestControllerPhaseAndSerializerMatrix()"
    "void TestKeyedMutableCommitAndAuthentication()"
    "void TestKeyedMutableRecoverableAbandonment()"
    "void TestUnsafeMutableBoundary(const bool corruptPostcondition)"
    "void TestLiveUnloadRetryResetReuseAndAba()"
    "void TestAbandonedReceiptResetAndGenerationExhaustion()"
    "void TestTerminalAdapterPhaseSerializerAndCorruptionGates()"
    "void TestCompositeRuntimeLiveUnloadResetAndReuse()"
    "void TestCompositePartialStageAbandonmentAndReuse()"
    "void TestCompositeScriptStringCapacityAndDemand()"
    "void TestCompositeRecoverablePlacementAndRangeRejection()"
    "void TestCompositeStageOutputAliasPreflight()"
    "void TestCompositeCallbackContextAliasPreflightAndDrain()"
    "void TestExactRegistryLifecycleCallbackMarkerClassification()"
    "void TestExactPendingCopyInspectionAndSnapshotStability()"
    "void TestUnsafePendingCopyInspectionBoundary("
    "kind == \"lifecycle-drift\""
    "kind == \"callback-lifecycle-drift\""
    "ZoneLoadContextSlotTestAccess::SetGeneration("
    "--unsafe-pending-copy"
    "void TestCompositeAbandonmentRetryAndReentryPreservation()"
    "void TestUnsafeLiveUnloadBoundary("
    "void ObserveAdmittingController(void *const context) noexcept"
    "current - previous == sizeof(ZoneRuntimeEntry)"
    "zone_slots::kUsableZoneSlotCount == 32"
    "zone_slots::kPhysicalZoneSlotCount == 33"
    "ZoneRuntimeTableStatus::InvalidSlot"
    "ZoneRuntimeTableStatus::StaleKey"
    "ZoneRuntimeTableStatus::UnsafeFailure"
    "ZoneRuntimeTableStatus::Busy"
    "ZoneRuntimeTableStatus::Retry"
    "ZoneRuntimeTableStatus::Rejected"
    "ZoneRuntimeTableStatus::CountMismatch"
    "ZoneRuntimeTableStatus::CapacityExceeded"
    "ZoneScriptStringOwnershipControllerTestAccess::SetStorage("
    "kInitializedFlag | kCleanupActiveFlag"
    "kInitializedFlag | kCleanupPoisonedFlag"
    "probe.status == ZoneRuntimeTableStatus::Busy"
    "ZoneScriptStringOwnershipPhase::UnpublishingCallback"
    "ZoneScriptStringOwnershipPhase::Live"
    "ZoneScriptStringOwnershipPhase::Unloading"
    "ZoneScriptStringOwnershipPhase::Unloaded"
    "driver.lookupReentry == ZoneRuntimeTableStatus::Busy"
    "driver.unloadReentry == ZoneRuntimeTableStatus::Busy"
    "driver.resetReentry == ZoneRuntimeTableStatus::Busy"
    "driver.claimReentry == ZoneRuntimeTableStatus::Busy"
    "driver.usedContextAfterFree"
    "maximumGeneration"
    "ZoneRuntimeTableStatus::GenerationExhausted"
    "sizeof(ZoneRuntimeReceiptCapsule)"
    "sizeof(ZoneRuntimeEntry)"
    "sizeof(ZoneRuntimeTable)"
    "ZoneRuntimeTableTestAccess::ReceiptCapsulePristine("
    "ZoneRuntimeTableTestAccess::SharedResourcesPristine("
    "ZoneRuntimeTableTestAccess:: AuthenticateExactRegistryLifecycleCallback("
    "ZoneRuntimeTableTestAccess::SetCallbackMarker("
    "TestPassiveReceiptPristineAuthentication()"
    "physical_memory::TryBegin("
    "zone_stream_ownership::TryBeginZoneStreamGeneration("
    "zone_stream_ownership::TryBindZoneStreams("
    "zone_pending_copy::TryBeginPendingCopyAdmission("
    "zone_runtime_storage::TryBindZoneRuntimeStorage("
    "ExpectReceiptCorruptionFailsClosed("
    "ZoneScriptStringOwnershipControllerTestAccess::SetLifecycle("
    "ZoneScriptStringOwnershipControllerTestAccess::SetTransactionSerial("
    "ZoneScriptStringOwnershipControllerTestAccess::SetReserved("
    "hiddenGenerationLifecycle->generation() == 23"
    "newKey.generation == oldKey.generation + 1"
    "if (argc != 1)"
    "if (argc != 3"
    "std::string_view(argv[1]) == \"--unsafe-mutable\""
    "kind == \"backend\""
    "kind == \"postcondition\""
    "std::string_view(argv[1]) != \"--unsafe-live-unload\""
    "Zone runtime table tests passed")
    require_contains(_fixture "${_marker}" "focused runtime coverage")
endforeach()

extract_slice(
    _fixture
    "void TestExactPendingCopyInspectionAndSnapshotStability()"
    "void TestExactRegistryLifecycleCallbackMarkerClassification()"
    _exact_pending_copy_fixture
    "exact pending-copy runtime coverage")
foreach(_marker IN ITEMS
    "std::array<std::uint8_t, sizeof(XZoneMemory)> zoneBefore{};"
    "std::memcpy(zoneBefore.data(), &fixture.zone, zoneBefore.size());"
    "reinterpret_cast<ZoneRuntimePendingCopyView *>(&fixture.zone)"
    "reinterpret_cast<zone_pending_copy::PendingCopyRecord *>( &fixture.zone)"
    "zoneBefore.data(), &fixture.zone, zoneBefore.size()) == 0"
    "CHECK(fixture.table->initialized());")
    require_contains(
        _exact_pending_copy_fixture "${_marker}"
        "retained pending-copy caller outputs remain fail-closed")
endforeach()
require_substring_count(
    _exact_pending_copy_fixture
    "zoneBefore.data(), &fixture.zone, zoneBefore.size()) == 0" 2
    "both retained pending-copy caller outputs remain failure-atomic")
require_ordered(
    _exact_pending_copy_fixture
    "reinterpret_cast<ZoneRuntimePendingCopyView *>(&fixture.zone)"
    "reinterpret_cast<zone_pending_copy::PendingCopyRecord *>( &fixture.zone)"
    "pending-copy view and record both authenticate retained stream aliases")

extract_slice(
    _fixture
    "void TestCompositeRuntimeLiveUnloadResetAndReuse()"
    "void TestCompositePartialStageAbandonmentAndReuse()"
    _registry_admission_unload_fixture
    "admission and unload callback registry authorization fixture")
foreach(_marker IN ITEMS
    "pendingRegistryAuthentication == ZoneRuntimeTableStatus::Success"
    "pendingRegistryReauthentication == ZoneRuntimeTableStatus::Busy"
    "pendingOwnershipPhase == ZoneScriptStringOwnershipPhase::Admitting"
    "admitRegistryAuthentication == ZoneRuntimeTableStatus::Success"
    "admitRegistryReauthentication == ZoneRuntimeTableStatus::Busy"
    "admitOwnershipPhase == ZoneScriptStringOwnershipPhase::Admitting"
    "externalRegistryAuthentication[index] == ZoneRuntimeTableStatus::Success"
    "externalRegistryReauthentication[index] == ZoneRuntimeTableStatus::Busy"
    "externalOwnershipPhases[index] == ZoneScriptStringOwnershipPhase::UnloadingCallback")
    require_contains(
        _registry_admission_unload_fixture "${_marker}"
        "real admission and unload callback phase authorization")
endforeach()

extract_slice(
    _fixture
    "void TestExactRegistryLifecycleCallbackMarkerClassification()"
    "void TestCompositeAbandonmentRetryAndReentryPreservation()"
    _registry_callback_marker_fixture
    "callback registry marker classification fixture")
foreach(_marker IN ITEMS
    "AuthenticateExactRegistryLifecycleCallback( nullptr, ordinary.physicalSlot, ordinary.key)"
    "ZoneRuntimeTableStatus::InvalidArgument"
    "ZoneRuntimeTableStatus::InvalidKey"
    "ZoneRuntimeTableStatus::InvalidSlot"
    "ZoneRuntimeTableStatus::StaleKey"
    "ZoneRuntimeTableStatus::Busy"
    "SetCallbackMarker( ordinary.table.get(), ordinary.physicalSlot, 1)"
    "SetCallbackMarker( forgedAuthority.table.get(), forgedAuthority.physicalSlot, 2)"
    "SetCallbackMarker( unknownMarker.table.get(), unknownMarker.physicalSlot, 3)"
    "ZoneRuntimeTableStatus::UnsafeFailure"
    "!forgedAuthority.table->initialized()"
    "!unknownMarker.table->initialized()")
    require_contains(
        _registry_callback_marker_fixture "${_marker}"
        "Busy ordinary callback and poisoned authority contradictions")
endforeach()

extract_slice(
    _fixture
    "void TestCompositeAbandonmentRetryAndReentryPreservation()"
    "void TestUnsafeLiveUnloadBoundary("
    _registry_unpublish_cleanup_fixture
    "unpublish and cleanup callback registry authorization fixture")
foreach(_marker IN ITEMS
    "ensureRegistryAuthentications[0] == ZoneRuntimeTableStatus::Success"
    "ensureRejectedRegistryAuthentication == ZoneRuntimeTableStatus::StaleKey"
    "ensureRegistryReauthentications[0] == ZoneRuntimeTableStatus::Busy"
    "ensureRegistryRestore == ZoneRuntimeTableStatus::Success"
    "ensureRegistryRetryAuthentication == ZoneRuntimeTableStatus::Success"
    "ensureRegistryRetryReauthentication == ZoneRuntimeTableStatus::Busy"
    "ensureOwnershipPhases[0] == ZoneScriptStringOwnershipPhase::UnpublishingCallback"
    "observedCleaningEnsure"
    "externalRegistryAuthentication[index] == ZoneRuntimeTableStatus::Success"
    "externalRegistryReauthentication[index] == ZoneRuntimeTableStatus::Busy"
    "externalOwnershipPhases[index] == ZoneScriptStringOwnershipPhase::Cleaning")
    require_contains(
        _registry_unpublish_cleanup_fixture "${_marker}"
        "real unpublish and cleanup callback phase authorization")
endforeach()

extract_slice(
    _fixture
    "void TestCompositeScriptStringCapacityAndDemand()"
    "void TestCompositeRecoverablePlacementAndRangeRejection()"
    _capacity_demand_fixture
    "capacity/demand runtime fixture")
extract_slice(
    _capacity_demand_fixture
    "// Binding retains the allocation capacity with zero expected demand."
    "// A nonempty retained placement with zero actual strings is canonical"
    _oversized_demand_retry_fixture
    "oversized-demand failure-atomic retry fixture")
foreach(_marker IN ITEMS
    "fixture.setupStorage(3)"
    "fixture.storagePlan.scriptStringCapacity"
    "4) == ZoneRuntimeTableStatus::CapacityExceeded"
    "ZoneRuntimeSetupStage::PendingCopyBegun"
    "entry->scriptStringOwnership().isEmptyCanonical()"
    "journal && !journal->initialized()"
    "fixture.beginExactScriptStrings(1)"
    "journal->capacity() == 3"
    "journal->expectedCount() == 1")
    require_contains(
        _oversized_demand_retry_fixture "${_marker}"
        "oversized demand leaves exact placed state retryable")
endforeach()
require_ordered(
    _oversized_demand_retry_fixture
    "4) == ZoneRuntimeTableStatus::CapacityExceeded"
    "ZoneRuntimeSetupStage::PendingCopyBegun"
    "oversized demand preserves the pre-begin setup stage")
require_ordered(
    _oversized_demand_retry_fixture
    "4) == ZoneRuntimeTableStatus::CapacityExceeded"
    "fixture.beginExactScriptStrings(1)"
    "valid exact demand can retry after oversized demand")

foreach(_marker IN ITEMS
    "std::array<std::uint8_t, sizeof(JournalEntry)> prefix{};"
    "overlappingEntries"
    "3, 1) == ZoneRuntimeTableStatus::InvalidArgument"
    "aliasEntry->scriptStringOwnership().isEmptyCanonical()"
    "fixture.beginExactScriptStrings(0)"
    "journal->capacity() == 3"
    "journal->expectedCount() == 0")
    require_contains(
        _capacity_demand_fixture "${_marker}"
        "capacity-span and zero-demand runtime coverage")
endforeach()

extract_slice(
    _fixture
    "void TestCompositeStageOutputAliasPreflight()"
    "void TestCompositeCallbackContextAliasPreflightAndDrain()"
    _retained_output_fixture
    "retained output alias regression coverage")
require_substring_count(
    _fixture
    "alignas(ZoneRuntimeGenerationView) XZoneMemory zone{};"
    1
    "composite bound-zone output target has cross-ABI alignment")
foreach(_marker IN ITEMS
    "kManagedOutputBytes = 64"
    "managedClaimSlot = 32"
    "kIdleStreamOutputBytes = 64"
    "kIdleStreamOutputAlignment"
    "kIdleStreamOutputAlignment >= alignof(ZoneLoadContextKey)"
    "kIdleStreamOutputAlignment >= alignof(const ZoneRuntimeEntry *)"
    "std::align("
    "static_cast<void *>(g_streamPosStack)"
    "reinterpret_cast<const ZoneRuntimeEntry **>(&fixture.zone)"
    "reinterpret_cast<ZoneRuntimeGenerationView *>(&fixture.zone)"
    "reinterpret_cast<ZoneLoadContextKey *>(&fixture.zone)"
    "reinterpret_cast<pmem_runtime::AllocationResult *>( &fixture.zone)"
    "reinterpret_cast<std::uint32_t *>(&fixture.zone)"
    "&g_streamPosIndex"
    "TryGetZoneRuntimeEntry("
    "TryClaimZoneRuntimeGeneration("
    "TryGetZoneRuntimeGeneration("
    "TryAllocateZoneRuntimeMemory("
    "TryStageZoneRuntimeScriptString("
    "ZoneRuntimeTableStatus::InvalidArgument"
    "managedClaimLifecycle->phase() == ZoneLoadContextPhase::Empty"
    "allocationAfter.freeBytes == allocationBefore.freeBytes"
    "runtime_table_backend::backend.acquireCalls == 0")
    require_contains(
        _retained_output_fixture "${_marker}"
        "managed, idle-stream, and bound-zone outputs remain fail-closed")
endforeach()

# Compile the source in production targets, execute it on all portable hosts,
# and retain explicit measured Windows x86 Debug/Release coverage.
foreach(_marker IN ITEMS
    "\${SRC_DIR}/database/db_zone_runtime_table.cpp"
    "\${SRC_DIR}/database/db_zone_runtime_table.h")
    require_contains(_manifest "${_marker}" "production source manifest")
endforeach()
foreach(_marker IN ITEMS
    "add_executable(kisakcod-db-zone-runtime-table-tests"
    "add_executable( kisakcod-db-zone-runtime-table-production-seal-tests"
    "db_zone_runtime_table_tests.cpp"
    "db_zone_runtime_table_production_seal_tests.cpp"
    "\${SRC_DIR}/database/db_zone_runtime_table.cpp"
    "\${SRC_DIR}/database/db_zone_runtime_storage.cpp"
    "\${SRC_DIR}/database/db_zone_stream_ownership.cpp"
    "\${SRC_DIR}/database/db_zone_pending_copy_ledger.cpp"
    "\${SRC_DIR}/universal/physicalmemory_checked.cpp"
    "$<TARGET_OBJECTS:kisakcod-fx-fastfile-zone-adapter-disk32-subject>"
    "KISAK_DB_ZONE_RUNTIME_TABLE_TESTING=1"
    "KISAK_DB_ZONE_PENDING_COPY_LEDGER_TESTING=1"
    "KISAK_DB_ZONE_LOAD_CONTEXT_TESTING=1"
    "KISAK_DB_ZONE_SCRIPT_STRING_OWNERSHIP_TESTING=1"
    "NAME database-zone-runtime-table-ownership"
    "NAME database-zone-runtime-table-production-test-access-sealed"
    "database-zone-runtime-table-unload-unsafe-${_zone_runtime_unsafe_boundary}"
    "--unsafe-live-unload ${_zone_runtime_unsafe_boundary}"
    "database-zone-runtime-table-unload-invalid-missing-value"
    "database-zone-runtime-table-unload-invalid-extra-value"
    "database-zone-runtime-table-unload-invalid-unknown-option"
    "database-zone-runtime-table-mutation-unsafe-${_zone_runtime_mutation_unsafe_kind}"
    "--unsafe-mutable ${_zone_runtime_mutation_unsafe_kind}"
    "database-zone-runtime-table-mutation-invalid-missing-value"
    "database-zone-runtime-table-mutation-invalid-extra-value"
    "database-zone-runtime-table-mutation-invalid-kind"
    "database-zone-runtime-table-pending-copy-unsafe-${_zone_runtime_pending_copy_unsafe_kind}"
    "foreach(_zone_runtime_pending_copy_unsafe_kind IN ITEMS malformed-record postauth-drift count-drift duplicate-marker duplicate-unrelated-markers unknown-marker lifecycle-drift callback-lifecycle-drift)"
    "--unsafe-pending-copy ${_zone_runtime_pending_copy_unsafe_kind}"
    "database-zone-runtime-table-pending-copy-invalid-missing-value"
    "database-zone-runtime-table-pending-copy-invalid-extra-value"
    "database-zone-runtime-table-pending-copy-invalid-kind"
    "PROPERTIES TIMEOUT 30 WILL_FAIL TRUE"
    "target_link_options( kisakcod-db-zone-runtime-table-tests PRIVATE \"LINKER:/STACK:8388608\")"
    "NAME database-zone-runtime-table-source-invariants"
    "db_zone_runtime_table_source_test.cmake")
    require_contains(_tests "${_marker}" "portable CMake test integration")
endforeach()
foreach(_marker IN ITEMS
    "kisakcod-db-zone-runtime-table-tests"
    "kisakcod-db-zone-runtime-table-production-seal-tests"
    "production-test-access-sealed"
    "mutation-unsafe-(backend|postcondition)"
    "mutation-invalid-(missing-value|extra-value|kind)")
    require_contains(_ci "${_marker}" "measured Windows x86 CI integration")
endforeach()
foreach(_forbidden_warning_suppression IN ITEMS
    "-Wno-unused-parameter"
    "-Wno-unused-variable"
    "-Wno-strict-aliasing"
    "-Wno-dollar-in-identifier-extension")
    require_not_contains(
        _tests "${_forbidden_warning_suppression}"
        "runtime fixtures must fix warning causes instead of hiding them")
endforeach()

message(STATUS "Zone runtime table source invariants passed")
