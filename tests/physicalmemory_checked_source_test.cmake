cmake_minimum_required(VERSION 3.16)

function(normalize_physicalmemory_source INPUT OUTPUT)
    string(REGEX REPLACE "[ \t\r\n]+" " " _normalized "${INPUT}")
    set(${OUTPUT} "${_normalized}" PARENT_SCOPE)
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

file(GLOB_RECURSE _production_sources
    "${SOURCE_ROOT}/src/*.cpp" "${SOURCE_ROOT}/src/*.h")
foreach(_production_path IN LISTS _production_sources)
    if(_production_path MATCHES "physicalmemory_checked\\.(cpp|h)$")
        continue()
    endif()
    file(READ "${_production_path}" _production_text)
    string(FIND "${_production_text}" "physicalmemory_checked.h"
        _checked_pmem_include)
    if(NOT _checked_pmem_include EQUAL -1)
        message(FATAL_ERROR
            "Premature checked PMem header enrollment in ${_production_path}")
    endif()
    if(_production_text MATCHES
        "physical_memory[ \t\r\n]*::[ \t\r\n]*Try(Begin|End|Free)")
        message(FATAL_ERROR
            "Premature checked PMem enrollment in ${_production_path}")
    endif()
    if(_production_text MATCHES
        "namespace[ \t\r\n]+physical_memory([ \t\r\n]*\\{|[ \t\r\n]*::)")
        message(FATAL_ERROR
            "Premature checked PMem namespace declaration in "
            "${_production_path}")
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
