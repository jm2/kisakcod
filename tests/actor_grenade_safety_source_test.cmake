cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

function(read_normalized PATH OUT_VARIABLE DESCRIPTION)
    if(NOT EXISTS "${PATH}")
        message(FATAL_ERROR
            "Missing grenade-safety source (${DESCRIPTION}): ${PATH}")
    endif()
    file(READ "${PATH}" _source)
    string(REGEX REPLACE "[ \t\r\n]+" " " _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(extract_slice
    SOURCE_VARIABLE START_MARKER END_MARKER OUT_VARIABLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${START_MARKER}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR
            "Missing start of grenade-safety slice (${DESCRIPTION}): "
            "'${START_MARKER}'")
    endif()
    string(SUBSTRING "${${SOURCE_VARIABLE}}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${END_MARKER}" _end)
    if(_end LESS_EQUAL 0)
        message(FATAL_ERROR
            "Missing ordered end of grenade-safety slice (${DESCRIPTION}): "
            "'${END_MARKER}'")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_end} _slice)
    set(${OUT_VARIABLE} "${_slice}" PARENT_SCOPE)
endfunction()

function(require_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing grenade-safety invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden grenade-safety regression (${DESCRIPTION}): '${NEEDLE}'")
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
            "Unexpected grenade-safety invariant count (${DESCRIPTION}): "
            "expected ${EXPECTED}, found ${_count}")
    endif()
endfunction()

read_normalized(
    "${SOURCE_ROOT}/src/game/actor_grenade.cpp"
    _actor "production grenade logic")
read_normalized(
    "${SOURCE_ROOT}/src/game/actor_grenade_safety.h"
    _helper "portable safety predicate")
read_normalized(
    "${SOURCE_ROOT}/tests/CMakeLists.txt"
    _tests "portable test registration")
read_normalized(
    "${SOURCE_ROOT}/.github/workflows/ci.yml"
    _ci "measured Windows x86 workflow")

# Mutation mode is used only by the self-checks at the end of this file.
if(DEFINED CONTRACT_MUTATION AND NOT CONTRACT_MUTATION STREQUAL "")
    if(CONTRACT_MUTATION STREQUAL "linear_threshold")
        string(REPLACE
            "return distanceSquared <= safetyRadius * safetyRadius;"
            "return distanceSquared <= safetyRadius;"
            _helper "${_helper}")
    elseif(CONTRACT_MUTATION STREQUAL "missing_negative_guard")
        string(REPLACE
            "if (explosionRadius < 0) return false;"
            ""
            _helper "${_helper}")
    elseif(CONTRACT_MUTATION STREQUAL "production_bypass")
        string(REPLACE
            "actor_grenade_safety::IsTargetWithinSafetyRadius( vTargetPos, sentOrigin, weapDef->iExplosionRadius)"
            "false"
            _actor "${_actor}")
    else()
        message(FATAL_ERROR
            "Unknown grenade-safety mutation: ${CONTRACT_MUTATION}")
    endif()
endif()

require_contains(
    _actor "#include \"actor_grenade_safety.h\""
    "production includes the portable predicate")
extract_slice(
    _actor
    "int Actor_Grenade_IsSafeTarget("
    "void __cdecl Actor_PredictGrenadeLandPos("
    _safe_target "Actor_Grenade_IsSafeTarget")
set(_production_call
    "actor_grenade_safety::IsTargetWithinSafetyRadius( vTargetPos, sentOrigin, weapDef->iExplosionRadius)")
require_contains(
    _safe_target "${_production_call}"
    "the live toss gate uses the portable squared-radius predicate")
require_count(
    _safe_target "actor_grenade_safety::IsTargetWithinSafetyRadius(" 1
    "the live toss gate evaluates the predicate exactly once per sentient")
foreach(_legacy IN ITEMS
    "explosionCutoff"
    "vTargetPos[0] - sentOrigin[0]"
    "vTargetPos[1] - sentOrigin[1]"
    "vTargetPos[2] - sentOrigin[2]")
    forbid_contains(
        _safe_target "${_legacy}"
        "production cannot bypass the shared dimensional contract")
endforeach()

foreach(_required IN ITEMS
    "if (explosionRadius < 0) return false;"
    "const float deltaY = targetPosition[1] - sentientPosition[1];"
    "const float deltaZ = targetPosition[2] - sentientPosition[2];"
    "const float deltaX = targetPosition[0] - sentientPosition[0];"
    "const float distanceSquared = deltaY * deltaY + (deltaZ * deltaZ + deltaX * deltaX);"
    "const float safetyRadius = static_cast<float>(explosionRadius) * 1.1f;"
    "return distanceSquared <= safetyRadius * safetyRadius;")
    require_contains(
        _helper "${_required}"
        "the portable predicate preserves the exact three-dimensional contract")
endforeach()

# The separate 100.0 comparison is an intentional squared ten-unit arrival
# tolerance, not the explosion-radius dimensional defect fixed by this batch.
extract_slice(
    _actor
    "bool __cdecl Actor_Grenade_ShouldIgnore("
    "int __cdecl Actor_IsAwareOfGrenade("
    _should_ignore "Actor_Grenade_ShouldIgnore")
require_contains(
    _should_ignore ")) > 100.0;"
    "the intentional squared ten-unit ignore tolerance remains unchanged")
forbid_contains(
    _should_ignore "actor_grenade_safety::"
    "the explosion-radius helper must not replace the arrival tolerance")

foreach(_required IN ITEMS
    "add_executable(kisakcod-actor-grenade-safety-tests actor_grenade_safety_tests.cpp )"
    "NAME actor-grenade-safe-target-radius-contracts"
    "NAME actor-grenade-safe-target-source-invariants")
    require_contains(
        _tests "${_required}"
        "portable grenade-safety test registration")
endforeach()
require_contains(
    _ci "kisakcod-actor-grenade-safety-tests"
    "measured Windows x86 explicitly builds the runtime predicate test")
require_contains(
    _ci
    "actor-grenade-safe-target-(radius-contracts|source-invariants)"
    "measured Windows x86 selects both grenade-safety contracts")

if(NOT DEFINED CONTRACT_MUTATION OR CONTRACT_MUTATION STREQUAL "")
    foreach(_mutation IN ITEMS
        linear_threshold
        missing_negative_guard
        production_bypass)
        execute_process(
            COMMAND "${CMAKE_COMMAND}"
                "-DSOURCE_ROOT=${SOURCE_ROOT}"
                "-DCONTRACT_MUTATION=${_mutation}"
                -P "${CMAKE_CURRENT_LIST_FILE}"
            RESULT_VARIABLE _mutation_result
            OUTPUT_VARIABLE _mutation_stdout
            ERROR_VARIABLE _mutation_stderr)
        if(_mutation_result EQUAL 0)
            message(STATUS "Mutation stdout: ${_mutation_stdout}")
            message(STATUS "Mutation stderr: ${_mutation_stderr}")
            message(FATAL_ERROR
                "Grenade-safety contract accepted mutation: ${_mutation}")
        endif()
    endforeach()
endif()

message(STATUS "Actor grenade safe-target source contract passed")
