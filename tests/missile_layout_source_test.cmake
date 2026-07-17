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

function(extract_scope SOURCE_VARIABLE START_MARKER NEXT_MARKER OUT_VARIABLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${START_MARKER}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR
            "Missing missile-layout scope (${DESCRIPTION}): '${START_MARKER}'")
    endif()

    string(SUBSTRING "${${SOURCE_VARIABLE}}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${NEXT_MARKER}" _end)
    if(_end EQUAL -1 OR _end EQUAL 0)
        message(FATAL_ERROR
            "Missing missile-layout scope terminator (${DESCRIPTION}): "
            "'${NEXT_MARKER}'")
    endif()

    string(SUBSTRING "${_tail}" 0 ${_end} _scope)
    set(${OUT_VARIABLE} "${_scope}" PARENT_SCOPE)
endfunction()

read_normalized("src/bgame/bg_public.h" _bg_public)
require_count(
    _bg_public
    "enum team_t : __int32;"
    1
    "team_t forward declaration fixes its ABI width")

extract_scope(
    _bg_public
    "struct missile_ent_t // sizeof=0x3C"
    "enum EntHandler_t : uint8_t"
    _mp_missile_layout
    "MP missile declaration")
foreach(_vector_field IN ITEMS
    "float surfaceNormal[3]"
    "float curvature[3]"
    "float targetOffset[3]")
    require_count(
        _mp_missile_layout
        "${_vector_field}"
        1
        "MP missile vector keeps its exact three-component extent")
endforeach()

extract_scope(
    _bg_public
    "struct missile_ent_t // sizeof=0x54"
    "struct gentity_s_tag"
    _sp_missile_layout
    "SP missile declaration")
foreach(_vector_field IN ITEMS
    "float predictLandPos[3]"
    "float surfaceNormal[3]"
    "float curvature[3]"
    "float targetOffset[3]")
    require_count(
        _sp_missile_layout
        "${_vector_field}"
        1
        "SP missile vector keeps its exact three-component extent")
endforeach()

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

extract_scope(
    _g_missile
    "static team_t G_MissileParentTeam(const gentity_s *const parent)"
    "void __cdecl G_RegisterMissileDvars()"
    _parent_team
    "variant-aware parent-team helper")
foreach(_required IN ITEMS
    "iassert(parent)"
    "if (parent->client) return parent->client->sess.cs.team"
    "if (parent->sentient) return parent->sentient->eTeam"
    "return TEAM_FREE")
    require_contains(_parent_team "${_required}" "parent-team helper branch")
endforeach()

extract_scope(
    _g_missile
    "void __cdecl G_ExplodeMissile(gentity_s *ent)"
    "int32_t __cdecl GetSplashMethodOfDeath(gentity_s *ent)"
    _explode_missile
    "G_ExplodeMissile")
require_contains(
    _explode_missile
    "level.time - ent->missile.timeOfBirth <= 60000"
    "smoke lifetime reads timeOfBirth")
require_contains(
    _explode_missile
    "Vec3Mad(ent->r.currentOrigin, -16.0, ent->missile.surfaceNormal, end)"
    "sticky explosion reads the typed surface normal")

extract_scope(
    _g_missile
    "void __cdecl RunMissile_BroadcastActorEvents(gentity_s *missile)"
    "void __cdecl G_RunMissile(gentity_s *ent)"
    _broadcast_events
    "RunMissile_BroadcastActorEvents")
require_contains(
    _broadcast_events
    "level.time - missile->missile.timestamp >= 250"
    "grenade ping throttle reads timestamp")
require_contains(
    _broadcast_events
    "missile->missile.timestamp = level.time"
    "grenade ping throttle publishes timestamp")

extract_scope(
    _g_missile
    "void __cdecl G_RunMissile(gentity_s *ent)"
    "void __cdecl MissileImpact(gentity_s *ent, trace_t *trace, float *dir, float *endpos)"
    _run_missile
    "G_RunMissile")
require_contains(
    _run_missile
    "Vec3Mad(origin, -1.635f, ent->missile.surfaceNormal, origin)"
    "stationary missile reads the typed surface normal")
require_contains(
    _run_missile
    "ent->missile.travelDist = ent->missile.travelDist + v9"
    "missile travel accumulation stays in travelDist")

extract_scope(
    _g_missile
    "void __cdecl MissileImpact(gentity_s *ent, trace_t *trace, float *dir, float *endpos)"
    "bool __cdecl CheckCrumpleMissile(gentity_s *ent, trace_t *trace)"
    _missile_impact
    "MissileImpact")
require_contains(
    _missile_impact
    "ent->missile.travelDist = -1.0e10f"
    "dud impact sentinel stays in travelDist")

extract_scope(
    _g_missile
    "bool __cdecl BounceMissile(gentity_s *ent, trace_t *trace)"
    "void __cdecl MissileLandAngles(gentity_s *ent, trace_t *trace, float *vAngles, int32_t bForceAlign)"
    _bounce_missile
    "BounceMissile")
require_contains(
    _bounce_missile
    "Vec3Copy(trace->normal, ent->missile.surfaceNormal)"
    "bounce publishes the typed surface normal")

extract_scope(
    _g_missile
    "bool __cdecl GrenadeDud(gentity_s *ent, WeaponDef *weapDef)"
    "bool __cdecl JavelinProjectile(gentity_s *ent, WeaponDef *weapDef)"
    _grenade_dud
    "GrenadeDud")
require_contains(
    _grenade_dud
    "ent->missile.travelDist < (double)weapDef->iProjectileActivateDist"
    "dud threshold reads travelDist")
forbid_contains(
    _grenade_dud
    "ent->missile.time <"
    "dud threshold must not silently move to missile time")

extract_scope(
    _g_missile
    "void __cdecl RunMissile_Destabilize(gentity_s *missile)"
    "double __cdecl RunMissile_GetPerturbation(float destabilizationCurvatureMax)"
    _destabilize
    "RunMissile_Destabilize")
foreach(_required IN ITEMS
    "missile->s.lerp.pos.trTime + (int)missile->missile.time >= level.time"
    "missile->missile.curvature[1] < 0.0f"
    "missile->missile.curvature[0] > 0.0f"
    "Vec3Copy(newAngleAccel, missile->missile.curvature)"
    "missile->missile.time = weaponDef->destabilizationRateTime * 1000.0f"
    "Vec3Mad(missile->s.lerp.apos.trBase, 0.050000001f, missile->missile.curvature, newAPos)")
    require_contains(_destabilize "${_required}" "destabilization typed field")
endforeach()

extract_scope(
    _g_missile
    "void __cdecl MissileTrajectory(gentity_s *ent, float *result)"
    "bool __cdecl MissileIsReadyForSteering(gentity_s *ent)"
    _missile_trajectory
    "MissileTrajectory")
require_contains(
    _missile_trajectory
    "Vec3Mad(ent->s.lerp.pos.trDelta, 0.050000001f, ent->missile.curvature, ent->s.lerp.pos.trDelta)"
    "trajectory applies the typed curvature")

extract_scope(
    _g_missile
    "void __cdecl GetTargetPosition(gentity_s *ent, float *result)"
    "void __cdecl JavelinSteering(gentity_s *ent, WeaponDef *weapDef)"
    _target_position
    "GetTargetPosition")
require_contains(
    _target_position
    "Vec3Add(target->r.currentOrigin, ent->missile.targetOffset, result)"
    "target position reads the complete typed offset")

extract_scope(
    _g_missile
    "void __cdecl JavelinSteering(gentity_s *ent, WeaponDef *weapDef)"
    "void __cdecl JavelinClimbOffset(gentity_s *ent, float *targetPos)"
    _javelin_steering
    "JavelinSteering")
require_contains(
    _javelin_steering
    "height = ent->s.lerp.pos.trBase[2] - targetPos[2] - ent->missile.targetOffset[2]"
    "Javelin debug height reads targetOffset z")

extract_scope(
    _g_missile
    "bool __cdecl JavelinClimbIsAboveCeiling(gentity_s *ent, const float *targetPos)"
    "void __cdecl G_InitGrenadeEntity(gentity_s *parent, gentity_s *grenade)"
    _javelin_above_ceiling
    "JavelinClimbIsAboveCeiling")
require_contains(
    _javelin_above_ceiling
    "height = ent->s.lerp.pos.trBase[2] - targetPos[2] - ent->missile.targetOffset[2]"
    "Javelin ceiling height reads targetOffset z")

extract_scope(
    _g_missile
    "void __cdecl G_InitGrenadeEntity(gentity_s *parent, gentity_s *grenade)"
    "void __cdecl G_InitGrenadeMovement(gentity_s *grenade, const float *start, const float *dir, int32_t rotate)"
    _init_grenade_entity
    "G_InitGrenadeEntity")
require_count(
    _init_grenade_entity
    "grenade->missile.team = G_MissileParentTeam(parent)"
    1
    "grenade initialization uses the team helper exactly once")

extract_scope(
    _g_missile
    "void __cdecl G_InitGrenadeMovement(gentity_s *grenade, const float *start, const float *dir, int32_t rotate)"
    "int32_t __cdecl CalcMissileNoDrawTime(float speed)"
    _init_grenade_movement
    "G_InitGrenadeMovement")
require_contains(
    _init_grenade_movement
    "grenade->missile.travelDist = 0.0f"
    "grenade movement initializes travelDist")

extract_scope(
    _g_missile
    "void __cdecl InitGrenadeTimer(const gentity_s *parent, gentity_s *grenade, const WeaponDef *weapDef, int32_t time)"
    "gentity_s *__cdecl G_FireRocket("
    _init_grenade_timer
    "InitGrenadeTimer")
require_contains(
    _init_grenade_timer
    "grenade->missile.timeOfBirth = level.time"
    "grenade timer initializes timeOfBirth")

extract_scope(
    _g_missile
    "gentity_s *__cdecl G_FireRocket("
    "void __cdecl InitRocketTimer(gentity_s *bolt, WeaponDef *weapDef)"
    _fire_rocket
    "G_FireRocket")
foreach(_required IN ITEMS
    "bolt->missile.travelDist = 0.0f"
    "Vec3Copy(targetOffset, bolt->missile.targetOffset)"
    "Vec3Clear(bolt->missile.targetOffset)"
    "bolt->missile.team = G_MissileParentTeam(parent)"
    "Vec3Scale(v, v13, bolt->missile.curvature)"
    "Vec3Mad(bolt->missile.curvature, scale, up, bolt->missile.curvature)"
    "COERCE_UNSIGNED_INT(bolt->missile.curvature[0])"
    "COERCE_UNSIGNED_INT(bolt->missile.curvature[1])"
    "COERCE_UNSIGNED_INT(bolt->missile.curvature[2])"
    "bolt->missile.time = (double)weapDef->destabilizeDistance / (double)weapDef->iProjectileSpeed * 1000.0")
    require_contains(_fire_rocket "${_required}" "rocket typed field initialization")
endforeach()

read_normalized("src/game/g_scr_main.cpp" _g_scr_sp)
foreach(_required IN ITEMS
    "Vec3Clear(Entity->missile.targetOffset)"
    "Scr_GetVector(1u, Entity->missile.targetOffset)")
    require_contains(_g_scr_sp "${_required}" "SP script missile target offset")
endforeach()
extract_scope(
    _g_scr_sp
    "void __cdecl GScr_MissileSetTarget(scr_entref_t entref)"
    "void __cdecl GScr_EnableAimAssist(scr_entref_t entref)"
    _sp_set_target
    "SP GScr_MissileSetTarget")
require_contains(
    _sp_set_target
    "if (Scr_GetNumParam() <= 1) { Vec3Clear(Entity->missile.targetOffset); } else { Scr_GetVector(1u, Entity->missile.targetOffset); }"
    "SP setter clears or writes the same typed targetOffset")
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
extract_scope(
    _g_scr_mp
    "void __cdecl GScr_MissileSetTarget(scr_entref_t entref)"
    "void __cdecl GScr_EnableAimAssist(scr_entref_t entref)"
    _mp_set_target
    "MP GScr_MissileSetTarget")
require_contains(
    _mp_set_target
    "if (Scr_GetNumParam() <= 1) { Vec3Clear(missile->missile.targetOffset); } else { Scr_GetVector(1u, missile->missile.targetOffset); }"
    "MP setter clears or writes the same typed targetOffset")

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

extract_scope(
    _actor_grenade
    "void __cdecl Actor_PredictGrenadeLandPos(gentity_s *pGrenade)"
    "bool __cdecl Actor_Grenade_IsPointSafe(actor_s *self, const float *vPoint)"
    _predict_land_pos
    "Actor_PredictGrenadeLandPos")
require_count(
    _predict_land_pos
    "actor_grenade_prediction_cache::Store("
    2
    "success and fallback each publish one prediction")
foreach(_required IN ITEMS
    "actor_grenade_prediction_cache::IsValid(pGrenade->missile.predictLandTime)"
    "G_PredictMissile(pGrenade, nextthink - level.time, v8, 1, &v7)"
    "pGrenade->missile.predictLandPos, pGrenade->missile.predictLandTime, v8, v7"
    "pGrenade->missile.predictLandPos, pGrenade->missile.predictLandTime, pGrenade->r.currentOrigin, v7")
    require_contains(_predict_land_pos "${_required}" "prediction cache call arguments")
endforeach()

extract_scope(
    _actor_grenade
    "float __cdecl Actor_Grenade_EscapePlane(actor_s *self, float *normal)"
    "void __cdecl Actor_Grenade_GetPickupPos(actor_s *self, const float *enemyPos, float *vGrenadePickupPos)"
    _escape_plane
    "Actor_Grenade_EscapePlane")
foreach(_required IN ITEMS
    "TargetEntity->r.currentOrigin[0] - v8->missile.predictLandPos[0]"
    "TargetEntity->r.currentOrigin[1] - v8->missile.predictLandPos[1]"
    "? self->pGrenade.ent()->missile.predictLandPos : self->ent->r.currentOrigin")
    require_contains(_escape_plane "${_required}" "escape plane prediction position")
endforeach()

extract_scope(
    _actor_grenade
    "void __cdecl Actor_Grenade_GetPickupPos(actor_s *self, const float *enemyPos, float *vGrenadePickupPos)"
    "bool __cdecl Actor_Grenade_ShouldIgnore(actor_s *self, gentity_s *grenade)"
    _pickup_pos
    "Actor_Grenade_GetPickupPos")
foreach(_required IN ITEMS
    "dir[0] = v6->missile.predictLandPos[0] - *enemyPos"
    "dir[1] = v6->missile.predictLandPos[1] - (float)v7"
    "*vGrenadePickupPos = (float)(dir[0] * (float)29.5) + v8->missile.predictLandPos[0]"
    "vGrenadePickupPos[1] = (float)((float)v9 * (float)29.5) + v8->missile.predictLandPos[1]"
    "vGrenadePickupPos[2] = (float)((float)v10 * (float)29.5) + v8->missile.predictLandPos[2]")
    require_contains(_pickup_pos "${_required}" "pickup position prediction component")
endforeach()

extract_scope(
    _actor_grenade
    "bool __cdecl Actor_Grenade_ShouldIgnore(actor_s *self, gentity_s *grenade)"
    "int __cdecl Actor_IsAwareOfGrenade(actor_s *self)"
    _should_ignore
    "Actor_Grenade_ShouldIgnore")
foreach(_required IN ITEMS
    "dir[0] = grenade->missile.predictLandPos[0] - self->ent->r.currentOrigin[0]"
    "dir[1] = grenade->missile.predictLandPos[1] - ent->r.currentOrigin[1]"
    "self->ent->r.currentOrigin[0] - grenade->missile.predictLandPos[0]"
    "self->ent->r.currentOrigin[2] - grenade->missile.predictLandPos[2]"
    "self->ent->r.currentOrigin[1] - grenade->missile.predictLandPos[1]")
    require_contains(_should_ignore "${_required}" "ignore decision prediction component")
endforeach()

extract_scope(
    _actor_grenade
    "void __cdecl Actor_GrenadePing(actor_s *self, gentity_s *pGrenade)"
    "void __cdecl Actor_DissociateGrenade(gentity_s *pGrenade)"
    _grenade_ping
    "Actor_GrenadePing")
foreach(_required IN ITEMS
    "actor_grenade_prediction_cache::IsValid( self->pGrenade.ent()->missile.predictLandTime)"
    "Actor_PredictGrenadeLandPos(pGrenade)")
    require_contains(_grenade_ping "${_required}" "grenade ping cache contract")
endforeach()

extract_scope(
    _actor_grenade
    "void __cdecl Actor_Grenade_Detach(actor_s *self)"
    "int __cdecl Actor_Grenade_InActorHands(gentity_s *grenade)"
    _grenade_detach
    "Actor_Grenade_Detach")
require_count(
    _grenade_detach
    "actor_grenade_prediction_cache::Invalidate(v2->missile.predictLandTime)"
    1
    "detach invalidates the exact cache sentinel")

extract_scope(
    _actor_grenade
    "int __cdecl Actor_Grenade_AttemptReturn(actor_s *self)"
    "void __cdecl Actor_Grenade_DropIfHeld(actor_s *self)"
    _attempt_return
    "Actor_Grenade_AttemptReturn")
foreach(_required IN ITEMS
    "decelTime = v2->missile.predictLandPos[0]"
    "aDecelTime = v2->missile.predictLandPos[1]"
    "speed = v2->missile.predictLandPos[2]"
    "Vec2Distance(currentOrigin, v2->missile.predictLandPos)"
    "Actor_PointAt(self->ent->r.currentOrigin, v2->missile.predictLandPos)"
    "Vec3Copy(v2->missile.predictLandPos, v25)")
    require_contains(_attempt_return "${_required}" "return attempt prediction position")
endforeach()

extract_scope(
    _actor_grenade
    "void __cdecl Actor_GrenadeBounced(gentity_s *pGrenade, gentity_s *pHitEnt)"
    "bool __cdecl Actor_Grenade_Start(actor_s *self, ai_state_t ePrevState)"
    _grenade_bounced
    "Actor_GrenadeBounced")
require_count(
    _grenade_bounced
    "actor_grenade_prediction_cache::Invalidate(pGrenade->missile.predictLandTime)"
    1
    "bounce invalidates the exact cache sentinel")
require_contains(
    _grenade_bounced
    "actor_grenade_prediction_cache::Invalidate(pGrenade->missile.predictLandTime); Actor_PredictGrenadeLandPos(pGrenade)"
    "bounce invalidates immediately before repredicting")
forbid_contains(
    _grenade_bounced
    "Invalidate(pGrenade->missile.timestamp)"
    "bounce must not invalidate the actor-event timestamp")

extract_scope(
    _actor_grenade
    "actor_think_result_t __cdecl Actor_Grenade_Acquire(actor_s *self)"
    "actor_think_result_t __cdecl Actor_Grenade_Think(actor_s *self)"
    _grenade_acquire
    "Actor_Grenade_Acquire")
foreach(_required IN ITEMS
    "dir[0] = v6->missile.predictLandPos[0] - ent->r.currentOrigin[0]"
    "dir[1] = v6->missile.predictLandPos[1] - ent->r.currentOrigin[1]"
    "dir[2] = v6->missile.predictLandPos[2] - ent->r.currentOrigin[2]"
    "self->pGrenade.ent()->missile.predictLandTime <= level.time"
    "self->pGrenade.ent()->missile.thrownBack = 1")
    require_contains(_grenade_acquire "${_required}" "throwback cache field")
endforeach()

read_normalized("src/game/g_items.cpp" _g_items)
require_contains(
    _g_items
    "ent->s.index.brushmodel = static_cast<uint16_t>(ent->item[0].index)"
    "spawned items publish their actual item index")
forbid_contains(
    _g_items
    "ent->s.index.brushmodel = LOWORD(ent->missile.travelDist)"
    "spawned items must not read a missile union alias")

extract_scope(
    _g_items
    "void __cdecl G_SpawnItem(gentity_s *ent, const gitem_s *item)"
    "void __cdecl G_RunItem(gentity_s *ent)"
    _spawn_item
    "G_SpawnItem")
require_count(
    _spawn_item
    "ent->s.index.brushmodel = static_cast<uint16_t>(ent->item[0].index)"
    1
    "MP item spawn publishes its exact item-union index")

read_normalized("scripts/sp/sp_files.cmake" _sp_files)
require_contains(
    _sp_files
    "\"\${SRC_DIR}/game/actor_grenade.cpp\" \"\${SRC_DIR}/game/actor_grenade.h\" \"\${SRC_DIR}/game/actor_grenade_prediction_cache.h\""
    "SP source manifest ships the cache helper with actor_grenade")

read_normalized(".github/workflows/ci.yml" _ci)
foreach(_required IN ITEMS
    "kisakcod-actor-grenade-prediction-cache-tests"
    "kisakcod-missile-layout-mp-compile-tests"
    "kisakcod-missile-layout-sp-compile-tests"
    "actor-grenade-prediction-cache-contracts"
    "missile-layout-(mp|sp)-compile-contracts"
    "missile-union-layout-source-invariants")
    require_contains(
        _ci "${_required}"
        "measured Windows x86 builds and runs the missile contracts")
endforeach()

read_normalized("tests/missile_layout_compile_tests.cpp" _compile_probe)
foreach(_required IN ITEMS
    "enum team_t : __int32"
    "#include \"game/teams.h\""
    "struct MissileLayoutProbe"
    "sizeof(MissileLayoutProbe) == 0x3C"
    "sizeof(MissileLayoutProbe) == 0x54")
    require_contains(_compile_probe "${_required}" "dual-variant compile/layout probe")
endforeach()

message(STATUS "Missile union/layout source contract passed")
