cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

file(READ
    "${SOURCE_ROOT}/src/universal/physicalmemory.h"
    physicalmemory_header)
file(READ
    "${SOURCE_ROOT}/src/universal/physicalmemory.cpp"
    physicalmemory_source)
file(READ
    "${SOURCE_ROOT}/tests/physicalmemory_legacy_tests.cpp"
    physicalmemory_fixture)
file(READ
    "${SOURCE_ROOT}/tests/CMakeLists.txt"
    test_manifest)
file(READ
    "${SOURCE_ROOT}/.github/workflows/ci.yml"
    ci_workflow)

function(require_text content needle description)
    string(FIND "${content}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Missing ${description}: ${needle}")
    endif()
endfunction()

function(reject_text content needle description)
    string(FIND "${content}" "${needle}" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR "Found forbidden ${description}: ${needle}")
    endif()
endfunction()

function(extract_function content begin_marker end_marker output_name)
    string(FIND "${content}" "${begin_marker}" begin_position)
    if(begin_position EQUAL -1)
        message(FATAL_ERROR "Missing function start: ${begin_marker}")
    endif()
    string(FIND "${content}" "${end_marker}" end_position)
    if(end_position EQUAL -1 OR end_position LESS_EQUAL begin_position)
        message(FATAL_ERROR
            "Missing ordered function end after ${begin_marker}: ${end_marker}")
    endif()
    math(EXPR function_length "${end_position} - ${begin_position}")
    string(SUBSTRING
        "${content}" ${begin_position} ${function_length} function_body)
    set(${output_name} "${function_body}" PARENT_SCOPE)
endfunction()

function(require_literal_count content needle expected_count description)
    set(remainder "${content}")
    set(actual_count 0)
    string(LENGTH "${needle}" needle_length)
    while(TRUE)
        string(FIND "${remainder}" "${needle}" position)
        if(position EQUAL -1)
            break()
        endif()
        math(EXPR actual_count "${actual_count} + 1")
        math(EXPR remainder_begin "${position} + ${needle_length}")
        string(SUBSTRING "${remainder}" ${remainder_begin} -1 remainder)
    endwhile()
    if(NOT actual_count EQUAL expected_count)
        message(FATAL_ERROR
            "Expected ${expected_count} ${description}, found ${actual_count}: ${needle}")
    endif()
endfunction()

function(require_order content first second description)
    string(FIND "${content}" "${first}" first_position)
    string(FIND "${content}" "${second}" second_position)
    if(first_position EQUAL -1 OR second_position EQUAL -1)
        message(FATAL_ERROR
            "Missing ordered ${description}: ${first} -> ${second}")
    endif()
    if(NOT first_position LESS second_position)
        message(FATAL_ERROR
            "Misordered ${description}: ${first} -> ${second}")
    endif()
endfunction()

extract_function(
    "${physicalmemory_source}"
    "void __cdecl PMem_BeginAlloc("
    "void __cdecl PMem_BeginAllocInPrim("
    begin_wrapper)
extract_function(
    "${physicalmemory_source}"
    "void __cdecl PMem_BeginAllocInPrim("
    "void __cdecl PMem_EndAlloc("
    begin_in_prim)
extract_function(
    "${physicalmemory_source}"
    "void __cdecl PMem_EndAlloc("
    "void __cdecl PMem_EndAllocInPrim("
    end_wrapper)
extract_function(
    "${physicalmemory_source}"
    "void __cdecl PMem_EndAllocInPrim("
    "void __cdecl PMem_Free("
    end_in_prim)
extract_function(
    "${physicalmemory_source}"
    "void __cdecl PMem_Free("
    "void __cdecl PMem_FreeInPrim("
    free_wrapper)
extract_function(
    "${physicalmemory_source}"
    "void __cdecl PMem_FreeInPrim("
    "void __cdecl PMem_FreeIndex("
    free_in_prim)
extract_function(
    "${physicalmemory_source}"
    "void __cdecl PMem_FreeIndex("
    "int __cdecl PMem_GetOverAllocatedSize("
    free_index)

require_text(
    "${physicalmemory_header}"
    "#include <universal/kisak_abi.h>"
    "portable ABI contract include")
require_text(
    "${physicalmemory_header}"
    "#include <cstddef>"
    "standalone offsetof include")
require_text(
    "${physicalmemory_header}"
    "void KISAK_CDECL PMem_Init();"
    "portable public calling convention")
require_text(
    "${physicalmemory_header}"
    "RUNTIME_SIZE(PhysicalMemoryAllocation, 0x8, 0x10);"
    "allocation layout freeze")
require_text(
    "${physicalmemory_header}"
    "RUNTIME_SIZE(PhysicalMemoryPrim, 0x10C, 0x210);"
    "prim layout freeze")
require_text(
    "${physicalmemory_header}"
    "RUNTIME_SIZE(PhysicalMemory, 0x21C, 0x428);"
    "global layout freeze")

# Each public alloc-type wrapper must report exactly once and return before it
# can use the rejected type as a g_mem.prim index.
foreach(wrapper IN ITEMS begin_wrapper end_wrapper free_wrapper)
    require_literal_count(
        "${${wrapper}}" "if (allocType >= 2)" 1
        "alloc-type guard in ${wrapper}")
    require_literal_count(
        "${${wrapper}}" "return;" 1
        "fail-closed return in ${wrapper}")
    require_literal_count(
        "${${wrapper}}" "g_mem.prim[allocType]" 1
        "global prim index in ${wrapper}")
    require_order(
        "${${wrapper}}" "if (allocType >= 2)" "return;"
        "alloc-type guard/report return in ${wrapper}")
    require_order(
        "${${wrapper}}" "return;" "g_mem.prim[allocType]"
        "alloc-type return before global indexing in ${wrapper}")
    require_text(
        "${${wrapper}}" "\\t%u not in [0, %u)"
        "type-correct alloc-type diagnostic in ${wrapper}")
    require_text(
        "${${wrapper}}" "2u);"
        "unsigned alloc-type bound argument in ${wrapper}")
    reject_text(
        "${${wrapper}}" "\\t%i not in [0, %i)"
        "signed alloc-type diagnostic in ${wrapper}")
endforeach()

# BeginInPrim validates every input/state gate exactly once, then mutates.
require_literal_count("${begin_in_prim}" "if (!prim)" 1
    "null-prim guard in BeginInPrim")
require_literal_count("${begin_in_prim}" "if (!name)" 1
    "null-name guard in BeginInPrim")
require_literal_count("${begin_in_prim}" "if (prim->allocName)" 1
    "busy guard in BeginInPrim")
require_literal_count("${begin_in_prim}" "if (prim->allocListCount >= 0x20)" 1
    "capacity guard in BeginInPrim")
require_literal_count("${begin_in_prim}" "return;" 4
    "fail-closed returns in BeginInPrim")
require_order("${begin_in_prim}" "if (!prim)" "if (!name)"
    "BeginInPrim null guards")
require_order("${begin_in_prim}" "if (!name)" "if (prim->allocName)"
    "BeginInPrim name before busy guard")
require_order("${begin_in_prim}" "if (prim->allocName)" "if (prim->allocListCount >= 0x20)"
    "BeginInPrim busy before capacity guard")
require_order("${begin_in_prim}" "if (prim->allocListCount >= 0x20)" "prim->allocName = name;"
    "BeginInPrim validation before active-name mutation")
require_order("${begin_in_prim}" "prim->allocName = name;" "allocEntry = &prim->allocList[prim->allocListCount++];"
    "BeginInPrim active name before bounded entry append")

# EndInPrim validates identity, count bounds, and the typed tail before clear.
require_literal_count("${end_in_prim}" "if (!prim)" 1
    "null-prim guard in EndInPrim")
require_literal_count("${end_in_prim}" "if (!name)" 1
    "null-name guard in EndInPrim")
require_literal_count("${end_in_prim}" "if (prim->allocName != name)" 1
    "active-name guard in EndInPrim")
require_literal_count("${end_in_prim}" "if (!prim->allocListCount)" 1
    "zero-count guard in EndInPrim")
require_literal_count("${end_in_prim}" "if (prim->allocListCount > 0x20)" 1
    "overcount guard in EndInPrim")
require_literal_count("${end_in_prim}" "if (allocEntry.name != name)" 1
    "tail-identity guard in EndInPrim")
require_literal_count("${end_in_prim}" "return;" 6
    "fail-closed returns in EndInPrim")
require_order("${end_in_prim}" "if (!prim)" "if (!name)"
    "EndInPrim null guards")
require_order("${end_in_prim}" "if (!name)" "if (prim->allocName != name)"
    "EndInPrim name before active identity")
require_order("${end_in_prim}" "if (prim->allocName != name)" "if (!prim->allocListCount)"
    "EndInPrim identity before count")
require_order("${end_in_prim}" "if (!prim->allocListCount)" "if (prim->allocListCount > 0x20)"
    "EndInPrim zero before overcount")
require_order("${end_in_prim}" "if (prim->allocListCount > 0x20)" "const PhysicalMemoryAllocation &allocEntry"
    "EndInPrim bounded typed tail access")
require_order("${end_in_prim}" "if (allocEntry.name != name)" "prim->allocName = nullptr;"
    "EndInPrim topology validation before clear")

# FreeInPrim bounds the scan before the first allocList dereference.
require_literal_count("${free_in_prim}" "if (!prim)" 1
    "null-prim guard in FreeInPrim")
require_literal_count("${free_in_prim}" "if (!name)" 1
    "null-name guard in FreeInPrim")
require_literal_count("${free_in_prim}" "if (prim->allocListCount > 0x20)" 1
    "overcount guard in FreeInPrim")
require_literal_count("${free_in_prim}" "return;" 4
    "three rejected inputs plus successful-match return in FreeInPrim")
require_order("${free_in_prim}" "if (!prim)" "if (!name)"
    "FreeInPrim null guards")
require_order("${free_in_prim}" "if (!name)" "if (prim->allocListCount > 0x20)"
    "FreeInPrim name before overcount")
require_order("${free_in_prim}" "if (prim->allocListCount > 0x20)" "for (allocIndex = 0;"
    "FreeInPrim validation before scan")

# FreeIndex validates every dereference prerequisite before clearing an entry.
require_literal_count("${free_index}" "if (!prim)" 1
    "null-prim guard in FreeIndex")
require_literal_count("${free_index}" "if (prim->allocName)" 1
    "busy guard in FreeIndex")
require_literal_count("${free_index}" "if (!prim->allocListCount)" 1
    "zero-count guard in FreeIndex")
require_literal_count("${free_index}" "if (prim->allocListCount > 0x20)" 1
    "overcount guard in FreeIndex")
require_literal_count("${free_index}" "if (allocIndex >= prim->allocListCount)" 1
    "index guard in FreeIndex")
require_literal_count("${free_index}" "if (!name)" 1
    "entry-name guard in FreeIndex")
require_literal_count("${free_index}" "return;" 6
    "fail-closed returns in FreeIndex")
require_order("${free_index}" "if (!prim)" "if (prim->allocName)"
    "FreeIndex null before busy")
require_order("${free_index}" "if (prim->allocName)" "if (!prim->allocListCount)"
    "FreeIndex busy before count")
require_order("${free_index}" "if (!prim->allocListCount)" "if (prim->allocListCount > 0x20)"
    "FreeIndex zero before overcount")
require_order("${free_index}" "if (prim->allocListCount > 0x20)" "if (allocIndex >= prim->allocListCount)"
    "FreeIndex count before index")
require_order("${free_index}" "if (allocIndex >= prim->allocListCount)" "allocEntry = &prim->allocList[allocIndex];"
    "FreeIndex validation before entry dereference")
require_order("${free_index}" "if (!name)" "allocEntry->name = nullptr;"
    "FreeIndex entry validation before mutation")

require_text(
    "${end_in_prim}"
    "const PhysicalMemoryAllocation &allocEntry = prim->allocList[prim->allocListCount - 1];"
    "typed end-allocation tail access")
require_text(
    "${free_index}"
    "allocEntry = &prim->allocList[prim->allocListCount - 1];"
    "typed tail-hole walk")
require_text(
    "${free_index}"
    "\"freeing '%s' caused a memory hole\\n\","
    "fixed-format legacy middle-hole report")
string(CONCAT fixed_middle_hole_call
    "\"freeing '%s' caused a memory hole\\n\",\n"
    "                name);")
require_text(
    "${free_index}"
    "${fixed_middle_hole_call}"
    "fixed middle-hole format with its string argument")

reject_text(
    "${physicalmemory_source}"
    "*(&prim->pos + 2 * prim->allocListCount)"
    "raw x86 end-allocation indexing")
reject_text(
    "${physicalmemory_source}"
    "&prim->allocListCount + 2 * prim->allocListCount"
    "raw x86 tail-hole indexing")
reject_text(
    "${physicalmemory_source}"
    "(void)(prim->pos -"
    "discarded disabled-tracking read")
reject_text(
    "${physicalmemory_source}"
    "__int64 v2"
    "dead allocation-size local")
reject_text(
    "${physicalmemory_source}"
    "__int64 v4"
    "dead tail-size local")
reject_text(
    "${free_index}"
    "va("
    "double-formatted middle-hole diagnostic")
reject_text(
    "${free_index}"
    "const char *v3"
    "dead dynamic-format local")

# The source contract must travel with its executable fixture and every CI
# registration so later build-list edits cannot silently drop this boundary.
foreach(marker IN ITEMS
    "TestNativeLayout();"
    "TestWrapperAllocationTypeFailureAtomicity();"
    "TestBeginFailureAtomicity();"
    "TestEndFailureAtomicity();"
    "TestFreeInPrimFailureAtomicity();"
    "TestFreeIndexFailureAtomicity();"
    "TestLowPrimTailHoleCollapse();"
    "TestHighPrimRetainsInitialAllocation();"
    "low-%n-%s-middle")
    require_text(
        "${physicalmemory_fixture}" "${marker}"
        "legacy PMem runtime regression marker")
endforeach()

foreach(marker IN ITEMS
    "add_executable(kisakcod-physicalmemory-legacy-tests"
    "physicalmemory_legacy_tests.cpp"
    "NAME universal-physicalmemory-legacy-layout-and-indexing"
    "NAME universal-physicalmemory-legacy-source-invariants"
    "physicalmemory_legacy_source_test.cmake")
    require_text(
        "${test_manifest}" "${marker}"
        "legacy PMem CMake registration")
endforeach()

foreach(marker IN ITEMS
    "kisakcod-physicalmemory-legacy-tests"
    "universal-physicalmemory-legacy-(layout-and-indexing|source-invariants)"
    "universal-physicalmemory-checked-(scopes|api-sealed|source-invariants)")
    require_text(
        "${ci_workflow}" "${marker}"
        "legacy/checked PMem CI registration")
endforeach()
