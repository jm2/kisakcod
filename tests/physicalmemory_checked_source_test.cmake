cmake_minimum_required(VERSION 3.16)

function(normalize_physicalmemory_source INPUT OUTPUT)
    string(REGEX REPLACE "[ \t\r\n]+" " " _normalized "${INPUT}")
    set(${OUTPUT} "${_normalized}" PARENT_SCOPE)
endfunction()

string(ASCII 92 _checked_pmem_backslash)
string(ASCII 13 _checked_pmem_carriage_return)
string(ASCII 10 _checked_pmem_line_feed)
set(_checked_pmem_block_comment "/\\*([^*]|\\*+[^*/])*\\*+/")
set(_checked_pmem_comment_atom
    "([ \t\r\n]|${_checked_pmem_block_comment}|//[^\r\n]*)")
set(_checked_pmem_comment_gap "${_checked_pmem_comment_atom}*")
set(_checked_pmem_comment_separator "${_checked_pmem_comment_atom}+")

set(_checked_pmem_protected_tokens
    physicalmemory_checked
    physical_memory
    AllocationScopeStatus
    AllocationReceipt
    TryBegin
    TryEnd
    TryFree)

function(normalize_checked_pmem_phase2 SOURCE_VAR OUT_VAR)
    set(_candidate "${${SOURCE_VAR}}")
    string(REPLACE
        "${_checked_pmem_backslash}${_checked_pmem_carriage_return}${_checked_pmem_line_feed}"
        "" _candidate "${_candidate}")
    string(REPLACE
        "${_checked_pmem_backslash}${_checked_pmem_line_feed}"
        "" _candidate "${_candidate}")
    string(REPLACE
        "${_checked_pmem_backslash}${_checked_pmem_carriage_return}"
        "" _candidate "${_candidate}")
    set(${OUT_VAR} "${_candidate}" PARENT_SCOPE)
endfunction()

function(checked_pmem_source_has_identifier SOURCE_VAR IDENTIFIER OUT_VAR)
    string(REGEX MATCH
        "(^|[^A-Za-z0-9_])${IDENTIFIER}([^A-Za-z0-9_]|$)"
        _match "${${SOURCE_VAR}}")
    if(_match STREQUAL "")
        set(${OUT_VAR} FALSE PARENT_SCOPE)
    else()
        set(${OUT_VAR} TRUE PARENT_SCOPE)
    endif()
endfunction()

function(checked_pmem_source_has_namespace_declaration SOURCE_VAR OUT_VAR)
    string(REGEX MATCH
        "(^|[^A-Za-z0-9_])namespace${_checked_pmem_comment_separator}physical_memory(${_checked_pmem_comment_gap}::|${_checked_pmem_comment_gap}\\{)"
        _match "${${SOURCE_VAR}}")
    if(_match STREQUAL "")
        set(${OUT_VAR} FALSE PARENT_SCOPE)
    else()
        set(${OUT_VAR} TRUE PARENT_SCOPE)
    endif()
endfunction()

function(checked_pmem_source_has_token_paste SOURCE_VAR OUT_VAR)
    foreach(_operator IN ITEMS "##" "%:%:" "??/" "??=")
        string(FIND "${${SOURCE_VAR}}" "${_operator}" _position)
        if(NOT _position EQUAL -1)
            set(${OUT_VAR} TRUE PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${OUT_VAR} FALSE PARENT_SCOPE)
endfunction()

function(remove_reviewed_checked_pmem_token_text PATH SOURCE_VAR OUT_VAR)
    set(_candidate "${${SOURCE_VAR}}")
    if(PATH MATCHES "/universal/q_shared\\.h$")
        string(REPLACE "num ## LL" "" _candidate "${_candidate}")
        string(REPLACE "num ## i64" "" _candidate "${_candidate}")
    elseif(PATH MATCHES "/server_mp/sv_client_mp\\.cpp$")
        string(REPLACE
            "\"###!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!###\\n\""
            "" _candidate "${_candidate}")
        string(REPLACE
            "\"########################################\\n\""
            "" _candidate "${_candidate}")
    elseif(PATH MATCHES "/ui/ui_component\\.cpp$")
        string(REPLACE "\"##\"" "" _candidate "${_candidate}")
    elseif(PATH MATCHES "/ui/ui_shared_obj\\.cpp$")
        string(REPLACE "\"##\"" "" _candidate "${_candidate}")
        string(REPLACE
            "\"define with misplaced ##\"" "" _candidate "${_candidate}")
    endif()
    set(${OUT_VAR} "${_candidate}" PARENT_SCOPE)
endfunction()

function(remove_reviewed_runtime_table_pmem_constructs
    PATH SOURCE_VAR OUT_VAR)
    set(_candidate "${${SOURCE_VAR}}")
    string(REGEX REPLACE "[ \t\r\n]+" " " _candidate "${_candidate}")

    if(PATH MATCHES "database/db_zone_runtime_table\\.h$")
        set(_reviewed_constructs
            "#include <universal/physicalmemory_checked.h>"
            "physical_memory::AllocationReceipt allocationReceipt_{}"
            "static physical_memory::AllocationReceipt *AllocationReceipt(")
    elseif(PATH MATCHES "database/db_zone_runtime_table\\.cpp$")
        set(_reviewed_constructs
            "bool IsPristineRuntimeReceipt( const physical_memory::AllocationReceipt &receipt) noexcept")
    else()
        message(FATAL_ERROR
            "Checked-PMem passive review received an unexpected path: ${PATH}")
    endif()

    foreach(_reviewed_construct IN LISTS _reviewed_constructs)
        set(_remaining "${_candidate}")
        set(_construct_count 0)
        while(TRUE)
            string(FIND "${_remaining}" "${_reviewed_construct}" _position)
            if(_position EQUAL -1)
                break()
            endif()
            math(EXPR _construct_count "${_construct_count} + 1")
            string(LENGTH "${_reviewed_construct}" _construct_length)
            math(EXPR _next "${_position} + ${_construct_length}")
            string(SUBSTRING "${_remaining}" ${_next} -1 _remaining)
        endwhile()
        if(NOT _construct_count EQUAL 1)
            message(FATAL_ERROR
                "Checked-PMem reviewed construct count drifted in ${PATH}: "
                "expected one '${_reviewed_construct}', found ${_construct_count}")
        endif()
        string(REPLACE
            "${_reviewed_construct}"
            "KISAK_REVIEWED_PASSIVE_PMEM_CONSTRUCT"
            _candidate "${_candidate}")
    endforeach()

    set(${OUT_VAR} "${_candidate}" PARENT_SCOPE)
endfunction()

function(detect_checked_pmem_enrollment SOURCE_VAR OUT_VAR)
    set(_found FALSE)
    foreach(_token IN LISTS _checked_pmem_protected_tokens)
        checked_pmem_source_has_identifier(
            ${SOURCE_VAR} "${_token}" _token_found)
        if(_token_found)
            set(_found TRUE)
        endif()
    endforeach()
    checked_pmem_source_has_token_paste(${SOURCE_VAR} _paste_found)
    if(_paste_found)
        set(_found TRUE)
    endif()
    set(${OUT_VAR} ${_found} PARENT_SCOPE)
endfunction()

function(require_physicalmemory_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing checked physical-memory invariant (${DESCRIPTION}): "
            "${NEEDLE}")
    endif()
endfunction()

function(forbid_physicalmemory_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden checked physical-memory regression (${DESCRIPTION})")
    endif()
endfunction()

function(require_physicalmemory_ordered SOURCE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${FIRST}" _first)
    string(FIND "${${SOURCE_VAR}}" "${SECOND}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered checked physical-memory invariant "
            "(${DESCRIPTION})")
    endif()
endfunction()

foreach(_relative IN ITEMS
    "src/universal/physicalmemory_checked.h"
    "src/universal/physicalmemory_checked.cpp"
    "tests/physicalmemory_checked_tests.cpp"
    "tests/physicalmemory_checked_api_seal_tests.cpp")
    if(NOT EXISTS "${SOURCE_ROOT}/${_relative}")
        message(FATAL_ERROR "Missing checked physical-memory file: ${_relative}")
    endif()
endforeach()

file(READ "${SOURCE_ROOT}/src/universal/physicalmemory_checked.h" _header_raw)
file(READ "${SOURCE_ROOT}/src/universal/physicalmemory_checked.cpp" _source_raw)
file(READ "${SOURCE_ROOT}/tests/physicalmemory_checked_tests.cpp" _tests_raw)
file(READ
    "${SOURCE_ROOT}/tests/physicalmemory_checked_api_seal_tests.cpp"
    _seal_raw)
file(READ "${SOURCE_ROOT}/scripts/common_files.cmake" _common_raw)
file(READ "${SOURCE_ROOT}/tests/CMakeLists.txt" _cmake_raw)
file(READ "${SOURCE_ROOT}/.github/workflows/ci.yml" _ci_raw)
file(READ "${SOURCE_ROOT}/src/database/db_registry.cpp" _registry_raw)

normalize_physicalmemory_source("${_header_raw}" _header)
normalize_physicalmemory_source("${_source_raw}" _source)
normalize_physicalmemory_source("${_tests_raw}" _tests)
normalize_physicalmemory_source("${_seal_raw}" _seal)
normalize_physicalmemory_source("${_common_raw}" _common)
normalize_physicalmemory_source("${_cmake_raw}" _cmake)
normalize_physicalmemory_source("${_ci_raw}" _ci)
normalize_physicalmemory_source("${_registry_raw}" _registry)

foreach(_marker IN ITEMS
    "enum class AllocationScopeStatus : std::uint8_t"
    "class AllocationReceipt final"
    "AllocationReceipt(const AllocationReceipt &) = delete;"
    "AllocationReceipt(AllocationReceipt &&) = delete;"
    "~AllocationReceipt() noexcept;"
    "std::uint8_t phaseWitness_;"
    "[[nodiscard]] bool isCanonical() const noexcept;"
    "[[nodiscard]] AllocationScopeStatus TryBegin( PhysicalMemory *memory, std::uint32_t allocType, const char *stableName, AllocationReceipt *receipt) noexcept;"
    "[[nodiscard]] AllocationScopeStatus TryEnd( AllocationReceipt *receipt) noexcept;"
    "[[nodiscard]] AllocationScopeStatus TryFree( AllocationReceipt *receipt) noexcept;"
    "externally serialize every access to both prims"
    "lifecycle/init helpers or directly replace"
    "only after authenticating that exact generation's Freed terminal"
    "PhysicalMemory control storage and AllocationReceipt storage must be"
    "mutually disjoint. Both objects must remain wholly outside the entire"
    "High-prim topology can suggest a historical upper bound"
    "does not retain an independently authenticated"
    "cannot authenticate or validate"
    "owns the authoritative initialization"
    "reclaimable backing range unless the caller independently guarantees"
    "cannot be overwritten or reused"
    "never dereferenced by this")
    require_physicalmemory_contains(
        _header "${_marker}" "sealed report-free public contract")
endforeach()

foreach(_marker IN ITEMS
    "for (std::uint32_t index = prim.allocListCount; index < kPhysicalAllocationCapacity; ++index)"
    "if (prim.allocList[index].name != nullptr)"
    "prim.allocList[prim.allocListCount - 1].name == nullptr"
    "prim.allocName != prim.allocList[prim.allocListCount - 1].name"
    "allocType != 0 || prim.pos == 0"
    "allocType == 0 && prim.allocList[0].pos != 0"
    "prim.allocList[index - 1].pos > position"
    "prim.allocList[index - 1].pos < position"
    "memory->prim[0].pos <= memory->prim[1].pos"
    "ValidatePrimTopology(memory->prim[0], 0)"
    "ValidatePrimTopology(memory->prim[1], 1)"
    "if (!receipt->isCanonical())"
    "receipt->phaseWitness_ = static_cast<std::uint8_t>(receipt->phase_) ^ kPhaseWitnessMask;"
    "PhysicalMemoryAllocation &entry = prim.allocList[index];"
    "PhysicalMemoryAllocation &entry = prim.allocList[receipt->index_];"
    "while (remaining != 0 && prim.allocList[remaining - 1].name == nullptr)"
    "restoredPosition = prim.allocList[remaining - 1].pos;")
    require_physicalmemory_contains(
        _source "${_marker}" "full typed topology and receipt validation")
endforeach()

set(_canonical_remaining "${_source}")
set(_canonical_check_count 0)
while(TRUE)
    string(FIND "${_canonical_remaining}"
        "if (!receipt->isCanonical())" _canonical_position)
    if(_canonical_position EQUAL -1)
        break()
    endif()
    math(EXPR _canonical_check_count "${_canonical_check_count} + 1")
    math(EXPR _canonical_next "${_canonical_position} + 28")
    string(SUBSTRING "${_canonical_remaining}"
        ${_canonical_next} -1 _canonical_remaining)
endwhile()
if(NOT _canonical_check_count EQUAL 3)
    message(FATAL_ERROR
        "Every checked lifecycle operation must validate canonical receipt "
        "state (expected 3, found ${_canonical_check_count})")
endif()

string(FIND "${_source}" "AllocationScopeStatus TryFree(" _free_start)
string(FIND "${_source}" "} // namespace physical_memory" _free_end)
if(_free_start EQUAL -1 OR _free_end LESS_EQUAL _free_start)
    message(FATAL_ERROR "Could not isolate checked physical-memory free")
endif()
math(EXPR _free_length "${_free_end} - ${_free_start}")
string(SUBSTRING "${_source}" ${_free_start} ${_free_length} _free)
require_physicalmemory_ordered(
    _free
    "if (!receipt->matchesEntry(prim))"
    "if (prim.allocName != nullptr)"
    "exact receipt authentication must precede selected-prim Busy")
require_physicalmemory_ordered(
    _free
    "if (!ValidateMemoryTopology(receipt->owner_))"
    "PhysicalMemoryPrim &prim = *receipt->prim_;"
    "both prims must validate before free state access/mutation")

foreach(_forbidden IN ITEMS
    "PMem_BeginAlloc("
    "PMem_BeginAllocInPrim("
    "PMem_EndAlloc("
    "PMem_EndAllocInPrim("
    "PMem_Free("
    "PMem_FreeInPrim("
    "PMem_FreeIndex("
    "MyAssertHandler("
    "Com_Error("
    "Sys_OutOfMem"
    "strlen("
    "strcmp("
    "std::string"
    "throw "
    "new ")
    forbid_physicalmemory_contains(
        _source "${_forbidden}" "no legacy/reporting/allocation/string path")
endforeach()

# This foundation remains production-neutral. Existing registry callers stay
# on the legacy route until the generation-keyed resource coordinator owns the
# exact receipt and can enforce its no-bypass precondition.
foreach(_legacy_call IN ITEMS
    "PMem_BeginAlloc(zone->name, g_zoneAllocType);"
    "PMem_EndAlloc(zone->name, g_zoneAllocType);"
    "PMem_Free(zone->name, zone->allocType);")
    require_physicalmemory_contains(
        _registry "${_legacy_call}" "legacy production caller preservation")
endforeach()
foreach(_forbidden IN ITEMS
    "physicalmemory_checked.h"
    "physical_memory::TryBegin"
    "physical_memory::TryEnd"
    "physical_memory::TryFree")
    forbid_physicalmemory_contains(
        _registry "${_forbidden}" "no premature registry enrollment")
endforeach()

function(require_runtime_table_checked_pmem_fixture SOURCE_VAR DESCRIPTION)
    set(_fixture_with_reviewed_storage
        "${_runtime_table_passive_pmem_header_fixture}\n${${SOURCE_VAR}}")
    normalize_checked_pmem_phase2(
        _fixture_with_reviewed_storage _candidate)
    checked_pmem_source_has_namespace_declaration(
        _candidate _namespace_declaration)
    remove_reviewed_runtime_table_pmem_constructs(
        "database/db_zone_runtime_table.h" _candidate _candidate)
    detect_checked_pmem_enrollment(_candidate _detected)
    if(NOT _namespace_declaration AND NOT _detected)
        message(FATAL_ERROR
            "Checked-PMem runtime-table seal missed ${DESCRIPTION}")
    endif()
endfunction()

string(CONCAT _runtime_table_passive_pmem_header_fixture
    "#include <universal/physicalmemory_checked.h>\n"
    "physical_memory::AllocationReceipt allocationReceipt_{};\n"
    "static physical_memory::AllocationReceipt *AllocationReceipt(")
remove_reviewed_runtime_table_pmem_constructs(
    "database/db_zone_runtime_table.h"
    _runtime_table_passive_pmem_header_fixture
    _runtime_table_passive_pmem_reviewed)
detect_checked_pmem_enrollment(
    _runtime_table_passive_pmem_reviewed _runtime_table_passive_pmem_enrolled)
if(_runtime_table_passive_pmem_enrolled)
    message(FATAL_ERROR
        "Checked-PMem seal rejected reviewed passive table storage")
endif()
string(CONCAT _runtime_table_passive_pmem_source_fixture
    "bool IsPristineRuntimeReceipt(\n"
    "    const physical_memory::AllocationReceipt &receipt) noexcept")
remove_reviewed_runtime_table_pmem_constructs(
    "database/db_zone_runtime_table.cpp"
    _runtime_table_passive_pmem_source_fixture
    _runtime_table_passive_pmem_source_reviewed)
detect_checked_pmem_enrollment(
    _runtime_table_passive_pmem_source_reviewed
    _runtime_table_passive_pmem_source_enrolled)
if(_runtime_table_passive_pmem_source_enrolled)
    message(FATAL_ERROR
        "Checked-PMem seal rejected the exact const pristine predicate")
endif()
set(_runtime_table_pmem_pointer_bypass
    "auto begin = &physical_memory::TryBegin;")
require_runtime_table_checked_pmem_fixture(
    _runtime_table_pmem_pointer_bypass "a qualified function pointer")
set(_runtime_table_pmem_using_bypass
    "using physical_memory/**/::/**/TryEnd; auto end = &TryEnd;")
require_runtime_table_checked_pmem_fixture(
    _runtime_table_pmem_using_bypass "a commented using declaration")
string(CONCAT _runtime_table_pmem_splice_bypass
    "auto free_scope = &Try"
    "${_checked_pmem_backslash}${_checked_pmem_line_feed}Free;")
require_runtime_table_checked_pmem_fixture(
    _runtime_table_pmem_splice_bypass "a phase-2-spliced API")
set(_runtime_table_pmem_alias_bypass
    "namespace pmem = physical_memory; auto begin = &pmem::TryBegin;")
require_runtime_table_checked_pmem_fixture(
    _runtime_table_pmem_alias_bypass "a namespace-alias API")
set(_runtime_table_pmem_adl_bypass
    "physical_memory::AllocationReceipt receipt; TryBegin(nullptr, 0, nullptr, &receipt);")
require_runtime_table_checked_pmem_fixture(
    _runtime_table_pmem_adl_bypass "an unqualified ADL operation")
set(_runtime_table_pmem_paste_bypass
    "#define KISAK_PMEM_CAT(a,b) a ## b\n"
    "auto begin = &KISAK_PMEM_CAT(Try,Begin);")
require_runtime_table_checked_pmem_fixture(
    _runtime_table_pmem_paste_bypass "a token-pasted API")
set(_runtime_table_pmem_namespace_bypass
    "namespace/**/physical_memory { class Forged; }")
require_runtime_table_checked_pmem_fixture(
    _runtime_table_pmem_namespace_bypass "a reopened checked-PMem namespace")
set(_runtime_table_pmem_bare_alias_bypass
    "namespace pmem = physical_memory;")
require_runtime_table_checked_pmem_fixture(
    _runtime_table_pmem_bare_alias_bypass
    "a namespace alias without an immediate operation")
set(_runtime_table_pmem_type_alias_bypass
    "using ReceiptAlias = physical_memory::AllocationReceipt;")
require_runtime_table_checked_pmem_fixture(
    _runtime_table_pmem_type_alias_bypass
    "an unreviewed receipt type alias")
set(_runtime_table_pmem_using_namespace_bypass
    "using namespace physical_memory;")
require_runtime_table_checked_pmem_fixture(
    _runtime_table_pmem_using_namespace_bypass
    "an unreviewed using-namespace directive")
set(_runtime_table_pmem_namespace_macro_bypass
    "#define KISAK_RUNTIME_PMEM_NAMESPACE physical_memory")
require_runtime_table_checked_pmem_fixture(
    _runtime_table_pmem_namespace_macro_bypass
    "a macro-exposed checked-PMem namespace")
set(_runtime_table_pmem_header_macro_bypass
    "#define KISAK_RUNTIME_PMEM_HEADER physicalmemory_checked")
require_runtime_table_checked_pmem_fixture(
    _runtime_table_pmem_header_macro_bypass
    "a macro-exposed checked-PMem header stem")

file(GLOB_RECURSE _production_sources
    LIST_DIRECTORIES FALSE "${SOURCE_ROOT}/src/*")
foreach(_non_extension_sentinel IN ITEMS
    "${SOURCE_ROOT}/src/groupvoice/speex/Makefile.am"
    "${SOURCE_ROOT}/src/groupvoice/speex/Makefile.in")
    list(FIND _production_sources
        "${_non_extension_sentinel}" _sentinel_index)
    if(_sentinel_index EQUAL -1)
        message(FATAL_ERROR
            "Checked-PMem production seal lost extension-independent "
            "traversal: ${_non_extension_sentinel}")
    endif()
endforeach()
foreach(_production_path IN LISTS _production_sources)
    if(_production_path MATCHES "physicalmemory_checked\\.(cpp|h)$")
        continue()
    endif()
    file(READ "${_production_path}" _production_raw)
    normalize_checked_pmem_phase2(_production_raw _production_text)
    remove_reviewed_checked_pmem_token_text(
        "${_production_path}" _production_text _production_text)
    if(_production_path MATCHES
        "database/db_zone_runtime_table\.(h|cpp)$")
        checked_pmem_source_has_namespace_declaration(
            _production_text _runtime_namespace_declaration)
        if(_runtime_namespace_declaration)
            message(FATAL_ERROR
                "Passive runtime-table composition reopened the checked-PMem "
                "namespace in ${_production_path}")
        endif()
        remove_reviewed_runtime_table_pmem_constructs(
            "${_production_path}" _production_text _production_text)
    endif()
    detect_checked_pmem_enrollment(_production_text _enrolled)
    if(_enrolled)
        message(FATAL_ERROR
            "Premature checked PMem enrollment in ${_production_path}")
    endif()
endforeach()

# Keep the exact include/using bypass that motivated this seal recognizable to
# its detectors. The production scan above rejects the include independently;
# the qualified detector also rejects the using-declaration before its later
# call becomes unqualified.
set(_checked_pmem_using_bypass
    "#include <universal/physicalmemory_checked.h>\n"
    "using physical_memory::TryBegin;\n"
    "void Bypass() { TryBegin(nullptr, 0, nullptr, nullptr); }")
string(FIND "${_checked_pmem_using_bypass}" "physicalmemory_checked.h"
    _checked_pmem_bypass_header)
if(_checked_pmem_bypass_header EQUAL -1 OR NOT _checked_pmem_using_bypass
    MATCHES "physical_memory[ \t\r\n]*::[ \t\r\n]*Try(Begin|End|Free)")
    message(FATAL_ERROR
        "Checked PMem production seal no longer recognizes include/using bypass")
endif()

foreach(_marker IN ITEMS
    "physicalmemory_checked.cpp"
    "physicalmemory_checked.h")
    require_physicalmemory_contains(
        _common "${_marker}" "engine source-manifest coverage")
endforeach()
foreach(_marker IN ITEMS
    "add_executable(kisakcod-physicalmemory-checked-tests"
    "physicalmemory_checked_tests.cpp"
    "NAME universal-physicalmemory-checked-scopes"
    "add_executable(kisakcod-physicalmemory-checked-api-seal-tests"
    "physicalmemory_checked_api_seal_tests.cpp"
    "NAME universal-physicalmemory-checked-api-sealed"
    "NAME universal-physicalmemory-checked-source-invariants")
    require_physicalmemory_contains(
        _cmake "${_marker}" "portable CMake test registration")
endforeach()
foreach(_marker IN ITEMS
    "!std::is_trivially_copyable_v<Receipt>"
    "!CanReachSelf<Receipt>"
    "!CanReachOwner<Receipt>"
    "!CanReachPrim<Receipt>"
    "!CanReachName<Receipt>"
    "!CanReachIndex<Receipt>"
    "!CanReachAllocType<Receipt>"
    "!CanReachStartPosition<Receipt>"
    "!CanReachPhase<Receipt>"
    "!CanReachPhaseWitness<Receipt>"
    "!CanReachReserved<Receipt>"
    "!CanReset<Receipt>")
    require_physicalmemory_contains(
        _seal "${_marker}" "positive production receipt authority seal")
endforeach()
string(FIND "${_cmake}"
    "add_executable(kisakcod-physicalmemory-checked-tests" _seal_start)
string(FIND "${_cmake}"
    "add_executable(kisakcod-fx-visibility-atomic-tests" _seal_end)
if(_seal_start EQUAL -1 OR _seal_end LESS_EQUAL _seal_start)
    message(FATAL_ERROR
        "Could not isolate checked physical-memory test registration")
endif()
math(EXPR _seal_length "${_seal_end} - ${_seal_start}")
string(SUBSTRING "${_cmake}" ${_seal_start} ${_seal_length}
    _seal_registration)
foreach(_forbidden IN ITEMS "WILL_FAIL" "EXCLUDE_FROM_ALL")
    forbid_physicalmemory_contains(
        _seal_registration "${_forbidden}"
        "API seal must be a positive portable target")
endforeach()

foreach(_marker IN ITEMS
    "kisakcod-physicalmemory-checked-tests"
    "kisakcod-physicalmemory-checked-api-seal-tests"
    "universal-physicalmemory-checked-(scopes|api-sealed|source-invariants)")
    require_physicalmemory_contains(
        _ci "${_marker}" "measured Windows x86 CI selection")
endforeach()

foreach(_marker IN ITEMS
    "TestMiddleHolesAndLowTailCollapse();"
    "TestMiddleHolesAndHighTailCollapse();"
    "TestReceiptPhaseWitnessAndTerminalCanonicality();"
    "TestBothPrimsRevalidatedBeforeEndAndFree();"
    "AllocationScopeStatus::CapacityExceeded"
    "AllocationScopeStatus::ReceiptMismatch"
    "AllocationScopeStatus::AlreadyComplete")
    require_physicalmemory_contains(
        _tests "${_marker}" "runtime failure-atomicity and topology coverage")
endforeach()

message(STATUS "Checked physical-memory source invariants verified")
