cmake_minimum_required(VERSION 3.16)

# Cinematic playback source contracts (CIN-1..CIN-5 companion). The retail
# Bink integration in r_cinematic.cpp / deps/binklib stays the reference for
# decode scheduling, error handling, Y/Cb/Cr(/A) texture split and volume
# wiring. Any Bink replacement work (acceptance criterion 5) must keep these
# statements true or consciously renegotiate them through the compatibility
# process with #122 reference-build evidence.

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

function(read_normalized RELATIVE_PATH OUT_VARIABLE)
    set(_path "${SOURCE_ROOT}/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing cinematic gate source: ${_path}")
    endif()
    file(READ "${_path}" _source)
    string(REGEX REPLACE "[ \t\r\n]+" " " _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(require_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing cinematic gate invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden cinematic gate regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_count SOURCE_VARIABLE NEEDLE EXPECTED_COUNT DESCRIPTION)
    set(_remaining "${${SOURCE_VARIABLE}}")
    string(LENGTH "${NEEDLE}" _needle_length)
    if(_needle_length EQUAL 0)
        message(FATAL_ERROR "Empty cinematic gate count needle (${DESCRIPTION})")
    endif()
    set(_count 0)
    while(TRUE)
        string(FIND "${_remaining}" "${NEEDLE}" _position)
        if(_position EQUAL -1)
            break()
        endif()
        math(EXPR _count "${_count} + 1")
        math(EXPR _next "${_position} + ${_needle_length}")
        string(SUBSTRING "${_remaining}" ${_next} -1 _remaining)
    endwhile()
    if(NOT _count EQUAL EXPECTED_COUNT)
        message(FATAL_ERROR
            "Unexpected cinematic gate invariant count (${DESCRIPTION}): "
            "expected ${EXPECTED_COUNT}, found ${_count}")
    endif()
endfunction()

read_normalized("src/gfx_d3d/r_cinematic.cpp" _cinematic)
read_normalized("deps/binklib/binktextures.cpp" _binktextures)

# --- Decode scheduling and error handling (r_cinematic.cpp) ------------------
require_contains(_cinematic
    "BinkControlBackgroundIO(cinematicGlob.bink, 1);"
    "retail background IO control stays on")
require_contains(_cinematic
    "binkError = (const char *)BinkGetError();"
    "Bink error state is polled through BinkGetError")
require_contains(_cinematic
    "\"!binkError || binkError[0] == '\\\\0'\""
    "a non-empty Bink error string is a fatal assert, never swallowed")
require_contains(_cinematic
    "BinkSetMixBinVolumes(cinematicGlob.bink, 0, 0, volumes, 8);"
    "retail 8-entry mix-bin volume wiring")

# --- Texture split (deps/binklib/binktextures.cpp) ---------------------------
require_contains(_binktextures
    "static const char StrYCrCbToRGBNoPixelAlpha[] ="
    "the YCrCb-to-RGB shader path stays present")
require_contains(_binktextures
    "static const char StrYCrCbAToRGBA[] ="
    "the YCrCb-alpha-to-RGBA shader path stays present")

# --- Guard rails --------------------------------------------------------------
forbid_contains(_cinematic "Opus" "cinematic audio is not a voice-codec surface")
forbid_contains(_cinematic "license" "cinematic path records no license text (proprietary deps)")

# Contract mutation self-verification: each mutation below is a plausible
# regression and must be rejected by the checks above.
if(DEFINED CONTRACT_MUTATION AND NOT CONTRACT_MUTATION STREQUAL "")
    if(CONTRACT_MUTATION STREQUAL "background_io_off")
        string(REPLACE
            "BinkControlBackgroundIO(cinematicGlob.bink, 1);"
            "BinkControlBackgroundIO(cinematicGlob.bink, 0);"
            _cinematic "${_cinematic}")
    elseif(CONTRACT_MUTATION STREQUAL "error_swallowed")
        string(REPLACE
            "binkError = (const char *)BinkGetError();"
            "binkError = 0;"
            _cinematic "${_cinematic}")
    elseif(CONTRACT_MUTATION STREQUAL "volume_count_changed")
        string(REPLACE
            "BinkSetMixBinVolumes(cinematicGlob.bink, 0, 0, volumes, 8);"
            "BinkSetMixBinVolumes(cinematicGlob.bink, 0, 0, volumes, 4);"
            _cinematic "${_cinematic}")
    elseif(CONTRACT_MUTATION STREQUAL "alpha_shader_dropped")
        string(REPLACE
            "static const char StrYCrCbAToRGBA[] ="
            "static const char StrYCrCbAToRGBA_UNUSED[] ="
            _binktextures "${_binktextures}")
    else()
        message(FATAL_ERROR
            "Unknown cinematic gate contract mutation: '${CONTRACT_MUTATION}'")
    endif()
endif()

require_contains(_cinematic
    "BinkControlBackgroundIO(cinematicGlob.bink, 1);"
    "background IO pin survives mutations")
require_contains(_cinematic
    "binkError = (const char *)BinkGetError();"
    "error poll survives mutations")
require_contains(_cinematic
    "BinkSetMixBinVolumes(cinematicGlob.bink, 0, 0, volumes, 8);"
    "mix-bin volume pin survives mutations")
require_contains(_binktextures
    "static const char StrYCrCbAToRGBA[] ="
    "alpha shader pin survives mutations")
