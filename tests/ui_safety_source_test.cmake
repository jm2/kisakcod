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

function(require_ordered SOURCE_VARIABLE FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${FIRST}" _first)
    string(FIND "${${SOURCE_VARIABLE}}" "${SECOND}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered UI-safety invariant (${DESCRIPTION})")
    endif()
endfunction()

function(require_count SOURCE_VARIABLE NEEDLE EXPECTED DESCRIPTION)
    set(_remaining "${${SOURCE_VARIABLE}}")
    set(_count 0)
    string(LENGTH "${NEEDLE}" _length)
    if(_length EQUAL 0)
        message(FATAL_ERROR
            "Empty UI-safety invariant count needle (${DESCRIPTION})")
    endif()
    while(TRUE)
        string(FIND "${_remaining}" "${NEEDLE}" _position)
        if(_position EQUAL -1)
            break()
        endif()
        math(EXPR _count "${_count} + 1")
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
    "${SOURCE_ROOT}/src/cgame/cg_newdraw.cpp"
    _cg_sp "single-player invalid-command hint")
read_normalized(
    "${SOURCE_ROOT}/src/cgame_mp/cg_newDraw_mp.cpp"
    _cg_mp "multiplayer invalid-command hint")
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

if(DEFINED CONTRACT_MUTATION AND NOT CONTRACT_MUTATION STREQUAL "")
    if(CONTRACT_MUTATION STREQUAL "slot_read_before_guard")
        string(REPLACE
            "*slotIndex = -1;"
            "*slotIndex = -1; const int candidate = displaySavegames[displayIndex];"
            _helper "${_helper}")
    elseif(CONTRACT_MUTATION STREQUAL "elapsed_rewind_unclamped")
        string(REPLACE
            "? elapsed : 0u;"
            "? elapsed : elapsed;"
            _helper "${_helper}")
    elseif(CONTRACT_MUTATION STREQUAL "sp_raw_expiry_addition")
        string(REPLACE
            "if (ui_safety::InvalidCmdHintExpired( cgameGlob->time, cgameGlob->invalidCmdHintTime, cg_invalidCmdHintDuration->current.integer))"
            "if (cg_invalidCmdHintDuration->current.integer + cgameGlob->invalidCmdHintTime < cgameGlob->time)"
            _cg_sp "${_cg_sp}")
    elseif(CONTRACT_MUTATION STREQUAL "mp_raw_elapsed_subtraction")
        string(REPLACE
            "ui_safety::InvalidCmdHintBlinkAlpha( cgameGlob->time, cgameGlob->invalidCmdHintTime, blinkInterval)"
            "ui_safety::InvalidCmdHintBlinkAlpha( cgameGlob->time - cgameGlob->invalidCmdHintTime, blinkInterval)"
            _cg_mp "${_cg_mp}")
    else()
        message(FATAL_ERROR
            "Unknown UI-safety contract mutation: ${CONTRACT_MUTATION}")
    endif()
endif()

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
    "|| candidate >= savegameCount")
    require_contains(
        _helper "${_required}"
        "the helper enforces the single fail-closed savegame capacity")
endforeach()
require_ordered(
    _helper
    "|| displayIndex >= savegameCount) { return false; }"
    "const int candidate = displaySavegames[displayIndex];"
    "all display-map bounds guards precede the first indexed read")
forbid_contains(
    _helper
    "static_cast<std::size_t>(candidate) >= kSavegameStorageLayoutCapacity"
    "the live-count guard is the single load-bearing slot bound")

foreach(_required IN ITEMS
    "constexpr std::uint32_t MonotonicElapsedMilliseconds("
    "(std::numeric_limits<int>::max)()) ? elapsed : 0u;"
    "constexpr bool InvalidCmdHintExpired("
    "return MonotonicElapsedMilliseconds(currentTime, startTime) > static_cast<std::uint32_t>(duration);"
    "constexpr float InvalidCmdHintBlinkAlpha("
    "if (blinkInterval <= 0) return 0.0f;"
    "MonotonicElapsedMilliseconds(currentTime, startTime) % static_cast<std::uint32_t>(blinkInterval);"
    "return static_cast<float>(phase) / static_cast<float>(blinkInterval);")
    require_contains(
        _helper "${_required}"
        "the blink helper guards modulo and retains fractional alpha")
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

foreach(_cg_source IN ITEMS _cg_sp _cg_mp)
    foreach(_required IN ITEMS
        "#include <ui/ui_safety.h>"
        "if (ui_safety::InvalidCmdHintExpired( cgameGlob->time, cgameGlob->invalidCmdHintTime, cg_invalidCmdHintDuration->current.integer))"
        "if (blinkInterval <= 0) {"
        "color[3] = 0.0f;"
        "color[3] = ui_safety::InvalidCmdHintBlinkAlpha( cgameGlob->time, cgameGlob->invalidCmdHintTime, blinkInterval);")
        require_contains(
            ${_cg_source} "${_required}"
            "SP and MP use the guarded fractional blink contract")
    endforeach()
    foreach(_forbidden IN ITEMS
        "% blinkInterval"
        "cgameGlob->time - cgameGlob->invalidCmdHintTime"
        "cg_invalidCmdHintDuration->current.integer + cgameGlob->invalidCmdHintTime")
        forbid_contains(
            ${_cg_source} "${_forbidden}"
            "SP and MP cannot perform raw signed timer arithmetic")
    endforeach()
endforeach()

require_contains(
    _ui_main "constexpr int kMainMenuSoundFadeMilliseconds = 1000;"
    "the upstream one-second main-menu fade has a named unit-bearing value")
require_contains(
    _ui_main
    "SND_FadeAllSounds(1.0, kMainMenuSoundFadeMilliseconds);"
    "opening the main menu uses the intended one-second sound fade")
forbid_contains(
    _ui_main "SND_FadeAllSounds(1.0, (int)String);"
    "the error-message pointer cannot become a fade duration")

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

foreach(_boundary IN ITEMS 0 255 256 257 511 512 513)
    require_contains(
        _runtime "IsSavegameCountValid(${_boundary})"
        "runtime coverage validates the requested savegame-count boundary")
    require_contains(
        _runtime "GetFailClosedSavegameCount(${_boundary})"
        "runtime coverage fail-closes the requested savegame-count boundary")
endforeach()
foreach(_required IN ITEMS
    "displaySavegames[4] = -1;"
    "displaySavegames[4] = 256;"
    "displaySavegames[4] = 511;"
    "displaySavegames[4] = 255;"
    "displaySavegames.data(), 0, 0, &slotIndex"
    "nullptr, 256, 0, &slotIndex"
    "displaySavegames.data(), 256, 0, nullptr")
    require_contains(
        _runtime "${_required}"
        "runtime coverage rejects invalid savegame mappings")
endforeach()
foreach(_required IN ITEMS
    "InvalidCmdHintBlinkAlpha(125, 0, 0)"
    "InvalidCmdHintBlinkAlpha(125, 0, -1)"
    "InvalidCmdHintBlinkAlpha(125, 0, 500)"
    "InvalidCmdHintBlinkAlpha(499, 0, 500)"
    "InvalidCmdHintBlinkAlpha(500, 0, 500)"
    "InvalidCmdHintBlinkAlpha(750, 0, 500)"
    "MonotonicElapsedMilliseconds(900, 1000)"
    "InvalidCmdHintBlinkAlpha(900, 1000, 500)"
    "InvalidCmdHintExpired(1101, 1000, 100)"
    "InvalidCmdHintExpired(1000, 1000, -1)")
    require_contains(
        _runtime "${_required}"
        "runtime coverage measures guarded fractional blink phases")
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

if(NOT DEFINED CONTRACT_MUTATION OR CONTRACT_MUTATION STREQUAL "")
    foreach(_mutation IN ITEMS
        slot_read_before_guard
        elapsed_rewind_unclamped
        sp_raw_expiry_addition
        mp_raw_elapsed_subtraction)
        execute_process(
            COMMAND "${CMAKE_COMMAND}"
                "-DSOURCE_ROOT=${SOURCE_ROOT}"
                "-DCONTRACT_MUTATION=${_mutation}"
                -P "${CMAKE_CURRENT_LIST_FILE}"
            RESULT_VARIABLE _mutation_result
            OUTPUT_QUIET
            ERROR_QUIET)
        if(_mutation_result EQUAL 0)
            message(FATAL_ERROR
                "UI-safety contract accepted mutation: ${_mutation}")
        endif()
    endforeach()
endif()

message(STATUS "UI-safety source contract passed")
