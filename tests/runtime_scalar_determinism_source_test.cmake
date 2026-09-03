cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

# M10 architecture-neutral determinism source contract.
#
# Pins the producer-side float-to-int material-time conversions to the
# runtime::determinism layer: a bare cast in the vehicle producers silently
# changes meaning between x86 (cvttss2si -> INT32_MIN) and AArch64 (FCVTZS ->
# saturation) for out-of-range script inputs, which is the PR #42 deferred
# forcedMaterialSpeed range risk.

function(read_normalized PATH OUT_VARIABLE DESCRIPTION)
    if(NOT EXISTS "${PATH}")
        message(FATAL_ERROR
            "Missing scalar-determinism source (${DESCRIPTION}): ${PATH}")
    endif()
    file(READ "${PATH}" _source)
    string(REGEX REPLACE "[ \t\r\n]+" " " _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(require_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing scalar-determinism invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden scalar-determinism regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

read_normalized(
    "${SOURCE_ROOT}/src/runtime/scalar_determinism.h"
    _layer "architecture-neutral determinism layer")
read_normalized(
    "${SOURCE_ROOT}/src/game/g_scr_vehicle.cpp"
    _sp_producer "SP vehicle material-time producer")
read_normalized(
    "${SOURCE_ROOT}/src/game_mp/g_vehicles_mp.cpp"
    _mp_producer "MP vehicle material-time producer")
read_normalized(
    "${SOURCE_ROOT}/tests/CMakeLists.txt"
    _tests "portable test registration")

# The layer owns the defined conversion semantics.
foreach(_required IN ITEMS
    "constexpr std::int32_t FloatToIntSaturating(const double value) noexcept"
    "constexpr bool TotalOrderLess(const double lhs, const double rhs) noexcept"
    "constexpr std::uint16_t ReadLe16(const unsigned char *bytes) noexcept"
    "constexpr std::int32_t SignExtend8(const std::uint8_t value) noexcept")
    require_contains(_layer "${_required}"
        "the determinism layer publishes the primitive")
endforeach()

# Both producers route through the layer and declare it.
foreach(_producer IN ITEMS _sp_producer _mp_producer)
    require_contains(${_producer} "#include <runtime/scalar_determinism.h>"
        "producer includes the determinism layer")
    require_contains(${_producer} "runtime::determinism::FloatToIntSaturating("
        "producer converts through the defined primitive")
endforeach()

# The exact deferred conversions must not return as bare casts.
forbid_contains(
    _sp_producer
    "delta = (int)((scale * 0.050000001f) * 1000.0f);"
    "SP material-time delta must not use the undefined bare cast")
forbid_contains(
    _mp_producer
    "(int)(deltaTime * 1000.0)"
    "MP material-time delta must not use the undefined bare cast")

message(STATUS "Runtime scalar-determinism source contract passed")
