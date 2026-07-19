cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

function(read_normalized RELATIVE_PATH OUT_VARIABLE)
    set(_path "${SOURCE_ROOT}/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing sound dry-send source: ${_path}")
    endif()
    file(READ "${_path}" _source)
    string(REGEX REPLACE "[ \t\r\n]+" " " _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(extract_slice
    SOURCE_VARIABLE START_MARKER END_MARKER OUT_VARIABLE DESCRIPTION)
    set(_source "${${SOURCE_VARIABLE}}")
    string(FIND "${_source}" "${START_MARKER}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR
            "Missing start of sound dry-send slice (${DESCRIPTION}): "
            "'${START_MARKER}'")
    endif()
    string(SUBSTRING "${_source}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${END_MARKER}" _relative_end)
    if(_relative_end LESS_EQUAL 0)
        message(FATAL_ERROR
            "Missing ordered end of sound dry-send slice (${DESCRIPTION}): "
            "'${END_MARKER}'")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_relative_end} _slice)
    set(${OUT_VARIABLE} "${_slice}" PARENT_SCOPE)
endfunction()

function(require_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing sound dry-send invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden sound dry-send regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_count SOURCE_VARIABLE NEEDLE EXPECTED_COUNT DESCRIPTION)
    set(_remaining "${${SOURCE_VARIABLE}}")
    string(LENGTH "${NEEDLE}" _needle_length)
    if(_needle_length EQUAL 0)
        message(FATAL_ERROR "Empty sound dry-send count needle (${DESCRIPTION})")
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
            "Unexpected sound dry-send invariant count (${DESCRIPTION}): "
            "expected ${EXPECTED_COUNT}, found ${_count}")
    endif()
endfunction()

read_normalized("src/sound/snd_driver.cpp" _driver)
read_normalized("src/sound/snd_local.h" _header)
read_normalized("src/sound/snd_mss.cpp" _mss)
read_normalized("tests/CMakeLists.txt" _tests)
read_normalized(".github/workflows/ci.yml" _ci)

# Each mutation is a plausible regression. The normal invocation reruns this
# contract against every mutation below and requires each one to be rejected.
if(DEFINED CONTRACT_MUTATION AND NOT CONTRACT_MUTATION STREQUAL "")
    if(CONTRACT_MUTATION STREQUAL "non_unity_dry")
        string(REPLACE "return 1.0f;" "return 0.5f;" _mss "${_mss}")
    elseif(CONTRACT_MUTATION STREQUAL "live_dry_state")
        string(REPLACE
            "return 1.0f;" "return g_snd.effect->drylevel;" _mss "${_mss}")
    elseif(CONTRACT_MUTATION STREQUAL "wet_abi_change")
        string(REPLACE
            "double __cdecl MSS_GetWetLevel"
            "float __cdecl MSS_GetWetLevel" _header "${_header}")
    elseif(CONTRACT_MUTATION STREQUAL "scoreboard_dependency")
        string(APPEND _driver " CG_BannerScoreboardScaleMultiplier();")
    elseif(CONTRACT_MUTATION STREQUAL "cgame_dependency")
        string(PREPEND _driver "#include <cgame_mp/cg_local_mp.h> ")
    elseif(CONTRACT_MUTATION STREQUAL "missing_dry_send")
        string(REPLACE
            "AIL_set_digital_master_reverb_levels( milesGlob.driver, MSS_GetDryLevel(), MSS_GetWetLevel(0));"
            "AIL_set_digital_master_reverb_levels( milesGlob.driver, 1.0f, MSS_GetWetLevel(0));"
            _driver "${_driver}")
    elseif(CONTRACT_MUTATION STREQUAL "swapped_reverb_operands")
        string(REPLACE
            "AIL_set_sample_reverb_levels(handle, MSS_GetDryLevel(), baseSlavePercentage);"
            "AIL_set_sample_reverb_levels(handle, baseSlavePercentage, MSS_GetDryLevel());"
            _driver "${_driver}")
    elseif(CONTRACT_MUTATION STREQUAL "stale_dry_temporary")
        string(REPLACE
            "void __cdecl SND_SetRoomtype(int roomtype) {"
            "void __cdecl SND_SetRoomtype(int roomtype) { float v1;"
            _driver "${_driver}")
    elseif(CONTRACT_MUTATION STREQUAL "missing_test_registration")
        string(REPLACE
            "sound-dry-send-source-invariants"
            "sound-dry-send-contract-removed" _tests "${_tests}")
    elseif(CONTRACT_MUTATION STREQUAL "missing_ci_selection")
        string(REPLACE
            "sound-dry-send-source-invariants"
            "sound-dry-send-contract-removed" _ci "${_ci}")
    else()
        message(FATAL_ERROR "Unknown sound dry-send mutation: ${CONTRACT_MUTATION}")
    endif()
endif()

# Keep the callable ABI explicit. In particular, the unrelated wet helper is
# deliberately left at its established double return type.
require_count(
    _header "float __cdecl MSS_GetDryLevel();" 1
    "one cdecl float dry-helper declaration")
require_count(
    _header "double __cdecl MSS_GetWetLevel(const snd_alias_t *pAlias);" 1
    "the wet-helper declaration keeps its established ABI")
forbid_contains(
    _header "float __cdecl MSS_GetWetLevel"
    "the wet-helper declaration cannot be narrowed")

extract_slice(
    _mss
    "float __cdecl MSS_GetDryLevel()"
    "double __cdecl MSS_GetWetLevel(const snd_alias_t *pAlias)"
    _dry_helper
    "MSS_GetDryLevel")
require_contains(
    _dry_helper
    "Miles models reverb as a unity dry signal plus a separate wet send."
    "the helper documents the Miles unity-dry/wet-send model")
require_count(
    _dry_helper "return 1.0f;" 1
    "the helper returns exactly one literal unity value")
forbid_contains(
    _dry_helper "g_snd.effect"
    "the fixed dry signal cannot inherit mutable environment state")
require_count(
    _mss "double __cdecl MSS_GetWetLevel(const snd_alias_t *pAlias)" 1
    "the wet-helper definition keeps its established ABI")

# Sound owns this policy locally; linking it must not pull either cgame variant
# or the scoreboard-scale function into the driver.
foreach(_dependency IN ITEMS
    "#include <cgame"
    "<cgame_mp/"
    "<cgame/"
    "CG_BannerScoreboardScaleMultiplier")
    forbid_contains(
        _driver "${_dependency}" "no cgame or scoreboard dependency")
endforeach()
require_count(
    _driver "MSS_GetDryLevel()" 7
    "all seven Miles reverb calls use the dedicated dry helper")

extract_slice(
    _driver
    "int __cdecl SND_StartAlias2DSample("
    "void __cdecl SND_Apply3DSpatializationTweaks("
    _start_2d
    "SND_StartAlias2DSample")
extract_slice(
    _driver
    "int __cdecl SND_StartAlias3DSample("
    "void __cdecl SND_Set3DStreamPosition("
    _start_3d
    "SND_StartAlias3DSample")
extract_slice(
    _driver
    "int __cdecl SND_StartAliasStreamOnChannel("
    "void __cdecl SND_SetRoomtype("
    _start_stream
    "SND_StartAliasStreamOnChannel")
extract_slice(
    _driver
    "void __cdecl SND_SetRoomtype("
    "void __cdecl SND_UpdateEqs("
    _roomtype
    "SND_SetRoomtype")
extract_slice(
    _driver
    "void __cdecl SND_Update2DChannelReverb("
    "void __cdecl SND_Update3DChannelReverb("
    _update_2d
    "SND_Update2DChannelReverb")
extract_slice(
    _driver
    "void __cdecl SND_Update3DChannelReverb("
    "void __cdecl SND_UpdateStreamChannelReverb("
    _update_3d
    "SND_Update3DChannelReverb")
extract_slice(
    _driver
    "void __cdecl SND_UpdateStreamChannelReverb("
    "int __cdecl SND_Get2DChannelLength("
    _update_stream
    "SND_UpdateStreamChannelReverb")

foreach(_slice IN ITEMS
    _start_2d
    _start_3d
    _start_stream
    _roomtype
    _update_2d
    _update_3d
    _update_stream)
    require_count(
        ${_slice} "MSS_GetDryLevel()" 1
        "${_slice} supplies exactly one dry operand")
endforeach()

require_contains(
    _start_2d
    "AIL_set_sample_reverb_levels(handle, MSS_GetDryLevel(), baseSlavePercentage);"
    "2D sample sends dry before wet")
require_contains(
    _start_3d
    "AIL_set_sample_reverb_levels(handle, MSS_GetDryLevel(), maxdist);"
    "3D sample sends dry before wet")
require_contains(
    _start_stream
    "AIL_set_sample_reverb_levels(handle_sample, MSS_GetDryLevel(), baseSlavePercentage);"
    "stream sample sends dry before wet")
require_contains(
    _roomtype
    "AIL_set_digital_master_reverb_levels( milesGlob.driver, MSS_GetDryLevel(), MSS_GetWetLevel(0));"
    "room type sends unity dry and the existing wet level")
foreach(_slice IN ITEMS _update_2d _update_3d)
    require_contains(
        ${_slice}
        "AIL_set_sample_reverb_levels( milesGlob.handle_sample[index], MSS_GetDryLevel(), MSS_GetWetLevel(g_snd.chaninfo[index].alias0));"
        "${_slice} updates dry before wet")
endforeach()
require_contains(
    _update_stream
    "AIL_set_sample_reverb_levels( (_SAMPLE *)AIL_stream_sample_handle((HSTREAM)milesGlob.handle_sample[index]), MSS_GetDryLevel(), MSS_GetWetLevel(g_snd.chaninfo[index].alias0));"
    "stream update sends dry before wet")

# Remove the decompiler temporaries that previously ferried a scoreboard value.
forbid_contains(_start_2d "float v3;" "2D dry temporary is obsolete")
forbid_contains(_start_3d "float mindist;" "3D dry temporary is obsolete")
forbid_contains(_start_stream "float v6;" "stream dry temporary is obsolete")
foreach(_slice IN ITEMS _roomtype _update_2d _update_3d _update_stream)
    forbid_contains(${_slice} "float v1;" "${_slice} dry temporary is obsolete")
    forbid_contains(${_slice} "float WetLevel;" "${_slice} wet temporary is obsolete")
endforeach()

# Register the source contract for normal CTest discovery and explicitly select
# it in the measured Windows x86 job, where the Miles ABI is exercised.
require_count(
    _tests "NAME sound-dry-send-source-invariants" 1
    "CTest registers the dry-send source contract once")
require_count(
    _tests "sound_dry_send_source_test.cmake" 1
    "CTest invokes this source-contract file once")
require_count(
    _ci "sound-dry-send-source-invariants" 1
    "measured Windows x86 selects the dry-send source contract once")

if(NOT DEFINED CONTRACT_MUTATION OR CONTRACT_MUTATION STREQUAL "")
    foreach(_mutation IN ITEMS
        non_unity_dry
        live_dry_state
        wet_abi_change
        scoreboard_dependency
        cgame_dependency
        missing_dry_send
        swapped_reverb_operands
        stale_dry_temporary
        missing_test_registration
        missing_ci_selection)
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
                "Sound dry-send contract accepted mutation: ${_mutation}")
        endif()
    endforeach()
endif()

message(STATUS "Sound dry-send source contract passed")
