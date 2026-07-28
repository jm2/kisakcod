cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

function(read_normalized PATH OUT_VARIABLE DESCRIPTION)
    if(NOT EXISTS "${PATH}")
        message(FATAL_ERROR
            "Missing UI-safety source (${DESCRIPTION}): ${PATH}")
    endif()
    file(READ "${PATH}" _source)
    string(REGEX REPLACE "[ \t\r\n]+" " " _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(require_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing UI-safety invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden UI-safety regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_count SOURCE_VARIABLE NEEDLE EXPECTED DESCRIPTION)
    set(_remaining "${${SOURCE_VARIABLE}}")
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
            "Unexpected UI-safety invariant count (${DESCRIPTION}): "
            "expected ${EXPECTED}, found ${_count}")
    endif()
endfunction()

read_normalized(
    "${SOURCE_ROOT}/src/ui/ui_safety.h"
    _helper "portable UI-safety helper")
read_normalized(
    "${SOURCE_ROOT}/src/ui/ui.h"
    _ui_header "single-player UI layout")
read_normalized(
    "${SOURCE_ROOT}/src/ui/ui_main.cpp"
    _ui_main "single-player UI implementation")
read_normalized(
    "${SOURCE_ROOT}/tests/ui_safety_tests.cpp"
    _runtime "portable UI-safety runtime test")
read_normalized(
    "${SOURCE_ROOT}/tests/CMakeLists.txt"
    _tests_cmake "portable test graph")
read_normalized(
    "${SOURCE_ROOT}/scripts/common_files.cmake"
    _common_manifest "common source manifest")
read_normalized(
    "${SOURCE_ROOT}/.github/workflows/ci.yml"
    _ci "measured Windows x86 workflow")

foreach(_required IN ITEMS
    "inline constexpr std::size_t kSavegameCapacity = 256u;"
    "inline constexpr std::size_t kSavegameStorageLayoutCapacity = 512u;"
    "static_cast<std::size_t>(savegameCount) <= kSavegameCapacity"
    "return IsSavegameCountValid(savegameCount) ? savegameCount : 0;"
    "static_cast<std::size_t>(savegameCount) < kSavegameCapacity"
    "constexpr bool TryResolveSavegameSlot("
    "|| !IsSavegameCountValid(savegameCount)"
    "|| displayIndex < 0"
    "|| displayIndex >= savegameCount"
    "const int candidate = displaySavegames[displayIndex];"
    "|| candidate >= savegameCount"
    ">= kSavegameStorageLayoutCapacity")
    require_contains(
        _helper "${_required}"
        "the helper enforces the single fail-closed savegame capacity")
endforeach()

require_contains(
    _ui_header
    "int displaySavegames[ui_safety::kSavegameCapacity];"
    "the frozen display map uses the 256-entry safety capacity")
require_contains(
    _ui_header
    "SavegameInfo savegameList[ui_safety::kSavegameStorageLayoutCapacity];"
    "the 512-entry backing-array layout remains unchanged")

foreach(_required IN ITEMS
    "return ui_safety::GetFailClosedSavegameCount(uiInfo.savegameCount);"
    "return ui_safety::TryResolveSavegameSlot( uiInfo.savegameStatus.displaySavegames, uiInfo.savegameCount, displayIndex, slotIndex);"
    "const int savegameCount = UI_GetFailClosedSavegameCount();"
    "if (!UI_TryResolveSavegameSlotIndex(displayIndex, &slotIndex)) return -1;"
    "if (UI_TryResolveSavegameSlotIndex(displayIndex, &slotIndex))"
    "if (!UI_TryResolveSavegameSlotIndex(index, &slotIdx)) return \"\";"
    "if (!UI_TryResolveSavegameSlotIndex(index, &slotIdx)) return;"
    "if (UI_AreSavegameMappingsValid(savegameCount))"
    "sizeof(uiInfo.savegameStatus.displaySavegames[0])"
    "&& ui_safety::CanAppendSavegame(uiInfo.savegameCount);"
    "if (UI_GetFailClosedSavegameCount() > 0)"
    "if (!UI_TryResolveSavegameSlotIndex(displayIdx, &slotIdx)) return;"
    "return UI_GetFailClosedSavegameCount();")
    require_contains(
        _ui_main "${_required}"
        "live savegame paths share the bounded count and resolver")
endforeach()

require_count(
    _ui_main "uiInfo.savegameStatus.displaySavegames[" 2
    "only bounded load initialization and qsort element sizing index the map")
foreach(_forbidden IN ITEMS
    "uiInfo.savegameCount < 512"
    "uiInfo.savegameStatus.displaySavegames[v1]"
    "uiInfo.savegameStatus.displaySavegames[index]"
    "uiInfo.savegameStatus.displaySavegames[displayIdx]"
    "v5 << 6"
    "(char *)&uiInfo.savegameList[0].imageName")
    forbid_contains(
        _ui_main "${_forbidden}"
        "legacy unbounded savegame indexing is sealed out")
endforeach()

foreach(_boundary IN ITEMS "255" "256" "257" "512")
    require_contains(
        _runtime "${_boundary}"
        "runtime coverage includes the requested savegame-count boundary")
endforeach()
foreach(_required IN ITEMS
    "displaySavegames[4] = -1;"
    "displaySavegames[4] = 256;"
    "displaySavegames[4] = 255;"
    "nullptr, 256, 0, &slotIndex"
    "displaySavegames.data(), 256, 0, nullptr")
    require_contains(
        _runtime "${_required}"
        "runtime coverage rejects invalid savegame mappings")
endforeach()

foreach(_required IN ITEMS
    "add_executable(kisakcod-ui-safety-tests ui_safety_tests.cpp )"
    "NAME ui-safety-runtime-contracts"
    "NAME ui-safety-source-invariants"
    "ui_safety_source_test.cmake")
    require_contains(
        _tests_cmake "${_required}"
        "portable CMake registers both UI-safety gates")
endforeach()
require_count(
    _common_manifest "\"\${SRC_DIR}/ui/ui_safety.h\"" 1
    "the common source manifest owns the helper exactly once")
require_contains(
    _ci "kisakcod-ui-safety-tests"
    "Windows x86 explicitly builds the UI-safety runtime test")
require_contains(
    _ci "ui-safety-(runtime-contracts|source-invariants)"
    "Windows x86 explicitly runs both UI-safety gates")
