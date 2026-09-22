cmake_minimum_required(VERSION 3.16)

# Voice device integration source contracts (VOX-4/5/6 companion). The
# DirectSound capture/playback device lifecycle in win_voice.cpp and
# groupvoice is the compatibility surface for acceptance criterion 3:
# Voice_Init/Voice_Shutdown wiring from the sound system, the retail capture
# permission defaults, the playback underrun recovery path, the mixer device
# enumeration guards and the teardown/restore ordering.
#
# NON-CLAIM: these are source contracts, not runtime device evidence. The
# VOX-4/5/6 runtime rows (device lifecycle exercise, permission and
# default-device change handling, loss/recovery on real capture/playback
# devices) stay pending/not-implemented per the gate document section 6.
# No device-fixture result may be inferred from a pass here.

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

function(read_normalized RELATIVE_PATH OUT_VARIABLE)
    set(_path "${SOURCE_ROOT}/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing voice device gate source: ${_path}")
    endif()
    file(READ "${_path}" _source)
    string(REGEX REPLACE "[ \t\r\n]+" " " _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(require_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing voice device gate invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_count SOURCE_VARIABLE NEEDLE EXPECTED_COUNT DESCRIPTION)
    set(_remaining "${${SOURCE_VARIABLE}}")
    string(LENGTH "${NEEDLE}" _needle_length)
    if(_needle_length EQUAL 0)
        message(FATAL_ERROR "Empty voice device gate count needle (${DESCRIPTION})")
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
            "Unexpected voice device gate invariant count (${DESCRIPTION}): "
            "expected ${EXPECTED_COUNT}, found ${_count}")
    endif()
endfunction()

read_normalized("src/win32/win_voice.cpp" _winvoice)
read_normalized("src/sound/snd.cpp" _snd)
read_normalized("src/groupvoice/play_dsound.cpp" _play)

# --- Device lifecycle wiring (snd.cpp, VOX-4) ---------------------------------
require_count(_snd "Voice_Init();" 1
    "SND_Init starts voice capture/playback exactly once")
require_count(_snd "Voice_Shutdown();" 1
    "SND_Shutdown tears voice capture/playback down exactly once")

# --- Capture permission default (win_voice.cpp, VOX-4) ------------------------
require_contains(_winvoice
    "winvoice_mic_mute = Dvar_RegisterBool(\"winvoice_mic_mute\", 1, DVAR_ARCHIVE, \"Mute the microphone\");"
    "retail capture starts muted; enabling the mic is an explicit user action")

# --- Lifecycle entries and teardown ordering (win_voice.cpp, VOX-4) -----------
require_contains(_winvoice
    "bool __cdecl Voice_Init()"
    "Voice_Init is the capture/playback start entry")
require_contains(_winvoice
    "void __cdecl Voice_Shutdown()"
    "Voice_Shutdown is the capture/playback teardown entry")
require_contains(_winvoice
    "Voice_StopRecording(); Record_Shutdown(); Encode_Shutdown(); Decode_Shutdown();"
    "teardown stops recording before codecs and sound device")

# --- Playback path and underrun recovery (VOX-5) -------------------------------
require_contains(_winvoice
    "void __cdecl Voice_IncomingVoiceData(unsigned __int8 talker, unsigned __int8 *data, int packetDataSize)"
    "incoming voice playback has the retail entry")
require_contains(_winvoice
    "s_clientTalkTime[talker] = Sys_Milliseconds();"
    "playback updates per-talker talk time for the retail talk overlay")
require_contains(_play
    "void __cdecl DSound_HandleBufferUnderrun(dsound_sample_t *sample)"
    "the DirectSound underrun recovery handler stays present")
require_contains(_play
    "DSound_HandleBufferUnderrun(sample);"
    "the mixer loop routes detected underruns through the recovery handler")

# --- Device enumeration guards and state restore (VOX-6) -----------------------
require_contains(_winvoice
    "if (!waveInGetNumDevs()) return 0;"
    "capture setup refuses to run without a wave input device")
require_contains(_winvoice
    "if (!mixerGetNumDevs()) return 0;"
    "capture setup refuses to run without a mixer device")
require_contains(_winvoice
    "mixerSetMicrophoneMute(1);"
    "shutdown restores the retail muted microphone state")
require_contains(_winvoice
    "g_voice_initialized = 0;"
    "shutdown clears the voice-initialized flag")

# Contract mutation self-verification: each mutation below is a plausible
# regression and must be rejected by the checks above.
if(DEFINED CONTRACT_MUTATION AND NOT CONTRACT_MUTATION STREQUAL "")
    if(CONTRACT_MUTATION STREQUAL "init_call_dropped")
        string(REPLACE
            "Voice_Init();" "" _snd "${_snd}")
    elseif(CONTRACT_MUTATION STREQUAL "shutdown_call_dropped")
        string(REPLACE
            "Voice_Shutdown();" "" _snd "${_snd}")
    elseif(CONTRACT_MUTATION STREQUAL "underrun_call_dropped")
        string(REPLACE
            "DSound_HandleBufferUnderrun(sample);" "" _play "${_play}")
    elseif(CONTRACT_MUTATION STREQUAL "device_guard_removed")
        string(REPLACE
            "if (!waveInGetNumDevs()) return 0;"
            "if (false) return 0;"
            _winvoice "${_winvoice}")
    elseif(CONTRACT_MUTATION STREQUAL "mute_default_changed")
        string(REPLACE
            "Dvar_RegisterBool(\"winvoice_mic_mute\", 1,"
            "Dvar_RegisterBool(\"winvoice_mic_mute\", 0,"
            _winvoice "${_winvoice}")
    elseif(CONTRACT_MUTATION STREQUAL "talk_time_dropped")
        string(REPLACE
            "s_clientTalkTime[talker] = Sys_Milliseconds();" "" _winvoice "${_winvoice}")
    else()
        message(FATAL_ERROR
            "Unknown voice device gate contract mutation: '${CONTRACT_MUTATION}'")
    endif()
endif()

# Survival checks. Pins repeated after the mutation block are the rejection
# mechanism: each registered mutation must break at least one of them.
require_count(_snd "Voice_Init();" 1
    "single voice init call survives mutations")
require_count(_snd "Voice_Shutdown();" 1
    "single voice shutdown call survives mutations")
require_contains(_play
    "DSound_HandleBufferUnderrun(sample);"
    "underrun routing survives mutations")
require_contains(_winvoice
    "if (!waveInGetNumDevs()) return 0;"
    "wave-in guard survives mutations")
require_contains(_winvoice
    "winvoice_mic_mute = Dvar_RegisterBool(\"winvoice_mic_mute\", 1, DVAR_ARCHIVE, \"Mute the microphone\");"
    "retail muted capture default survives mutations")
require_contains(_winvoice
    "s_clientTalkTime[talker] = Sys_Milliseconds();"
    "talk-time update survives mutations")
require_contains(_winvoice
    "bool __cdecl Voice_Init()"
    "Voice_Init entry survives mutations")
require_contains(_winvoice
    "void __cdecl Voice_Shutdown()"
    "Voice_Shutdown entry survives mutations")
require_contains(_winvoice
    "void __cdecl Voice_IncomingVoiceData(unsigned __int8 talker, unsigned __int8 *data, int packetDataSize)"
    "playback entry survives mutations")
require_contains(_play
    "void __cdecl DSound_HandleBufferUnderrun(dsound_sample_t *sample)"
    "underrun handler survives mutations")
require_contains(_winvoice
    "Voice_StopRecording(); Record_Shutdown(); Encode_Shutdown(); Decode_Shutdown();"
    "teardown ordering survives mutations")
require_contains(_winvoice
    "mixerSetMicrophoneMute(1);"
    "mute restore survives mutations")
require_contains(_winvoice
    "if (!mixerGetNumDevs()) return 0;"
    "mixer guard survives mutations")

if(NOT DEFINED CONTRACT_MUTATION OR CONTRACT_MUTATION STREQUAL "")
    foreach(_mutation IN ITEMS
        init_call_dropped
        shutdown_call_dropped
        underrun_call_dropped
        device_guard_removed
        mute_default_changed
        talk_time_dropped)
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
                "Voice device gate contract accepted mutation: ${_mutation}")
        endif()
    endforeach()
endif()

message(STATUS "Voice device gate source contract passed")
