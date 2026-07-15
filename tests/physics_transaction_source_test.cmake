cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_source_path "${SOURCE_ROOT}/src/physics/phys_ode.cpp")
set(_cg_sp_path "${SOURCE_ROOT}/src/cgame/cg_ents.cpp")
set(_cg_mp_path "${SOURCE_ROOT}/src/cgame_mp/cg_ents_mp.cpp")
set(_dynent_path "${SOURCE_ROOT}/src/DynEntity/DynEntity_pieces.cpp")
foreach(_path IN ITEMS
    "${_source_path}" "${_cg_sp_path}" "${_cg_mp_path}" "${_dynent_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing physics transaction source: ${_path}")
    endif()
endforeach()
file(READ "${_source_path}" _source)
file(READ "${_cg_sp_path}" _cg_sp)
file(READ "${_cg_mp_path}" _cg_mp)
file(READ "${_dynent_path}" _dynent)

function(extract_slice source start_marker end_marker out_var description)
    string(FIND "${source}" "${start_marker}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR "Could not find start of ${description}")
    endif()
    string(SUBSTRING "${source}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${end_marker}" _end)
    if(_end LESS_EQUAL 0)
        message(FATAL_ERROR "Could not find ordered end of ${description}")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_end} _slice)
    set(${out_var} "${_slice}" PARENT_SCOPE)
endfunction()

function(require_contains haystack needle description)
    string(FIND "${haystack}" "${needle}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing physics transaction invariant: ${description}")
    endif()
endfunction()

function(forbid_contains haystack needle description)
    string(FIND "${haystack}" "${needle}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden physics transaction behavior: ${description}")
    endif()
endfunction()

function(require_ordered haystack first second description)
    string(FIND "${haystack}" "${first}" _first)
    if(_first EQUAL -1)
        message(FATAL_ERROR
            "Missing first physics transaction invariant: ${description}")
    endif()
    string(LENGTH "${first}" _first_length)
    math(EXPR _tail_start "${_first} + ${_first_length}")
    string(SUBSTRING "${haystack}" ${_tail_start} -1 _tail)
    string(FIND "${_tail}" "${second}" _second)
    if(_second EQUAL -1)
        message(FATAL_ERROR
            "Missing or unordered physics transaction invariant: ${description}")
    endif()
endfunction()

function(forbid_matches haystack pattern description)
    string(REGEX MATCH "${pattern}" _match "${haystack}")
    if(NOT _match STREQUAL "")
        message(FATAL_ERROR
            "Forbidden physics transaction behavior (${description}): ${_match}")
    endif()
endfunction()

set(_reporting_pattern
    "(MyAssertHandler|Com_Print[A-Za-z0-9_]*|Com_Error|Sys_Error|iassert|vassert|fprintf)[ \\t\\r\\n]*\\(")
set(_legacy_mutation_pattern
    "(dBody(Set|Create|Destroy)|dGeomTransformSet|dSpace(Remove|Add)|ODE_GeomDestruct|Pool_(Alloc|Free))[A-Za-z0-9_]*[ \\t\\r\\n]*\\(")

# Body and user-data acquisition is one caller-locked, intrinsically silent
# transaction. Every calculation and capacity check precedes allocation; the
# body is silently returned if the companion allocation fails; publication is
# a direct-store suffix with the caller output written last.
extract_slice(
    "${_source}"
    "static PhysBodyModelCreateStatus Phys_TryCreateBodyFromStateInternal("
    "void __cdecl Phys_ReportBodyModelCreateFailure("
    _body_create
    "silent body/user-data creation core")
require_ordered("${_body_create}"
    "*outBody = nullptr;"
    "Phys_TryBuildBodyRotationNoReport("
    "body output is cleared before body-state validation")
require_ordered("${_body_create}"
    "Phys_TryBuildGeomMassNoReport("
    "Phys_TryGetFreeResourceCapacityLockedNoReport("
    "mass construction precedes fixed-pool capacity inspection")
require_ordered("${_body_create}"
    "Phys_TryGetFreeResourceCapacityLockedNoReport("
    "ODE_TryBodyCreateNoReport("
    "complete capacity validation precedes native body allocation")
require_ordered("${_body_create}"
    "ODE_TryBodyCreateNoReport("
    "Pool_TryAllocNoReport("
    "the native body is acquired before its user-data companion")
require_ordered("${_body_create}"
    "Pool_TryAllocNoReport("
    "ODE_TryBodyDestroyNoReport(body)"
    "user-data allocation failure silently returns the fresh body")
require_ordered("${_body_create}"
    "// All fallible work is complete. Publish only direct stores from here;"
    "body->userdata = userData;"
    "body publication starts only after all rejection paths")
require_ordered("${_body_create}"
    "body->userdata = userData;"
    "userData->body = body;"
    "body/user-data ownership is published reciprocally")
require_ordered("${_body_create}"
    "userData->body = body;"
    "*outBody = body;"
    "caller ownership is published last")
forbid_matches("${_body_create}" "${_reporting_pattern}"
    "body creation cannot report while PHYSICS is owned")
forbid_matches("${_body_create}" "${_legacy_mutation_pattern}"
    "body creation cannot call assertion-bearing legacy mutation APIs")

# Geometry construction follows the same prepare/acquire/commit split. In
# particular, ODE's legacy oriented-geom path was a direct transpose (not a
# quaternion round trip), and center-of-mass or mass state is not published
# until the optional transform has been completely nested.
extract_slice(
    "${_source}"
    "static PhysBodyModelCreateStatus Phys_TryBodyAddGeomAndSetMass("
    "void __cdecl Phys_BodyAddGeomAndSetMass("
    _geom_create
    "silent geometry transaction core")
require_ordered("${_geom_create}"
    "Phys_TryBuildGeomMassNoReport("
    "Phys_TryPrepareCenterOfMassUpdateNoReport("
    "mass validation precedes center-of-mass preparation")
require_ordered("${_geom_create}"
    "Phys_TryPrepareCenterOfMassUpdateNoReport("
    "Phys_TryGetFreeResourceCapacityLockedNoReport("
    "all pointer and COM validation precedes capacity inspection")
require_ordered("${_geom_create}"
    "capacity.geomCount < requiredGeomCount"
    "Phys_TryCreatePrimaryGeomNoReport("
    "exact geom demand is rejected before the first allocation")
require_contains("${_geom_create}"
    "orientedRotation[row * 4 + column] =\n                    geomState->orientation[column][row];"
    "oriented transforms preserve the legacy direct Axis-to-ODE transpose")
forbid_matches("${_geom_create}"
    "(QuatToAxis|Phys_TryBuildBodyRotationNoReport|dGeomTransformSetRotation)[ \\t\\r\\n]*\\("
    "oriented geoms must not round-trip through a quaternion")
require_ordered("${_geom_create}"
    "ODE_TryCreateGeomTransformNoReport("
    "ODE_TryGeomTransformSetGeomNoReport(transform, primary)"
    "transform allocation precedes silent child nesting")
require_ordered("${_geom_create}"
    "ODE_TryGeomTransformSetGeomNoReport(transform, primary)"
    "Phys_CommitCenterOfMassUpdateNoReport("
    "all fallible geom construction precedes COM publication")
require_ordered("${_geom_create}"
    "Phys_CommitCenterOfMassUpdateNoReport("
    "body->mass = mass;"
    "mass publication follows the completed COM/space commit")
require_ordered("${_geom_create}"
    "body->mass = mass;"
    "body->invMass = inverseMass;"
    "mass and inverse state publish as one no-fail suffix")
require_contains("${_geom_create}"
    "ODE_TryGeomDestructNoReport(primary)"
    "partial primary allocation has silent rollback")
require_contains("${_geom_create}"
    "ODE_TryGeomDestructNoReport(transform)"
    "partial transform allocation has silent rollback")
forbid_matches("${_geom_create}" "${_reporting_pattern}"
    "geometry creation cannot report while PHYSICS is owned")
forbid_matches("${_geom_create}" "${_legacy_mutation_pattern}"
    "geometry creation cannot call assertion-bearing legacy mutation APIs")

# Preserve dSINGLE expression order. Algebraically equivalent double
# intermediates change inertia and trajectories, so the silent builder must
# keep ODE's float operations and reject underflowed moments.
extract_slice(
    "${_source}"
    "bool Phys_TryBuildGeomMassNoReport(\n    const float totalMass,\n    const GeomState &geomState,\n    dMass *const outMass,\n    float (*const outInverse)[12],\n    float *const outInverseMass) noexcept\n{"
    "bool Phys_TryPrepareCenterOfMassUpdateNoReport(\n    const PhysWorld worldIndex,\n    dxBody *const body,\n    const float *const newRelativeCenter,\n    dxGeom *const excludedTransform,\n    PhysCenterOfMassUpdate *const outUpdate) noexcept\n{"
    _mass_builder
    "silent primitive mass builder")
foreach(_float_expression IN ITEMS
    "const float inertia = 0.4f * totalMass * 1.0f * 1.0f;"
    "const float scale = totalMass / 12.0f;"
    "const float radiusSquared = radius * radius;"
    "const float cylinderMass ="
    "const float capMass = (4.0f / 3.0f)"
    "const float adjustment = totalMass / mass.mass;"
    "mass.I[row * 4 + column] *= adjustment;"
    "if (!(moment > 0.0f) || !std::isfinite(moment))"
    "Phys_TryFinalizeMassTensorNoReport(")
    require_contains("${_mass_builder}" "${_float_expression}"
        "primitive mass construction retains exact float validation/order")
endforeach()
forbid_matches("${_mass_builder}"
    "(^|[^A-Za-z0-9_])double([^A-Za-z0-9_]|$)"
    "primitive inertia must not introduce double intermediates")
forbid_matches("${_mass_builder}"
    "(^|[\\r\\n])[ \\t]*dMass(Set|Adjust|Translate|Rotate)[A-Za-z0-9_]*[ \\t\\r\\n]*\\("
    "silent mass construction cannot call assertion-bearing ODE helpers")

# COM preparation owns every fallible topology/finite check. The commit then
# preserves the simple-space dirty-prefix invariant and restores the newly
# attached outer geom as list head before mass publication in the caller.
extract_slice(
    "${_source}"
    "bool Phys_TryPrepareCenterOfMassUpdateNoReport(\n    const PhysWorld worldIndex,"
    "void Phys_CommitCenterOfMassUpdateNoReport(\n    const PhysWorld worldIndex,\n    dxBody *const body,\n    dxGeom *const excludedTransform,\n    dxGeom *const newOuter,\n    const PhysCenterOfMassUpdate &update) noexcept\n{"
    _com_prepare
    "center-of-mass prepare pass")
require_ordered("${_com_prepare}"
    "*outUpdate = {};"
    "ODE_TryValidateGlobalGeomListsNoReport()"
    "COM output is cleared before global topology validation")
require_ordered("${_com_prepare}"
    "for (dxGeom *geom = body->geom; geom; geom = geom->body_next)"
    "*outUpdate = update;"
    "every existing transform is validated before output publication")
forbid_matches("${_com_prepare}" "${_reporting_pattern}"
    "COM preparation is intrinsically silent")

extract_slice(
    "${_source}"
    "void Phys_CommitCenterOfMassUpdateNoReport(\n    const PhysWorld worldIndex,\n    dxBody *const body,\n    dxGeom *const excludedTransform,\n    dxGeom *const newOuter,\n    const PhysCenterOfMassUpdate &update) noexcept\n{"
    "bool Phys_RollbackBodyStateIsValid("
    _com_commit
    "center-of-mass no-fail commit")
require_ordered("${_com_commit}"
    "geom->spaceRemove();"
    "geom->spaceAdd(&space->first);"
    "pre-existing clean geoms are promoted before being dirtied")
require_ordered("${_com_commit}"
    "geom->spaceAdd(&space->first);"
    "geom->gflags |= GEOM_DIRTY | GEOM_AABB_BAD;"
    "simple-space promotion precedes dirty publication")
require_ordered("${_com_commit}"
    "if (newOuter && space->first != newOuter)"
    "space->current_geom = nullptr;"
    "the new outer is reheaded before enumeration cache invalidation")
forbid_matches("${_com_commit}" "${_reporting_pattern}"
    "the COM commit must remain no-fail and non-reporting")

# Public entry points own lock transitions and perform legacy diagnostics only
# after leaving PHYSICS. The silent core itself never acquires or releases the
# lock, which also makes it safe for larger caller-owned transactions.
function(require_reporting_wrapper start end core report description)
    extract_slice("${_source}" "${start}" "${end}" _wrapper "${description}")
    require_ordered("${_wrapper}"
        "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
        "${core}"
        "${description} enters PHYSICS before its silent core")
    require_ordered("${_wrapper}"
        "${core}"
        "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
        "${description} retains PHYSICS through its silent core")
    require_ordered("${_wrapper}"
        "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
        "${report}"
        "${description} reports only after leaving PHYSICS")
endfunction()

require_reporting_wrapper(
    "dxBody *__cdecl Phys_ObjCreate("
    "void __cdecl Phys_ObjSetOrientation("
    "Phys_TryObjCreateLockedNoReportInternal("
    "Phys_ReportBodyModelCreateFailure(status, resourceFailure);"
    "Phys_ObjCreate")
require_reporting_wrapper(
    "dxBody *__cdecl Phys_ObjCreateAxis("
    "dxBody *__cdecl Phys_CreateBodyFromState("
    "Phys_TryObjCreateAxisLockedNoReportInternal("
    "Phys_ReportBodyModelCreateFailure(status, resourceFailure);"
    "Phys_ObjCreateAxis")
require_reporting_wrapper(
    "dxBody *__cdecl Phys_CreateBodyFromState("
    "void __cdecl Phys_BodyGetCenterOfMass("
    "Phys_TryCreateBodyFromStateInternal("
    "Phys_ReportBodyModelCreateFailure(status, resourceFailure);"
    "Phys_CreateBodyFromState")
require_reporting_wrapper(
    "void __cdecl Phys_BodyAddGeomAndSetMass("
    "void __cdecl Phys_AdjustForNewCenterOfMass("
    "Phys_TryBodyAddGeomAndSetMass("
    "Phys_ReportBodyModelCreateFailure("
    "Phys_BodyAddGeomAndSetMass")
require_reporting_wrapper(
    "void __cdecl Phys_ObjSetCollisionFromXModel("
    "namespace\n{\n#if defined(_MSC_VER)"
    "Phys_TryObjSetCollisionFromXModelLockedNoReport("
    "Phys_ReportBodyModelCreateFailure("
    "Phys_ObjSetCollisionFromXModel")
require_reporting_wrapper(
    "PhysBodyModelCreateStatus __cdecl Phys_TryCreateBodyFromStateAndXModel("
    "PhysBodyModelCreateStatus __cdecl\nPhys_TryCreateBodyFromStateAndXModelLockedNoReport("
    "Phys_TryCreateBodyFromStateAndXModelInternal("
    "Phys_ReportBodyModelCreateFailure(status, resourceFailure);"
    "Phys_TryCreateBodyFromStateAndXModel")
require_reporting_wrapper(
    "PhysBodyModelCreateStatus __cdecl Phys_TryCreateBodyFromPresetAndXModel("
    "PhysBodyModelCreateStatus __cdecl\nPhys_TryCreateBodyFromPresetAndXModelLockedNoReport("
    "Phys_TryCreateBodyFromPresetAndXModelInternal("
    "Phys_ReportBodyModelCreateFailure(status, resourceFailure);"
    "Phys_TryCreateBodyFromPresetAndXModel")

extract_slice(
    "${_source}"
    "PhysBodyModelCreateStatus __cdecl Phys_TryObjCreateLockedNoReport("
    "dxBody *__cdecl Phys_ObjCreateAxis("
    _locked_body_api
    "public caller-locked body API")
forbid_matches("${_locked_body_api}"
    "Sys_(Enter|Leave)CriticalSection[ \\t\\r\\n]*\\("
    "caller-locked body API must not alter lock depth")
forbid_matches("${_locked_body_api}" "${_reporting_pattern}"
    "caller-locked body API must remain non-reporting")

# CG owns one continuous transaction from complete body/model construction
# through initial impact. A rejected impact silently retires the unpublished
# body; fail-stop and ordinary diagnostics happen only after unlock, and the
# pose token is the final success publication.
function(require_cg_spawn_contract source start_marker description)
    extract_slice("${source}" "${start_marker}"
        "void __cdecl CG_UpdatePhysicsPose("
        _cg_spawn "${description} physics spawn")
    require_ordered("${_cg_spawn}"
        "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
        "Phys_TryCreateBodyFromPresetAndXModelLockedNoReport("
        "${description} locks before complete silent construction")
    require_ordered("${_cg_spawn}"
        "Phys_TryCreateBodyFromPresetAndXModelLockedNoReport("
        "Phys_TryObjBulletImpactLockedNoReport("
        "${description} completes collision before initial impact")
    require_ordered("${_cg_spawn}"
        "Phys_TryObjBulletImpactLockedNoReport("
        "Phys_TryDestroyBodyLockedNoReport("
        "${description} has silent rollback for rejected impact")
    require_ordered("${_cg_spawn}"
        "Phys_TryDestroyBodyLockedNoReport("
        "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
        "${description} retains exclusion through rollback")
    require_ordered("${_cg_spawn}"
        "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
        "std::abort();"
        "${description} fail-stops only after unlock")
    require_ordered("${_cg_spawn}"
        "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
        "Phys_ReportBodyModelCreateFailure(status, resourceFailure);"
        "${description} exact resource diagnostics occur only after unlock")
    require_ordered("${_cg_spawn}"
        "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
        "cent->pose.physObjId ="
        "${description} publishes body ownership only after unlock")
    foreach(_legacy_call IN ITEMS
        "Phys_ObjCreate("
        "DObjPhysicsSetCollisionFromXModel("
        "Phys_ObjBulletImpact("
        "Phys_ObjDestroy(")
        forbid_contains("${_cg_spawn}" "${_legacy_call}"
            "${description} cannot call ${_legacy_call} under PHYSICS")
    endforeach()
endfunction()

require_cg_spawn_contract(
    "${_cg_sp}"
    "void __cdecl CG_CreatePhysicsObject(int localClientNum, centity_s *cent)"
    "SP CG")
require_cg_spawn_contract(
    "${_cg_mp}"
    "void __cdecl CG_CreatePhysicsObject(int32_t localClientNum, centity_s *cent)"
    "MP CG")

# Breakable pieces intentionally create in PHYS_WORLD_FX but retain the
# legacy PHYS_WORLD_DYNENT impact scaling. Force derivation, impact, rollback,
# and successful-body ownership all stay within/after the same lock boundary.
extract_slice(
    "${_dynent}"
    "bool __cdecl DynEntPieces_SpawnPhysicsModel("
    "dxBody *__cdecl DynEntPieces_SpawnPhysObj("
    _dynent_spawn
    "DynEnt pieces physics spawn")
require_ordered("${_dynent_spawn}"
    "impactDirection[component] = hitDir[component];"
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "DynEnt caches caller-owned impact vectors before locking")
require_ordered("${_dynent_spawn}"
    "const float impactForce = impactForceDvar->current.value;"
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "DynEnt caches the impact dvar before locking")
require_ordered("${_dynent_spawn}"
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "DynEntPieces_TrySpawnPhysObjLockedNoReport("
    "DynEnt pieces lock before silent body/geom/configuration creation")
require_ordered("${_dynent_spawn}"
    "DynEntPieces_TrySpawnPhysObjLockedNoReport("
    "DynEntPieces_CalcForceDir("
    "DynEnt force calculation occurs only for a successful fresh body")
require_ordered("${_dynent_spawn}"
    "DynEntPieces_CalcForceDir("
    "Phys_TryObjBulletImpactLockedNoReport(\n                    PHYS_WORLD_DYNENT,"
    "DynEnt impact preserves the legacy DYNENT force-scaling world")
require_ordered("${_dynent_spawn}"
    "Phys_TryObjBulletImpactLockedNoReport("
    "Phys_TryDestroyBodyLockedNoReport(\n                        PHYS_WORLD_FX,"
    "rejected DynEnt impact returns the body to its FX allocation world")
require_ordered("${_dynent_spawn}"
    "Phys_TryDestroyBodyLockedNoReport("
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "DynEnt rollback completes before unlock")
require_ordered("${_dynent_spawn}"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "g_breakablePieces[numPieces].physObjId ="
    "DynEnt ownership publishes only after unlock")
require_ordered("${_dynent_spawn}"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "Phys_ReportBodyModelCreateFailure(\n                spawnResult.status, spawnResult.resourceFailure);"
    "DynEnt exact resource diagnostics occur only after unlock")
require_ordered("${_dynent_spawn}"
    "if (spawnResult.capacityExceeded)"
    "piece capacity exhausted."
    "DynEnt capacity exhaustion is handled as an ordinary warning")
foreach(_legacy_call IN ITEMS
    "Phys_ObjCreate("
    "Phys_ObjAddGeomBox("
    "Phys_ObjSetAngularVelocity("
    "Phys_ObjBulletImpact("
    "Phys_ObjDestroy(")
    forbid_contains("${_dynent_spawn}" "${_legacy_call}"
        "DynEnt spawn cannot call ${_legacy_call} in its transaction")
endforeach()

extract_slice(
    "${_dynent}"
    "DynEntPiecesPhysSpawnResult DynEntPieces_TrySpawnPhysObjLockedNoReport("
    "} // namespace"
    _dynent_helper
    "DynEnt pieces silent construction helper")
require_ordered("${_dynent_helper}"
    "Phys_TryObjCreateLockedNoReport(\n        PHYS_WORLD_FX,"
    "Phys_TryObjAddGeomBoxLockedNoReport(\n        PHYS_WORLD_FX,"
    "DynEnt body/user-data creation precedes box construction")
require_ordered("${_dynent_helper}"
    "Phys_TryObjAddGeomBoxLockedNoReport("
    "Phys_TryDestroyBodyLockedNoReport("
    "DynEnt geom failure silently returns the fresh body")
forbid_contains("${_dynent_helper}"
    "Phys_TryObjAddGeomBoxNoReport("
    "DynEnt caller-locked helper cannot recurse through the locking wrapper")
require_ordered("${_dynent_helper}"
    "Phys_TryObjSetAngularVelocityLockedNoReport("
    "Phys_TryDestroyBodyLockedNoReport("
    "DynEnt configuration failure silently returns the fresh body")
forbid_matches("${_dynent_helper}" "${_reporting_pattern}"
    "DynEnt caller-locked helper must remain non-reporting")

message(STATUS "Physics transaction source invariants verified")
