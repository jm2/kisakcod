cmake_minimum_required(VERSION 3.16)

# Null/headless media path source contracts (NUL-1..NUL-4 companion). A
# genuine null media path must exist for dedicated servers: no sound driver
# init, no client/media sources, no proprietary Bink/MSS dependencies in the
# headless target. The KISAK_DEDI_HEADLESS guard is the boundary this gate
# pins.

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

function(read_normalized RELATIVE_PATH OUT_VARIABLE)
    set(_path "${SOURCE_ROOT}/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing null media gate source: ${_path}")
    endif()
    file(READ "${_path}" _source)
    string(REGEX REPLACE "[ \t\r\n]+" " " _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(require_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing null media invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden null media regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_count SOURCE_VARIABLE NEEDLE EXPECTED_COUNT DESCRIPTION)
    set(_remaining "${${SOURCE_VARIABLE}}")
    string(LENGTH "${NEEDLE}" _needle_length)
    if(_needle_length EQUAL 0)
        message(FATAL_ERROR "Empty null media count needle (${DESCRIPTION})")
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
            "Unexpected null media invariant count (${DESCRIPTION}): "
            "expected ${EXPECTED_COUNT}, found ${_count}")
    endif()
endfunction()

read_normalized("src/qcommon/common.cpp" _common)
read_normalized("scripts/dedi/dedi_sources.cmake" _dedi)

# --- Headless sound init guards (common.cpp) ---------------------------------
require_contains(_common
    "static void __cdecl Com_InitSound()"
    "Com_InitSound is the single sound init entry")

function(extract_slice SOURCE_VARIABLE START_MARKER END_MARKER OUT_VARIABLE DESCRIPTION)
    set(_source "${${SOURCE_VARIABLE}}")
    string(FIND "${_source}" "${START_MARKER}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR "Missing start of null media slice (${DESCRIPTION}): '${START_MARKER}'")
    endif()
    string(SUBSTRING "${_source}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${END_MARKER}" _relative_end)
    if(_relative_end LESS_EQUAL 0)
        message(FATAL_ERROR "Missing ordered end of null media slice (${DESCRIPTION}): '${END_MARKER}'")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_relative_end} _slice)
    set(${OUT_VARIABLE} "${_slice}" PARENT_SCOPE)
endfunction()

extract_slice(_common
    "static void __cdecl Com_InitSound()"
    "static void __cdecl Com_ShutdownSoundChannels()"
    _init_slice
    "Com_InitSound")
require_count(_init_slice "#ifndef KISAK_DEDI_HEADLESS" 1
    "Com_InitSound is wrapped in the headless guard")
require_contains(_init_slice "SND_InitDriver();"
    "client sound driver init exists for the full client")
require_contains(_init_slice "SND_Init();"
    "client sound init exists for the full client")

extract_slice(_common
    "static void __cdecl Com_ShutdownSoundChannels()"
    "static void __cdecl Com_UnregisterEffects()"
    _shutdown_slice
    "Com_ShutdownSoundChannels")
require_count(_shutdown_slice "#ifndef KISAK_DEDI_HEADLESS" 1
    "Com_ShutdownSoundChannels is wrapped in the headless guard")
require_contains(_shutdown_slice "SND_ShutdownChannels();"
    "channel shutdown exists for the full client")

# --- Headless source list excludes media (dedi_sources.cmake) ----------------
require_contains(_dedi
    "if (_rel MATCHES \"^(client|client_mp|cgame|gfx_d3d|sound|ui|ui_mp|EffectsCore|aim_assist|groupvoice|devgui|DynEntity)/\")"
    "headless target rejects client and media sources including groupvoice")
require_contains(_dedi
    "if (_rel MATCHES \"^\\\\.\\\\./deps/(binklib|msslib)/\")"
    "headless target rejects proprietary Bink and MSS dependencies")
require_contains(_dedi
    "KISAK_DEDI_HEADLESS source list contains client/media source"
    "the media exclusion failure message stays actionable")
require_contains(_dedi
    "KISAK_DEDI_HEADLESS source list contains proprietary media dependency"
    "the proprietary dependency failure message stays actionable")

# Contract mutation self-verification: each mutation below is a plausible
# regression and must be rejected by the checks above.
if(DEFINED CONTRACT_MUTATION AND NOT CONTRACT_MUTATION STREQUAL "")
    if(CONTRACT_MUTATION STREQUAL "init_unguarded")
        string(REGEX REPLACE
            "#ifndef KISAK_DEDI_HEADLESS SND_InitDriver\\(\\);"
            "SND_InitDriver();"
            _common "${_common}")
    elseif(CONTRACT_MUTATION STREQUAL "groupvoice_allowed_in_dedi")
        string(REPLACE
            "groupvoice|devgui" "devgui"
            _dedi "${_dedi}")
    elseif(CONTRACT_MUTATION STREQUAL "bink_allowed_in_dedi")
        string(REPLACE
            "deps/(binklib|msslib)/" "deps/(msslib)/"
            _dedi "${_dedi}")
    elseif(CONTRACT_MUTATION STREQUAL "sound_allowed_in_dedi")
        string(REPLACE
            "gfx_d3d|sound|ui" "gfx_d3d|ui"
            _dedi "${_dedi}")
    else()
        message(FATAL_ERROR
            "Unknown null media contract mutation: '${CONTRACT_MUTATION}'")
    endif()
endif()

extract_slice(_common
    "static void __cdecl Com_InitSound()"
    "static void __cdecl Com_ShutdownSoundChannels()"
    _init_slice_mut
    "Com_InitSound (mutation check)")
require_count(_init_slice_mut "#ifndef KISAK_DEDI_HEADLESS" 1
    "Com_InitSound guard survives mutations")
require_contains(_dedi
    "if (_rel MATCHES \"^(client|client_mp|cgame|gfx_d3d|sound|ui|ui_mp|EffectsCore|aim_assist|groupvoice|devgui|DynEntity)/\")"
    "media source exclusion survives mutations")
require_contains(_dedi
    "if (_rel MATCHES \"^\\\\.\\\\./deps/(binklib|msslib)/\")"
    "proprietary dependency exclusion survives mutations")
