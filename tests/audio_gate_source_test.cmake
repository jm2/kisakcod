cmake_minimum_required(VERSION 3.16)

# Audio capture/playback device integration source contracts (AUD-1..AUD-8
# companion). These pins are the production Speex integration boundaries that
# the portable codec gate (tests/voice_gate_tests.cpp) cannot see because the
# files are Win32-coupled. Issue #122 / docs/NETWORK_COMPATIBILITY.md govern:
# the retail parameter defaults, the VAD/DTX configuration, and the retail
# Decode_Sample return semantics are compatibility surface.
#
# Known unresolved finding, pinned verbatim ON PURPOSE: Decode_Sample returns
# and copies `2 * frame_size` samples after speex_decode fills only frame_size
# (retail day46 behavior). Do NOT "fix" it without a compatibility analysis
# against the #122 reference builds; the gate rejects silent corrections.

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

function(read_normalized RELATIVE_PATH OUT_VARIABLE)
    set(_path "${SOURCE_ROOT}/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing audio gate source: ${_path}")
    endif()
    file(READ "${_path}" _source)
    string(REGEX REPLACE "[ \t\r\n]+" " " _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(require_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing audio gate invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden audio gate regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_count SOURCE_VARIABLE NEEDLE EXPECTED_COUNT DESCRIPTION)
    set(_remaining "${${SOURCE_VARIABLE}}")
    string(LENGTH "${NEEDLE}" _needle_length)
    if(_needle_length EQUAL 0)
        message(FATAL_ERROR "Empty audio gate count needle (${DESCRIPTION})")
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
            "Unexpected audio gate invariant count (${DESCRIPTION}): "
            "expected ${EXPECTED_COUNT}, found ${_count}")
    endif()
endfunction()

read_normalized("src/win32/win_voice.cpp" _winvoice)
read_normalized("src/groupvoice/directsound.h" _header)
read_normalized("src/groupvoice/encode.cpp" _encode)
read_normalized("src/groupvoice/decode.cpp" _decode)
read_normalized("src/server_mp/sv_init_mp.cpp" _svinit)

# --- Retail parameter defaults (directsound.h) -------------------------------
require_contains(_header "inline int32_t g_encoder_samplerate = 0x1F40;"
    "retail voice capture sample rate is 8000 Hz")
require_contains(_header "inline int32_t g_encoder_quality = 1;"
    "retail directsound.h encoder quality default is 1")

# --- Narrowband-only selection (win_voice.cpp) -------------------------------
require_count(_winvoice "g_current_bandwidth_setting = 0;" 1
    "voice init pins the retail narrowband bandwidth enum (wb/uwb never selected)")
require_contains(_winvoice "Encode_Init(g_current_bandwidth_setting);"
    "encoder init receives the retail bandwidth enum")
require_contains(_winvoice "Decode_Init(g_current_bandwidth_setting);"
    "decoder init receives the retail bandwidth enum")

# --- Encoder configuration (encode.cpp) --------------------------------------
require_contains(_encode
    "speex_encoder_ctl(g_encoder, SPEEX_SET_SAMPLING_RATE, &frequency);"
    "Encode_SetOptions applies the configured sample rate")
require_contains(_encode
    "speex_encoder_ctl(g_encoder, SPEEX_SET_VAD, &yes);"
    "production enables the retail voice activity detector")
require_contains(_encode
    "speex_encoder_ctl(g_encoder, SPEEX_SET_DTX, &yes);"
    "production enables retail discontinuous transmission")
require_contains(_encode
    "Encode_SetOptions(g_encoder_samplerate, g_encoder_quality);"
    "Encode_Init applies the directsound.h defaults")
require_contains(_encode
    "if (sv_voiceQuality->current.integer != g_encoder_quality)"
    "Encode_Sample re-syncs quality when sv_voiceQuality changes")
require_contains(_encode
    "speex_encoder_ctl(g_encoder, SPEEX_SET_QUALITY, &g_encoder_quality);"
    "quality re-sync reaches the encoder")

# --- Server default (sv_init_mp.cpp) -----------------------------------------
require_contains(_svinit "Dvar_RegisterInt(\"sv_voiceQuality\", 3,"
    "retail sv_voiceQuality default is 3")

# --- Decoder retail return semantics (decode.cpp) ----------------------------
require_count(_decode "v5 = 2 * frame_size;" 1
    "Decode_Sample keeps the retail 2x sample count (unresolved parity finding, see header comment)")
forbid_contains(_decode "v5 = frame_size;"
    "silent Decode_Sample correction is rejected; changes need a #122 compat analysis")
require_contains(_decode
    "speex_bits_read_from(&decodeBits, buffer, maxLength);"
    "Decode_Sample feeds exactly maxLength wire bytes per frame")

# Contract mutation self-verification: each mutation below is a plausible
# regression and must be rejected by the checks above.
if(DEFINED CONTRACT_MUTATION AND NOT CONTRACT_MUTATION STREQUAL "")
    if(CONTRACT_MUTATION STREQUAL "wideband_default")
        string(REPLACE
            "g_current_bandwidth_setting = 0;" "g_current_bandwidth_setting = 1;"
            _winvoice "${_winvoice}")
    elseif(CONTRACT_MUTATION STREQUAL "samplerate_changed")
        string(REPLACE
            "inline int32_t g_encoder_samplerate = 0x1F40;"
            "inline int32_t g_encoder_samplerate = 0x3E80;"
            _header "${_header}")
    elseif(CONTRACT_MUTATION STREQUAL "server_quality_default_changed")
        string(REPLACE
            "Dvar_RegisterInt(\"sv_voiceQuality\", 3,"
            "Dvar_RegisterInt(\"sv_voiceQuality\", 2,"
            _svinit "${_svinit}")
    elseif(CONTRACT_MUTATION STREQUAL "dtx_disabled")
        string(REPLACE
            "speex_encoder_ctl(g_encoder, SPEEX_SET_DTX, &yes);"
            "" _encode "${_encode}")
    elseif(CONTRACT_MUTATION STREQUAL "decode_silent_fix")
        string(REPLACE
            "v5 = 2 * frame_size;" "v5 = frame_size;"
            _decode "${_decode}")
    elseif(CONTRACT_MUTATION STREQUAL "quality_sync_inverted")
        string(REPLACE
            "if (sv_voiceQuality->current.integer != g_encoder_quality)"
            "if (sv_voiceQuality->current.integer == g_encoder_quality)"
            _encode "${_encode}")
    else()
        message(FATAL_ERROR
            "Unknown audio gate contract mutation: '${CONTRACT_MUTATION}'")
    endif()
endif()

require_count(_winvoice "g_current_bandwidth_setting = 0;" 1
    "narrowband pin survives mutations")
require_contains(_header "inline int32_t g_encoder_samplerate = 0x1F40;"
    "sample rate default survives mutations")
require_contains(_svinit "Dvar_RegisterInt(\"sv_voiceQuality\", 3,"
    "server quality default survives mutations")
require_contains(_encode "speex_encoder_ctl(g_encoder, SPEEX_SET_DTX, &yes);"
    "DTX pin survives mutations")
require_count(_decode "v5 = 2 * frame_size;" 1
    "retail Decode_Sample semantics survive mutations")
require_contains(_encode
    "if (sv_voiceQuality->current.integer != g_encoder_quality)"
    "quality sync survives mutations")
