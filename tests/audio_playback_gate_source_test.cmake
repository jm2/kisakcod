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

function(read_normalized RELATIVE_PATH OUT_VARIABLE)
    set(_path "${SOURCE_ROOT}/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing audio playback gate source: ${_path}")
    endif()
    file(READ "${_path}" _source)
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
forbid_contains(_driver "Opus" "no substitute codec may enter the playback engine")

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
    else()
        message(FATAL_ERROR
            "Unknown audio playback gate contract mutation: '${CONTRACT_MUTATION}'")
    endif()
endif()

# Survival checks. Pins repeated after the mutation block are the rejection
# mechanism: each registered mutation must break at least one of them.
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
forbid_contains(_driver "Opus" "codec exclusion survives mutations")

if(NOT DEFINED CONTRACT_MUTATION OR CONTRACT_MUTATION STREQUAL "")
    foreach(_mutation IN ITEMS
        zero_length_load_accepted
        invalid_format_load_accepted
        pitch_setter_dropped
        attenuation_dropped
        reverb_update_dropped
        stream_refill_dropped
        shutdown_call_dropped
        speaker_config_dropped)
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
