cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_source_path "${SOURCE_ROOT}/src/game/actor_navigation.cpp")
set(_helper_path "${SOURCE_ROOT}/src/game/actor_navigation_geometry.h")
set(_fixture_path "${SOURCE_ROOT}/tests/actor_navigation_geometry_tests.cpp")
set(_tests_path "${SOURCE_ROOT}/tests/CMakeLists.txt")
set(_sp_manifest_path "${SOURCE_ROOT}/scripts/sp/sp_files.cmake")

foreach(_path IN ITEMS
    "${_source_path}"
    "${_helper_path}"
    "${_fixture_path}"
    "${_tests_path}"
    "${_sp_manifest_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing actor-navigation contract source: ${_path}")
    endif()
endforeach()

file(READ "${_source_path}" _source)
file(READ "${_helper_path}" _helper)
file(READ "${_fixture_path}" _fixture)
file(READ "${_tests_path}" _tests)
file(READ "${_sp_manifest_path}" _sp_manifest)

foreach(_variable IN ITEMS _source _helper _fixture _tests _sp_manifest)
    string(REGEX REPLACE "[ \t\r\n]+" " " _normalized "${${_variable}}")
    set(${_variable} "${_normalized}")
endforeach()

function(require_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing actor-navigation invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden actor-navigation regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_occurrence_count SOURCE_VARIABLE NEEDLE EXPECTED DESCRIPTION)
    set(_remaining "${${SOURCE_VARIABLE}}")
    set(_count 0)
    while(TRUE)
        string(FIND "${_remaining}" "${NEEDLE}" _position)
        if(_position EQUAL -1)
            break()
        endif()

        math(EXPR _count "${_count} + 1")
        string(LENGTH "${NEEDLE}" _needle_length)
        math(EXPR _next "${_position} + ${_needle_length}")
        string(SUBSTRING "${_remaining}" ${_next} -1 _remaining)
    endwhile()

    if(NOT _count EQUAL EXPECTED)
        message(FATAL_ERROR
            "Unexpected actor-navigation invariant count (${DESCRIPTION}): "
            "expected ${EXPECTED}, found ${_count}")
    endif()
endfunction()

function(extract_slice SOURCE_VARIABLE START END OUT_VARIABLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${START}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR "Missing start of ${DESCRIPTION}: '${START}'")
    endif()

    string(SUBSTRING "${${SOURCE_VARIABLE}}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${END}" _end)
    if(_end LESS_EQUAL 0)
        message(FATAL_ERROR "Missing ordered end of ${DESCRIPTION}: '${END}'")
    endif()

    string(SUBSTRING "${_tail}" 0 ${_end} _slice)
    set(${OUT_VARIABLE} "${_slice}" PARENT_SCOPE)
endfunction()

require_contains(
    _sp_manifest
    "\${SRC_DIR}/game/actor_navigation_geometry.h"
    "the SP source manifest owns the production geometry helper")

# A* goal predicates need the caller's requested position. LOS searches use it
# for a strict, full-3D distance check before accepting a visible node.
extract_slice(
    _source
    "struct CustomSearchInfo_FindPathWithLOS"
    "/* 10049 */"
    _los_search
    "CustomSearchInfo_FindPathWithLOS")
require_contains(
    _los_search
    "bool IsGoal(pathnode_t *pCurrent, const float *vGoalPos)"
    "LOS goal checks receive the requested goal position")
require_contains(
    _los_search
    "ActorNavigationGeometry::IsWithinDistance3D( pCurrent->constant.vOrigin, vGoalPos, this->m_fWithinDistSqrd) && Path_NodesVisible(pCurrent, this->m_pNodeTo)"
    "LOS goals require strict 3D proximity and node visibility")

# The two public LOS search boundaries must reject a missing goal before
# nearest-node lookup, A* goal evaluation, or heuristic evaluation can
# dereference it. Generic A* intentionally permits null goals for policies
# such as FindPathAway that do not consume the goal position.
extract_slice(
    _source
    "bool __cdecl Path_FindPathInCylinderWithLOS("
    "bool __cdecl Path_FindPathInCylinderWithLOSNotCrossPlanes("
    _los_api
    "Path_FindPathInCylinderWithLOS")
extract_slice(
    _source
    "bool __cdecl Path_FindPathInCylinderWithLOSNotCrossPlanes("
    "bool __cdecl Path_FindPathFromInCylinder("
    _constrained_los_api
    "Path_FindPathInCylinderWithLOSNotCrossPlanes")
foreach(_slice IN ITEMS _los_api _constrained_los_api)
    require_contains(
        ${_slice}
        "iassert(vGoalPos); if (!vGoalPos) return false;"
        "LOS search boundaries reject a missing goal")
    extract_slice(
        ${_slice}
        "if (!vGoalPos) return false;"
        "Path_NearestNode(vGoalPos"
        _guarded_los_setup
        "LOS null guard ordering")
endforeach()

extract_slice(
    _source
    "template <typename T, bool USE_IGNORE = false, bool CHECK_NODETO = true>"
    "bool __cdecl Path_FindPathFromTo("
    _astar
    "Path_AStarAlgorithm")
require_contains(
    _astar
    "nodeToCheck = !custom->IsGoal(pCurrent, vGoalPos);"
    "A* forwards the requested goal position to custom goal predicates")

# All five searches whose custom policy supplies IgnoreNode must instantiate
# the filtering branch. A default instantiation silently bypasses constraints.
set(_constrained_searches
    CustomSearchInfo_FindPathNotCrossPlanes
    CustomSearchInfo_FindPathInCylinderWithLOS
    CustomSearchInfo_FindPathInCylinderWithLOSNotCrossPlanes
    CustomSearchInfo_FindPathFromInCylinder
    CustomSearchInfo_FindPathFromInCylinderNotCrossPlanes)
foreach(_search IN LISTS _constrained_searches)
    require_contains(
        _source
        "Path_AStarAlgorithm<${_search}, true>("
        "${_search} enables IgnoreNode filtering")
    forbid_contains(
        _source
        "Path_AStarAlgorithm<${_search}>("
        "${_search} must not fall back to unfiltered A*")
endforeach()

# Both cylinder policies own full XYZ origins and delegate to one tested helper.
extract_slice(
    _source
    "struct CustomSearchInfo_FindPathFromInCylinder :"
    "/* 10052 */"
    _cylinder_search
    "CustomSearchInfo_FindPathFromInCylinder")
extract_slice(
    _source
    "struct CustomSearchInfo_FindPathFromInCylinderNotCrossPlanes :"
    "/* 10053 */"
    _constrained_cylinder_search
    "CustomSearchInfo_FindPathFromInCylinderNotCrossPlanes")
foreach(_slice IN ITEMS _cylinder_search _constrained_cylinder_search)
    require_contains(
        ${_slice}
        "float m_vOrigin[3];"
        "cylinder policies retain the vertical origin")
    require_contains(
        ${_slice}
        "ActorNavigationGeometry::IsInsideCylinder("
        "cylinder policies use the shared 3D inclusion rule")
    forbid_contains(
        ${_slice}
        "vOrigin[2] - this->m_fRadiusSqrd"
        "cylinder height must never be measured from radius-squared")
endforeach()
require_occurrence_count(
    _source
    "info.m_vOrigin[2] = vOrigin[2];"
    2
    "cylinder search setup copies origin.z")

# Half-plane checks consistently accept exact boundaries and reject only a
# positive-side violation. The executable fixture covers both axes and zero planes.
foreach(_search IN ITEMS
    CustomSearchInfo_FindPathNotCrossPlanes
    CustomSearchInfo_FindPathAwayNotCrossPlanes
    CustomSearchInfo_FindPathInCylinderWithLOSNotCrossPlanes
    CustomSearchInfo_FindPathFromInCylinderNotCrossPlanes)
    extract_slice(
        _source
        "struct ${_search}"
        "};"
        _half_plane_search
        "${_search}")
    require_contains(
        _half_plane_search
        "ActorNavigationGeometry::IsOutsideHalfPlanes2D("
        "${_search} uses the shared half-plane rule")
endforeach()

# The nearest-node lookup must receive the same planes as the subsequent A*.
extract_slice(
    _source
    "pathnode_t *__cdecl Path_FindPathAwayNotCrossPlanes("
    "return pNodeTo;"
    _away_entry
    "Path_FindPathAwayNotCrossPlanes")
require_contains(
    _away_entry
    "Path_NearestNodeNotCrossPlanes( vStartPos, nodes, -2, 192.0, vNormal, fDist, iPlaneCount,"
    "the away-path nearest-node query receives the plane normals")

forbid_contains(
    _source
    "fMaxNodeDist"
    "the unrelated max-node-distance API churn remains excluded")

foreach(_marker IN ITEMS
    "point[2] - origin[2]"
    "DistanceSquared3D(point, origin) < distanceSquared"
    "projectedDistance > distances[planeIndex]"
    "dx * dx + dy * dy <= radiusSquared")
    require_contains(
        _helper "${_marker}" "the pure geometry helper pins boundary semantics")
endforeach()

foreach(_marker IN ITEMS
    "TestDistanceAndStrictGoalBoundary();"
    "TestHalfPlaneFiltering();"
    "TestCylinderInclusion();")
    require_contains(
        _fixture "${_marker}" "the executable geometry fixture remains complete")
endforeach()
foreach(_marker IN ITEMS
    "kisakcod-actor-navigation-geometry-tests"
    "NAME actor-navigation-geometry"
    "actor_navigation_source_test.cmake")
    require_contains(
        _tests "${_marker}" "the actor-navigation contracts remain registered")
endforeach()
