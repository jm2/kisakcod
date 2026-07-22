cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_header_path
    "${SOURCE_ROOT}/src/database/db_zone_runtime_callback_context.h")
set(_source_path
    "${SOURCE_ROOT}/src/database/db_zone_runtime_callback_context.cpp")
set(_fixture_path
    "${SOURCE_ROOT}/tests/db_zone_runtime_callback_context_tests.cpp")
set(_seal_path
    "${SOURCE_ROOT}/tests/db_zone_runtime_callback_context_production_seal_tests.cpp")
set(_object_seal_path
    "${SOURCE_ROOT}/tests/db_zone_runtime_callback_context_production_object_seal_test.cmake")
set(_facade_seal_path
    "${SOURCE_ROOT}/tests/db_zone_runtime_facade_source_test.cmake")
set(_manifest_path "${SOURCE_ROOT}/scripts/common_files.cmake")
set(_tests_path "${SOURCE_ROOT}/tests/CMakeLists.txt")
set(_ci_path "${SOURCE_ROOT}/.github/workflows/ci.yml")

foreach(_path IN ITEMS
    "${_header_path}"
    "${_source_path}"
    "${_fixture_path}"
    "${_seal_path}"
    "${_object_seal_path}"
    "${_facade_seal_path}"
    "${_manifest_path}"
    "${_tests_path}"
    "${_ci_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing callback-context source: ${_path}")
    endif()
endforeach()

file(READ "${_header_path}" _header_raw)
file(READ "${_source_path}" _source_raw)
file(READ "${_fixture_path}" _fixture_raw)
file(READ "${_seal_path}" _seal_raw)
file(READ "${_object_seal_path}" _object_seal_raw)
file(READ "${_facade_seal_path}" _facade_seal_raw)
file(READ "${_manifest_path}" _manifest_raw)
file(READ "${_tests_path}" _tests_raw)
file(READ "${_ci_path}" _ci_raw)

foreach(_var IN ITEMS
    _header
    _source
    _fixture
    _seal
    _object_seal
    _facade_seal
    _manifest
    _tests
    _ci)
    set(_raw_var "${_var}_raw")
    string(REGEX REPLACE "[ \t\r\n]+" " " _normalized "${${_raw_var}}")
    set(${_var} "${_normalized}")
endforeach()

function(require_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing callback-context invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_not_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden callback-context construct (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_ordered SOURCE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${FIRST}" _first)
    string(FIND "${${SOURCE_VAR}}" "${SECOND}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered callback-context invariant (${DESCRIPTION})")
    endif()
endfunction()

function(extract_slice SOURCE_VAR START END OUTPUT DESCRIPTION)
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
    set(${OUTPUT} "${_slice}" PARENT_SCOPE)
endfunction()

# Freeze the four-state identity model and immutable public result shapes.
foreach(_marker IN ITEMS
    "enum class ZoneRuntimeCallbackContextPhase : std::uint8_t { Reserved, Unbound, Bound, Terminal, };"
    "enum class ZoneRuntimeCallbackContextStatus : std::uint8_t { Success, Busy, InvalidArgument, InvalidSlot, InvalidKey, StaleKey, InvalidPhase, GenerationExhausted, UnsafeFailure, };"
    "RUNTIME_SIZE(ZoneRuntimeCallbackContext, 0x28, 0x28);"
    "const ZoneRuntimeCallbackContext *context = nullptr;"
    "RUNTIME_SIZE(ZoneRuntimeCallbackContextBindResult, 0x8, 0x10);"
    "phase == ZoneRuntimeCallbackContextPhase::Bound || phase == ZoneRuntimeCallbackContextPhase::Terminal"
    "RUNTIME_SIZE(ZoneRuntimeCallbackContextSnapshot, 0x18, 0x18);"
    "class ZoneRuntimeCallbackContextOwner final"
    "friend class ZoneRuntimeFacade;"
    "friend class ZoneRuntimeTable;"
    "held across the complete operation and any external callback window"
    "TryResolve( std::uint32_t physicalSlot, const zone_load::ZoneLoadContextKey &expectedKey) noexcept;"
    "TryCapture( const ZoneRuntimeCallbackContext *context, const zone_load::ZoneLoadContextKey &expectedKey) noexcept;"
    "#if !KISAK_ARCH_64BIT std::uint8_t pointerAlignmentPadding_[4]{}; #endif")
    require_contains(_header "${_marker}" "closed owner-private typed surface")
endforeach()

# The owner exposes no public operation. Only deleted construction precedes
# private:, and the seven capabilities occur after that boundary.
extract_slice(
    _header
    "class ZoneRuntimeCallbackContextOwner final"
    "#ifdef KISAK_DB_ZONE_RUNTIME_CALLBACK_CONTEXT_TESTING // Tests opt in"
    _owner_declaration
    "private owner declaration")
require_ordered(
    _owner_declaration
    "private:"
    "TryClassifyStorage("
    "classification stays private")
foreach(_operation IN ITEMS
    "TryClassifyStorage("
    "TryBind("
    "TryResolve("
    "TryAdvance("
    "TryAuthenticate("
    "TryCapture("
    "SpanIsSeparated(")
    require_contains(
        _owner_declaration "${_operation}" "seven private owner operations")
endforeach()
foreach(_forbidden_free IN ITEMS
    "TryClassifyZoneRuntimeCallbackContextStorage"
    "TryBindZoneRuntimeCallbackContext"
    "TryResolveZoneRuntimeCallbackContext"
    "TryAdvanceZoneRuntimeCallbackContext"
    "TryAuthenticateZoneRuntimeCallbackContext"
    "TryCaptureZoneRuntimeCallbackContext")
    require_not_contains(
        _header "${_forbidden_free}" "no public free capability survives")
endforeach()

# Full-span storage classification, exact member index, coherent slot/index
# identity, explicit padding, and overflow-safe successor order are structural.
foreach(_marker IN ITEMS
    "constinit std::array< ZoneRuntimeCallbackContext, zone_slots::kPhysicalZoneSlotCount> g_contextStore{};"
    "std::size_t ExactStoreIndex("
    "offset % sizeof(ZoneRuntimeCallbackContext) != 0"
    "physicalSlot_ != exactStoreIndex"
    "exactStoreIndex == zone_slots::kDefaultZoneSlot"
    "pointerAlignmentPadding_[0] != 0"
    "return pmem_runtime::TryClassifyStorageIsolation( exact, sizeof(*exact));"
    "case pmem_runtime::StorageIsolationStatus::InvalidArgument: case pmem_runtime::StorageIsolationStatus::ProtectedStorageOverlap:"
    "return ZoneRuntimeCallbackContextStatus::UnsafeFailure;"
    "ZoneRuntimeCallbackContextOwner::TryResolve("
    "inputKey.slot != exactIndex"
    "inputKey.generation != context->key_.generation + 1u"
    "snapshot.key = exact->key_;"
    "bool ZoneRuntimeCallbackContextOwner::SpanIsSeparated(")
    require_contains(_source "${_marker}" "exact private store implementation")
endforeach()
require_ordered(
    _source
    "store[slot].initialize(slot);"
    "return pmem_runtime::TryClassifyStorageIsolation( exact, sizeof(*exact));"
    "all 33 contexts initialize before PMem classification")
require_ordered(
    _source
    "== (std::numeric_limits<std::uint64_t>::max)()"
    "inputKey.generation != context->key_.generation + 1u"
    "maximum generation rejects before successor arithmetic")
require_not_contains(
    _header "ContextStore" "mutable store has no public accessor")

extract_slice(
    _source
    "ZoneRuntimeCallbackContextOwner::TryResolve("
    "ZoneRuntimeCallbackContextOwner::TryAdvance("
    _resolve_body
    "nonmutating slot/key resolution body")
foreach(_marker IN ITEMS
    "ZoneRuntimeCallbackContextBindResult result{};"
    "!zone_slots::IsUsableZoneSlot(physicalSlot)"
    "!static_cast<bool>(inputKey)"
    "inputKey.slot != physicalSlot"
    "const ZoneRuntimeCallbackContext *const context = &ContextStore()[physicalSlot];"
    "MapExactStorageStatus(TryClassifyStorage(context));"
    "!context->canonical(physicalSlot)"
    "!IsRetainedPhase(context->phase_)"
    "inputKey != context->key_"
    "result.context = context;"
    "result.status = ZoneRuntimeCallbackContextStatus::Success;")
    require_contains(
        _resolve_body "${_marker}" "resolve fails closed before disclosure")
endforeach()
require_ordered(
    _resolve_body
    "!zone_slots::IsUsableZoneSlot(physicalSlot)"
    "!static_cast<bool>(inputKey)"
    "slot validity precedes key validation")
require_ordered(
    _resolve_body
    "!static_cast<bool>(inputKey)"
    "inputKey.slot != physicalSlot"
    "malformed keys precede stale cross-slot keys")
require_ordered(
    _resolve_body
    "inputKey.slot != physicalSlot"
    "MapExactStorageStatus(TryClassifyStorage(context));"
    "input identity rejects before PMem classification")
require_ordered(
    _resolve_body
    "MapExactStorageStatus(TryClassifyStorage(context));"
    "!context->canonical(physicalSlot)"
    "PMem classification precedes representation access")
require_ordered(
    _resolve_body
    "!context->canonical(physicalSlot)"
    "!IsRetainedPhase(context->phase_)"
    "canonical representation precedes retained phase")
require_ordered(
    _resolve_body
    "!IsRetainedPhase(context->phase_)"
    "inputKey != context->key_"
    "retained phase precedes exact-key comparison")
require_ordered(
    _resolve_body
    "inputKey != context->key_"
    "result.context = context;"
    "success pointer is disclosed only after exact-key validation")
string(REGEX MATCHALL
    "result\\.context[ ]*=[ ]*context"
    _resolve_disclosures
    "${_resolve_body}")
list(LENGTH _resolve_disclosures _resolve_disclosure_count)
if(NOT _resolve_disclosure_count EQUAL 1)
    message(FATAL_ERROR
        "TryResolve must contain one exact success-only pointer disclosure; "
        "found ${_resolve_disclosure_count}")
endif()
foreach(_forbidden IN ITEMS
    "context->bind("
    "context->setPhase("
    "context->refreshWitness("
    "mutableContext"
    "const_cast"
    "snapshot.")
    require_not_contains(
        _resolve_body "${_forbidden}" "TryResolve cannot mutate or disclose state")
endforeach()

extract_slice(
    _source
    "bool ZoneRuntimeCallbackContextOwner::SpanIsSeparated("
    "#ifdef KISAK_DB_ZONE_RUNTIME_CALLBACK_CONTEXT_TESTING"
    _span_separation_body
    "whole-bank span-separation body")
foreach(_marker IN ITEMS
    "!storage || size == 0 || alignment == 0"
    "start % alignment != 0"
    "size > maximum - start"
    "const std::uintptr_t end = start + size;"
    "reinterpret_cast<std::uintptr_t>(g_contextStore.data())"
    "const std::size_t bankSize = sizeof(g_contextStore);"
    "bankSize > maximum - bankBegin"
    "return end <= bankBegin || bankEnd <= start;")
    require_contains(
        _span_separation_body "${_marker}" "pure half-open bank separation")
endforeach()
foreach(_forbidden IN ITEMS
    "pmem_runtime"
    "TryClassify"
    "canonical("
    "key_"
    "phase_"
    "witness_")
    require_not_contains(
        _span_separation_body "${_forbidden}" "span check cannot read state/PMem")
endforeach()

foreach(_var IN ITEMS _header _source)
    foreach(_forbidden IN ITEMS
        "#include <database/db_zone_runtime_table.h>"
        "#include <database/db_zone_runtime_facade.h>"
        "g_zones"
        "PMem_"
        "Com_Error("
        "malloc("
        "calloc("
        "realloc("
        "operator new"
        "throw "
        "catch ("
        "std::vector"
        "std::string"
        "std::function")
        require_not_contains(
            ${_var} "${_forbidden}" "allocation-independent report-free store")
    endforeach()
endforeach()
foreach(_raw_var IN ITEMS _header_raw _source_raw)
    require_not_contains(
        ${_raw_var}
        "#define KISAK_DB_ZONE_RUNTIME_CALLBACK_CONTEXT_TESTING"
        "production module cannot self-enable TestAccess")
endforeach()

foreach(_marker IN ITEMS
    "db_zone_runtime_callback_context.cpp"
    "db_zone_runtime_callback_context.h")
    require_contains(_manifest "${_marker}" "production source manifest")
endforeach()

# The macro-off object is compiled without TestAccess; only the runtime fixture
# defines it. The production executable depends on that real object and its
# CTest wrapper performs cross-platform symbol inspection.
extract_slice(
    _tests
    "add_library(kisakcod-db-zone-runtime-callback-context-macro-off OBJECT"
    "add_executable(kisakcod-db-zone-runtime-callback-context-tests"
    _macro_off_registration
    "macro-off object registration")
require_not_contains(
    _macro_off_registration
    "KISAK_DB_ZONE_RUNTIME_CALLBACK_CONTEXT_TESTING"
    "macro-off object cannot enable TestAccess")
string(REGEX MATCHALL
    "KISAK_DB_ZONE_RUNTIME_CALLBACK_CONTEXT_TESTING"
    _test_macro_occurrences
    "${_tests_raw}")
list(LENGTH _test_macro_occurrences _test_macro_count)
if(NOT _test_macro_count EQUAL 1)
    message(FATAL_ERROR
        "Callback TestAccess macro must have one exact private fixture grant; "
        "found ${_test_macro_count}")
endif()
foreach(_marker IN ITEMS
    "kisakcod-db-zone-runtime-callback-context-macro-off"
    "kisakcod-db-zone-runtime-callback-context-tests"
    "KISAK_DB_ZONE_RUNTIME_CALLBACK_CONTEXT_TESTING=1"
    "kisakcod-db-zone-runtime-callback-context-production-seal-tests"
    "database-zone-runtime-callback-context-production-test-access-sealed"
    "database-zone-runtime-callback-context-macro-off-object-symbol-sealed"
    "db_zone_runtime_callback_context_production_object_seal_test.cmake"
    "database-zone-runtime-callback-context-source-invariants")
    require_contains(_tests "${_marker}" "portable test/seal wiring")
endforeach()

get_filename_component(
    _this_source_seal "${CMAKE_CURRENT_LIST_FILE}" ABSOLUTE)
file(GLOB_RECURSE _build_controls
    "${SOURCE_ROOT}/CMakeLists.txt"
    "${SOURCE_ROOT}/*.cmake"
    "${SOURCE_ROOT}/*.yml"
    "${SOURCE_ROOT}/*.yaml"
    "${SOURCE_ROOT}/*.ps1"
    "${SOURCE_ROOT}/*.bat")
foreach(_path IN LISTS _build_controls)
    get_filename_component(_absolute_path "${_path}" ABSOLUTE)
    if(_absolute_path STREQUAL _this_source_seal
       OR _absolute_path STREQUAL _tests_path)
        continue()
    endif()
    file(READ "${_absolute_path}" _build_control)
    string(FIND
        "${_build_control}"
        "KISAK_DB_ZONE_RUNTIME_CALLBACK_CONTEXT_TESTING"
        _macro_reference)
    if(NOT _macro_reference EQUAL -1)
        file(RELATIVE_PATH _relative "${SOURCE_ROOT}" "${_absolute_path}")
        message(FATAL_ERROR
            "Callback TestAccess macro leaked into production build control: "
            "${_relative}")
    endif()
endforeach()

foreach(_marker IN ITEMS
    "g_contextStore"
    "ContextStore"
    "ExactStoreIndex"
    "ZoneRuntimeCallbackContextTestAccess"
    "CXX_COMPILER_ID STREQUAL \"MSVC\""
    "/dump /symbols"
    "NM_TOOL"
    "READELF_TOOL"
    "OBJECT[ \\t]+LOCAL[ \\t]+DEFAULT"
    "foreach(_owner_operation IN ITEMS"
    "TryResolve"
    "ZoneRuntimeCallbackContextOwner.*")
    require_contains(_object_seal "${_marker}" "production object-symbol seal")
endforeach()
require_contains(
    _object_seal
    "Private owner methods intentionally remain normal cross-TU symbols"
    "private owner symbol rationale")
foreach(_marker IN ITEMS
    "set(_callback_context_header_path"
    "elseif(PATH STREQUAL _callback_context_header_path)"
    "OR _path STREQUAL _callback_context_header_path")
    require_contains(
        _facade_seal "${_marker}" "reviewed facade friendship exception")
endforeach()

# Hosted Windows x86 explicitly builds all compiled gates and selects runtime,
# macro-off object-symbol, production, and source tests.
foreach(_marker IN ITEMS
    "kisakcod-db-zone-runtime-callback-context-macro-off"
    "kisakcod-db-zone-runtime-callback-context-tests"
    "kisakcod-db-zone-runtime-callback-context-production-seal-tests"
    "database-zone-runtime-callback-context(-production-test-access-sealed|-macro-off-object-symbol-sealed|-source-invariants)?")
    require_contains(_ci "${_marker}" "Windows x86 callback-context CI gate")
endforeach()

foreach(_marker IN ITEMS
    "TestAllPhysicalSlotsAreStableAdjacentAndCanonical"
    "current - previous == sizeof(ZoneRuntimeCallbackContext)"
    "Access::TryClassifyStorage(interior)"
    "Access::TryClassifyStorage(onePast)"
    "Key(1, 1, 1)"
    "static_cast<pmem_runtime::StorageIsolationStatus>(0xFFu)"
    "ZoneRuntimeCallbackContextStatus::GenerationExhausted"
    "TestResolveRetainedContextWithoutFailureDisclosure"
    "Access::TryResolve(6u, key)"
    "ExpectResolveFailure"
    "TestWholeBankSpanSeparation"
    "bankBegin - 1u"
    "bankEnd - 1u"
    "bankBegin - 8u"
    "bankEnd + 1u"
    "maximum - 3u"
    "alignedToThree"
    "g_isolationCalls == isolationCalls"
    "Access::CorruptPointerAlignmentPadding"
    "Access::Restore( wrongIndex, 5u, wrongIndexKey"
    "ExpectSnapshotFailure")
    require_contains(_fixture "${_marker}" "runtime adversarial coverage")
endforeach()

foreach(_marker IN ITEMS
    "std::is_standard_layout_v<ZoneRuntimeCallbackContext>"
    "!std::is_copy_assignable_v<ZoneRuntimeCallbackContext>"
    "alignof(ZoneRuntimeCallbackContext) == 0x8u"
    "decltype(ZoneRuntimeCallbackContextBindResult::context)"
    "CanCallCanonical"
    "CanCallInitialize"
    "CanCallBind"
    "CanCallSetPhase"
    "CanCallRefreshWitness"
    "CanMutatePointerAlignmentPadding"
    "CanClassify"
    "CanOwnerBind"
    "CanResolve"
    "CanAdvance"
    "CanAuthenticate"
    "CanCapture"
    "CanSeparateSpan")
    require_contains(_seal "${_marker}" "macro-off private authority seal")
endforeach()
require_not_contains(
    _seal_raw
    "#define KISAK_DB_ZONE_RUNTIME_CALLBACK_CONTEXT_TESTING"
    "production seal cannot opt into test friendship")

# Zero enrollment covers every protected type, owner, operation, header, and
# test capability token throughout production sources outside this module.
set(_protected_tokens
    "db_zone_runtime_callback_context"
    "ZoneRuntimeCallbackContext"
    "ZoneRuntimeCallbackContextPhase"
    "ZoneRuntimeCallbackContextStatus"
    "ZoneRuntimeCallbackContextBindResult"
    "ZoneRuntimeCallbackContextSnapshot"
    "ZoneRuntimeCallbackContextOwner"
    "ZoneRuntimeCallbackContextOwner::TryClassifyStorage"
    "ZoneRuntimeCallbackContextOwner::TryBind"
    "ZoneRuntimeCallbackContextOwner::TryResolve"
    "ZoneRuntimeCallbackContextOwner::TryAdvance"
    "ZoneRuntimeCallbackContextOwner::TryAuthenticate"
    "ZoneRuntimeCallbackContextOwner::TryCapture"
    "KISAK_DB_ZONE_RUNTIME_CALLBACK_CONTEXT_TESTING")
file(GLOB_RECURSE _production_files
    "${SOURCE_ROOT}/src/*.cpp"
    "${SOURCE_ROOT}/src/*.h")
foreach(_path IN LISTS _production_files)
    if(_path STREQUAL _header_path OR _path STREQUAL _source_path)
        continue()
    endif()
    file(READ "${_path}" _candidate)
    foreach(_token IN LISTS _protected_tokens)
        string(FIND "${_candidate}" "${_token}" _reference)
        if(NOT _reference EQUAL -1)
            message(FATAL_ERROR
                "Callback context enrolled before atomic integration: "
                "${_path} contains '${_token}'")
        endif()
    endforeach()
endforeach()
