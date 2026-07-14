cmake_minimum_required(VERSION 3.16)

if (NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_phys_ode_path "${SOURCE_ROOT}/src/physics/phys_ode.cpp")
if (NOT EXISTS "${_phys_ode_path}")
    message(FATAL_ERROR "Missing physics source: ${_phys_ode_path}")
endif()
file(READ "${_phys_ode_path}" _phys_ode_source)

function(extract_source_slice SOURCE_VAR START_MARKER END_MARKER OUT_VAR DESCRIPTION)
    set(_source "${${SOURCE_VAR}}")
    string(FIND "${_source}" "${START_MARKER}" _start)
    if (_start EQUAL -1)
        message(FATAL_ERROR
            "Missing start of ${DESCRIPTION} in src/physics/phys_ode.cpp")
    endif()

    string(FIND "${_source}" "${END_MARKER}" _end)
    if (_end EQUAL -1 OR _end LESS_EQUAL _start)
        message(FATAL_ERROR
            "Missing end of ${DESCRIPTION} in src/physics/phys_ode.cpp")
    endif()

    math(EXPR _length "${_end} - ${_start}")
    string(SUBSTRING "${_source}" ${_start} ${_length} _slice)
    set(${OUT_VAR} "${_slice}" PARENT_SCOPE)
endfunction()

function(require_slice_contains SLICE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SLICE_VAR}}" "${NEEDLE}" _position)
    if (_position EQUAL -1)
        message(FATAL_ERROR
            "Missing physics transaction invariant (${DESCRIPTION})")
    endif()
endfunction()

function(require_slice_matches SLICE_VAR PATTERN DESCRIPTION)
    string(REGEX MATCH "${PATTERN}" _match "${${SLICE_VAR}}")
    if (_match STREQUAL "")
        message(FATAL_ERROR
            "Missing physics transaction invariant (${DESCRIPTION})")
    endif()
endfunction()

function(require_slice_ordered SLICE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SLICE_VAR}}" "${FIRST}" _first_position)
    string(FIND "${${SLICE_VAR}}" "${SECOND}" _second_position)
    if (_first_position EQUAL -1
        OR _second_position EQUAL -1
        OR _first_position GREATER_EQUAL _second_position)
        message(FATAL_ERROR
            "Missing or unordered physics transaction invariant (${DESCRIPTION})")
    endif()
endfunction()

function(require_slice_literal_count SLICE_VAR NEEDLE EXPECTED_COUNT DESCRIPTION)
    set(_remaining "${${SLICE_VAR}}")
    set(_count 0)
    string(LENGTH "${NEEDLE}" _needle_length)
    if (_needle_length EQUAL 0)
        message(FATAL_ERROR "Cannot count an empty physics invariant")
    endif()

    while(TRUE)
        string(FIND "${_remaining}" "${NEEDLE}" _position)
        if (_position EQUAL -1)
            break()
        endif()
        math(EXPR _count "${_count} + 1")
        math(EXPR _next "${_position} + ${_needle_length}")
        string(SUBSTRING "${_remaining}" ${_next} -1 _remaining)
    endwhile()

    if (NOT _count EQUAL EXPECTED_COUNT)
        message(FATAL_ERROR
            "Unexpected physics transaction invariant count (${DESCRIPTION}): "
            "expected ${EXPECTED_COUNT}, found ${_count}")
    endif()
endfunction()

extract_source_slice(
    _phys_ode_source
    "static bool Phys_ReleaseCreatedBodyResources("
    "static PhysBodyModelCreateStatus Phys_TryCreateBodyFromStateInternal("
    _release_body_scope
    "created body/user-data cleanup")
require_slice_ordered(
    _release_body_scope
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "dBodyDestroy(body);"
    "created body destruction must begin under the physics lock")
require_slice_ordered(
    _release_body_scope
    "dBodyDestroy(body);"
    "Pool_Free("
    "body destruction must precede its paired user-data release")
require_slice_ordered(
    _release_body_scope
    "Pool_Free("
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "pool-backed user data must be released before unlocking physics")
require_slice_ordered(
    _release_body_scope
    "free(userData);"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "all allocator variants must release user data before unlocking physics")

extract_source_slice(
    _phys_ode_source
    "static bool Phys_DestroyBodyResource("
    "static bool Phys_DestroyFreshBodyResourceNoReport("
    _destroy_body_scope
    "diagnostic body rollback callback")
require_slice_matches(
    _destroy_body_scope
    "if[ \t\r\n]*\\([ \t\r\n]*!body[ \t\r\n]*\\)[ \t\r\n]*return[ \t\r\n]+false[ \t\r\n]*;"
    "the diagnostic body rollback callback must reject an invalid handle")
require_slice_ordered(
    _destroy_body_scope
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "dBodyDestroy(static_cast<dxBody *>(body));"
    "diagnostic body rollback must acquire physics exclusion before destruction")
require_slice_ordered(
    _destroy_body_scope
    "dBodyDestroy(static_cast<dxBody *>(body));"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "diagnostic body rollback must retain physics exclusion through destruction")
require_slice_ordered(
    _destroy_body_scope
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "return true;"
    "the diagnostic body rollback callback must report successful destruction")

extract_source_slice(
    _phys_ode_source
    "static PhysBodyModelCreateStatus Phys_TryCreateBodyFromStateInternal("
    "dxBody *__cdecl Phys_CreateBodyFromState("
    _try_body_scope
    "Phys_TryCreateBodyFromStateInternal")
require_slice_matches(
    _try_body_scope
    "resources\\.status[ \t\r\n]*==[ \t\r\n]*physics::allocation::ResourcePairStatus::PrimaryCleanupFailed[^}]*return[ \t\r\n]+PhysBodyModelCreateStatus::CleanupFailed[ \t\r\n]*;"
    "failed body cleanup must be exposed as an unrecoverable creation failure")
require_slice_ordered(
    _try_body_scope
    "resources.status == physics::allocation::ResourcePairStatus::PrimaryCleanupFailed"
    "resources.status != physics::allocation::ResourcePairStatus::Success"
    "body cleanup failure must be classified before generic allocator errors")

set(_try_geom_start
    "static PhysBodyModelCreateStatus Phys_TryBodyAddGeomAndSetMass(")
set(_try_geom_end "void __cdecl Phys_BodyAddGeomAndSetMass(")
extract_source_slice(
    _phys_ode_source
    "${_try_geom_start}"
    "${_try_geom_end}"
    _try_geom_scope
    "Phys_TryBodyAddGeomAndSetMass")

extract_source_slice(
    _phys_ode_source
    "static bool Phys_DestroyPrimaryGeomResource("
    "static bool Phys_DestroyFreshPrimaryGeomResourceNoReport("
    _destroy_primary_scope
    "Phys_DestroyPrimaryGeomResource")
require_slice_matches(
    _destroy_primary_scope
    "if[ \t\r\n]*\\([ \t\r\n]*!geom[ \t\r\n]*\\)[ \t\r\n]*return[ \t\r\n]+false[ \t\r\n]*;"
    "the diagnostic primary-geometry rollback callback must reject an invalid handle")
require_slice_ordered(
    _destroy_primary_scope
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "ODE_GeomDestruct(static_cast<dxGeom *>(geom));"
    "diagnostic primary-geometry rollback must acquire physics exclusion before destruction")
require_slice_ordered(
    _destroy_primary_scope
    "ODE_GeomDestruct(static_cast<dxGeom *>(geom));"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "diagnostic primary-geometry rollback must retain physics exclusion through destruction")
require_slice_ordered(
    _destroy_primary_scope
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "return true;"
    "the diagnostic primary-geometry rollback callback must report successful destruction")

extract_source_slice(
    _phys_ode_source
    "static bool Phys_DestroyPrimaryGeomResource("
    "${_try_geom_start}"
    _destroy_geom_callbacks_scope
    "primary geometry rollback callbacks")
require_slice_contains(
    _destroy_geom_callbacks_scope
    "ODE_GeomDestruct(static_cast<dxGeom *>(geom));"
    "the production primary-geometry rollback callback must destroy its ODE geom")

# Keep callback validation scoped to the allocator owner. The callbacks live
# directly in Phys_TryBodyAddGeomAndSetMass today; a later refactor may move
# them into a dedicated Phys_CreateGeomResourcePair immediately above it.
string(FIND
    "${_phys_ode_source}"
    "Phys_CreateGeomResourcePair("
    _create_pair_position)
string(FIND "${_phys_ode_source}" "${_try_geom_start}" _try_geom_position)
if (NOT _create_pair_position EQUAL -1
    AND _create_pair_position LESS _try_geom_position)
    math(EXPR _create_pair_length
        "${_try_geom_position} - ${_create_pair_position}")
    string(SUBSTRING
        "${_phys_ode_source}"
        ${_create_pair_position}
        ${_create_pair_length}
        _resource_pair_scope)
    set(_allocation_call "Phys_CreateGeomResourcePair(")
else()
    set(_resource_pair_scope "${_try_geom_scope}")
    set(_allocation_call "physics::allocation::TryCreateResourcePair(")
endif()

require_slice_matches(
    _resource_pair_scope
    "ResourcePairCallbacks[ \t\r\n]+resourceCallbacks[ \t\r\n]*\\{[ \t\r\n]*&resourceContext[ \t\r\n]*,[ \t\r\n]*Phys_CreatePrimaryGeomResource[ \t\r\n]*,[ \t\r\n]*Phys_CreateTransformGeomResource[ \t\r\n]*,[ \t\r\n]*reportFailure[ \t\r\n]*\\?[ \t\r\n]*Phys_DestroyPrimaryGeomResource[ \t\r\n]*:[ \t\r\n]*Phys_DestroyFreshPrimaryGeomResourceNoReport[ \t\r\n]*,[ \t\r\n]*\\};"
    "the transform allocator must wire primary creation, transform creation, and diagnostic/no-report rollback in that order")

require_slice_contains(
    _try_geom_scope
    "${_allocation_call}"
    "fallible geometry acquisition must use the transactional allocator")

# Start ordering checks at the production allocation call. This deliberately
# excludes the PHYS_GEOM_NONE success path, whose center-of-mass adjustment is
# non-fallible and correctly precedes its mass publication.
string(FIND "${_try_geom_scope}" "${_allocation_call}" _allocation_position)
string(SUBSTRING
    "${_try_geom_scope}"
    0
    ${_allocation_position}
    _preallocation_scope)
string(SUBSTRING
    "${_try_geom_scope}"
    ${_allocation_position}
    -1
    _fallible_geom_scope)

set(_adjust_call "Phys_AdjustForNewCenterOfMass(body, centerOfMass);")
require_slice_literal_count(
    _preallocation_scope
    "${_adjust_call}"
    1
    "only the non-fallible PHYS_GEOM_NONE path may mutate center-of-mass state before allocation")
require_slice_ordered(
    _try_geom_scope
    "if (geomState->type == PHYS_GEOM_NONE)"
    "${_adjust_call}"
    "the sole pre-allocation center-of-mass mutation must belong to PHYS_GEOM_NONE")
require_slice_ordered(
    _try_geom_scope
    "${_adjust_call}"
    "dBodySetMass(body, &mass);"
    "the PHYS_GEOM_NONE path must adjust center-of-mass state before publishing mass")
require_slice_ordered(
    _try_geom_scope
    "dBodySetMass(body, &mass);"
    "return PhysBodyModelCreateStatus::Success;"
    "the PHYS_GEOM_NONE path must publish mass before returning success")
require_slice_ordered(
    _try_geom_scope
    "return PhysBodyModelCreateStatus::Success;"
    "${_allocation_call}"
    "fallible geometry allocation must start after the PHYS_GEOM_NONE early return")
require_slice_ordered(
    _fallible_geom_scope
    "${_allocation_call}"
    "${_adjust_call}"
    "all fallible geometry allocation must finish before center-of-mass mutation")
require_slice_ordered(
    _fallible_geom_scope
    "return PhysBodyModelCreateStatus::PrimaryGeomAllocationFailed;"
    "${_adjust_call}"
    "primary allocation failure must return before center-of-mass mutation")
require_slice_ordered(
    _fallible_geom_scope
    "return PhysBodyModelCreateStatus::TransformGeomAllocationFailed;"
    "${_adjust_call}"
    "transform allocation failure must return after rollback and before center-of-mass mutation")
require_slice_matches(
    _fallible_geom_scope
    "resources\\.status[ \t\r\n]*==[ \t\r\n]*physics::allocation::ResourcePairStatus::PrimaryCleanupFailed[^}]*return[ \t\r\n]+PhysBodyModelCreateStatus::CleanupFailed[ \t\r\n]*;"
    "failed primary-geometry cleanup must be exposed as an unrecoverable creation failure")
require_slice_ordered(
    _fallible_geom_scope
    "resources.status == physics::allocation::ResourcePairStatus::PrimaryCleanupFailed"
    "resources.status != physics::allocation::ResourcePairStatus::Success"
    "primary-geometry cleanup failure must be classified before generic allocator errors")
require_slice_ordered(
    _fallible_geom_scope
    "resources.status != physics::allocation::ResourcePairStatus::Success"
    "${_adjust_call}"
    "invalid allocator results must be rejected before center-of-mass mutation")
require_slice_ordered(
    _fallible_geom_scope
    "return PhysBodyModelCreateStatus::InvalidArgument;"
    "${_adjust_call}"
    "invalid allocator results must return before center-of-mass mutation")
require_slice_ordered(
    _fallible_geom_scope
    "${_adjust_call}"
    "dGeomTransformSetGeom(geomTransform, geom);"
    "successful allocation must mutate center-of-mass state before final transform nesting")
require_slice_ordered(
    _fallible_geom_scope
    "dGeomTransformSetGeom(geomTransform, geom);"
    "dSpaceRemove(physGlob.space[worldIndex], outerGeom);"
    "the completed outer geometry must be selected before space reinsertion")
require_slice_ordered(
    _fallible_geom_scope
    "dSpaceRemove(physGlob.space[worldIndex], outerGeom);"
    "dSpaceAdd(physGlob.space[worldIndex], outerGeom);"
    "legacy simple-space ordering must remove before re-adding the outer geometry")
require_slice_ordered(
    _fallible_geom_scope
    "dSpaceAdd(physGlob.space[worldIndex], outerGeom);"
    "dBodySetMass(body, &mass);"
    "mass publication must follow completed geometry construction")
require_slice_ordered(
    _fallible_geom_scope
    "dBodySetMass(body, &mass);"
    "return PhysBodyModelCreateStatus::Success;"
    "success must be returned only after the new mass is published")

message(STATUS "Physics transaction source invariants verified")
