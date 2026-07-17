cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

function(read_normalized RELATIVE_PATH OUT_VARIABLE DESCRIPTION)
    set(_path "${SOURCE_ROOT}/src/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR
            "Missing vehicle material-time source (${DESCRIPTION}): ${_path}")
    endif()
    file(READ "${_path}" _source)
    string(REGEX REPLACE "[ \t\r\n]+" " " _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(require_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing vehicle material-time integration (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden vehicle material-time regression (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

read_normalized(
    "cgame/cg_ents.cpp" _cg_sp "single-player rendering consumer")
foreach(_required IN ITEMS
    "#include <bgame/bg_vehicle_material_time.h>"
    "lightingOrigin[2] = cent->pose.origin[2] + 32.0f;"
    "materialTime = bg::vehicle_material_time::ForRender( cent->currentState.u.vehicle.materialTime, p_nextState->lerp.u.vehicle.materialTime, cgameGlob->frameInterpolation, cgameGlob->time);"
    "lightingOrigin, materialTime);")
    require_contains(_cg_sp "${_required}" "single-player rendering path")
endforeach()
forbid_contains(
    _cg_sp
    "cent->pose.origin, 0.0f); // KISAKTODO: is materialTime really 0.0 here?"
    "single-player rendering must not discard tread time")

read_normalized(
    "cgame_mp/cg_vehicles_mp.cpp" _cg_mp "multiplayer rendering consumer")
foreach(_required IN ITEMS
    "#include <bgame/bg_vehicle_material_time.h>"
    "materialTime = bg::vehicle_material_time::ForRender( p_currentState->u.vehicle.materialTime, ns->lerp.u.vehicle.materialTime, cgameGlob->frameInterpolation, cgameGlob->time);"
    "lightingOrigin, materialTime);")
    require_contains(_cg_mp "${_required}" "multiplayer rendering path")
endforeach()
forbid_contains(
    _cg_mp
    "p_currentState->u.vehicle.materialTime < 0"
    "negative enabled values must render")

read_normalized(
    "game/g_scr_vehicle.cpp" _game_sp "single-player state producer")
foreach(_required IN ITEMS
    "#include <bgame/bg_vehicle_material_time.h>"
    "ent->s.lerp.u.vehicle.materialTime = bg::vehicle_material_time::kDisabled;"
    "ent->s.lerp.u.vehicle.materialTime = bg::vehicle_material_time::Advance( ent->s.lerp.u.vehicle.materialTime, delta);")
    require_contains(_game_sp "${_required}" "single-player producer path")
endforeach()
foreach(_forbidden IN ITEMS
    "ent->s.lerp.u.vehicle.materialTime -= delta;"
    "ent->s.lerp.u.vehicle.materialTime += delta;")
    forbid_contains(_game_sp "${_forbidden}" "single-player wrapping must be defined")
endforeach()

read_normalized(
    "game_mp/g_vehicles_mp.cpp" _game_mp "multiplayer state producer")
foreach(_required IN ITEMS
    "#include <bgame/bg_vehicle_material_time.h>"
    "ent->s.lerp.u.vehicle.materialTime = bg::vehicle_material_time::kDisabled;"
    "ent->s.lerp.u.vehicle.materialTime = bg::vehicle_material_time::Advance( ent->s.lerp.u.vehicle.materialTime, (int)(deltaTime * 1000.0));")
    require_contains(_game_mp "${_required}" "multiplayer producer path")
endforeach()
forbid_contains(
    _game_mp
    "ent->s.lerp.u.vehicle.materialTime += (int)(deltaTime * 1000.0);"
    "multiplayer wrapping must be defined")
