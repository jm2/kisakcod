cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

# These fixes currently live in code that is not uniformly compiled by every
# build profile. Keep the assertions narrowly scoped to the affected functions
# so similarly named variables elsewhere cannot satisfy an invariant.
function(extract_function_slice
    RELATIVE_PATH START_MARKER END_MARKER OUT_VARIABLE DESCRIPTION)
    set(_path "${SOURCE_ROOT}/src/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR
            "Missing upstream-correctness source (${DESCRIPTION}): ${_path}")
    endif()

    file(READ "${_path}" _source)
    string(FIND "${_source}" "${START_MARKER}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR
            "Missing start of upstream-correctness function (${DESCRIPTION}): "
            "'${START_MARKER}'")
    endif()

    string(SUBSTRING "${_source}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${END_MARKER}" _relative_end)
    if(_relative_end LESS_EQUAL 0)
        message(FATAL_ERROR
            "Missing ordered end of upstream-correctness function "
            "(${DESCRIPTION}): '${END_MARKER}'")
    endif()

    string(SUBSTRING "${_tail}" 0 ${_relative_end} _slice)
    string(REGEX REPLACE "[ \t\r\n]+" " " _slice "${_slice}")
    set(${OUT_VARIABLE} "${_slice}" PARENT_SCOPE)
endfunction()

function(require_slice_contains SLICE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SLICE_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing upstream-correctness invariant (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(forbid_slice_contains SLICE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SLICE_VARIABLE}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden upstream-correctness regression (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(require_slice_ordered SLICE_VARIABLE FIRST SECOND DESCRIPTION)
    string(FIND "${${SLICE_VARIABLE}}" "${FIRST}" _first_position)
    string(FIND "${${SLICE_VARIABLE}}" "${SECOND}" _second_position)
    if(_first_position EQUAL -1
        OR _second_position EQUAL -1
        OR _first_position GREATER_EQUAL _second_position)
        message(FATAL_ERROR
            "Missing or unordered upstream-correctness invariant "
            "(${DESCRIPTION})")
    endif()
endfunction()

function(require_slice_match_count
    SLICE_VARIABLE PATTERN EXPECTED_COUNT DESCRIPTION)
    # MATCHALL returns a CMake list, so a matched C/C++ semicolon would be
    # miscounted as a list separator. Consume one scalar match at a time.
    set(_remaining "${${SLICE_VARIABLE}}")
    set(_count 0)
    while(TRUE)
        string(REGEX MATCH "${PATTERN}" _match "${_remaining}")
        if(_match STREQUAL "")
            break()
        endif()
        math(EXPR _count "${_count} + 1")
        string(FIND "${_remaining}" "${_match}" _match_position)
        string(LENGTH "${_match}" _match_length)
        math(EXPR _next_position "${_match_position} + ${_match_length}")
        string(SUBSTRING "${_remaining}" ${_next_position} -1 _remaining)
    endwhile()
    if(NOT _count EQUAL EXPECTED_COUNT)
        message(FATAL_ERROR
            "Unexpected upstream-correctness invariant count "
            "(${DESCRIPTION}): expected ${EXPECTED_COUNT}, found ${_count}")
    endif()
endfunction()

# Each duplicate-check loop must use one loop-local index consistently for the
# matching count and list. This catches the historical second-loop bug, where
# `i` controlled iteration but stale `listIndex` selected the element.
extract_function_slice(
    "gfx_d3d/r_marks.cpp"
    "void __cdecl R_AABBTreeSurfacesTwoLists_r("
    "int  R_BoxStaticModels("
    _r_marks
    "R_AABBTreeSurfacesTwoLists_r")
require_slice_contains(
    _r_marks
    "for (uint32_t listIndex = 0; listIndex < *surfCounts; ++listIndex) { iassert( surfLists[0][listIndex] != surf ); }"
    "the first duplicate walk indexes list zero with its own iterator")
require_slice_contains(
    _r_marks
    "for (uint32_t listIndex = 0; listIndex < surfCounts[1]; ++listIndex) { iassert( surfLists[1][listIndex] != surf ); }"
    "the second duplicate walk indexes list one with its own iterator")
require_slice_match_count(
    _r_marks
    "for \\(uint32_t listIndex = 0;"
    2
    "both duplicate walks retain loop-local matching indices")
foreach(_forbidden IN ITEMS
    "uint32_t i;"
    "uint32_t listIndex;"
    "for (i = 0;"
    "surfLists[0][i]"
    "surfLists[1][i]")
    forbid_slice_contains(
        _r_marks "${_forbidden}" "no stale cross-index may return")
endforeach()

# Both bounds are inclusive of zero. A truthiness assertion incorrectly rejects
# valid zero-FOV/zero-distance queries in debug builds.
extract_function_slice(
    "game/actor_senses.cpp"
    "int Actor_CanSeeEntityEx("
    "int __cdecl Actor_CanSeeSentientEx("
    _actor_can_see
    "Actor_CanSeeEntityEx")
require_slice_contains(
    _actor_can_see
    "iassert(fovDot >= 0);"
    "zero is a valid field-of-view dot threshold")
require_slice_contains(
    _actor_can_see
    "iassert(fMaxDistSqrd >= 0);"
    "zero is a valid squared sight distance")
foreach(_forbidden IN ITEMS
    "iassert(fovDot);"
    "iassert(fMaxDistSqrd);")
    forbid_slice_contains(
        _actor_can_see "${_forbidden}" "truthiness must not reject zero")
endforeach()

# The result must be initialized even when Path_NodesInCylinder returns zero;
# otherwise the function returns an indeterminate count on that branch.
extract_function_slice(
    "game/actor_badplace.cpp"
    "int __cdecl Actor_BadPlace_FindSafeNodeOutsideBadPlace("
    "int __cdecl Actor_BadPlace_AttemptEscape("
    _actor_badplace
    "Actor_BadPlace_FindSafeNodeOutsideBadPlace")
require_slice_match_count(
    _actor_badplace
    "nodesWritten = 0;"
    1
    "the output count has one authoritative initialization")
require_slice_ordered(
    _actor_badplace
    "nodeCount = Path_NodesInCylinder("
    "nodesWritten = 0;"
    "the node query precedes output-count initialization")
require_slice_ordered(
    _actor_badplace
    "nodesWritten = 0;"
    "if (nodeCount > 0)"
    "the output count is initialized before the zero-node branch")

# A float member must be returned as a float value. The decompiler-style
# double temporary plus pointer offset is both the wrong value and an aliasing /
# object-representation violation.
extract_function_slice(
    "cgame/cg_draw.cpp"
    "float __cdecl CG_GetBlurRadius("
    "void __cdecl CG_ScreenBlur("
    _cg_blur
    "CG_GetBlurRadius")
require_slice_contains(
    _cg_blur
    "return s_screenBlur[localClientNum].radius;"
    "the stored float radius is returned directly")
require_slice_match_count(
    _cg_blur
    "return "
    1
    "the blur getter has one direct return path")
foreach(_forbidden IN ITEMS
    "double "
    "radius ="
    "&radius"
    "(float *)"
    "(float*)"
    "reinterpret_cast"
    "bit_cast"
    "memcpy("
    "union ")
    forbid_slice_contains(
        _cg_blur "${_forbidden}" "no double or pointer-pun conversion may return")
endforeach()

# A successful trace must return success immediately. Breaking either branch
# exits the sample loop and falls through to the unconditional failure return.
extract_function_slice(
    "game/g_combat.cpp"
    "if (coneAngleCos == -1.0 || !coneDirection)"
    "else"
    _radius_damage_unrestricted
    "G_CanRadiusDamageFromPos unrestricted samples")
require_slice_contains(
    _radius_damage_unrestricted
    "if (G_LocationalTracePassed(centerPos, dest[i], targ->s.number, inflictorNum, contentMask, 0)) return 1;"
    "an unrestricted passing trace returns success")
forbid_slice_contains(
    _radius_damage_unrestricted
    "break;"
    "an unrestricted passing trace must not fall through")

extract_function_slice(
    "game/g_combat.cpp"
    "if (dot >= coneAngleCos)"
    "return 0;"
    _radius_damage_cone
    "G_CanRadiusDamageFromPos cone-qualified samples")
require_slice_contains(
    _radius_damage_cone
    "if (G_LocationalTracePassed(centerPos, dest[i], targ->s.number, inflictorNum, contentMask, 0)) return 1;"
    "a cone-qualified passing trace returns success")
forbid_slice_contains(
    _radius_damage_cone
    "break;"
    "a cone-qualified passing trace must not fall through")

extract_function_slice(
    "game/g_combat.cpp"
    "if (coneAngleCos == -1.0 || !coneDirection)"
    "float __cdecl EntDistToPoint("
    _radius_damage_tail
    "G_CanRadiusDamageFromPos sample-loop tail")
require_slice_match_count(
    _radius_damage_tail
    "return 0;"
    1
    "radius damage fails only after all samples are exhausted")
require_slice_contains(
    _radius_damage_tail
    "return 1; } } } return 0;"
    "the failure return remains outside the five-sample loop")
require_slice_ordered(
    _radius_damage_tail
    "if (dot >= coneAngleCos)"
    "return 0;"
    "loop-exhaustion failure follows the cone-qualified branch")
