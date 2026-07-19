cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

function(read_normalized RELATIVE_PATH OUT_VARIABLE)
    set(_path "${SOURCE_ROOT}/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing HUD sort source: ${_path}")
    endif()
    file(READ "${_path}" _source)
    string(REGEX REPLACE "[ \t\r\n]+" " " _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(require_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing HUD sort invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden HUD sort regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

read_normalized("src/cgame/cg_hudelem_sort.h" _helper)
foreach(_needle IN ITEMS
    "std::extent_v<Current> + std::extent_v<Archival>"
    "return std::extent_v<Elements>;"
    "if (!elems || elemCapacity < profileCapacity) return 0;"
    "while (activeCount < sourceCapacity && source[activeCount].type)"
    "if (activeCount > elemCapacity - elemCount) return false;"
    "std::sort(elems, elems + elemCount"
    "return kisak::sort::FloatLess(left->sort, right->sort);")
    require_contains(_helper "${_needle}" "typed, bounded production helper")
endforeach()
forbid_contains(_helper "qsort" "no untyped pointer sorting")

read_normalized("src/bgame/bg_hudelem.h" _layout)
foreach(_needle IN ITEMS
    "RUNTIME_SIZE(hudelem_color_t, 0x4, 0x4);"
    "RUNTIME_SIZE(hudelem_s, 0xA0, 0xA0);"
    "RUNTIME_SIZE(hudelem_s, 0xAC, 0xAC);"
    "RUNTIME_SIZE(playerState_s_hud, 0x26C0, 0x26C0);"
    "RUNTIME_SIZE(playerState_s_hud, 0xAC00, 0xAC00);")
    require_contains(_layout "${_needle}" "HUD ABI remains fixed on 32/64-bit")
endforeach()

read_normalized("src/bgame/bg_local.h" _bg_local)
require_contains(
    _bg_local [[#include "bg_hudelem.h"]]
    "the engine consumes the isolated production layout")
forbid_contains(
    _bg_local "struct hudelem_s"
    "the HUD ABI has one production definition")

read_normalized("src/cgame/cg_hudelem.cpp" _production)
foreach(_needle IN ITEMS
    "kisak::cgame::ProfileHudElemCapacity<playerState_s_hud>()"
    "hudelem_s *elems[hudElemCapacity];"
    "GetSortedHudElems(localClientNum, elems, hudElemCapacity);"
    "kisak::cgame::CollectActiveHudElems(elems, elemCapacity, ps->hud);"
    "kisak::cgame::SortHudElems(elems, elemCount);"
    "bcassert2(elemCount, ARRAY_COUNT(elems));")
    require_contains(_production "${_needle}" "profile-sized production collection")
endforeach()
foreach(_legacy IN ITEMS
    "qsort(elems"
    "compare_hudelems"
    "CopyInUseHudElems"
    "hudelem_s *elems[62]"
    "hudelem_s *elems[264]"
    "hudelem_s *elems[1025]")
    forbid_contains(_production "${_legacy}" "no fixed-width or undersized HUD path")
endforeach()

foreach(_ui_path IN ITEMS
    "src/ui/ui_expressions_logicfunctions.cpp"
    "src/ui/ui_shared.h")
    read_normalized("${_ui_path}" _ui_source)
    forbid_contains(
        _ui_source "compare_hudelems"
        "the retired fixed-32-bit HUD comparator cannot remain")
endforeach()

read_normalized("scripts/common_files.cmake" _manifest)
require_contains(
    _manifest [["${SRC_DIR}/bgame/bg_hudelem.h"]]
    "the isolated HUD ABI is in the engine manifest")
require_contains(
    _manifest [["${SRC_DIR}/cgame/cg_hudelem_sort.h"]]
    "the production helper is in the engine manifest")

read_normalized("tests/CMakeLists.txt" _tests)
require_contains(
    _tests "foreach(_hudelem_variant IN ITEMS MP SP)"
    "both game profiles are selected")
require_contains(
    _tests [["KISAK_${_hudelem_variant}"]]
    "each profile compiles the real HUD layout")
require_contains(
    _tests [[hudelem-sort-${_hudelem_variant_lower}-contracts]]
    "both profile tests are registered")

read_normalized(".github/workflows/ci.yml" _ci)
foreach(_target IN ITEMS
    "kisakcod-hudelem-sort-mp-tests"
    "kisakcod-hudelem-sort-sp-tests")
    require_contains(_ci "${_target}" "Win32 explicitly builds ${_target}")
endforeach()
require_contains(
    _ci "hudelem-sort-((mp|sp)-contracts|source-invariants)"
    "Win32 runs profile and source contracts")
