cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

# These are the four deliberately selected, non-navigation corrections from
# upstream 77404c6175b710d739bdc1b8aca5b395f72e5758. The objective command
# contract restores the original fixed 8c2ccbf8 complete/failed notification
# payloads after c76d429b appended an objective description.

function(load_source RELATIVE_PATH OUT_VARIABLE)
    set(_path "${SOURCE_ROOT}/src/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing safe-fixlet source: ${_path}")
    endif()
    file(READ "${_path}" _source)
    string(REPLACE "\r\n" "\n" _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(extract_slice
    SOURCE_VARIABLE START_MARKER END_MARKER OUT_VARIABLE DESCRIPTION)
    set(_source "${${SOURCE_VARIABLE}}")
    string(FIND "${_source}" "${START_MARKER}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR
            "Missing start of safe-fixlet slice (${DESCRIPTION}): "
            "'${START_MARKER}'")
    endif()

    string(SUBSTRING "${_source}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${END_MARKER}" _relative_end)
    if(_relative_end LESS_EQUAL 0)
        message(FATAL_ERROR
            "Missing ordered end of safe-fixlet slice (${DESCRIPTION}): "
            "'${END_MARKER}'")
    endif()

    string(SUBSTRING "${_tail}" 0 ${_relative_end} _slice)
    string(REGEX REPLACE "[ \t\r\n]+" " " _slice "${_slice}")
    set(${OUT_VARIABLE} "${_slice}" PARENT_SCOPE)
endfunction()

function(require_contains HAYSTACK_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${HAYSTACK_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing safe-fixlet invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains HAYSTACK_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${HAYSTACK_VARIABLE}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden safe-fixlet regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_ordered HAYSTACK_VARIABLE FIRST SECOND DESCRIPTION)
    string(FIND "${${HAYSTACK_VARIABLE}}" "${FIRST}" _first_position)
    string(FIND "${${HAYSTACK_VARIABLE}}" "${SECOND}" _second_position)
    if(_first_position EQUAL -1
        OR _second_position EQUAL -1
        OR _first_position GREATER_EQUAL _second_position)
        message(FATAL_ERROR
            "Missing or unordered safe-fixlet invariant (${DESCRIPTION})")
    endif()
endfunction()

function(require_count HAYSTACK_VARIABLE NEEDLE EXPECTED_COUNT DESCRIPTION)
    set(_remaining "${${HAYSTACK_VARIABLE}}")
    set(_count 0)
    string(LENGTH "${NEEDLE}" _needle_length)
    if(_needle_length EQUAL 0)
        message(FATAL_ERROR "Empty safe-fixlet count needle (${DESCRIPTION})")
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

    if(NOT _count EQUAL EXPECTED_COUNT)
        message(FATAL_ERROR
            "Unexpected safe-fixlet invariant count (${DESCRIPTION}): "
            "expected ${EXPECTED_COUNT}, found ${_count}")
    endif()
endfunction()

load_source("bgame/bg_weapons.cpp" _bg_weapons)
load_source("cgame/cg_snapshot.cpp" _cg_snapshot)
load_source("game/g_scr_main.cpp" _g_scr_main)
load_source("game/actor_aim.cpp" _actor_aim)

set(_ci_path "${SOURCE_ROOT}/.github/workflows/ci.yml")
if(NOT EXISTS "${_ci_path}")
    message(FATAL_ERROR "Missing CI workflow: ${_ci_path}")
endif()
file(READ "${_ci_path}" _ci)
string(REPLACE "\r\n" "\n" _ci "${_ci}")
extract_slice(
    _ci
    "- name: Run portable transaction tests"
    "- uses: actions/upload-artifact@v4"
    _measured_x86_tests
    "measured Windows x86 ctest selection")
require_count(
    _measured_x86_tests "upstream-safe-fixlets-source-invariants" 1
    "measured Windows x86 selects the safe-fixlet source contract")

# Mutation mode is used only by the self-tests at the end of this file. Each
# mutation represents a plausible regression and must make this contract fail.
if(DEFINED CONTRACT_MUTATION AND NOT CONTRACT_MUTATION STREQUAL "")
    if(CONTRACT_MUTATION STREQUAL "melee_mp_leak")
        string(REPLACE
            "#ifdef KISAK_SP\n    if ((ps->weapFlags & 8) != 0) /* g_friendlyFireDist */"
            "#if 1\n    if ((ps->weapFlags & 8) != 0) /* g_friendlyFireDist */"
            _bg_weapons "${_bg_weapons}")
    elseif(CONTRACT_MUTATION STREQUAL "melee_wrong_flag")
        string(REPLACE
            "if ((ps->weapFlags & 8) != 0) /* g_friendlyFireDist */"
            "if ((ps->weapFlags & 4) != 0) /* g_friendlyFireDist */"
            _bg_weapons "${_bg_weapons}")
    elseif(CONTRACT_MUTATION STREQUAL "snapshot_missing_unlink")
        string(REPLACE
            "            CG_UnlinkEntity(localClientNum, entityIndex);\n"
            ""
            _cg_snapshot "${_cg_snapshot}")
    elseif(CONTRACT_MUTATION STREQUAL "snapshot_late_unlink")
        string(REPLACE
            "            CG_UnlinkEntity(localClientNum, entityIndex);\n            FX_MarkEntDetachAll(localClientNum, entityIndex);"
            "            FX_MarkEntDetachAll(localClientNum, entityIndex);\n            CG_UnlinkEntity(localClientNum, entityIndex);"
            _cg_snapshot "${_cg_snapshot}")
    elseif(CONTRACT_MUTATION STREQUAL "objective_complete_append")
        string(REPLACE
            [=[v2 = va("obj_complete \"GAME_OBJECTIVECOMPLETED\"");]=]
            [=[v2 = va("obj_complete \"GAME_OBJECTIVECOMPLETED\x15%s\"", objectiveDesc);]=]
            _g_scr_main "${_g_scr_main}")
    elseif(CONTRACT_MUTATION STREQUAL "objective_failed_append")
        string(REPLACE
            [=[v2 = va("obj_failed \"GAME_OBJECTIVEFAILED\"");]=]
            [=[v2 = va("obj_failed \"GAME_OBJECTIVEFAILED\x15%s\"", objectiveDesc);]=]
            _g_scr_main "${_g_scr_main}")
    elseif(CONTRACT_MUTATION STREQUAL "actor_zero_sentinel")
        string(REPLACE
            "static float outerRadius = 6969.0f;"
            "static float outerRadius = 0.0f;"
            _actor_aim "${_actor_aim}")
    elseif(CONTRACT_MUTATION STREQUAL "actor_external_cache")
        string(REPLACE
            "static float outerRadius_0 = 6969.0f;"
            "float outerRadius_0 = 6969.0f;"
            _actor_aim "${_actor_aim}")
    elseif(CONTRACT_MUTATION STREQUAL "actor_rejected_dot")
        string(REPLACE
            "float dot = velocity[0] * dirY + (velocity[2] * dirZ - velocity[1] * dirX);"
            "float dot = velocity[0] * dirX + velocity[1] * dirY + velocity[2] * dirZ;"
            _actor_aim "${_actor_aim}")
    else()
        message(FATAL_ERROR "Unknown safe-fixlet mutation: ${CONTRACT_MUTATION}")
    endif()
endif()

# Melee suppression must match the established friendly-fire weapon flag, be
# confined to KISAK_SP, and return before any melee state can be initiated.
extract_slice(
    _bg_weapons
    "int32_t __cdecl PM_Weapon_ShouldBeFiring("
    "void __cdecl PM_Weapon_FireWeapon("
    _weapon_firing
    "PM_Weapon_ShouldBeFiring")
require_contains(
    _weapon_firing
    [=[#ifdef KISAK_SP if ((ps->weapFlags & 8) != 0)// g_friendlyfireDist return 0; #endif]=]
    "the established SP friendly-fire firing guard remains authoritative")

extract_slice(
    _bg_weapons
    "void __cdecl PM_Weapon_CheckForMelee("
    "void __cdecl PM_Weapon_MeleeInit("
    _melee
    "PM_Weapon_CheckForMelee")
require_contains(
    _melee
    [=[#ifdef KISAK_SP if ((ps->weapFlags & 8) != 0) /* g_friendlyFireDist */ return; #endif]=]
    "SP friendly-fire suppression is one complete preprocessor guard")
require_count(
    _melee "weapFlags & 8" 1
    "the melee path checks the friendly-fire flag exactly once")
require_count(
    _melee "#ifdef KISAK_SP" 1
    "the melee suppression has one SP-only boundary")
require_ordered(
    _melee "BG_GetWeaponDef(ps->weapon);" "#ifdef KISAK_SP"
    "weapon lookup precedes the SP early return as in the upstream fix")
require_ordered(
    _melee "#endif" "if (ps->weaponstate != WEAPON_MELEE_INIT"
    "the SP early return precedes all melee initiation checks")
forbid_contains(
    _melee "#if 1" "friendly-fire suppression must not leak into MP")

# Removed snapshot entities must leave the collision world before marks and
# render objects are detached/freed. Pin the unique call and its exact order.
extract_slice(
    _cg_snapshot
    "for (int entityIndex = 0; entityIndex < MAX_GENTITIES; entityIndex++)"
    "CG_CheckSnapshot(localClientNum, \"CG_SetNextSnap-post\");"
    _removed_entities
    "CG_SetNextSnap removed-entity cleanup")
require_contains(
    _removed_entities
    [=[CG_ShutdownEntity(localClientNum, cent); CG_UnlinkEntity(localClientNum, entityIndex); FX_MarkEntDetachAll(localClientNum, entityIndex); dobj = Com_GetClientDObj(entityIndex, localClientNum);]=]
    "shutdown, collision unlink, mark detach, and DObj lookup stay adjacent")
require_count(
    _removed_entities "CG_UnlinkEntity(localClientNum, entityIndex);" 1
    "each removed entity is unlinked exactly once in this cleanup path")
require_ordered(
    _removed_entities
    "CG_UnlinkEntity(localClientNum, entityIndex);"
    "Com_SafeClientDObjFree(entityIndex, localClientNum);"
    "collision unlink precedes DObj destruction")

# Complete and failed use fixed client-command payloads. Only active/current
# objective updates interpolate objectiveDesc.
extract_slice(
    _g_scr_main
    "int __cdecl PrintObjectiveUpdate("
    "int __cdecl ObjectiveStateIndexFromString("
    _objective_update
    "PrintObjectiveUpdate")
require_contains(
    _objective_update
    [=[v2 = va("obj_update \"%s\"", objectiveDesc);]=]
    "active/current objective descriptions remain formatted")
require_contains(
    _objective_update
    [=[v2 = va("obj_complete \"GAME_OBJECTIVECOMPLETED\"");]=]
    "complete uses the exact original command payload")
require_contains(
    _objective_update
    [=[v2 = va("obj_failed \"GAME_OBJECTIVEFAILED\"");]=]
    "failed uses the exact original command payload")
require_count(
    _objective_update "objectiveDesc" 2
    "objectiveDesc occurs only in the signature and active/current command")
require_count(
    _objective_update "obj_complete" 1
    "there is one complete command construction")
require_count(
    _objective_update "obj_failed" 1
    "there is one failed command construction")

# Zero-initialized storage bypassed both lazy initialization branches. Keep the
# sentinel initialization and internal linkage on both translation-unit caches.
extract_slice(
    _actor_aim
    "void __cdecl Actor_HitEnemy("
    "void __cdecl Actor_MissEnemy("
    _miss_radius_caches
    "Actor miss-radius caches")
foreach(_cache IN ITEMS outerRadius outerRadius_0)
    require_contains(
        _miss_radius_caches "static float ${_cache} = 6969.0f;"
        "${_cache} has internal linkage and begins at the lazy sentinel")
    require_contains(
        _miss_radius_caches "if (${_cache} == 6969.0f)"
        "${_cache} tests the same typed lazy sentinel")
endforeach()
require_count(
    _miss_radius_caches "6969.0f" 4
    "both miss-radius caches have one initializer and one lazy check")

# Do not pull the three actor-aim behavior changes rejected from this curated
# batch. These are deliberately separate from the cache-initialization fix.
extract_slice(
    _actor_aim
    "float __cdecl Actor_GetPlayerMovementAccuracy("
    "float __cdecl Actor_GetPlayerSightAccuracy("
    _movement_accuracy
    "Actor_GetPlayerMovementAccuracy")
require_contains(
    _movement_accuracy
    "float dot = velocity[0] * dirY + (velocity[2] * dirZ - velocity[1] * dirX);"
    "the existing player-movement projection remains unchanged")
forbid_contains(
    _movement_accuracy
    "velocity[0] * dirX + velocity[1] * dirY + velocity[2] * dirZ"
    "the rejected player-movement projection must not enter this batch")

extract_slice(
    _actor_aim
    "float __cdecl Actor_GetFinalAccuracy("
    "void Actor_HitSentient("
    _final_accuracy
    "Actor_GetFinalAccuracy")
require_contains(
    _final_accuracy "playerSightAccuracy = self->playerSightAccuracy;"
    "the existing sight-accuracy state remains unchanged")
forbid_contains(
    _final_accuracy "playerSightAccuracy = Actor_GetPlayerSightAccuracy("
    "the rejected live sight recomputation must not enter this batch")

extract_slice(
    _actor_aim
    "void Actor_HitSentient("
    "void Actor_HitTarget("
    _hit_sentient
    "Actor_HitSentient")
extract_slice(
    _hit_sentient
    "if (enemy->ent->client)"
    "else"
    _hit_player
    "Actor_HitSentient player spread")
require_contains(
    _hit_player "vertMax = 8.0f; horizMax = -44.0f;"
    "the existing player spread constants remain unchanged")
forbid_contains(
    _hit_player "accuracy >= 0.0f"
    "the rejected accuracy-scaled player spread must not enter this batch")

# Prove that every family above is sensitive to representative regressions.
# A normal invocation runs each mutated in-memory source through this same file
# and requires the child contract to reject it.
if(NOT DEFINED CONTRACT_MUTATION OR CONTRACT_MUTATION STREQUAL "")
    foreach(_mutation IN ITEMS
        melee_mp_leak
        melee_wrong_flag
        snapshot_missing_unlink
        snapshot_late_unlink
        objective_complete_append
        objective_failed_append
        actor_zero_sentinel
        actor_external_cache
        actor_rejected_dot)
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
                "Safe-fixlet contract accepted mutation: ${_mutation}")
        endif()
    endforeach()
endif()

message(STATUS "Curated upstream safe-fixlet source contract passed")
