cmake_minimum_required(VERSION 3.16)

# Audio playback engine source contracts (AUD-1..AUD-8 companion). The retail
# Miles-backed playback path in src/sound is the compatibility surface for
# acceptance criterion 2: loader error handling, looping-sound entry, the
# per-family playback-rate setters, attenuation and stream 3D fall-off,
# roomtype/reverb updates, stream refill, driver shutdown and the channel
# map / MSS speaker configuration wiring. These pins make that production
# call graph CI-verifiable on every platform; they are source contracts, not
# runtime playback evidence. AUD-1..AUD-8 runtime execution (and AUD-9
# retail-reference attenuation equivalence) stays pending/blocked per the
# gate document section 6: nothing here may be reported as runtime coverage.
#
# Issue #122 / docs/NETWORK_COMPATIBILITY.md govern: every pinned statement
# is behavior the unmodified original commercial client exercises.

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

# Strip C/C++ comments from a source string. String literals are copied
# verbatim (escape-aware) so a "https://" URL or an "/*" inside quotes can
# never be mistaken for a comment. Without this, a reviewer-observed
# regression that comments a pinned statement out (``// SND_Set...();``)
# would still match the pin and the gate would silently pass.
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
            # Copy the whole literal in one chunk; walk forward only when a
            # closing quote turns out to be escaped (odd backslash run).
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
            # Replace the comment body with one space and keep the newline.
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

# Case-insensitive codec-exclusion sweep over every playback-engine source
# this gate reads. The pre-2026-09 guard scanned only snd_driver.cpp and was
# case-sensitive, so a lowercase ``opus`` fallback in another translation
# unit (or inside a comment) slipped past; comments are already stripped by
# the time this runs.
function(forbid_playback_codec_substitution)
    foreach(_source_variable IN ITEMS _loader _snd _driver _mss)
        string(TOUPPER "${${_source_variable}}" _upper)
        string(FIND "${_upper}" "OPUS" _position)
        if(NOT _position EQUAL -1)
            message(FATAL_ERROR
                "Forbidden audio playback gate regression (playback engine "
                "stays codec-free): OPUS reference found in ${_source_variable}")
        endif()
    endforeach()
endfunction()

function(read_normalized RELATIVE_PATH OUT_VARIABLE)
    set(_path "${SOURCE_ROOT}/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing audio playback gate source: ${_path}")
    endif()
    file(READ "${_path}" _source)
    strip_c_comments(_source _source)
    string(REGEX REPLACE "[ \t\r\n]+" " " _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(require_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing audio playback gate invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        return()
    endif()
    message(FATAL_ERROR
        "Forbidden audio playback gate regression (${DESCRIPTION}): '${NEEDLE}'")
endfunction()

function(require_count SOURCE_VARIABLE NEEDLE EXPECTED_COUNT DESCRIPTION)
    set(_remaining "${${SOURCE_VARIABLE}}")
    string(LENGTH "${NEEDLE}" _needle_length)
    if(_needle_length EQUAL 0)
        message(FATAL_ERROR "Empty audio playback gate count needle (${DESCRIPTION})")
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
            "Unexpected audio playback gate invariant count (${DESCRIPTION}): "
            "expected ${EXPECTED_COUNT}, found ${_count}")
    endif()
endfunction()

read_normalized("src/sound/snd_driver_load_obj.cpp" _loader)
read_normalized("src/sound/snd.cpp" _snd)
read_normalized("src/sound/snd_driver.cpp" _driver)
read_normalized("src/sound/snd_mss.cpp" _mss)

# --- Loader error branches (snd_driver_load_obj.cpp, AUD-1/AUD-2) ------------
require_contains(_loader
    "ERROR: Sound file '%s' is zero length, invalid"
    "zero-length sound data is rejected with the retail error, never loaded")
require_contains(_loader
    "ERROR: Sound file '%s' is in an invalid or corrupted format"
    "invalid sound formats are rejected with the retail error")
require_contains(_loader
    "SND_SetData(&loadSnd->sound, (void*)info.data_ptr);"
    "valid loads hand the WAV data to SND_SetData unchanged")

# --- Looping sound entry (snd.cpp, AUD-2) ------------------------------------
require_contains(_snd
    "char __cdecl SND_ContinueLoopingSound("
    "the looping-sound continuation entry stays present")
require_contains(_snd
    "SND_ChoosePitchAndVolume(alias0, alias1, lerp, volumeScale, &startAliasInfo.volume, &startAliasInfo.pitch);"
    "alias playback derives pitch/volume through SND_ChoosePitchAndVolume")

# --- Playback-rate setters, one per channel family (snd_driver.cpp, AUD-3) ---
require_contains(_driver
    "void __cdecl SND_Set2DChannelPlaybackRate(int index, int rate)"
    "2D channel playback-rate setter stays present")
require_contains(_driver
    "void __cdecl SND_Set3DChannelPlaybackRate(int index, int rate)"
    "3D channel playback-rate setter stays present")
require_contains(_driver
    "void __cdecl SND_SetStreamChannelPlaybackRate(int index, int rate)"
    "stream channel playback-rate setter stays present")

# --- Attenuation and gain (snd.cpp / snd_driver.cpp, AUD-4) -------------------
require_contains(_snd
    "double __cdecl SND_Attenuate(SndCurve *volumeFalloffCurve, float radius, float mindist, float maxdist)"
    "distance attenuation runs through the retail SndCurve evaluator")
require_contains(_driver
    "double __cdecl SND_GetStream3DVolumeFallOff(int index, int listenerIndex)"
    "stream 3D volume fall-off has the retail accessor")
require_contains(_driver
    "SND_Set3DChannelVolume(index, realVolume);"
    "3D channel gain is applied through SND_Set3DChannelVolume")
require_contains(_driver
    "SND_SetStreamChannelVolume(index, realVolume);"
    "stream channel gain is applied through SND_SetStreamChannelVolume")

# --- Reverb / roomtype (snd_driver.cpp, AUD-5) --------------------------------
require_contains(_driver
    "void __cdecl SND_SetRoomtype(int roomtype)"
    "roomtype registration stays present")
require_contains(_driver
    "void __cdecl SND_Update2DChannelReverb(int index)"
    "2D channel reverb update stays present")
require_contains(_driver
    "void __cdecl SND_Update3DChannelReverb(int index)"
    "3D channel reverb update stays present")
require_contains(_driver
    "void __cdecl SND_UpdateStreamChannelReverb(int index)"
    "stream channel reverb update stays present")

# --- Streaming refill (snd_driver.cpp, AUD-6) ---------------------------------
require_contains(_driver
    "void __cdecl SND_UpdateStreamChannel(int i, int frametime)"
    "stream channel refill update stays present")
require_contains(_driver
    "int __cdecl SND_StartAliasStreamOnChannel(SndStartAliasInfo *startAliasInfo, int index)"
    "alias streaming starts through SND_StartAliasStreamOnChannel")

# --- Shutdown (snd_driver.cpp / snd.cpp, AUD-7) --------------------------------
require_contains(_driver
    "void __cdecl SND_ShutdownDriver()"
    "the driver shutdown entry stays present")
require_count(_snd "SND_ShutdownDriver();" 1
    "SND_Shutdown tears the driver down exactly once")

# --- Channel map and speaker configuration (AUD-8) -----------------------------
require_contains(_driver
    "void __cdecl SND_ApplyChannelMap(_SAMPLE *handle, const snd_alias_t *alias, int srcChannelCount)"
    "channel mapping has the retail production entry")
require_contains(_mss
    "AIL_speaker_configuration(driver, 0, &outputChannels, 0, &mcSpec);"
    "Miles speaker configuration is queried from the driver")
require_contains(_mss
    "AIL_set_speaker_configuration(milesGlob.driver, 0, 0, 3.0f);"
    "Miles speaker configuration is applied through the retail setter")

# Guard rail: the playback engine stays codec-free (voice codecs are pinned
# by the voice gates; cinematic audio is pinned by the cinematic gate).
forbid_playback_codec_substitution()

# Contract mutation self-verification: each mutation below is a plausible
# regression and must be rejected by the checks above.
if(DEFINED CONTRACT_MUTATION AND NOT CONTRACT_MUTATION STREQUAL "")
    if(CONTRACT_MUTATION STREQUAL "zero_length_load_accepted")
        string(REPLACE
            "ERROR: Sound file '%s' is zero length, invalid"
            "zero length accepted"
            _loader "${_loader}")
    elseif(CONTRACT_MUTATION STREQUAL "invalid_format_load_accepted")
        string(REPLACE
            "ERROR: Sound file '%s' is in an invalid or corrupted format"
            "invalid format accepted"
            _loader "${_loader}")
    elseif(CONTRACT_MUTATION STREQUAL "pitch_setter_dropped")
        string(REPLACE
            "void __cdecl SND_Set2DChannelPlaybackRate(int index, int rate)"
            "void __cdecl SND_Set2DChannelPlaybackRate_Dropped(int index, int rate)"
            _driver "${_driver}")
    elseif(CONTRACT_MUTATION STREQUAL "attenuation_dropped")
        string(REPLACE
            "SND_Set3DChannelVolume(index, realVolume);"
            "" _driver "${_driver}")
    elseif(CONTRACT_MUTATION STREQUAL "reverb_update_dropped")
        string(REPLACE
            "void __cdecl SND_UpdateStreamChannelReverb(int index)"
            "void __cdecl SND_UpdateStreamChannelReverb_Dropped(int index)"
            _driver "${_driver}")
    elseif(CONTRACT_MUTATION STREQUAL "stream_refill_dropped")
        string(REPLACE
            "void __cdecl SND_UpdateStreamChannel(int i, int frametime)"
            "void __cdecl SND_UpdateStreamChannel_Dropped(int i, int frametime)"
            _driver "${_driver}")
    elseif(CONTRACT_MUTATION STREQUAL "shutdown_call_dropped")
        string(REPLACE
            "SND_ShutdownDriver();" "" _snd "${_snd}")
    elseif(CONTRACT_MUTATION STREQUAL "speaker_config_dropped")
        string(REPLACE
            "AIL_set_speaker_configuration(milesGlob.driver, 0, 0, 3.0f);"
            "" _mss "${_mss}")
    elseif(CONTRACT_MUTATION STREQUAL "required_call_commented")
        # Reviewer scenario: a pinned call is commented out instead of
        # deleted. read_normalized strips comments, but the mutation is
        # applied to already-normalized text, so the stripped view must be
        # recomputed below before the survival checks run.
        string(REPLACE
            "SND_Set3DChannelVolume(index, realVolume);"
            "// SND_Set3DChannelVolume(index, realVolume);"
            _driver "${_driver}")
    elseif(CONTRACT_MUTATION STREQUAL "opus_lowercase_outside_driver")
        # Reviewer scenario: a lowercase codec reference in a different
        # translation unit than the one the old guard scanned.
        string(REPLACE
            "void __cdecl SND_Init()"
            "static const char *opus_decode_fallback = 0; void __cdecl SND_Init()"
            _snd "${_snd}")
    else()
        message(FATAL_ERROR
            "Unknown audio playback gate contract mutation: '${CONTRACT_MUTATION}'")
    endif()
endif()

# Survival checks. Pins repeated after the mutation block are the rejection
# mechanism: each registered mutation must break at least one of them.
# Mutations run against the normalized text, so the comment-free view the
# checks see must be recomputed here: a mutation that comments a pinned
# statement out only becomes detectable after stripping.
strip_c_comments(_loader _loader)
string(REGEX REPLACE "[ \t\r\n]+" " " _loader "${_loader}")
strip_c_comments(_snd _snd)
string(REGEX REPLACE "[ \t\r\n]+" " " _snd "${_snd}")
strip_c_comments(_driver _driver)
string(REGEX REPLACE "[ \t\r\n]+" " " _driver "${_driver}")
strip_c_comments(_mss _mss)
string(REGEX REPLACE "[ \t\r\n]+" " " _mss "${_mss}")
require_contains(_loader
    "ERROR: Sound file '%s' is zero length, invalid"
    "zero-length rejection survives mutations")
require_contains(_loader
    "ERROR: Sound file '%s' is in an invalid or corrupted format"
    "invalid-format rejection survives mutations")
require_contains(_driver
    "void __cdecl SND_Set2DChannelPlaybackRate(int index, int rate)"
    "2D rate setter survives mutations")
require_contains(_driver
    "SND_Set3DChannelVolume(index, realVolume);"
    "3D gain application survives mutations")
require_contains(_driver
    "void __cdecl SND_UpdateStreamChannelReverb(int index)"
    "stream reverb update survives mutations")
require_contains(_driver
    "void __cdecl SND_UpdateStreamChannel(int i, int frametime)"
    "stream refill update survives mutations")
require_count(_snd "SND_ShutdownDriver();" 1
    "single driver shutdown call survives mutations")
require_contains(_mss
    "AIL_set_speaker_configuration(milesGlob.driver, 0, 0, 3.0f);"
    "speaker setter survives mutations")
require_contains(_driver
    "void __cdecl SND_ShutdownDriver()"
    "driver shutdown entry survives mutations")
require_contains(_snd
    "double __cdecl SND_Attenuate(SndCurve *volumeFalloffCurve, float radius, float mindist, float maxdist)"
    "SndCurve attenuation survives mutations")
require_contains(_driver
    "int __cdecl SND_StartAliasStreamOnChannel(SndStartAliasInfo *startAliasInfo, int index)"
    "stream start entry survives mutations")
require_contains(_driver
    "void __cdecl SND_SetRoomtype(int roomtype)"
    "roomtype pin survives mutations")
require_contains(_mss
    "AIL_speaker_configuration(driver, 0, &outputChannels, 0, &mcSpec);"
    "speaker query pin survives mutations")
require_contains(_loader
    "SND_SetData(&loadSnd->sound, (void*)info.data_ptr);"
    "loader data hand-off survives mutations")
forbid_playback_codec_substitution()

if(NOT DEFINED CONTRACT_MUTATION OR CONTRACT_MUTATION STREQUAL "")
    foreach(_mutation IN ITEMS
        zero_length_load_accepted
        invalid_format_load_accepted
        pitch_setter_dropped
        attenuation_dropped
        reverb_update_dropped
        stream_refill_dropped
        shutdown_call_dropped
        speaker_config_dropped
        required_call_commented
        opus_lowercase_outside_driver)
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
                "Audio playback gate contract accepted mutation: ${_mutation}")
        endif()
    endforeach()
endif()

message(STATUS "Audio playback gate source contract passed")
