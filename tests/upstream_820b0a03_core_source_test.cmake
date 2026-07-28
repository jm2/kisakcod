cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

function(read_normalized RELATIVE_PATH OUT_VARIABLE)
    set(_path "${SOURCE_ROOT}/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing upstream core source: ${_path}")
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
            "Missing start of upstream core slice (${DESCRIPTION})")
    endif()
    string(SUBSTRING "${_source}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${END_MARKER}" _relative_end)
    if(_relative_end LESS_EQUAL 0)
        message(FATAL_ERROR
            "Missing ordered end of upstream core slice (${DESCRIPTION})")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_relative_end} _slice)
    set(${OUT_VARIABLE} "${_slice}" PARENT_SCOPE)
endfunction()

function(require_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing upstream core invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden upstream core regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_ordered SOURCE_VARIABLE FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${FIRST}" _first)
    string(FIND "${${SOURCE_VARIABLE}}" "${SECOND}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered upstream core invariant (${DESCRIPTION})")
    endif()
endfunction()

read_normalized("src/gfx_d3d/r_add_staticmodel.cpp" _pretess)
read_normalized("src/gfx_d3d/r_image_load_common.cpp" _image)
read_normalized("src/gfx_d3d/r_material_load_obj.cpp" _material)
read_normalized("src/physics/phys_coll_capsulebrush.cpp" _capsule)
read_normalized("src/script/scr_animtree.cpp" _animtree)
read_normalized("src/sound/snd.cpp" _sound)
read_normalized(".github/workflows/ci.yml" _ci)

if(DEFINED CONTRACT_MUTATION AND NOT CONTRACT_MUTATION STREQUAL "")
    if(CONTRACT_MUTATION STREQUAL "pretess_partial_pack")
        string(REPLACE
            "|| !gfx::pretess_encoding::TryPackSurface( surfaceIndex, lod, *list, &preTessSurf)"
            ""
            _pretess "${_pretess}")
    elseif(CONTRACT_MUTATION STREQUAL "image_lowbyte_clamp")
        string(REPLACE
            "picmipUsed = 3;"
            "LOBYTE(picmipUsed) = 3;"
            _image "${_image}")
    elseif(CONTRACT_MUTATION STREQUAL "image_missing_failure_gate")
        string(REPLACE
            "if (!width || !height)"
            "if (false)"
            _image "${_image}")
    elseif(CONTRACT_MUTATION STREQUAL "material_partial_state")
        string(REPLACE
            "mtl->stateFlags = 0;"
            "LOBYTE(stateFlags) = 0;"
            _material "${_material}")
    elseif(CONTRACT_MUTATION STREQUAL "capsule_result_return")
        string(REPLACE
            "void Phys_CancelSimilarContacts()"
            "int Phys_CancelSimilarContacts()"
            _capsule "${_capsule}")
    elseif(CONTRACT_MUTATION STREQUAL "animtree_stale_flags")
        string(REPLACE
            "const uint16_t varFlags = flagsId"
            "int varFlags = flagsId"
            _animtree "${_animtree}")
    elseif(CONTRACT_MUTATION STREQUAL "sound_stale_diagnostic_index")
        string(REPLACE
            "\"g_snd.chaninfo[ia].alias0\""
            "\"g_snd.chaninfo[i].alias0\""
            _sound "${_sound}")
    elseif(CONTRACT_MUTATION STREQUAL "ci_omits_contract")
        string(REPLACE
            "|upstream-820b0a03-core-source-invariants"
            ""
            _ci "${_ci}")
    else()
        message(FATAL_ERROR
            "Unknown upstream core mutation: ${CONTRACT_MUTATION}")
    endif()
endif()

extract_slice(
    _pretess
    "char __cdecl R_PreTessStaticModelCachedList("
    "GfxStaticModelId __cdecl R_GetStaticModelId("
    _pretess_slice
    "R_PreTessStaticModelCachedList")
require_contains(
    _pretess_slice
    "gfx::pretess_encoding::TryPackSurface( surfaceIndex, lod, *list, &preTessSurf)"
    "pre-tess values are validated and packed without byte aliases")
require_ordered(
    _pretess_slice
    "TryPackSurface("
    "XModelGetSurface("
    "packing bounds are checked before the model surface is accessed")
foreach(_forbidden IN ITEMS
    "LOBYTE(preTessSurf)"
    "BYTE1(preTessSurf)"
    "HIWORD(preTessSurf)")
    forbid_contains(
        _pretess_slice "${_forbidden}"
        "partial pre-tess packing must not return")
endforeach()

extract_slice(
    _image
    "void __cdecl Image_PicmipForSemantic("
    "int __cdecl Image_SourceBytesPerSlice_PC("
    _picmip_slice
    "Image_PicmipForSemantic")
foreach(_forbidden IN ITEMS "LOBYTE(picmipUsed)" "LOWORD(picmipUsed)")
    forbid_contains(
        _picmip_slice "${_forbidden}"
        "picmip clamping uses complete typed assignments")
endforeach()
require_contains(
    _picmip_slice
    "picmip->platform[0] = static_cast<uint8_t>(picmipUsed);"
    "picmip narrowing is explicit after clamping")

extract_slice(
    _image
    "void __cdecl Image_GetMipmapResolution("
    "void __cdecl Image_TrackFullscreenTexture("
    _resolution_slice
    "Image_GetMipmapResolution")
require_contains(
    _resolution_slice
    "gfx::image_dimensions::TryGetMipmapResolution("
    "mipmap shifts use the checked portable helper")
forbid_contains(
    _resolution_slice
    ">> mipmap"
    "unchecked or oversized shift counts must not return")

extract_slice(
    _image
    "void __cdecl Image_TrackFullscreenTexture("
    "if (!IsFastFileLoad())"
    _tracking_slice
    "Image_TrackFullscreenTexture")
require_ordered(
    _tracking_slice
    "if (!width || !height)"
    "Image_GetCardMemoryAmount("
    "invalid dimensions fail before card-memory calculation")

extract_slice(
    _material
    "uint8_t __cdecl Material_GetStreamDestForSemantic("
    "void __cdecl Material_SetVaryingParameterDef("
    _semantic_slice
    "Material_GetStreamDestForSemantic")
forbid_contains(
    _semantic_slice "LOBYTE("
    "semantic decoding does not partially initialize a return value")
require_contains(
    _semantic_slice
    "return static_cast<uint8_t>(semantic->UsageIndex + 4);"
    "semantic values narrow explicitly after range validation")

extract_slice(
    _material
    "void __cdecl Material_UpdateStateFlags("
    "void __cdecl Material_SetStateBits("
    _state_flags_slice
    "Material_UpdateStateFlags")
require_contains(
    _state_flags_slice
    "mtl->stateFlags = static_cast<uint8_t>(stateFlags);"
    "material state flags commit a completely initialized value")
require_contains(
    _state_flags_slice
    "mtl->stateFlags = 0;"
    "missing technique sets clear the complete state byte")
forbid_contains(
    _state_flags_slice "LOBYTE("
    "material state initialization cannot use partial writes")

extract_slice(
    _material
    "void __cdecl Material_SetMaterialDrawRegion("
    "Material *__cdecl Material_LoadRaw("
    _draw_region_slice
    "Material_SetMaterialDrawRegion")
require_contains(
    _draw_region_slice
    "material->cameraRegion = static_cast<uint8_t>("
    "camera region commits one complete typed value")
forbid_contains(
    _draw_region_slice "LOBYTE("
    "camera-region initialization cannot use partial writes")

extract_slice(
    _capsule
    "void Phys_CancelSimilarContacts()"
    "void __cdecl Phys_CapsuleOptimizeLocalResults("
    _capsule_slice
    "Phys_CancelSimilarContacts")
require_contains(
    _capsule_slice
    "const int numContacts = numLocalContacts;"
    "contact cancellation uses one bounded count snapshot")
forbid_contains(
    _capsule_slice "LOBYTE("
    "contact predicates remain boolean")
forbid_contains(
    _capsule_slice "return result"
    "void contact cleanup cannot return stale scratch state")

extract_slice(
    _animtree
    "int __cdecl Scr_CreateAnimationTree("
    "void __cdecl Scr_CheckAnimsDefined("
    _animtree_slice
    "Scr_CreateAnimationTree")
require_contains(
    _animtree_slice
    "const uint16_t varFlags = flagsId ? static_cast<uint16_t>("
    "each animation node initializes a fresh fixed-width flag value")
forbid_contains(
    _animtree_slice "LOWORD(varFlags)"
    "missing animation flags clear the complete value")

extract_slice(
    _sound
    "char __cdecl SND_ContinueLoopingSound("
    "void __cdecl SND_ContinueLoopingSound_Internal("
    _continue_looping_slice
    "SND_ContinueLoopingSound")
foreach(_required IN ITEMS
    "\"g_snd.chaninfo[ia].alias0\""
    "\"g_snd.chaninfo[ia].alias1\""
    "\"g_snd.chaninfo[ib].alias0\""
    "\"g_snd.chaninfo[ib].alias1\"")
    require_contains(
        _continue_looping_slice "${_required}"
        "looping-sound diagnostics name the checked channel")
endforeach()

extract_slice(
    _sound
    "void __cdecl SND_UpdateLoopingSounds()"
    "char __cdecl SND_UpdateBackgroundVolume("
    _update_looping_slice
    "SND_UpdateLoopingSounds")
foreach(_required IN ITEMS
    "\"g_snd.chaninfo[ia].alias0\""
    "\"g_snd.chaninfo[ib].alias0\"")
    require_contains(
        _update_looping_slice "${_required}"
        "loop-update diagnostics name the checked channel")
endforeach()

require_contains(
    _ci "|upstream-820b0a03-core-source-invariants"
    "measured Windows x86 tests select the upstream core contract")

if(NOT DEFINED CONTRACT_MUTATION OR CONTRACT_MUTATION STREQUAL "")
    foreach(_mutation IN ITEMS
        pretess_partial_pack
        image_lowbyte_clamp
        image_missing_failure_gate
        material_partial_state
        capsule_result_return
        animtree_stale_flags
        sound_stale_diagnostic_index
        ci_omits_contract)
        execute_process(
            COMMAND "${CMAKE_COMMAND}"
                "-DSOURCE_ROOT=${SOURCE_ROOT}"
                "-DCONTRACT_MUTATION=${_mutation}"
                -P "${CMAKE_CURRENT_LIST_FILE}"
            RESULT_VARIABLE _result
            OUTPUT_VARIABLE _stdout
            ERROR_VARIABLE _stderr)
        if(_result EQUAL 0)
            message(STATUS "Mutation stdout: ${_stdout}")
            message(STATUS "Mutation stderr: ${_stderr}")
            message(FATAL_ERROR
                "Upstream core contract accepted mutation: ${_mutation}")
        endif()
    endforeach()
endif()

message(STATUS "Upstream 820b0a03 core source contract passed")
