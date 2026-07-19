cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

function(load_text RELATIVE_PATH OUT_VARIABLE)
    set(_path "${SOURCE_ROOT}/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing upstream reconciliation input: ${_path}")
    endif()
    file(READ "${_path}" _source)
    string(REPLACE "\r\n" "\n" _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(require_contains HAYSTACK_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${HAYSTACK_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing upstream reconciliation invariant (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains HAYSTACK_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${HAYSTACK_VARIABLE}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden upstream reconciliation regression (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(require_count HAYSTACK_VARIABLE NEEDLE EXPECTED DESCRIPTION)
    set(_remaining "${${HAYSTACK_VARIABLE}}")
    set(_count 0)
    string(LENGTH "${NEEDLE}" _needle_length)
    while(TRUE)
        string(FIND "${_remaining}" "${NEEDLE}" _position)
        if(_position EQUAL -1)
            break()
        endif()
        math(EXPR _count "${_count} + 1")
        math(EXPR _next "${_position} + ${_needle_length}")
        string(SUBSTRING "${_remaining}" ${_next} -1 _remaining)
    endwhile()
    if(NOT _count EQUAL EXPECTED)
        message(FATAL_ERROR
            "Unexpected upstream reconciliation count (${DESCRIPTION}): "
            "expected ${EXPECTED}, found ${_count}")
    endif()
endfunction()

load_text("src/gfx_d3d/r_dpvs.cpp" _r_dpvs)
load_text("src/aim_assist/aim_assist.cpp" _aim_assist)
load_text("src/aim_assist/aim_assist.h" _aim_assist_header)
load_text("src/aim_assist/aim_assist_safety.h" _aim_safety)
load_text("src/universal/com_angle.cpp" _com_angle)
load_text("src/universal/com_math.cpp" _com_math)
load_text("scripts/common_files.cmake" _common_files)
load_text("tests/CMakeLists.txt" _tests_cmake)
load_text(".github/workflows/ci.yml" _ci)

if(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "rdpvs_stale_def")
    string(REPLACE
        "iassert( !dynEntDefa->xModel );"
        "iassert( !dynEntDef->xModel );"
        _r_dpvs "${_r_dpvs}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "angle_unsigned")
    string(REPLACE
        "return AngleDelta(a1, a2);"
        "return AngleNormalize360(a1 - a2);"
        _com_angle "${_com_angle}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "angle_duplicate")
    string(APPEND _com_math
        "\nfloat AngleSubtract(float a1, float a2) "
        "{ return AngleDelta(a1, a2); }\n")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "aim_no_release_guard")
    string(REPLACE
        "if (!ordinaryEntity)\n            return 0;"
        "if (false)\n            return 0;"
        _aim_assist "${_aim_assist}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "aim_packed_abi")
    string(REPLACE
        "AimAssist_GetWeaponIndex(localClientNum, ps)"
        "AimAssist_GetWeaponIndex(localClientNum, (const playerState_s *)HIDWORD(localClientNum))"
        _aim_assist "${_aim_assist}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "aim_unbounded_weapon")
    string(REPLACE
        "return aim_assist::safety::BoundedWeaponIndex(weapIndex, weaponCount);"
        "return weapIndex;"
        _aim_assist "${_aim_assist}")
elseif(DEFINED CONTRACT_MUTATION AND NOT CONTRACT_MUTATION STREQUAL "")
    message(FATAL_ERROR "Unknown reconciliation mutation: ${CONTRACT_MUTATION}")
endif()

string(REPLACE "\t" " " _r_dpvs_normalized "${_r_dpvs}")
string(REGEX REPLACE "[ \r\n]+" " " _r_dpvs_normalized
    "${_r_dpvs_normalized}")
require_contains(
    _r_dpvs_normalized
    "dynEntDefa = DynEnt_GetEntityDef(dynEntIndexa, DYNENT_DRAW_BRUSH); iassert( !dynEntDefa->xModel ); iassert( dynEntDefa->brushModel ); bmodel = R_GetBrushModel(dynEntDefa->brushModel);"
    "brush assertions and model lookup use the same fetched definition")

string(REGEX REPLACE "[ \t\r\n]+" " " _aim_normalized "${_aim_assist}")
require_contains(
    _aim_normalized
    "const bool ordinaryEntity = aim_assist::safety::IsOrdinaryEntityNumber( ps->viewlocked_entNum, ENTITYNUM_WORLD); iassert(ordinaryEntity); if (!ordinaryEntity) return 0; weapIndex = CG_GetEntity(localClientNum, ps->viewlocked_entNum)->nextState.weapon;"
    "release guard rejects sentinels and invalid entities before lookup")
require_contains(
    _aim_normalized
    "const uint32_t weaponCount = BG_GetNumWeapons(); bcassert(weapIndex, weaponCount); return aim_assist::safety::BoundedWeaponIndex(weapIndex, weaponCount);"
    "every selected weapon is range-checked in release")
require_count(
    _aim_assist "AimAssist_DrawTargets(localClientNum, ps, red);" 4
    "all four debug overlays pass the real player state")
require_contains(
    _aim_assist
    "void __cdecl AimAssist_DrawTargets(\n    int32_t localClientNum,\n    const playerState_s *ps,"
    "the debug target function exposes the native pointer explicitly")
require_contains(
    _aim_assist_header
    "void __cdecl AimAssist_DrawTargets(\n    int32_t localClientNum,\n    const playerState_s *ps,"
    "the public declaration matches the repaired debug target ABI")
forbid_contains(
    _aim_assist "HIDWORD(localClientNum)"
    "native pointers must not be decoded from a decompiler-packed integer")
require_contains(
    _aim_safety
    "return entityNumber >= 0 && entityNumber < ordinaryEntityEnd;"
    "ordinary-entity validation is signed and excludes both sentinels")
require_contains(
    _aim_safety
    "return weaponIndex < weaponCount ? weaponIndex : 0;"
    "invalid weapons collapse to the no-weapon sentinel")

require_contains(
    _com_angle "float KISAK_CDECL AngleDelta(float a1, float a2)"
    "the signed retail delta implementation is isolated for portable tests")
require_contains(
    _com_angle "return AngleDelta(a1, a2);"
    "AngleSubtract delegates to the canonical signed implementation")
forbid_contains(
    _com_math "AngleDelta(float a1, float a2)"
    "AngleDelta must have one definition")
forbid_contains(
    _com_math "AngleSubtract(float a1, float a2)"
    "AngleSubtract must have one definition")
require_count(
    _common_files "universal/com_angle.cpp" 1
    "the production source manifest contains com_angle exactly once")
require_count(
    _tests_cmake "NAME upstream-reconciliation-angle-math-contracts" 1
    "CTest registers the runtime angle contract")
require_count(
    _tests_cmake "NAME upstream-reconciliation-aim-safety-contracts" 1
    "CTest registers the runtime aim safety contract")
require_count(
    _tests_cmake "NAME upstream-reconciliation-source-invariants" 1
    "CTest registers this source contract")
require_count(
    _ci "kisakcod-upstream-angle-math-tests" 1
    "measured Windows x86 builds the runtime angle contract")
require_count(
    _ci "kisakcod-upstream-aim-safety-tests" 1
    "measured Windows x86 builds the runtime aim safety contract")
require_count(
    _ci "upstream-reconciliation-(aim-safety-contracts|angle-math-contracts|source-invariants)" 1
    "measured Windows x86 runs all reconciliation contracts")

if(NOT DEFINED CONTRACT_MUTATION OR CONTRACT_MUTATION STREQUAL "")
    foreach(_mutation IN ITEMS
        rdpvs_stale_def
        angle_unsigned
        angle_duplicate
        aim_no_release_guard
        aim_packed_abi
        aim_unbounded_weapon)
        execute_process(
            COMMAND "${CMAKE_COMMAND}"
                "-DSOURCE_ROOT=${SOURCE_ROOT}"
                "-DCONTRACT_MUTATION=${_mutation}"
                -P "${CMAKE_CURRENT_LIST_FILE}"
            RESULT_VARIABLE _mutation_result
            OUTPUT_QUIET ERROR_QUIET)
        if(_mutation_result EQUAL 0)
            message(FATAL_ERROR
                "Upstream reconciliation contract accepted mutation: "
                "${_mutation}")
        endif()
    endforeach()
endif()

message(STATUS "Curated upstream reconciliation source contract passed")
