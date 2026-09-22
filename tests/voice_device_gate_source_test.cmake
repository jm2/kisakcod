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

# Strip C/C++ comments (verbatim string literals, escape-aware). Without
# this, commenting a pinned statement out (``// Voice_Init();``) leaves the
# pin matching and the gate silently passes (review thread r4070007442).
function(strip_c_comments SOURCE_VARIABLE OUT_VARIABLE)
    set(_in "${${SOURCE_VARIABLE}}")
    string(LENGTH "${_in}" _in_len)
    set(_out "")
    set(_scan 0)
    while(_scan LESS _in_len)
        string(SUBSTRING "${_in}" ${_scan} -1 _rest)
        string(FIND "${_rest}" "\"" _quote_pos)
        string(FIND "${_rest}" "//" _line_pos)
        string(FIND "${_rest}" "/*" _block_pos)
        set(_next -1)
        set(_kind "")
        if(NOT _quote_pos EQUAL -1)
            set(_next ${_quote_pos})
            set(_kind "quote")
        endif()
        if(NOT _line_pos EQUAL -1 AND (_next EQUAL -1 OR _line_pos LESS _next))
            set(_next ${_line_pos})
            set(_kind "line")
        endif()
        if(NOT _block_pos EQUAL -1 AND (_next EQUAL -1 OR _block_pos LESS _next))
            set(_next ${_block_pos})
            set(_kind "block")
        endif()
        if(_kind STREQUAL "")
            string(SUBSTRING "${_in}" ${_scan} -1 _tail)
            string(APPEND _out "${_tail}")
            break()
        endif()
        string(SUBSTRING "${_in}" ${_scan} ${_next} _before)
        string(APPEND _out "${_before}")
        math(EXPR _scan "${_scan} + ${_next}")
        if(_kind STREQUAL "quote")
            string(SUBSTRING "${_in}" ${_scan} 1 _opening_quote)
            string(APPEND _out "${_opening_quote}")
            math(EXPR _scan "${_scan} + 1")
            while(_scan LESS _in_len)
                string(SUBSTRING "${_in}" ${_scan} -1 _rest)
                string(FIND "${_rest}" "\"" _close)
                if(_close EQUAL -1)
                    string(APPEND _out "${_rest}")
                    set(_scan ${_in_len})
                    break()
                endif()
                set(_backslashes 0)
                if(_close GREATER 0)
                    math(EXPR _bs "${_scan} + ${_close}")
                    while(_bs GREATER 0 AND _backslashes LESS 32)
                        math(EXPR _prev "${_bs} - 1")
                        string(SUBSTRING "${_in}" ${_prev} 1 _pc)
                        if(NOT _pc STREQUAL "\\")
                            break()
                        endif()
                        math(EXPR _backslashes "${_backslashes} + 1")
                        set(_bs ${_prev})
                    endwhile()
                endif()
                math(EXPR _chunk_len "${_close} + 1")
                string(SUBSTRING "${_in}" ${_scan} ${_chunk_len} _chunk)
                string(APPEND _out "${_chunk}")
                math(EXPR _scan "${_scan} + ${_chunk_len}")
                math(EXPR _escaped "${_backslashes} % 2")
                if(_escaped EQUAL 0)
                    break()
                endif()
            endwhile()
        elseif(_kind STREQUAL "line")
            string(APPEND _out " ")
            math(EXPR _scan "${_scan} + 2")
            string(SUBSTRING "${_in}" ${_scan} -1 _rest)
            string(FIND "${_rest}" "\n" _newline)
            if(_newline EQUAL -1)
                set(_scan ${_in_len})
            else()
                string(APPEND _out "\n")
                math(EXPR _scan "${_scan} + ${_newline} + 1")
            endif()
        elseif(_kind STREQUAL "block")
            math(EXPR _scan "${_scan} + 2")
            string(SUBSTRING "${_in}" ${_scan} -1 _rest)
            string(FIND "${_rest}" "*/" _end)
            if(_end EQUAL -1)
                set(_scan ${_in_len})
            else()
                math(EXPR _scan "${_scan} + ${_end} + 2")
                string(APPEND _out " ")
            endif()
        endif()
    endwhile()
    set(${OUT_VARIABLE} "${_out}" PARENT_SCOPE)
endfunction()

function(read_normalized RELATIVE_PATH OUT_VARIABLE)
    set(_path "${SOURCE_ROOT}/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing voice device gate source: ${_path}")
    endif()
    file(READ "${_path}" _source)
    strip_c_comments(_source _source)
    string(REGEX REPLACE "[ \t\r\n]+" " " _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

# Copy of the ordered-slice helper used by the null media gate: [start,
# end) of the normalized source, failing closed when either marker is
# missing or out of order. Scoping the lifecycle pins to the SND_Init and
# SND_Shutdown function bodies is what makes a relocated Voice_Init /
# Voice_Shutdown call detectable (review thread r4070007460): a whole-file
# count cannot tell the init function from any other sound-system function.
function(extract_slice SOURCE_VARIABLE START_MARKER END_MARKER OUT_VARIABLE DESCRIPTION)
    set(_source "${${SOURCE_VARIABLE}}")
    string(FIND "${_source}" "${START_MARKER}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR "Missing start of voice device slice (${DESCRIPTION}): '${START_MARKER}'")
    endif()
    string(SUBSTRING "${_source}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${END_MARKER}" _relative_end)
    if(_relative_end LESS_EQUAL 0)
        message(FATAL_ERROR "Missing ordered end of voice device slice (${DESCRIPTION}): '${END_MARKER}'")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_relative_end} _slice)
    set(${OUT_VARIABLE} "${_slice}" PARENT_SCOPE)
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
extract_slice(_snd
    "void __cdecl SND_Init()"
    "void __cdecl SND_PlayLocal_f()"
    _snd_init_body
    "SND_Init")
require_count(_snd_init_body "Voice_Init();" 1
    "SND_Init starts voice capture/playback exactly once")
extract_slice(_snd
    "void __cdecl SND_Shutdown()"
    "void __cdecl SND_ShutdownChannels()"
    _snd_shutdown_body
    "SND_Shutdown")
require_count(_snd_shutdown_body "Voice_Shutdown();" 1
    "SND_Shutdown tears voice capture/playback down exactly once")
require_count(_snd "Voice_Init();" 1
    "the sound system declares exactly one voice init call")
require_count(_snd "Voice_Shutdown();" 1
    "the sound system declares exactly one voice shutdown call")

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
    "Voice_StopRecording(); Record_Shutdown(); Encode_Shutdown(); Decode_Shutdown(); Sound_Shutdown(); mixerSetMicrophoneMute(1); mixerSetRecordLevel((char*)\"Mic\", mic_old_reclevel); mixerSetRecordSource(old_rec_source); g_voice_initialized = 0;"
    "teardown stops recording, then the codecs, then the sound device, and restores the muted microphone state before clearing the initialized flag, in exactly that order")

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
    elseif(CONTRACT_MUTATION STREQUAL "init_call_relocated")
        # Reviewer scenario: the call survives the file but is parked in a
        # function that is not on the SND_Init -> voice startup path.
        string(REPLACE
            "Voice_Init();" "" _snd "${_snd}")
        string(REPLACE
            "void __cdecl SND_PlayLocal_f() {"
            "void __cdecl SND_PlayLocal_f() { Voice_Init();"
            _snd "${_snd}")
    elseif(CONTRACT_MUTATION STREQUAL "shutdown_call_relocated")
        string(REPLACE
            "Voice_Shutdown();" "" _snd "${_snd}")
        string(REPLACE
            "void __cdecl SND_ShutdownChannels() {"
            "void __cdecl SND_ShutdownChannels() { Voice_Shutdown();"
            _snd "${_snd}")
    elseif(CONTRACT_MUTATION STREQUAL "sound_shutdown_reordered")
        # Reviewer scenario: the sound device is torn down before the codecs
        # that still reference it.
        string(REPLACE
            "Voice_StopRecording(); Record_Shutdown(); Encode_Shutdown(); Decode_Shutdown(); Sound_Shutdown();"
            "Sound_Shutdown(); Voice_StopRecording(); Record_Shutdown(); Encode_Shutdown(); Decode_Shutdown();"
            _winvoice "${_winvoice}")
    elseif(CONTRACT_MUTATION STREQUAL "mute_flag_cleared_early")
        # Reviewer scenario: the initialized flag is cleared before the
        # muted microphone state has been restored.
        string(REPLACE
            "mixerSetMicrophoneMute(1); mixerSetRecordLevel((char*)\"Mic\", mic_old_reclevel); mixerSetRecordSource(old_rec_source); g_voice_initialized = 0;"
            "g_voice_initialized = 0; mixerSetMicrophoneMute(1); mixerSetRecordLevel((char*)\"Mic\", mic_old_reclevel); mixerSetRecordSource(old_rec_source);"
            _winvoice "${_winvoice}")
    elseif(CONTRACT_MUTATION STREQUAL "underrun_call_commented")
        # Reviewer scenario: the recovery call is commented out instead of
        # deleted; only the stripped view makes this detectable.
        string(REPLACE
            "DSound_HandleBufferUnderrun(sample);"
            "// DSound_HandleBufferUnderrun(sample);"
            _play "${_play}")
    else()
        message(FATAL_ERROR
            "Unknown voice device gate contract mutation: '${CONTRACT_MUTATION}'")
    endif()
endif()

# Survival checks. Pins repeated after the mutation block are the rejection
# mechanism: each registered mutation must break at least one of them.
# Mutations run against the normalized text, so the comment-free view is
# recomputed here (a commented-out pinned call is only detectable after
# stripping) and the lifecycle slices are re-extracted from the mutated
# text (a relocated call must fail the scoped counts).
strip_c_comments(_winvoice _winvoice)
string(REGEX REPLACE "[ \t\r\n]+" " " _winvoice "${_winvoice}")
strip_c_comments(_snd _snd)
string(REGEX REPLACE "[ \t\r\n]+" " " _snd "${_snd}")
strip_c_comments(_play _play)
string(REGEX REPLACE "[ \t\r\n]+" " " _play "${_play}")
extract_slice(_snd
    "void __cdecl SND_Init()"
    "void __cdecl SND_PlayLocal_f()"
    _snd_init_body_mut
    "SND_Init (mutation check)")
require_count(_snd_init_body_mut "Voice_Init();" 1
    "SND_Init voice startup survives mutations")
extract_slice(_snd
    "void __cdecl SND_Shutdown()"
    "void __cdecl SND_ShutdownChannels()"
    _snd_shutdown_body_mut
    "SND_Shutdown (mutation check)")
require_count(_snd_shutdown_body_mut "Voice_Shutdown();" 1
    "SND_Shutdown voice teardown survives mutations")
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
    "Voice_StopRecording(); Record_Shutdown(); Encode_Shutdown(); Decode_Shutdown(); Sound_Shutdown(); mixerSetMicrophoneMute(1); mixerSetRecordLevel((char*)\"Mic\", mic_old_reclevel); mixerSetRecordSource(old_rec_source); g_voice_initialized = 0;"
    "full teardown/restore ordering survives mutations")
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
        talk_time_dropped
        init_call_relocated
        shutdown_call_relocated
        sound_shutdown_reordered
        mute_flag_cleared_early
        underrun_call_commented)
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
