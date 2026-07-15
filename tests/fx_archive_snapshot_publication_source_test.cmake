cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_snapshot_header_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_snapshot_publication.h")
set(_fx_update_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_update.cpp")
set(_fx_update_util_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_update_util.cpp")
set(_fx_draw_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_draw.cpp")
set(_fx_marks_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_marks.cpp")
set(_fx_sort_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_sort.cpp")
set(_fx_system_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_system.cpp")
set(_fx_archive_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive.cpp")

foreach(_source_path IN ITEMS
    "${_snapshot_header_path}"
    "${_fx_update_path}"
    "${_fx_update_util_path}"
    "${_fx_draw_path}"
    "${_fx_marks_path}"
    "${_fx_sort_path}"
    "${_fx_system_path}"
    "${_fx_archive_path}")
    if(NOT EXISTS "${_source_path}")
        message(FATAL_ERROR
            "Missing FX snapshot-publication source: ${_source_path}")
    endif()
endforeach()

file(READ "${_snapshot_header_path}" _snapshot_header)
file(READ "${_fx_update_path}" _fx_update_source)
file(READ "${_fx_update_util_path}" _fx_update_util_source)
file(READ "${_fx_draw_path}" _fx_draw_source)
file(READ "${_fx_marks_path}" _fx_marks_source)
file(READ "${_fx_sort_path}" _fx_sort_source)
file(READ "${_fx_system_path}" _fx_system_source)
file(READ "${_fx_archive_path}" _fx_archive_source)

# These contracts care about ownership and publication order, not formatting.
foreach(_source_var IN ITEMS
    _snapshot_header
    _fx_update_source
    _fx_update_util_source
    _fx_draw_source
    _fx_marks_source
    _fx_sort_source
    _fx_system_source
    _fx_archive_source)
    string(REGEX REPLACE
        "[ \t\r\n]+" " " _normalized_source "${${_source_var}}")
    set(${_source_var} "${_normalized_source}")
endforeach()

function(extract_source_slice
    SOURCE_VAR START_MARKER END_MARKER OUT_VAR DESCRIPTION)
    set(_source "${${SOURCE_VAR}}")
    string(FIND "${_source}" "${START_MARKER}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR
            "Missing start of ${DESCRIPTION}: '${START_MARKER}'")
    endif()
    string(SUBSTRING "${_source}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${END_MARKER}" _relative_end)
    if(_relative_end LESS_EQUAL 0)
        message(FATAL_ERROR
            "Missing ordered end of ${DESCRIPTION}: '${END_MARKER}'")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_relative_end} _slice)
    set(${OUT_VAR} "${_slice}" PARENT_SCOPE)
endfunction()

function(extract_source_tail SOURCE_VAR START_MARKER OUT_VAR DESCRIPTION)
    set(_source "${${SOURCE_VAR}}")
    string(FIND "${_source}" "${START_MARKER}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR
            "Missing start of ${DESCRIPTION}: '${START_MARKER}'")
    endif()
    string(SUBSTRING "${_source}" ${_start} -1 _tail)
    set(${OUT_VAR} "${_tail}" PARENT_SCOPE)
endfunction()

function(extract_between
    SOURCE_VAR START_MARKER END_MARKER OUT_VAR DESCRIPTION)
    set(_source "${${SOURCE_VAR}}")
    string(FIND "${_source}" "${START_MARKER}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR
            "Missing start of ${DESCRIPTION}: '${START_MARKER}'")
    endif()
    string(SUBSTRING "${_source}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${END_MARKER}" _relative_end)
    if(_relative_end LESS_EQUAL 0)
        message(FATAL_ERROR
            "Missing ordered end of ${DESCRIPTION}: '${END_MARKER}'")
    endif()
    string(LENGTH "${END_MARKER}" _end_length)
    math(EXPR _slice_length "${_relative_end} + ${_end_length}")
    string(SUBSTRING "${_tail}" 0 ${_slice_length} _slice)
    set(${OUT_VAR} "${_slice}" PARENT_SCOPE)
endfunction()

function(require_slice_contains SLICE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SLICE_VAR}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing FX snapshot-publication invariant (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(require_slice_not_contains SLICE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SLICE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden FX snapshot-publication regression (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(require_slice_matches SLICE_VAR PATTERN DESCRIPTION)
    string(REGEX MATCH "${PATTERN}" _match "${${SLICE_VAR}}")
    if(_match STREQUAL "")
        message(FATAL_ERROR
            "Missing FX snapshot-publication invariant (${DESCRIPTION})")
    endif()
endfunction()

function(require_slice_ordered SLICE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SLICE_VAR}}" "${FIRST}" _first)
    string(FIND "${${SLICE_VAR}}" "${SECOND}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered FX snapshot-publication invariant "
            "(${DESCRIPTION})")
    endif()
endfunction()

function(require_literal_count
    SLICE_VAR NEEDLE EXPECTED_COUNT DESCRIPTION)
    set(_remaining "${${SLICE_VAR}}")
    set(_count 0)
    string(LENGTH "${NEEDLE}" _needle_length)
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
            "Invalid FX snapshot-publication invariant (${DESCRIPTION}): "
            "expected ${EXPECTED_COUNT}, found ${_count}")
    endif()
endfunction()

# Invalidation closes publication immediately and clears the plane count while
# deliberately retaining stale payload; canonical reset detection is separate.
extract_source_slice(
    _snapshot_header
    "inline void FX_InvalidateCameraPublication("
    "inline void FX_PublishCamera("
    _invalidate_publication_source
    "FX_InvalidateCameraPublication")
require_slice_ordered(
    _invalidate_publication_source
    "Sys_AtomicStore(&camera->isValid, 0);"
    "camera->frustumPlaneCount = 0;"
    "camera invalidation withdraws validity before clearing plane admission")

extract_source_slice(
    _snapshot_header
    "inline void FX_PublishCamera("
    "inline bool FX_TryDeriveVisibilitySelectors("
    _publish_camera_source
    "FX_PublishCamera")
set(_camera_invalid_store
    "Sys_AtomicStore(&destination->isValid, 0);")
set(_camera_valid_store
    "Sys_AtomicStore(&destination->isValid, desiredValidity ? 1 : 0);")
foreach(_camera_payload_marker IN ITEMS
    "source.origin"
    "source.frustum"
    "source.axis"
    "source.frustumPlaneCount"
    "source.viewOffset"
    "source.pad")
    require_slice_ordered(
        _publish_camera_source
        "${_camera_invalid_store}"
        "${_camera_payload_marker}"
        "camera validity is withdrawn before every payload field")
    require_slice_ordered(
        _publish_camera_source
        "${_camera_payload_marker}"
        "${_camera_valid_store}"
        "camera validity is published after every payload field")
endforeach()
extract_source_tail(
    _publish_camera_source
    "${_camera_valid_store}"
    _publish_camera_after_valid
    "FX_PublishCamera final validity store")
require_literal_count(
    _publish_camera_after_valid
    "destination->"
    1
    "camera payload cannot be touched after the validity store")

# Only an exact all-zero reset image is canonical invalid. Archive readiness
# rejects the time-to-camera gap for active frames and permits canonical reset
# cameras only before the first draw frame.
extract_source_slice(
    _snapshot_header
    "inline bool FX_IsCanonicalInvalidCamera("
    "inline bool FX_AreArchiveCamerasReady("
    _canonical_invalid_source
    "FX_IsCanonicalInvalidCamera")
foreach(_canonical_marker IN ITEMS
    "Sys_AtomicLoad(&camera.isValid) != 0"
    "camera.frustumPlaneCount != 0"
    "camera.origin"
    "camera.frustum"
    "camera.axis"
    "camera.viewOffset"
    "camera.pad"
    "value != 0.0f"
    "value != 0")
    require_slice_contains(
        _canonical_invalid_source
        "${_canonical_marker}"
        "canonical invalid cameras require an exact all-zero reset image")
endforeach()

extract_source_slice(
    _snapshot_header
    "inline bool FX_AreArchiveCamerasReady("
    "inline bool FX_TryDeriveVisibilitySelectors("
    _camera_readiness_source
    "FX_AreArchiveCamerasReady")
require_slice_ordered(
    _camera_readiness_source
    "Sys_AtomicLoad(&camera.isValid);"
    "Sys_AtomicLoad(&previousCamera.isValid);"
    "archive readiness samples both publication markers")
require_slice_contains(
    _camera_readiness_source
    "if (msecDraw >= 0) return cameraIsValid == 1 && previousCameraIsValid == 1;"
    "active archive snapshots require both cameras to be published")
require_slice_contains(
    _camera_readiness_source
    "if (msecDraw != -1) return false;"
    "pre-draw archive readiness accepts only the exact sentinel time")
require_slice_contains(
    _camera_readiness_source
    "cameraIsValid == 1 || FX_IsCanonicalInvalidCamera(camera)"
    "pre-draw current cameras must be valid or canonical invalid")
require_slice_contains(
    _camera_readiness_source
    "previousCameraIsValid == 1 || FX_IsCanonicalInvalidCamera(previousCamera)"
    "pre-draw previous cameras must be valid or canonical invalid")

# Selector derivation accepts only exact owned slots, proves read/write are
# distinct, and commits both outputs after the complete validation transaction.
extract_source_tail(
    _snapshot_header
    "inline bool FX_TryDeriveVisibilitySelectors("
    _selector_source
    "FX_TryDeriveVisibilitySelectors")
foreach(_selector_marker IN ITEMS
    "readState == writeState"
    "outReadSelector == outWriteSelector"
    "readState == slot1"
    "readState != slot0"
    "writeState == slot1"
    "writeState != slot0"
    "readSelector == writeSelector")
    require_slice_contains(
        _selector_source
        "${_selector_marker}"
        "visibility selectors require exact, distinct owned slots")
endforeach()
require_slice_ordered(
    _selector_source
    "if (readSelector == writeSelector)"
    "*outReadSelector = readSelector;"
    "selector outputs remain untouched until both inputs validate")
require_slice_ordered(
    _selector_source
    "*outReadSelector = readSelector;"
    "*outWriteSelector = writeSelector;"
    "selector outputs commit as one ordered result")
foreach(_forbidden_selector_codec IN ITEMS
    "reinterpret_cast"
    "uintptr_t"
    "readState -"
    "writeState -")
    require_slice_not_contains(
        _selector_source
        "${_forbidden_selector_codec}"
        "visibility selectors never derive indices through pointer arithmetic")
endforeach()

# The camera gate nests inside the main cooperative iterator: readers share
# it, writers own it exclusively, and longjmp cleanup must release it before
# abandoning the outer lifetime/admission ownership.
extract_source_slice(
    _fx_draw_source
    "void __cdecl FX_ErrorCleanup() noexcept"
    "bool __cdecl FX_CurrentThreadOwnsCooperativeIterator("
    _fx_error_cleanup_source
    "FX_ErrorCleanup")
require_slice_ordered(
    _fx_error_cleanup_source
    "FX_AbandonCurrentThreadCameraPublicationForError();"
    "FX_AbandonCurrentThreadCooperativeIteratorsForError();"
    "error cleanup releases the nested camera gate before main ownership")

extract_source_slice(
    _fx_draw_source
    "void __cdecl FX_BeginReadingCameraPublication("
    "void __cdecl FX_EndReadingCameraPublication("
    _begin_camera_read_source
    "FX_BeginReadingCameraPublication")
require_slice_ordered(
    _begin_camera_read_source
    "FX_CurrentThreadOwnsExactCooperativeIterator(system)"
    "FxIteratorBeginCooperative(&fx_cameraPublicationIteratorCount);"
    "camera readers prove exact main ownership before shared admission")
require_slice_ordered(
    _begin_camera_read_source
    "FxIteratorBeginCooperative(&fx_cameraPublicationIteratorCount);"
    "FxCameraPublicationThreadMode::Read;"
    "camera reader TLS is published only after shared admission")

extract_source_slice(
    _fx_draw_source
    "void __cdecl FX_EndReadingCameraPublication("
    "void __cdecl FX_BeginWritingCameraPublication("
    _end_camera_read_source
    "FX_EndReadingCameraPublication")
require_slice_ordered(
    _end_camera_read_source
    "FX_CurrentThreadOwnsExactCooperativeIterator(system)"
    "FxIteratorEndCooperative( &fx_cameraPublicationIteratorCount, &remaining)"
    "camera reader release validates the matching outer owner")
require_slice_ordered(
    _end_camera_read_source
    "FxIteratorEndCooperative( &fx_cameraPublicationIteratorCount, &remaining)"
    "fx_cameraPublicationThreadState = {};"
    "camera reader TLS remains available until shared release succeeds")

extract_source_slice(
    _fx_draw_source
    "void __cdecl FX_BeginWritingCameraPublication("
    "void __cdecl FX_EndWritingCameraPublication("
    _begin_camera_write_source
    "FX_BeginWritingCameraPublication")
require_slice_ordered(
    _begin_camera_write_source
    "FX_CurrentThreadOwnsExactCooperativeIterator(system)"
    "FxIteratorWaitBeginExclusive(&fx_cameraPublicationIteratorCount);"
    "camera writers prove exact main ownership before exclusive admission")
require_slice_ordered(
    _begin_camera_write_source
    "FxIteratorWaitBeginExclusive(&fx_cameraPublicationIteratorCount);"
    "FxCameraPublicationThreadMode::Write;"
    "camera writer TLS is published only after exclusive admission")
require_slice_not_contains(
    _begin_camera_write_source
    "FxIteratorBeginCooperative(&fx_cameraPublicationIteratorCount);"
    "camera writers cannot enter the camera gate as shared readers")

extract_source_slice(
    _fx_draw_source
    "void __cdecl FX_EndWritingCameraPublication("
    "void __cdecl FX_AbandonCurrentThreadCameraPublicationForError() noexcept"
    _end_camera_write_source
    "FX_EndWritingCameraPublication")
require_slice_ordered(
    _end_camera_write_source
    "FX_CurrentThreadOwnsExactCooperativeIterator(system)"
    "FxIteratorEndExclusive(&fx_cameraPublicationIteratorCount)"
    "camera writer release validates the matching outer owner")
require_slice_ordered(
    _end_camera_write_source
    "FxIteratorEndExclusive(&fx_cameraPublicationIteratorCount)"
    "fx_cameraPublicationThreadState = {};"
    "camera writer TLS remains available until exclusive release succeeds")

extract_source_slice(
    _fx_draw_source
    "void __cdecl FX_AbandonCurrentThreadCameraPublicationForError() noexcept"
    "#ifdef KISAK_MP"
    _abandon_camera_publication_source
    "FX_AbandonCurrentThreadCameraPublicationForError")
require_slice_ordered(
    _abandon_camera_publication_source
    "FxCameraPublicationThreadMode::Read"
    "FxIteratorEndCooperative( &fx_cameraPublicationIteratorCount, &remaining)"
    "camera error cleanup releases shared ownership in read mode")
require_slice_ordered(
    _abandon_camera_publication_source
    "FxCameraPublicationThreadMode::Write"
    "FxIteratorEndExclusive(&fx_cameraPublicationIteratorCount)"
    "camera error cleanup releases exclusive ownership in write mode")
require_slice_ordered(
    _abandon_camera_publication_source
    "fx_cameraPublicationThreadState = {};"
    "std::abort();"
    "camera error cleanup clears TLS before fail-stop on corrupt gate state")

# Camera publishers: current time/camera and previous-camera publication.
extract_source_slice(
    _fx_update_source
    "void __cdecl FX_EndUpdate("
    "void __cdecl FX_AddNonSpriteDrawSurfs("
    _end_update_source
    "FX_EndUpdate")
require_slice_ordered(
    _end_update_source
    "FX_BeginIteratingOverEffects_Cooperative(system);"
    "FX_BeginWritingCameraPublication(system);"
    "previous-camera publication nests its writer gate inside main ownership")
require_slice_ordered(
    _end_update_source
    "FX_BeginWritingCameraPublication(system);"
    "Sys_AtomicLoad(&system->camera.isValid)"
    "previous-camera publication samples validity only after writer exclusion")
require_slice_ordered(
    _end_update_source
    "Sys_AtomicLoad(&system->camera.isValid)"
    "FX_PublishCamera("
    "previous-camera publication samples its source before copying payload")
require_slice_ordered(
    _end_update_source
    "FX_PublishCamera("
    "FX_EndWritingCameraPublication(system);"
    "previous-camera payload publication completes before writer release")
require_slice_ordered(
    _end_update_source
    "Sys_AtomicLoad(&system->cameraPrev.isValid)"
    "FX_EndWritingCameraPublication(system);"
    "previous-camera publication retains writer exclusion through its final sample")
require_slice_ordered(
    _end_update_source
    "FX_EndWritingCameraPublication(system);"
    "FX_EndIteratingOverEffects_Cooperative(system);"
    "previous-camera writer exclusion releases before main ownership")
require_literal_count(
    _end_update_source
    "FX_BeginIteratingOverEffects_Cooperative(system);"
    1
    "FX_EndUpdate has one cooperative admission")
require_literal_count(
    _end_update_source
    "FX_EndIteratingOverEffects_Cooperative(system);"
    1
    "FX_EndUpdate has one cooperative release")

extract_source_slice(
    _fx_update_source
    "void __cdecl FX_SetNextUpdateCamera("
    "void __cdecl FX_SetNextUpdateTime("
    _set_camera_source
    "FX_SetNextUpdateCamera")
require_slice_matches(
    _set_camera_source
    "FxCamera +[A-Za-z_][A-Za-z0-9_]* *\\{ *\\}"
    "the next camera is assembled off-side")
require_slice_ordered(
    _set_camera_source
    "nextCamera.frustumPlaneCount = 6;"
    "FX_BeginIteratingOverEffects_Cooperative(system);"
    "off-side camera assembly finishes before publication admission")
require_slice_ordered(
    _set_camera_source
    "FX_BeginIteratingOverEffects_Cooperative(system);"
    "FX_BeginWritingCameraPublication(system);"
    "current-camera publication nests its writer gate inside main ownership")
require_slice_ordered(
    _set_camera_source
    "FX_BeginWritingCameraPublication(system);"
    "FX_PublishCamera(&system->camera, nextCamera, true);"
    "current camera publishes only after writer exclusion")
require_slice_ordered(
    _set_camera_source
    "FX_PublishCamera(&system->camera, nextCamera, true);"
    "FX_EndWritingCameraPublication(system);"
    "current-camera payload publication completes before writer release")
require_slice_ordered(
    _set_camera_source
    "FX_EndWritingCameraPublication(system);"
    "FX_EndIteratingOverEffects_Cooperative(system);"
    "current-camera writer exclusion releases before main ownership")
require_literal_count(
    _set_camera_source
    "FX_BeginIteratingOverEffects_Cooperative(system);"
    1
    "FX_SetNextUpdateCamera has one cooperative admission")
require_literal_count(
    _set_camera_source
    "FX_EndIteratingOverEffects_Cooperative(system);"
    1
    "FX_SetNextUpdateCamera has one cooperative release")
extract_between(
    _set_camera_source
    "FX_BeginWritingCameraPublication(system);"
    "FX_EndWritingCameraPublication(system);"
    _set_camera_owned_interval
    "FX_SetNextUpdateCamera publication interval")
require_slice_not_contains(
    _set_camera_owned_interval
    "nextCamera."
    "off-side camera construction cannot continue during publication")

extract_source_slice(
    _fx_update_source
    "void __cdecl FX_SetNextUpdateTime("
    "void __cdecl FX_FillUpdateCmd("
    _set_time_source
    "FX_SetNextUpdateTime")
require_slice_ordered(
    _set_time_source
    "FX_BeginIteratingOverEffects_Cooperative(system);"
    "FX_BeginWritingCameraPublication(system);"
    "time publication nests its writer gate inside main ownership")
require_slice_ordered(
    _set_time_source
    "FX_BeginWritingCameraPublication(system);"
    "FX_InvalidateCameraPublication(&system->camera);"
    "time publication owns exclusive camera access before invalidation")
require_slice_ordered(
    _set_time_source
    "FX_InvalidateCameraPublication(&system->camera);"
    "Sys_AtomicExchange(&system->msecDraw, time);"
    "camera invalidation precedes publication of the draw time")
require_slice_ordered(
    _set_time_source
    "system->frameCount = 1;"
    "FX_EndWritingCameraPublication(system);"
    "time and frame publication complete before writer release")
require_slice_ordered(
    _set_time_source
    "FX_EndWritingCameraPublication(system);"
    "FX_EndIteratingOverEffects_Cooperative(system);"
    "time-publication writer exclusion releases before main ownership")
require_literal_count(
    _set_time_source
    "FX_BeginIteratingOverEffects_Cooperative(system);"
    1
    "FX_SetNextUpdateTime has one cooperative admission")
require_literal_count(
    _set_time_source
    "FX_EndIteratingOverEffects_Cooperative(system);"
    1
    "FX_SetNextUpdateTime has one cooperative release")

# Camera readers acquire main cooperative ownership first and then the shared
# camera gate before checking the marker or consuming payload.
extract_source_slice(
    _fx_draw_source
    "void __cdecl FX_DrawNonSpriteElems("
    "void __cdecl FX_BeginIteratingOverEffects_Cooperative("
    _draw_non_sprite_source
    "FX_DrawNonSpriteElems")
require_slice_ordered(
    _draw_non_sprite_source
    "FX_BeginIteratingOverEffects_Cooperative(system);"
    "FX_BeginReadingCameraPublication(system);"
    "non-sprite draw nests camera-read ownership inside main admission")
require_slice_ordered(
    _draw_non_sprite_source
    "FX_BeginReadingCameraPublication(system);"
    "Sys_AtomicLoad(&system->camera.isValid)"
    "non-sprite draw checks the camera only after shared exclusion")
require_slice_ordered(
    _draw_non_sprite_source
    "FX_DrawNonSpriteEffect(system, effect, 1u, system->msecDraw);"
    "FX_EndReadingCameraPublication(system);"
    "non-sprite draw retains camera ownership through payload use")
require_slice_ordered(
    _draw_non_sprite_source
    "FX_EndReadingCameraPublication(system);"
    "FX_EndIteratingOverEffects_Cooperative(system);"
    "non-sprite draw releases its camera gate before main ownership")
require_literal_count(
    _draw_non_sprite_source
    "FX_BeginIteratingOverEffects_Cooperative(system);"
    1
    "non-sprite draw has one cooperative admission")
require_literal_count(
    _draw_non_sprite_source
    "FX_EndIteratingOverEffects_Cooperative(system);"
    1
    "non-sprite draw has one cooperative release")

extract_source_slice(
    _fx_draw_source
    "void __cdecl FX_DrawSpotLight("
    "void __cdecl FX_DrawSpotLightEffect("
    _draw_spotlight_source
    "FX_DrawSpotLight")
require_slice_ordered(
    _draw_spotlight_source
    "FX_BeginIteratingOverEffects_Cooperative(system);"
    "FX_BeginReadingCameraPublication(system);"
    "spotlight draw nests camera-read ownership inside main admission")
require_slice_ordered(
    _draw_spotlight_source
    "FX_BeginReadingCameraPublication(system);"
    "Sys_AtomicLoad(&system->camera.isValid)"
    "spotlight draw checks the camera only after shared exclusion")
require_slice_matches(
    _draw_spotlight_source
    "FX_EndReadingCameraPublication\\(system\\); *FX_EndIteratingOverEffects_Cooperative\\(system\\); *FX_DropCorruptDrawList\\(\"invalid spotlight count pair\"\\)"
    "spotlight rejection releases nested camera then main ownership before dropping")
extract_source_tail(
    _draw_spotlight_source
    "FX_DrawSpotLightEffect(system, v1, msecDraw);"
    _draw_spotlight_after_payload
    "FX_DrawSpotLight payload interval")
require_slice_contains(
    _draw_spotlight_after_payload
    "FX_EndReadingCameraPublication(system);"
    "spotlight draw retains camera ownership through its last payload use")
require_slice_ordered(
    _draw_spotlight_after_payload
    "FX_EndReadingCameraPublication(system);"
    "FX_EndIteratingOverEffects_Cooperative(system);"
    "spotlight draw releases its camera gate before main ownership")
require_literal_count(
    _draw_spotlight_source
    "FX_EndIteratingOverEffects_Cooperative(system);"
    2
    "spotlight draw covers normal and corrupt-count releases")
require_literal_count(
    _draw_spotlight_source
    "FX_BeginIteratingOverEffects_Cooperative(system);"
    1
    "spotlight draw has one cooperative admission")

extract_source_slice(
    _fx_draw_source
    "void __cdecl FX_DrawSpriteElems("
    "void __cdecl FX_DrawTrailsForEffect("
    _draw_sprite_source
    "FX_DrawSpriteElems")
require_slice_ordered(
    _draw_sprite_source
    "FX_BeginIteratingOverEffects_Cooperative(system);"
    "FX_BeginReadingCameraPublication(system);"
    "sprite draw nests camera-read ownership inside main admission")
require_slice_ordered(
    _draw_sprite_source
    "FX_BeginReadingCameraPublication(system);"
    "Sys_AtomicLoad(&system->camera.isValid)"
    "sprite draw checks the camera only after shared exclusion")
require_slice_ordered(
    _draw_sprite_source
    "R_AddCodeMeshDrawSurf("
    "FX_EndReadingCameraPublication(system);"
    "sprite draw retains camera ownership through queued payload publication")
require_slice_ordered(
    _draw_sprite_source
    "FX_EndReadingCameraPublication(system);"
    "FX_EndIteratingOverEffects_Cooperative(system);"
    "sprite draw releases its camera gate before main ownership")
require_literal_count(
    _draw_sprite_source
    "FX_BeginIteratingOverEffects_Cooperative(system);"
    1
    "sprite draw has one outer cooperative admission")
require_literal_count(
    _draw_sprite_source
    "FX_EndIteratingOverEffects_Cooperative(system);"
    1
    "sprite draw has one outer cooperative release")

# Mark generation is another camera consumer. Impact-mark setup takes a short
# owned copy of viewOffset, while entity model generation deliberately spans
# its public Begin/End pair so XModel, DObj, and brush helpers share one pin.
extract_source_slice(
    _fx_marks_source
    "void __cdecl FX_ImpactMark_Generate("
    "void __cdecl FX_ImpactMark_Generate_AddEntityBrush("
    _impact_mark_generate_source
    "FX_ImpactMark_Generate")
require_slice_ordered(
    _impact_mark_generate_source
    "FX_BeginIteratingOverEffects_Cooperative(System);"
    "FX_BeginReadingCameraPublication(System);"
    "impact-mark setup nests camera-read ownership inside main admission")
require_slice_ordered(
    _impact_mark_generate_source
    "FX_BeginReadingCameraPublication(System);"
    "Vec3Copy(System->camera.viewOffset, cameraViewOffset);"
    "impact-mark setup owns shared camera access before copying view offset")
require_slice_ordered(
    _impact_mark_generate_source
    "Vec3Copy(System->camera.viewOffset, cameraViewOffset);"
    "FX_EndReadingCameraPublication(System);"
    "impact-mark setup retains camera ownership through the camera copy")
require_slice_ordered(
    _impact_mark_generate_source
    "FX_EndReadingCameraPublication(System);"
    "FX_EndIteratingOverEffects_Cooperative(System);"
    "impact-mark setup releases its camera gate before main ownership")
require_slice_ordered(
    _impact_mark_generate_source
    "FX_EndIteratingOverEffects_Cooperative(System);"
    "R_MarkFragments_Begin("
    "impact-mark setup uses only its local camera snapshot after release")
require_slice_contains(
    _impact_mark_generate_source
    "radius, cameraViewOffset, material);"
    "impact-mark fragment setup consumes the local view-offset snapshot")
require_literal_count(
    _impact_mark_generate_source
    "System->camera.viewOffset"
    1
    "impact-mark setup has one owned live camera access")
require_literal_count(
    _impact_mark_generate_source
    "FX_BeginIteratingOverEffects_Cooperative(System);"
    1
    "impact-mark setup has one cooperative admission")
require_literal_count(
    _impact_mark_generate_source
    "FX_EndIteratingOverEffects_Cooperative(System);"
    1
    "impact-mark setup has one cooperative release")

extract_between(
    _fx_marks_source
    "FX_BeginIteratingOverEffects_Cooperative( FX_GetSystem(localClientNum));"
    "FX_EndIteratingOverEffects_Cooperative( FX_GetSystem(localClientNum));"
    _entity_mark_generation_scope
    "entity mark-generation cooperative scope")
require_slice_ordered(
    _entity_mark_generation_scope
    "FX_BeginIteratingOverEffects_Cooperative( FX_GetSystem(localClientNum));"
    "FX_BeginReadingCameraPublication( FX_GetSystem(localClientNum));"
    "entity mark generation nests camera-read ownership inside main admission")
require_slice_ordered(
    _entity_mark_generation_scope
    "FX_BeginReadingCameraPublication( FX_GetSystem(localClientNum));"
    "void __cdecl FX_GenerateMarkVertsForEntXModel("
    "entity mark camera payload use begins only after shared exclusion")
require_slice_ordered(
    _entity_mark_generation_scope
    "void __cdecl FX_GenerateMarkVertsForEntBrush("
    "FX_EndReadingCameraPublication( FX_GetSystem(localClientNum));"
    "entity mark generation retains shared exclusion through all readers")
require_slice_ordered(
    _entity_mark_generation_scope
    "FX_EndReadingCameraPublication( FX_GetSystem(localClientNum));"
    "FX_EndIteratingOverEffects_Cooperative( FX_GetSystem(localClientNum));"
    "entity mark generation releases its camera gate before main ownership")
foreach(_entity_camera_reader IN ITEMS
    "void __cdecl FX_GenerateMarkVertsForEntXModel("
    "void __cdecl FX_GenerateMarkVertsForEntDObj("
    "void __cdecl FX_GenerateMarkVertsForEntBrush(")
    require_slice_contains(
        _entity_mark_generation_scope
        "${_entity_camera_reader}"
        "the public entity mark scope covers every entity camera reader")
endforeach()
require_literal_count(
    _entity_mark_generation_scope
    "&camera->camera"
    3
    "XModel, DObj, and brush mark generation share the owned camera")
require_slice_ordered(
    _entity_mark_generation_scope
    "void __cdecl FX_GenerateMarkVertsForEntXModel("
    "void __cdecl FX_GenerateMarkVertsForEntDObj("
    "entity XModel camera use remains inside the public generation scope")
require_slice_ordered(
    _entity_mark_generation_scope
    "void __cdecl FX_GenerateMarkVertsForEntDObj("
    "void __cdecl FX_GenerateMarkVertsForEntBrush("
    "entity DObj camera use remains inside the public generation scope")
require_literal_count(
    _entity_mark_generation_scope
    "FX_BeginIteratingOverEffects_Cooperative( FX_GetSystem(localClientNum));"
    1
    "entity mark generation has one public cooperative admission")
require_literal_count(
    _entity_mark_generation_scope
    "FX_EndIteratingOverEffects_Cooperative( FX_GetSystem(localClientNum));"
    1
    "entity mark generation has one public cooperative release")

extract_source_slice(
    _fx_marks_source
    "void __cdecl FX_GenerateMarkVertsForStaticModels("
    "void __cdecl FX_ExpandMarkVerts_NoTransform_GfxPackedVertex_("
    _static_mark_generation_source
    "FX_GenerateMarkVertsForStaticModels")
require_slice_ordered(
    _static_mark_generation_source
    "FX_BeginIteratingOverEffects_Cooperative(system);"
    "FX_BeginReadingCameraPublication(system);"
    "static-model marks nest camera-read ownership inside main admission")
require_slice_ordered(
    _static_mark_generation_source
    "FX_BeginReadingCameraPublication(system);"
    "&camera->camera"
    "static-model marks own shared camera access before generating vertices")
require_slice_ordered(
    _static_mark_generation_source
    "&camera->camera"
    "FX_EndReadingCameraPublication(system);"
    "static-model marks retain camera ownership through vertex generation")
require_slice_ordered(
    _static_mark_generation_source
    "FX_EndReadingCameraPublication(system);"
    "FX_EndIteratingOverEffects_Cooperative(system);"
    "static-model marks release their camera gate before main ownership")
require_literal_count(
    _static_mark_generation_source
    "FX_BeginIteratingOverEffects_Cooperative(system);"
    1
    "static-model mark generation has one cooperative admission")
require_literal_count(
    _static_mark_generation_source
    "FX_EndIteratingOverEffects_Cooperative(system);"
    1
    "static-model mark generation has one cooperative release")

extract_source_slice(
    _fx_marks_source
    "void __cdecl FX_GenerateMarkVertsForWorld("
    "char __cdecl FX_GenerateMarkVertsForList_WorldBrush("
    _world_mark_generation_source
    "FX_GenerateMarkVertsForWorld")
require_slice_ordered(
    _world_mark_generation_source
    "FX_BeginIteratingOverEffects_Cooperative(System);"
    "FX_BeginReadingCameraPublication(System);"
    "world marks nest camera-read ownership inside main admission")
require_slice_ordered(
    _world_mark_generation_source
    "FX_BeginReadingCameraPublication(System);"
    "&System->camera"
    "world marks own shared camera access before generating vertices")
require_slice_ordered(
    _world_mark_generation_source
    "&System->camera"
    "FX_EndReadingCameraPublication(System);"
    "world marks retain camera ownership through vertex generation")
require_slice_ordered(
    _world_mark_generation_source
    "FX_EndReadingCameraPublication(System);"
    "FX_EndIteratingOverEffects_Cooperative(System);"
    "world marks release their camera gate before main ownership")
require_literal_count(
    _world_mark_generation_source
    "FX_BeginIteratingOverEffects_Cooperative(System);"
    1
    "world mark generation has one cooperative admission")
require_literal_count(
    _world_mark_generation_source
    "FX_EndIteratingOverEffects_Cooperative(System);"
    1
    "world mark generation has one cooperative release")

# Sprite insertion computes both new and existing element distances from the
# previous camera. The public sorter holds one shared camera scope across the
# helper that performs both payload reads; the gate API proves main ownership.
extract_source_slice(
    _fx_sort_source
    "void __cdecl FX_SortNewElemsInEffect("
    "void __cdecl FX_SortSpriteElemIntoEffect("
    _sort_new_elems_source
    "FX_SortNewElemsInEffect")
require_slice_ordered(
    _sort_new_elems_source
    "FX_BeginReadingCameraPublication(system);"
    "FX_SortSpriteElemIntoEffect(system, effect, &elema->item);"
    "new-element sorting owns shared camera access before sprite insertion")
require_slice_ordered(
    _sort_new_elems_source
    "FX_SortSpriteElemIntoEffect(system, effect, &elema->item);"
    "FX_EndReadingCameraPublication(system);"
    "new-element sorting retains camera ownership through every insertion")
require_literal_count(
    _sort_new_elems_source
    "FX_BeginReadingCameraPublication(system);"
    1
    "new-element sorting has one shared camera admission")
require_literal_count(
    _sort_new_elems_source
    "FX_EndReadingCameraPublication(system);"
    1
    "new-element sorting has one shared camera release")

extract_source_slice(
    _fx_sort_source
    "void __cdecl FX_SortSpriteElemIntoEffect("
    "void __cdecl FX_GetInsertSortElem("
    _sort_sprite_elem_source
    "FX_SortSpriteElemIntoEffect")
require_slice_ordered(
    _sort_sprite_elem_source
    "FX_GetInsertSortElem(system, effect, elem, &sortElem);"
    "FX_ExistingElemSortsBeforeNewElem(system, effect, nextElem, &sortElem)"
    "the gated insertion covers new-camera distance before existing distances")

# Spawn culling consumes the previous camera under a shared camera gate. The
# Begin API above enforces that each of these inner scopes already owns the
# exact main cooperative iterator supplied by its update/spawn caller.
extract_source_slice(
    _fx_system_source
    "static FxEffect* FX_SpawnEffect_Internal("
    "FxEffect* __cdecl FX_SpawnEffect("
    _spawn_effect_internal_source
    "FX_SpawnEffect_Internal")
require_slice_ordered(
    _spawn_effect_internal_source
    "FX_BeginReadingCameraPublication(system);"
    "FX_CullEffectForSpawn(&system->cameraPrev, remoteDef, origin);"
    "effect-spawn culling enters the camera gate before previous-camera use")
require_slice_ordered(
    _spawn_effect_internal_source
    "FX_CullEffectForSpawn(&system->cameraPrev, remoteDef, origin);"
    "FX_EndReadingCameraPublication(system);"
    "effect-spawn culling releases the camera gate after payload use")

extract_source_slice(
    _fx_system_source
    "FxEffect* __cdecl FX_SpawnEffect("
    "void __cdecl FX_AddRefToEffect("
    _spawn_effect_public_source
    "FX_SpawnEffect")
require_slice_ordered(
    _spawn_effect_public_source
    "FX_BeginIteratingOverEffects_Cooperative(system);"
    "FX_SpawnEffect_Internal("
    "effect spawn owns main cooperative admission before its gated cull")
require_slice_ordered(
    _spawn_effect_public_source
    "FX_SpawnEffect_Internal("
    "FX_EndIteratingOverEffects_Cooperative(system);"
    "effect spawn retains main ownership through its gated cull")

extract_source_slice(
    _fx_system_source
    "void __cdecl FX_SpawnTrailElem_Cull("
    "bool __cdecl FX_CullTrailElem("
    _spawn_trail_elem_cull_source
    "FX_SpawnTrailElem_Cull")
require_slice_ordered(
    _spawn_trail_elem_cull_source
    "FX_BeginReadingCameraPublication(system);"
    "&system->cameraPrev"
    "trail-element culling enters the camera gate before previous-camera use")
require_slice_ordered(
    _spawn_trail_elem_cull_source
    "&system->cameraPrev"
    "FX_EndReadingCameraPublication(system);"
    "trail-element culling releases the camera gate after payload use")

extract_source_slice(
    _fx_system_source
    "void __cdecl FX_SpawnElem("
    "FxPool<FxElem> *__cdecl FX_AllocElem("
    _spawn_elem_source
    "FX_SpawnElem")
require_slice_ordered(
    _spawn_elem_source
    "FX_BeginReadingCameraPublication(system);"
    "&system->cameraPrev"
    "element culling enters the camera gate before previous-camera use")
require_slice_ordered(
    _spawn_elem_source
    "&system->cameraPrev"
    "FX_EndReadingCameraPublication(system);"
    "element culling releases the camera gate after payload use")

# Camera culling must sample the publication marker atomically before any
# plane-count or frustum payload access. Its callers provide ownership.
extract_source_slice(
    _fx_update_util_source
    "char __cdecl FX_CullSphere("
    "void __cdecl FX_GetElemAxis("
    _cull_sphere_source
    "FX_CullSphere")
require_slice_ordered(
    _cull_sphere_source
    "Sys_AtomicLoad(&camera->isValid)"
    "camera->frustumPlaneCount"
    "sphere culling atomically checks validity before camera metadata")
require_slice_ordered(
    _cull_sphere_source
    "Sys_AtomicLoad(&camera->isValid)"
    "camera->frustum[planeIndex]"
    "sphere culling atomically checks validity before frustum payload")
require_slice_not_contains(
    _cull_sphere_source
    "if (!camera->isValid)"
    "sphere culling cannot read the publication marker non-atomically")

extract_source_slice(
    _fx_draw_source
    "char __cdecl FX_CullCylinder("
    "void __cdecl FX_DrawElem_Cloud("
    _cull_cylinder_source
    "FX_CullCylinder")
require_slice_ordered(
    _cull_cylinder_source
    "Sys_AtomicLoad(&camera->isValid)"
    "camera->frustumPlaneCount"
    "cylinder culling atomically checks validity before camera metadata")
require_slice_ordered(
    _cull_cylinder_source
    "Sys_AtomicLoad(&camera->isValid)"
    "camera->frustum[planeIndex]"
    "cylinder culling atomically checks validity before frustum payload")
require_slice_not_contains(
    _cull_cylinder_source
    "if (!camera->isValid)"
    "cylinder culling cannot read the publication marker non-atomically")

# Visibility publication/read scopes remain owned from selector/scalar sampling
# through the final blocker swap or blocker payload access.
extract_source_slice(
    _fx_draw_source
    "void __cdecl FX_GenerateVerts("
    "void __cdecl FX_FillGenerateVertsCmd("
    _generate_verts_source
    "FX_GenerateVerts")
require_slice_ordered(
    _generate_verts_source
    "FX_BeginIteratingOverEffects_Cooperative(localSystem);"
    "FX_BeginReadingCameraPublication(localSystem);"
    "vertex generation nests camera-read ownership inside main admission")
require_slice_ordered(
    _generate_verts_source
    "FX_BeginReadingCameraPublication(localSystem);"
    "drawTime = localSystem->msecDraw;"
    "vertex generation owns the camera gate before draw payload reads")
require_slice_ordered(
    _generate_verts_source
    "FX_ToggleVisBlockerFrame(localSystem);"
    "FX_EndReadingCameraPublication(localSystem);"
    "vertex generation retains camera ownership through nested readers")
require_slice_ordered(
    _generate_verts_source
    "FX_EndReadingCameraPublication(localSystem);"
    "FX_EndIteratingOverEffects_Cooperative(localSystem);"
    "vertex generation releases its camera gate before main ownership")

extract_source_slice(
    _fx_update_util_source
    "void __cdecl FX_AddVisBlocker("
    "void __cdecl FX_ToggleVisBlockerFrame("
    _add_vis_blocker_source
    "FX_AddVisBlocker")
require_slice_ordered(
    _add_vis_blocker_source
    "FX_CurrentThreadOwnsCooperativeIterator(system)"
    "visState = system->visStateBufferWrite;"
    "visibility appends enforce ownership before selecting their buffer")

extract_source_slice(
    _fx_update_util_source
    "void __cdecl FX_ToggleVisBlockerFrame("
    "char __cdecl FX_CullSphere("
    _toggle_vis_blocker_source
    "FX_ToggleVisBlockerFrame")
require_slice_ordered(
    _toggle_vis_blocker_source
    "FX_CurrentThreadOwnsCooperativeIterator(system)"
    "visStateSwapCache = (FxVisState *)system->visStateBufferRead;"
    "visibility swaps enforce ownership before touching selectors")

extract_source_slice(
    _fx_system_source
    "double __cdecl FX_GetClientVisibility("
    "double FX_GetServerVisibility("
    _client_visibility_source
    "FX_GetClientVisibility")
require_slice_ordered(
    _client_visibility_source
    "FX_TryBeginIteratingOverEffects_Cooperative(system)"
    "visState = system->visStateBufferRead;"
    "visibility reads own the system before sampling the read selector")
require_slice_ordered(
    _client_visibility_source
    "visBlocker = &visState->blocker[blockerIndex];"
    "FX_EndIteratingOverEffects_Cooperative(system);"
    "visibility reads retain ownership through blocker payloads")
extract_between(
    _client_visibility_source
    "visState = system->visStateBufferRead;"
    "FX_EndIteratingOverEffects_Cooperative(system);"
    _client_visibility_owned_interval
    "FX_GetClientVisibility owned interval")
require_slice_not_contains(
    _client_visibility_owned_interval
    "return "
    "visibility readers cannot bypass cooperative release")

# Neutral-result queries use a report-free admission attempt. It may inspect
# gate/generation sidecars before admission, but mutable FxSystem state is
# sampled only after the reader count is owned. A failed postcheck must roll
# that count back, and an impossible rollback failure is fail-closed.
extract_source_slice(
    _fx_draw_source
    "if (FX_ArchiveGateIsActive(system) || FX_EffectKillGateIsActive(system) || !FxIteratorTryBeginCooperative(&system->iteratorCount))"
    "bool __cdecl FX_DowngradeEffectKillExclusiveToCooperative("
    _try_cooperative_admission_source
    "report-free cooperative iterator admission")
require_slice_ordered(
    _try_cooperative_admission_source
    "FxIteratorTryBeginCooperative(&system->iteratorCount)"
    "const bool initialized = system->isInitialized != 0;"
    "report-free admission owns the reader count before mutable state")
require_slice_ordered(
    _try_cooperative_admission_source
    "const bool initialized = system->isInitialized != 0;"
    "const bool archiveActive = FX_ArchiveGateIsActive(system);"
    "report-free admission rechecks the archive race after mutable sampling")
require_slice_ordered(
    _try_cooperative_admission_source
    "FxIteratorEndCooperative(&system->iteratorCount, &remaining)"
    "failed to roll back report-free cooperative iterator admission"
    "failed report-free admission cannot silently strand reader ownership")

# Archive validation delegates the active-frame busy rule and exact pre-draw
# canonical-reset policy to the portable readiness helper.
extract_source_slice(
    _fx_archive_source
    "bool FX_ValidateArchiveSystemState("
    "bool FX_BuildArchiveExpectedTokens("
    _archive_system_validation_source
    "FX_ValidateArchiveSystemState")
require_slice_contains(
    _archive_system_validation_source
    "FX_AreArchiveCamerasReady( system->camera, system->cameraPrev, system->msecDraw)"
    "archive validation must use the portable camera readiness policy")
require_slice_not_contains(
    _archive_system_validation_source
    "Sys_AtomicLoad(&system->camera"
    "archive validation cannot duplicate publication-marker policy")

# Save owns one heap workspace containing distinct serialized and validation
# systems, copied buffers, graph scratch, and the exact nested physics scratch.
extract_source_slice(
    _fx_archive_source
    "struct FxArchiveSaveSnapshotWorkspace"
    "struct FxArchiveRestorePhysicsScratch"
    _save_workspace_source
    "FxArchiveSaveSnapshotWorkspace")
foreach(_workspace_member IN ITEMS
    "FxSystem serializedSystem{};"
    "FxSystem validationSystem{};"
    "FxSystemBuffers buffers{};"
    "FxArchivePoolAllocationStates allocationStates{};"
    "FxPoolAllocationGraphScratch poolGraph{};"
    "FxArchivePhysicsOwnershipScratch physics{};"
    "std::uint8_t readVisibilitySelector = 0;"
    "std::uint8_t writeVisibilitySelector = 0;")
    require_slice_contains(
        _save_workspace_source
        "${_workspace_member}"
        "save workspace retains its bounded staged member layout")
endforeach()

extract_source_slice(
    _fx_archive_source
    "bool FX_ValidateArchiveCopiedSnapshot("
    "// The archive restore transaction owns CRITSECT_PHYSICS"
    _copied_snapshot_source
    "FX_ValidateArchiveCopiedSnapshot")
require_slice_ordered(
    _copied_snapshot_source
    "FX_TryDeriveVisibilitySelectors("
    "std::memcpy( validationSystem, serializedSystem, sizeof(*validationSystem));"
    "raw visibility selectors validate before staged relinking")
require_slice_ordered(
    _copied_snapshot_source
    "std::memcpy( validationSystem, serializedSystem, sizeof(*validationSystem));"
    "FX_LinkSystemBuffers(validationSystem, bufferSnapshot);"
    "only the separate validation system is relinked")
require_slice_ordered(
    _copied_snapshot_source
    "FX_LinkSystemBuffers(validationSystem, bufferSnapshot);"
    "validationSystem->visStateBufferRead = &bufferSnapshot->visState[readSelector];"
    "the staged read selector resolves against copied storage")
require_slice_ordered(
    _copied_snapshot_source
    "validationSystem->visStateBufferRead = &bufferSnapshot->visState[readSelector];"
    "validationSystem->visStateBufferWrite = &bufferSnapshot->visState[writeSelector];"
    "the staged selectors retain distinct read/write roles")
require_slice_ordered(
    _copied_snapshot_source
    "validationSystem->visStateBufferWrite = &bufferSnapshot->visState[writeSelector];"
    "FX_ValidateArchiveEffectDefinitionReferences("
    "staged selectors are complete before effect-definition admission")
require_slice_ordered(
    _copied_snapshot_source
    "FX_ValidateArchiveEffectDefinitionReferences("
    "FX_RebuildArchivePoolAllocationStates("
    "effect definitions are admitted before copied graph traversal")
require_slice_not_contains(
    _copied_snapshot_source
    "FX_LinkSystemBuffers(serializedSystem"
    "the serialized legacy pointer image must never be relinked")
require_slice_ordered(
    _copied_snapshot_source
    "FX_RebuildArchivePoolAllocationStates("
    "FX_CollectArchivePhysicsEntries("
    "copied allocation sidecars rebuild before semantic collection")
require_slice_matches(
    _copied_snapshot_source
    "FX_CollectArchivePhysicsEntries\\( *validationSystem, *physicsEntries, *physicsEntryCapacity, *&physicsEntryCount, *false, *nullptr, *&spotLightBoltDobj\\)"
    "copied semantic collection must not capture live physics states")

extract_source_slice(
    _fx_archive_source
    "void __cdecl FX_Save("
    "void __cdecl FX_Archive("
    _fx_save_source
    "FX_Save")
require_slice_ordered(
    _fx_save_source
    "FX_StageEffectTableNoDrop(&effectTableStaging)"
    "if (!FX_BeginArchive(system))"
    "the effect table is fully staged before archive exclusion")
require_slice_ordered(
    _fx_save_source
    "FX_AllocateArchiveSaveSnapshotWorkspace();"
    "if (!FX_BeginArchive(system))"
    "save allocates all checked workspace before archive exclusion")
require_slice_ordered(
    _fx_save_source
    "FX_ValidateArchiveExclusiveState(system);"
    "memcpy(&systemSnapshot, system, sizeof(systemSnapshot));"
    "raw system capture requires proven exclusive ownership")
require_slice_ordered(
    _fx_save_source
    "memcpy(&systemSnapshot, system, sizeof(systemSnapshot));"
    "memcpy(bufferSnapshot, systemBuffers, sizeof(*bufferSnapshot));"
    "raw system and buffer images are copied in one order")
require_slice_ordered(
    _fx_save_source
    "memcpy(bufferSnapshot, systemBuffers, sizeof(*bufferSnapshot));"
    "Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);"
    "both copies finish before the allocator interval closes")
require_slice_ordered(
    _fx_save_source
    "Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);"
    "FX_ValidateArchiveCopiedSnapshot("
    "copied graph validation starts only after raw capture")
extract_between(
    _fx_save_source
    "if (!FX_BeginArchive(system))"
    "FX_ValidateArchiveCopiedSnapshot("
    _fx_save_prevalidation_interval
    "FX_Save raw capture interval")
foreach(_forbidden_precopy_traversal IN ITEMS
    "FX_RebuildArchivePoolAllocationStates("
    "FX_CollectArchivePhysicsEntries("
    "FX_CaptureArchivePhysicsStates(")
    require_slice_not_contains(
        _fx_save_prevalidation_interval
        "${_forbidden_precopy_traversal}"
        "save cannot traverse graph or physics before raw capture completes")
endforeach()
require_slice_ordered(
    _fx_save_source
    "FX_ValidateArchiveCopiedSnapshot("
    "FX_CaptureArchivePhysicsStates("
    "staged graph collection precedes the live physics cross-check")
require_slice_ordered(
    _fx_save_source
    "FX_CaptureArchivePhysicsStates("
    "if (!copiedSnapshotValid)"
    "save rejects either staged or live cross-check failure")
require_slice_not_contains(
    _fx_save_source
    "FX_LinkSystemBuffers(&systemSnapshot"
    "serialized system pointers must remain the raw legacy image")

# Once workspace ownership is established, each returning path destroys it.
extract_source_tail(
    _fx_save_source
    "FxSystem &systemSnapshot = snapshotWorkspace->serializedSystem;"
    _fx_save_owned_workspace
    "FX_Save owned workspace interval")
require_literal_count(
    _fx_save_owned_workspace
    "FX_DestroyArchiveSaveSnapshotWorkspace(snapshotWorkspace);"
    7
    "all save workspace exits destroy checked storage")
require_literal_count(
    _fx_save_owned_workspace
    "return;"
    8
    "owned returns occur only after a per-path or final workspace destruction")
extract_source_tail(
    _fx_save_owned_workspace
    "const FxEffectTableSaveOutcome effectTableWriteOutcome ="
    _fx_save_write_interval
    "FX_Save staged write interval")
require_slice_ordered(
    _fx_save_write_interval
    "FX_WriteStagedEffectTableNoDrop(&effectTableStaging, memFile)"
    "FX_ARCHIVE_SYSTEM_SIZE, &systemSnapshot"
    "the legacy effect table remains first in the staged wire image")
require_slice_ordered(
    _fx_save_write_interval
    "FX_DestroyArchiveSaveSnapshotWorkspace(snapshotWorkspace);"
    "if (effectTableWriteOutcome"
    "the final workspace is destroyed before reporting table-write failure")
require_slice_ordered(
    _fx_save_write_interval
    "FX_DestroyArchiveSaveSnapshotWorkspace(snapshotWorkspace);"
    "if (!archiveWritten)"
    "the final workspace is destroyed before reporting write failure")

extract_source_slice(
    _fx_archive_source
    "void FX_DestroyArchiveSaveSnapshotWorkspace("
    "[[nodiscard]] FxArchiveRestoreTransactionWorkspace *"
    _destroy_save_workspace_source
    "FX_DestroyArchiveSaveSnapshotWorkspace")
require_slice_ordered(
    _destroy_save_workspace_source
    "fx::archive::DestroyArchiveRestoreWorkspace("
    "std::abort();"
    "save workspace destruction must fail-stop if nested cleanup is refused")
