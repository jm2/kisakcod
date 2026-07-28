cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

function(load_text RELATIVE_PATH OUT_VARIABLE)
    set(_path "${SOURCE_ROOT}/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing last-four reconciliation input: ${_path}")
    endif()
    file(READ "${_path}" _source)
    string(REPLACE "\r\n" "\n" _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(normalize SOURCE_VARIABLE OUT_VARIABLE)
    string(REGEX REPLACE "[ \t\r\n]+" " " _normalized
        "${${SOURCE_VARIABLE}}")
    set(${OUT_VARIABLE} "${_normalized}" PARENT_SCOPE)
endfunction()

function(require_contains HAYSTACK_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${HAYSTACK_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing last-four reconciliation invariant (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains HAYSTACK_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${HAYSTACK_VARIABLE}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden last-four reconciliation regression (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(require_count HAYSTACK_VARIABLE NEEDLE EXPECTED DESCRIPTION)
    set(_remaining "${${HAYSTACK_VARIABLE}}")
    set(_count 0)
    string(LENGTH "${NEEDLE}" _needle_length)
    if(_needle_length EQUAL 0)
        message(FATAL_ERROR "Empty count needle (${DESCRIPTION})")
    endif()
    while(TRUE)
        string(FIND "${_remaining}" "${NEEDLE}" _position)
        if(_position EQUAL -1)
            break()
        endif()
        math(EXPR _count "${_count} + 1")
        math(EXPR _next "${_position} + ${_needle_length}")
        string(SUBSTRING "${_remaining}" ${_next} -1 _remaining)
    endwhile()
    if(NOT _count EQUAL EXPECTED)
        message(FATAL_ERROR
            "Unexpected last-four reconciliation count (${DESCRIPTION}): "
            "expected ${EXPECTED}, found ${_count}")
    endif()
endfunction()

function(extract_slice
    SOURCE_VARIABLE START_MARKER END_MARKER OUT_VARIABLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${START_MARKER}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR
            "Missing start of last-four slice (${DESCRIPTION}): "
            "'${START_MARKER}'")
    endif()
    string(SUBSTRING "${${SOURCE_VARIABLE}}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${END_MARKER}" _relative_end)
    if(_relative_end LESS_EQUAL 0)
        message(FATAL_ERROR
            "Missing ordered end of last-four slice (${DESCRIPTION}): "
            "'${END_MARKER}'")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_relative_end} _slice)
    string(REGEX REPLACE "[ \t\r\n]+" " " _slice "${_slice}")
    set(${OUT_VARIABLE} "${_slice}" PARENT_SCOPE)
endfunction()

function(require_ordered HAYSTACK_VARIABLE FIRST SECOND DESCRIPTION)
    string(FIND "${${HAYSTACK_VARIABLE}}" "${FIRST}" _first)
    string(FIND "${${HAYSTACK_VARIABLE}}" "${SECOND}" _second)
    if(_first EQUAL -1
        OR _second EQUAL -1
        OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered last-four invariant (${DESCRIPTION})")
    endif()
endfunction()

load_text("src/bgame/bg_local.h" _bg_local)
load_text("src/aim_assist/aim_target.cpp" _aim_target)
load_text("src/bgame/bg_pmove.cpp" _bg_pmove)
load_text("src/bgame/bg_weapons.cpp" _bg_weapons)
load_text("src/cgame/cg_consolecmds.cpp" _cg_consolecmds)
load_text("src/cgame_mp/cg_compassfriendlies_mp.cpp" _cg_compassfriendlies_mp)
load_text("src/cgame_mp/cg_predict_mp.cpp" _cg_predict_mp)
load_text("src/cgame/cg_draw.cpp" _cg_draw)
load_text("src/cgame/cg_main.cpp" _cg_main)
load_text("src/cgame/cg_modelpreviewer.cpp" _cg_modelpreviewer)
load_text("src/cgame/cg_newdraw.cpp" _cg_newdraw)
load_text("src/cgame/cg_predict.cpp" _cg_predict)
load_text("src/cgame/cg_scoreboard.cpp" _cg_scoreboard)
load_text("src/cgame/cg_view.cpp" _cg_view)
load_text("src/client/cl_input.cpp" _cl_input)
load_text("src/game/g_active.cpp" _g_active)
load_text("src/game/g_client.cpp" _g_client)
load_text("src/game/g_combat.cpp" _g_combat)
load_text("src/game/g_main.cpp" _g_main)
load_text("src/game/g_utils.cpp" _g_utils)
load_text("src/game_mp/player_use_mp.cpp" _player_use_mp)
load_text("src/game_mp/g_utils_mp.cpp" _g_utils_mp)
load_text("src/physics/phys_local.h" _phys_local)
load_text("src/physics/phys_ode.cpp" _phys_ode)
load_text("src/physics/phys_world_collision.cpp" _phys_world_collision)
load_text("src/ragdoll/ragdoll_update.cpp" _ragdoll_update)
load_text("src/gfx_d3d/r_marks.cpp" _r_marks)
load_text("tests/CMakeLists.txt" _tests_cmake)
load_text(".github/workflows/ci.yml" _ci)

if(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "sp_pm_dead_reverted")
    string(REPLACE "PM_DEAD = 0x5," "PM_DEAD = 0x4,"
        _bg_local "${_bg_local}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "pm_numeric_comparison")
    string(REPLACE "ps.pm_type != PM_DEAD" "ps.pm_type != 5"
        _cg_draw "${_cg_draw}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "pm_arithmetic_transition")
    string(REPLACE
        "self->client->ps.pm_type = self->client->ps.pm_type == PM_NORMAL_LINKED\n            ? PM_DEAD_LINKED\n            : PM_DEAD;"
        "self->client->ps.pm_type = (pmtype_t)((self->client->ps.pm_type == 1) + 5);"
        _g_combat "${_g_combat}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "pm_truthiness")
    string(REPLACE
        "ps->pm_type != PM_NORMAL && ps->pm_type != PM_NOCLIP"
        "ps->pm_type && ps->pm_type != PM_NOCLIP"
        _cg_predict_mp "${_cg_predict_mp}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "sp_physics_uses_mp_mask")
    string(REPLACE
        "inline constexpr int PHYS_WORLD_CLIPMASK = 0x280E491;"
        "inline constexpr int PHYS_WORLD_CLIPMASK = 0x2806C91;"
        _phys_local "${_phys_local}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "physics_mask_literal_restored")
    string(REPLACE
        "CM_BoxTrace(&trace, userData->savedPos, newPos, mins, maxs, 0, PHYS_WORLD_CLIPMASK);"
        "CM_BoxTrace(&trace, userData->savedPos, newPos, mins, maxs, 0, 0x2806C91);"
        _phys_ode "${_phys_ode}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "physics_integer_division")
    string(REPLACE
        "data->timeNowLerpFrac = static_cast<float>(\n            static_cast<double>(timeNow - data->timeLastSnapshot)\n            / static_cast<double>(data->timeLastUpdate - data->timeLastSnapshot));"
        "data->timeNowLerpFrac = (timeNow - data->timeLastSnapshot) / (data->timeLastUpdate - data->timeLastSnapshot);"
        _phys_ode "${_phys_ode}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "ragdoll_integer_division")
    string(REPLACE
        "lerp = static_cast<float>(\n                        static_cast<double>(body->stateMsec)\n                        / static_cast<double>(goalMsec));"
        "lerp = body->stateMsec / goalMsec;"
        _ragdoll_update "${_ragdoll_update}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "ragdoll_minus_one_sentinel")
    string(REPLACE
        "if (boneDef->animBoneNames[1])\n        {\n            if (!DObjGetBoneIndex(obj, boneDef->animBoneNames[1], &bone->animBones[1])"
        "if (boneDef->animBoneNames[1] == -1)\n        {\n            if (!DObjGetBoneIndex(obj, boneDef->animBoneNames[1], &bone->animBones[1])"
        _ragdoll_update "${_ragdoll_update}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "ragdoll_secondary_slot")
    string(REPLACE
        "|| bone->animBones[1] == 255"
        "|| bone->animBones[0] == 255"
        _ragdoll_update "${_ragdoll_update}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "ragdoll_precedence")
    string(REPLACE
        "&& (!DObjGetBoneIndex(obj, boneDef->animBoneNames[1], &boneIdx) || boneIdx == 255)"
        "&& !DObjGetBoneIndex(obj, boneDef->animBoneNames[1], &boneIdx) || boneIdx == 255"
        _ragdoll_update "${_ragdoll_update}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "marks_function_cast")
    string(REPLACE
        "AllowAllStaticModels,"
        "(int(__cdecl *)(int))CL_GetLocalClientActiveCount,"
        _r_marks "${_r_marks}")
elseif(DEFINED CONTRACT_MUTATION AND NOT CONTRACT_MUTATION STREQUAL "")
    message(FATAL_ERROR
        "Unknown last-four reconciliation mutation: ${CONTRACT_MUTATION}")
endif()

normalize(_bg_local _bg_local_normalized)
require_contains(
    _bg_local_normalized
    "enum pmtype_t : __int32 { PM_NORMAL = 0x0, PM_NORMAL_LINKED = 0x1, PM_NOCLIP = 0x2, PM_UFO = 0x3, PM_SPECTATOR = 0x4, PM_INTERMISSION = 0x5, PM_LASTSTAND = 0x6, PM_DEAD = 0x7, PM_DEAD_LINKED = 0x8, }; static_assert(std::is_same_v<std::underlying_type_t<pmtype_t>, __int32>); static_assert(PM_NORMAL == 0); static_assert(PM_NORMAL_LINKED == 1); static_assert(PM_NOCLIP == 2); static_assert(PM_UFO == 3); static_assert(PM_SPECTATOR == 4); static_assert(PM_INTERMISSION == 5); static_assert(PM_LASTSTAND == 6); static_assert(PM_DEAD == 7); static_assert(PM_DEAD_LINKED == 8);"
    "the MP movement-state ABI remains unchanged and exhaustively asserted")
require_contains(
    _bg_local_normalized
    "enum pmtype_t : __int32 { PM_NORMAL = 0x0, PM_NORMAL_LINKED = 0x1, PM_NOCLIP = 0x2, PM_UFO = 0x3, PM_MPVIEWER = 0x4, PM_DEAD = 0x5, PM_DEAD_LINKED = 0x6, }; static_assert(std::is_same_v<std::underlying_type_t<pmtype_t>, __int32>); static_assert(PM_NORMAL == 0); static_assert(PM_NORMAL_LINKED == 1); static_assert(PM_NOCLIP == 2); static_assert(PM_UFO == 3); static_assert(PM_MPVIEWER == 4); static_assert(PM_DEAD == 5); static_assert(PM_DEAD_LINKED == 6);"
    "the SP movement-state ABI includes viewer and the corrected death values")
require_count(
    _bg_local
    "static_assert(std::is_same_v<std::underlying_type_t<pmtype_t>, __int32>);"
    2
    "both profiles assert the movement-state storage width")
forbid_contains(
    _bg_local "operator--(pmtype_t"
    "movement states cannot be transitioned with enum arithmetic")

set(_pm_sources)
foreach(_pm_source IN ITEMS
    _aim_target
    _bg_pmove
    _bg_weapons
    _cg_consolecmds
    _cg_compassfriendlies_mp
    _cg_predict_mp
    _cg_draw
    _cg_main
    _cg_modelpreviewer
    _cg_newdraw
    _cg_predict
    _cg_scoreboard
    _cg_view
    _cl_input
    _g_active
    _g_client
    _g_combat
    _g_main
    _g_utils
    _player_use_mp
    _g_utils_mp)
    normalize(${_pm_source} _pm_source_normalized)
    string(APPEND _pm_sources " ${_pm_source_normalized}")
endforeach()

foreach(_state RANGE 0 8)
    foreach(_operator IN ITEMS "==" "!=" "<" "<=" ">" ">=")
        forbid_contains(
            _pm_sources "pm_type ${_operator} ${_state}"
            "movement-state comparisons use named profile constants")
    endforeach()
endforeach()
foreach(_forbidden IN ITEMS
    "!ps->pm_type"
    "!cgArray[0].predictedPlayerState.pm_type"
    "pm_type - 2"
    "(pmtype_t)v3"
    "(pmtype_t)v6"
    "pm_type == 1) + 5"
    "if (pm_type)"
    "predictedPlayerState.pm_type)"
    "ps->pm_type &&"
    "--v6->ps.pm_type")
    forbid_contains(
        _pm_sources "${_forbidden}"
        "movement-state truthiness, casts, and arithmetic cannot return")
endforeach()

normalize(_g_active _g_active_normalized)
require_contains(
    _g_active_normalized
    "if (client->noclip) { client->ps.pm_type = PM_NOCLIP; } else if (client->ufo) { client->ps.pm_type = PM_UFO; } else if (level.mpviewer) { client->ps.pm_type = PM_MPVIEWER; } else if (client->ps.stats[0] <= 0) { client->ps.pm_type = ent->tagInfo ? PM_DEAD_LINKED : PM_DEAD; } else { client->ps.pm_type = ent->tagInfo ? PM_NORMAL_LINKED : PM_NORMAL; }"
    "SP client thinking maps every mode, life, and link state explicitly")
require_contains(
    _g_active_normalized
    "if (v6->ps.pm_type == PM_NORMAL_LINKED) v6->ps.pm_type = PM_NORMAL; else if (v6->ps.pm_type == PM_DEAD_LINKED) v6->ps.pm_type = PM_DEAD;"
    "unlinking maps both linked states without enum arithmetic")

normalize(_g_combat _g_combat_normalized)
require_contains(
    _g_combat_normalized
    "self->client->ps.pm_type = self->client->ps.pm_type == PM_NORMAL_LINKED ? PM_DEAD_LINKED : PM_DEAD;"
    "SP death preserves linked state with named values")

normalize(_cg_predict _cg_predict_normalized)
require_contains(
    _cg_predict_normalized
    "pmType = PM_UFO; if (v2->current.integer == 1) pmType = PM_NOCLIP; cgameGlob->predictedPlayerState.pm_type = pmType;"
    "free movement selects named movement states")
require_contains(
    _cg_predict_normalized
    "predictedPlayerState.pm_type != PM_NORMAL && cgArray[0].predictedPlayerState.pm_type != PM_NOCLIP && cgArray[0].predictedPlayerState.pm_type != PM_UFO"
    "view smoothing uses named normal and free-movement states")

normalize(_phys_local _phys_local_normalized)
require_contains(
    _phys_local_normalized
    "#if defined(KISAK_SP) inline constexpr int PHYS_WORLD_CLIPMASK = 0x280E491; #elif defined(KISAK_MP) inline constexpr int PHYS_WORLD_CLIPMASK = 0x2806C91; #endif"
    "SP and MP expose their exact world collision masks")

normalize(_phys_ode _phys_ode_normalized)
normalize(_phys_world_collision _phys_world_collision_normalized)
normalize(_ragdoll_update _ragdoll_normalized)
require_count(
    _phys_ode "PHYS_WORLD_CLIPMASK" 1
    "physics object tracing uses the shared mask exactly once")
require_count(
    _phys_world_collision "PHYS_WORLD_CLIPMASK" 1
    "world geometry collision uses the shared mask exactly once")
require_count(
    _ragdoll_update "PHYS_WORLD_CLIPMASK" 3
    "ragdoll tracing uses the shared mask at all three calls")
foreach(_caller IN ITEMS
    _phys_ode
    _phys_world_collision
    _ragdoll_update)
    forbid_contains(
        ${_caller} "0x2806C91"
        "MP mask literals cannot be restored at physics callers")
    forbid_contains(
        ${_caller} "0x280E491"
        "SP mask literals cannot be restored at physics callers")
endforeach()

extract_slice(
    _phys_ode
    "void __cdecl Phys_RunToTime("
    "void __cdecl Phys_ObjDraw("
    _phys_run_to_time
    "Phys_RunToTime")
require_ordered(
    _phys_run_to_time
    "if (data->timeLastUpdate <= data->timeLastSnapshot)"
    "static_cast<double>(timeNow - data->timeLastSnapshot)"
    "the denominator guard precedes physics interpolation")
require_contains(
    _phys_run_to_time
    "data->timeNowLerpFrac = static_cast<float>( static_cast<double>(timeNow - data->timeLastSnapshot) / static_cast<double>(data->timeLastUpdate - data->timeLastSnapshot));"
    "physics interpolation promotes both integer differences")

extract_slice(
    _ragdoll_update
    "void __cdecl Ragdoll_SnapshotBaseLerpBones("
    "DObjAnimMat *__cdecl Ragdoll_GetDObjLocalBoneMatrix("
    _ragdoll_lerp
    "Ragdoll_SnapshotBaseLerpBones")
require_ordered(
    _ragdoll_lerp
    "if (goalMsec <= 0)"
    "static_cast<double>(body->stateMsec)"
    "the nonpositive-goal guard precedes ragdoll interpolation")
require_contains(
    _ragdoll_lerp
    "lerp = static_cast<float>( static_cast<double>(body->stateMsec) / static_cast<double>(goalMsec));"
    "ragdoll interpolation promotes both integer operands")

extract_slice(
    _ragdoll_update
    "char __cdecl Ragdoll_ValidateBodyObj("
    "void __cdecl Ragdoll_SnapshotBaseLerpOffsets("
    _ragdoll_validate
    "Ragdoll_ValidateBodyObj")
require_contains(
    _ragdoll_validate
    "boneDef->animBoneNames[1] && (!DObjGetBoneIndex(obj, boneDef->animBoneNames[1], &boneIdx) || boneIdx == 255)"
    "optional secondary validation is correctly parenthesized")

extract_slice(
    _ragdoll_update
    "char __cdecl Ragdoll_CreatePhysObj("
    "char __cdecl Ragdoll_GetDObjBaseBoneOrigin("
    _ragdoll_create
    "Ragdoll_CreatePhysObj")
require_contains(
    _ragdoll_create
    "if (boneDef->animBoneNames[1])"
    "secondary creation treats zero as no bone")
require_contains(
    _ragdoll_create
    "|| bone->animBones[1] == 255"
    "secondary creation validates the returned secondary slot")

extract_slice(
    _ragdoll_update
    "bool __cdecl Ragdoll_ExitDObjWait("
    "bool __cdecl Ragdoll_ExitIdle("
    _ragdoll_exit_wait
    "Ragdoll_ExitDObjWait")
require_contains(
    _ragdoll_exit_wait
    "if (boneDef->animBoneNames[1])"
    "DObj wait treats zero as no secondary bone")
require_contains(
    _ragdoll_exit_wait
    "|| bone->animBones[1] == 255"
    "DObj wait validates the returned secondary slot")
forbid_contains(
    _ragdoll_exit_wait
    "animBoneNames[1] == -1"
    "unsigned secondary names cannot use a negative sentinel")

normalize(_r_marks _r_marks_normalized)
require_contains(
    _r_marks_normalized
    "namespace { int __cdecl AllowAllStaticModels(int) { return 1; } }"
    "the mark filter has the exact typed callback signature")
require_contains(
    _r_marks_normalized
    "markInfo->maxs, AllowAllStaticModels, markInfo->smodelsCollided, 32);"
    "mark collection passes the typed callback directly")
forbid_contains(
    _r_marks
    "(int(__cdecl *)(int))CL_GetLocalClientActiveCount"
    "mark collection cannot cast an incompatible no-argument function")

require_count(
    _tests_cmake "NAME upstream-last-four-source-invariants" 1
    "CTest registers this focused reconciliation contract")
require_count(
    _ci "upstream-last-four-source-invariants" 1
    "measured Windows x86 runs this focused reconciliation contract")

if(NOT DEFINED CONTRACT_MUTATION OR CONTRACT_MUTATION STREQUAL "")
    foreach(_mutation IN ITEMS
        sp_pm_dead_reverted
        pm_numeric_comparison
        pm_arithmetic_transition
        pm_truthiness
        sp_physics_uses_mp_mask
        physics_mask_literal_restored
        physics_integer_division
        ragdoll_integer_division
        ragdoll_minus_one_sentinel
        ragdoll_secondary_slot
        ragdoll_precedence
        marks_function_cast)
        execute_process(
            COMMAND "${CMAKE_COMMAND}"
                "-DSOURCE_ROOT=${SOURCE_ROOT}"
                "-DCONTRACT_MUTATION=${_mutation}"
                -P "${CMAKE_CURRENT_LIST_FILE}"
            RESULT_VARIABLE _mutation_result
            OUTPUT_QUIET
            ERROR_QUIET)
        if(_mutation_result EQUAL 0)
            message(FATAL_ERROR
                "Last-four reconciliation contract accepted mutation: "
                "${_mutation}")
        endif()
    endforeach()
endif()

message(STATUS "Curated upstream last-four source contract passed")
