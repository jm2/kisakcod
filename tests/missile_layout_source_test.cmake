cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

function(read_normalized RELATIVE_PATH OUT_VARIABLE)
    set(_path "${SOURCE_ROOT}/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing missile-layout source: ${_path}")
    endif()
    file(READ "${_path}" _source)
    string(REGEX REPLACE "[ \t\r\n]+" " " _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(require_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing missile-layout invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden missile-layout regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_count SOURCE_VARIABLE NEEDLE EXPECTED DESCRIPTION)
    set(_remaining "${${SOURCE_VARIABLE}}")
    set(_count 0)
    while(TRUE)
        string(FIND "${_remaining}" "${NEEDLE}" _position)
        if(_position EQUAL -1)
            break()
        endif()
        math(EXPR _count "${_count} + 1")
        string(LENGTH "${NEEDLE}" _length)
        math(EXPR _next "${_position} + ${_length}")
        string(SUBSTRING "${_remaining}" ${_next} -1 _remaining)
    endwhile()
    if(NOT _count EQUAL EXPECTED)
        message(FATAL_ERROR
            "Unexpected missile-layout invariant count (${DESCRIPTION}): "
            "expected ${EXPECTED}, found ${_count}")
    endif()
endfunction()

read_normalized("src/bgame/bg_public.h" _bg_public)
foreach(_layout IN ITEMS
    "RUNTIME_SIZE(missile_ent_t, 0x3C, 0x3C)"
    "RUNTIME_OFFSET(missile_ent_t, time, 0x0, 0x0)"
    "RUNTIME_OFFSET(missile_ent_t, timeOfBirth, 0x4, 0x4)"
    "RUNTIME_OFFSET(missile_ent_t, travelDist, 0x8, 0x8)"
    "RUNTIME_OFFSET(missile_ent_t, surfaceNormal, 0xC, 0xC)"
    "RUNTIME_OFFSET(missile_ent_t, team, 0x18, 0x18)"
    "RUNTIME_OFFSET(missile_ent_t, curvature, 0x1C, 0x1C)"
    "RUNTIME_OFFSET(missile_ent_t, targetOffset, 0x28, 0x28)"
    "RUNTIME_OFFSET(missile_ent_t, stage, 0x34, 0x34)"
    "RUNTIME_OFFSET(missile_ent_t, flightMode, 0x38, 0x38)"
    "RUNTIME_SIZE(missile_ent_t, 0x54, 0x54)"
    "RUNTIME_OFFSET(missile_ent_t, predictLandPos, 0x0, 0x0)"
    "RUNTIME_OFFSET(missile_ent_t, predictLandTime, 0xC, 0xC)"
    "RUNTIME_OFFSET(missile_ent_t, timestamp, 0x10, 0x10)"
    "RUNTIME_OFFSET(missile_ent_t, time, 0x14, 0x14)"
    "RUNTIME_OFFSET(missile_ent_t, timeOfBirth, 0x18, 0x18)"
    "RUNTIME_OFFSET(missile_ent_t, travelDist, 0x1C, 0x1C)"
    "RUNTIME_OFFSET(missile_ent_t, surfaceNormal, 0x20, 0x20)"
    "RUNTIME_OFFSET(missile_ent_t, team, 0x2C, 0x2C)"
    "RUNTIME_OFFSET(missile_ent_t, thrownBack, 0x30, 0x30)"
    "RUNTIME_OFFSET(missile_ent_t, curvature, 0x34, 0x34)"
    "RUNTIME_OFFSET(missile_ent_t, targetOffset, 0x40, 0x40)"
    "RUNTIME_OFFSET(missile_ent_t, stage, 0x4C, 0x4C)"
    "RUNTIME_OFFSET(missile_ent_t, flightMode, 0x50, 0x50)")
    require_contains(_bg_public "${_layout}" "variant-specific missile layout")
endforeach()
forbid_contains(
    _bg_public
    "static_assert(sizeof(missile_ent_t)"
    "missile layout must use the runtime ABI contract")

read_normalized("src/game/g_missile.cpp" _g_missile)
foreach(_required IN ITEMS
    "level.time - ent->missile.timeOfBirth <= 60000"
    "Vec3Mad(ent->r.currentOrigin, -16.0, ent->missile.surfaceNormal, end)"
    "level.time - missile->missile.timestamp >= 250"
    "missile->missile.timestamp = level.time"
    "Vec3Copy(trace->normal, ent->missile.surfaceNormal)"
    "ent->missile.travelDist = ent->missile.travelDist + v9"
    "missile->s.lerp.pos.trTime + (int)missile->missile.time >= level.time"
    "Vec3Copy(newAngleAccel, missile->missile.curvature)"
    "Vec3Add(target->r.currentOrigin, ent->missile.targetOffset, result)"
    "grenade->missile.timeOfBirth = level.time"
    "Vec3Copy(targetOffset, bolt->missile.targetOffset)"
    "Vec3Clear(bolt->missile.targetOffset)"
    "bolt->missile.time = (double)weapDef->destabilizeDistance")
    require_contains(_g_missile "${_required}" "typed missile field access")
endforeach()
forbid_contains(
    _g_missile "->mover." "missiles must not alias the mover union member")
forbid_contains(
    _g_missile "->item[" "missiles must not alias the item union member")
foreach(_team_branch IN ITEMS
    "static team_t G_MissileParentTeam(const gentity_s *const parent)"
    "if (parent->client) return parent->client->sess.cs.team"
    "if (parent->sentient) return parent->sentient->eTeam"
    "return TEAM_FREE")
    require_contains(_g_missile "${_team_branch}" "variant-aware parent team")
endforeach()
require_count(
    _g_missile
    "missile.team = G_MissileParentTeam(parent)"
    2
    "grenades and rockets share the parent-team helper")

read_normalized("src/game/g_scr_main.cpp" _g_scr_sp)
foreach(_required IN ITEMS
    "Vec3Clear(Entity->missile.targetOffset)"
    "Scr_GetVector(1u, Entity->missile.targetOffset)")
    require_contains(_g_scr_sp "${_required}" "SP script missile target offset")
endforeach()
foreach(_forbidden IN ITEMS
    "Entity->mover.apos1[1]"
    "Entity->mover.apos2[0]")
    forbid_contains(_g_scr_sp "${_forbidden}" "SP target-offset mover alias")
endforeach()

read_normalized("src/game_mp/g_scr_main_mp.cpp" _g_scr_mp)
foreach(_required IN ITEMS
    "Vec3Clear(missile->missile.targetOffset)"
    "Scr_GetVector(1u, missile->missile.targetOffset)")
    require_contains(_g_scr_mp "${_required}" "MP script missile target offset")
endforeach()
foreach(_forbidden IN ITEMS
    "missile->mover.pos2[1]"
    "missile->mover.pos3[0]")
    forbid_contains(_g_scr_mp "${_forbidden}" "MP target-offset mover alias")
endforeach()

read_normalized("src/game/actor_grenade_prediction_cache.h" _cache)
foreach(_required IN ITEMS
    "return predictLandTime != 0"
    "predictLandTime = 0"
    "cachedPosition[0] = predictedPosition[0]"
    "cachedPosition[1] = predictedPosition[1]"
    "cachedPosition[2] = predictedPosition[2]"
    "cachedTime = predictedTime")
    require_contains(_cache "${_required}" "prediction cache contract")
endforeach()

read_normalized("src/game/actor_grenade.cpp" _actor_grenade)
foreach(_forbidden IN ITEMS
    "->mover.decelTime"
    "->mover.aDecelTime"
    "->mover.speed"
    "->item[1].ammoCount")
    forbid_contains(
        _actor_grenade "${_forbidden}" "grenade prediction union alias")
endforeach()
require_count(
    _actor_grenade
    "actor_grenade_prediction_cache::IsValid("
    2
    "prediction and ping query the time sentinel")
require_count(
    _actor_grenade
    "actor_grenade_prediction_cache::Store("
    2
    "prediction success and fallback publish through the cache helper")
require_count(
    _actor_grenade
    "actor_grenade_prediction_cache::Invalidate("
    2
    "detach and bounce invalidate through the cache helper")
require_contains(
    _actor_grenade
    "self->pGrenade.ent()->missile.predictLandTime <= level.time"
    "throwback timing reads the dedicated prediction time")

read_normalized("src/game/g_items.cpp" _g_items)
require_contains(
    _g_items
    "ent->s.index.brushmodel = static_cast<uint16_t>(ent->item[0].index)"
    "spawned items publish their actual item index")
forbid_contains(
    _g_items
    "ent->s.index.brushmodel = LOWORD(ent->missile.travelDist)"
    "spawned items must not read a missile union alias")

message(STATUS "Missile union/layout source contract passed")
