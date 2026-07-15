cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_header_path "${SOURCE_ROOT}/src/physics/phys_local.h")
set(_source_path "${SOURCE_ROOT}/src/physics/phys_ode.cpp")
set(_ode_path "${SOURCE_ROOT}/src/physics/ode/ode.cpp")
set(_geom_path "${SOURCE_ROOT}/src/physics/ode/collision_kernel.cpp")
set(_box_path "${SOURCE_ROOT}/src/physics/ode/collision_std.cpp")
set(_transform_path "${SOURCE_ROOT}/src/physics/ode/collision_transform.cpp")
foreach(_path IN ITEMS
    "${_header_path}" "${_source_path}" "${_ode_path}"
    "${_geom_path}" "${_box_path}" "${_transform_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing physics rollback source: ${_path}")
    endif()
endforeach()
file(READ "${_header_path}" _header)
file(READ "${_source_path}" _source)
file(READ "${_ode_path}" _ode)
file(READ "${_geom_path}" _geom)
file(READ "${_box_path}" _box)
file(READ "${_transform_path}" _transform)

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
        message(FATAL_ERROR "Missing physics rollback invariant: ${description}")
    endif()
endfunction()

function(require_matches haystack pattern description)
    string(REGEX MATCH "${pattern}" _match "${haystack}")
    if(_match STREQUAL "")
        message(FATAL_ERROR "Missing physics rollback invariant: ${description}")
    endif()
endfunction()

function(require_ordered haystack first second description)
    string(FIND "${haystack}" "${first}" _first)
    if(_first EQUAL -1)
        message(FATAL_ERROR "Missing first physics rollback invariant: ${description}")
    endif()
    string(SUBSTRING "${haystack}" ${_first} -1 _tail)
    string(FIND "${_tail}" "${second}" _second)
    if(_second LESS_EQUAL 0)
        message(FATAL_ERROR
            "Missing or unordered physics rollback invariant: ${description}")
    endif()
endfunction()

function(forbid_matches haystack pattern description)
    string(REGEX MATCH "${pattern}" _match "${haystack}")
    if(NOT _match STREQUAL "")
        message(FATAL_ERROR
            "Forbidden physics rollback behavior (${description}): ${_match}")
    endif()
endfunction()

set(_reporting_pattern
    "(MyAssertHandler|Com_Print[A-Za-z0-9_]*|Com_Error|Sys_Error|iassert|vassert|dAASSERT|dUASSERT|dDEBUGMSG|fprintf)[ \\t\\r\\n]*\\(")

# These APIs are intentionally status-bearing and noexcept. Callers that own
# CRITSECT_PHYSICS can compose body creation, collision, configuration,
# sidecar publication, and rollback without invoking legacy diagnostics.
foreach(_api IN ITEMS
    Phys_TryObjCreateLockedNoReport
    Phys_TryCreateBodyFromStateAndXModelLockedNoReport
    Phys_TryCreateBodyFromPresetAndXModelLockedNoReport
    Phys_TryGetFreeResourceCapacityLockedNoReport
    Phys_TryValidateBodyDestroyLockedNoReport
    Phys_TryDestroyBodyLockedNoReport
    Phys_TryObjAddGeomBoxNoReport
    Phys_TryObjSetCollisionFromXModelLockedNoReport
    Phys_TryObjSetAngularVelocityLockedNoReport
    Phys_TryObjBulletImpactLockedNoReport)
    require_matches("${_header}"
        "\\[\\[nodiscard\\]\\][ \\t\\r\\n]+[^;{}]+${_api}\\([^;{}]*\\)[ \\t\\r\\n]*noexcept[ \\t\\r\\n]*;"
        "${_api} remains a nodiscard noexcept API")
endforeach()

# The old callback resource-pair and manual body-destroy implementations had
# divergent cleanup semantics. Production now uses the fixed-pool no-report
# primitives and one unified body/user-data destroy core.
foreach(_obsolete IN ITEMS
    "physics::allocation::TryCreateResourcePair("
    "ResourcePairCallbacks"
    "PhysBodyDestroyPlan"
    "Phys_CommitBodyDestroyPlanLockedNoReport("
    "Phys_ReturnValidatedPoolSlotNoReport(")
    string(FIND "${_source}" "${_obsolete}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Obsolete physics transaction implementation remains: ${_obsolete}")
    endif()
endforeach()

# Archive topology classification is large but serialized by PHYSICS. Keep it
# in static storage, clear it on each use, and never put a copy on the small
# engine thread stack.
extract_slice(
    "${_source}"
    "struct PhysGlobalTopologySnapshot"
    "bool Phys_RollbackTransformHasUniqueInnerOwnership("
    _global_topology
    "static archive topology classification")
require_ordered("${_global_topology}"
    "struct PhysGlobalTopologySnapshot"
    "PhysGlobalTopologySnapshot physGlobalTopologyScratch{};"
    "the bounded classification arrays have static storage")
require_ordered("${_global_topology}"
    "PhysGlobalTopologySnapshot physGlobalTopologyScratch{};"
    "PhysGlobalTopologySnapshot &snapshot = physGlobalTopologyScratch;"
    "validation borrows the serialized scratch object")
foreach(_classification IN ITEMS
    "snapshot.worldForBody.fill(PHYS_ROLLBACK_NO_OWNER);"
    "snapshot.bodyForUserData.fill(PHYS_ROLLBACK_NO_OWNER);"
    "snapshot.spaceForGeom.fill(PHYS_ROLLBACK_NO_OWNER);"
    "snapshot.bodyForGeom.fill(PHYS_ROLLBACK_NO_OWNER);"
    "snapshot.transformForInner.fill(PHYS_ROLLBACK_NO_OWNER);"
    "if (!isOuter && !isInner)")
    require_contains("${_global_topology}" "${_classification}"
        "every allocated resource receives one exact topology classification")
endforeach()
forbid_matches("${_global_topology}" "${_reporting_pattern}"
    "archive topology validation must not report")
forbid_matches("${_global_topology}"
    "(Pool_Free|ODE_GeomDestruct|dBodyDestroy|Phys_ObjDestroy)[ \\t\\r\\n]*\\("
    "archive topology validation must remain non-destructive")

extract_slice(
    "${_source}"
    "PhysBodyRollbackStatus __cdecl\nPhys_TryGetFreeResourceCapacityLockedNoReport("
    "PhysBodyRollbackStatus __cdecl Phys_TryCaptureBodyStateLocked("
    _capacity
    "silent free-capacity query")
require_ordered("${_capacity}"
    "*outCapacity = {};"
    "Pool_TryValidateFullNoReport(bodyStorage, &odeGlob.bodyPool)"
    "capacity output is cleared before pool inspection")
require_ordered("${_capacity}"
    "Pool_TryValidateFullNoReport(geomStorage, &odeGlob.geomPool)"
    "Phys_TryValidateBodyUserDataBindingsLockedNoReport()"
    "all pools validate before reciprocal body/user-data classification")
require_ordered("${_capacity}"
    "Phys_TryValidateBodyUserDataBindingsLockedNoReport()"
    "*outCapacity = {"
    "capacity publishes only after exact ownership validation")
forbid_matches("${_capacity}" "${_reporting_pattern}"
    "capacity validation is intrinsically silent")

# Archive reconstruction publishes no body until its complete model collision
# succeeds. Collision failure uses the same unified destroy core as public and
# FX cleanup; a cleanup refusal becomes a terminal status, never lost ownership.
extract_slice(
    "${_source}"
    "static PhysBodyModelCreateStatus\nPhys_TryCreateBodyFromStateAndXModelInternal("
    "PhysBodyModelCreateStatus __cdecl Phys_TryCreateBodyFromStateAndXModel("
    _model_create
    "complete body/model transaction")
require_ordered("${_model_create}"
    "*outBody = nullptr;"
    "Phys_TryGetBodyModelResourceDemand(model, &demand)"
    "complete-model output is cleared before recipe validation")
require_ordered("${_model_create}"
    "Phys_ClassifyModelGeomCapacityFailure("
    "Phys_TryCreateBodyFromStateInternal("
    "complete resource demand is proven before body allocation")
require_ordered("${_model_create}"
    "Phys_TryCreateBodyFromStateInternal("
    "Phys_TryBuildCollisionFromXModel("
    "body acquisition precedes model collision construction")
require_ordered("${_model_create}"
    "Phys_TryBuildCollisionFromXModel("
    "Phys_TryDestroyBodyAndUserDataLockedNoReport(worldIndex, body)"
    "collision failure silently returns every body-owned resource")
require_ordered("${_model_create}"
    "return PhysBodyModelCreateStatus::CleanupFailed;"
    "*outBody = body;"
    "cleanup failure returns before ownership publication")
forbid_matches("${_model_create}" "${_reporting_pattern}"
    "complete-model construction cannot report under caller ownership")

extract_slice(
    "${_source}"
    "PhysBodyDestroyStatus Phys_TryDestroyBodyAndUserDataLockedNoReport(\n    const PhysWorld worldIndex,\n    dxBody *const body) noexcept\n{"
    "} // namespace\n\nvoid __cdecl Phys_ObjDestroy("
    _unified_destroy
    "unified body/user-data destroy core")
require_ordered("${_unified_destroy}"
    "Pool_TryValidateAllocatedNoReport(\n            ODE_BodyPoolStorage()"
    "body->world != physGlob.world[worldIndex]"
    "the body pool address is classified before body fields are trusted")
require_ordered("${_unified_destroy}"
    "userData->body != body"
    "ODE_TryValidateBodyDestroyNoReport(body)"
    "reciprocal user-data ownership is proven before native destroy validation")
require_ordered("${_unified_destroy}"
    "ODE_TryValidateBodyDestroyNoReport(body)"
    "ODE_TryBodyDestroyNoReport(body)"
    "native destruction has a complete non-mutating preflight")
require_ordered("${_unified_destroy}"
    "ODE_TryBodyDestroyNoReport(body)"
    "Pool_TryFreeNoReport("
    "native body/geom retirement precedes companion user-data release")
forbid_matches("${_unified_destroy}" "${_reporting_pattern}"
    "unified destruction cannot report while PHYSICS is owned")
forbid_matches("${_unified_destroy}"
    "(Pool_Free|dBodyDestroy|ODE_GeomDestruct)[ \\t\\r\\n]*\\("
    "unified destruction cannot call legacy assertion-bearing cleanup")

extract_slice(
    "${_source}"
    "PhysBodyRollbackStatus __cdecl\nPhys_TryDestroyBodyLockedNoReport("
    "namespace\n{\nbool Phys_TryFinalizeMassTensorNoReport("
    _archive_destroy
    "archive destroy adapter")
require_ordered("${_archive_destroy}"
    "Phys_TryValidateArchiveBodyDestroyGateLockedNoReport("
    "Phys_TryDestroyBodyAndUserDataLockedNoReport("
    "archive-specific gating precedes the shared destroy core")
forbid_matches("${_archive_destroy}" "${_reporting_pattern}"
    "archive destruction remains non-reporting")

# Brush tensors reject NaN before applying the legacy nonpositive fallback,
# then preserve float multiplication order and ODE's fixed-size float inverse.
extract_slice(
    "${_source}"
    "bool Phys_RollbackMassTensorIsReconstructible("
    "bool Phys_TryGetExactPoolSlotIndex("
    _mass_validation
    "brush tensor validation")
require_ordered("${_mass_validation}"
    "if (!std::isfinite(value))"
    "const double diagonal[3]"
    "nonfinite moments/products are rejected before fallback selection")
require_contains("${_mass_validation}"
    "leadingMinor > 0.0"
    "brush tensor validation retains the complete Sylvester test")

extract_slice(
    "${_source}"
    "bool Phys_TryBuildRoundedMassTensorNoReport(\n    const float totalMass,\n    const PhysMass *const physMass,\n    dMass *const outMass,\n    float (*const outInverse)[12],\n    float *const outInverseMass) noexcept\n{"
    "bool Phys_TrySetInertialTensorLockedNoReport("
    _brush_mass
    "silent brush mass construction")
require_ordered("${_brush_mass}"
    "Phys_RollbackMassTensorIsReconstructible(*physMass)"
    "const float selectedI33"
    "finite/positive-definite validation precedes legacy fallback")
foreach(_brush_expression IN ITEMS
    "const float i23 = totalMass * physMass->productsOfInertia[2];"
    "const float i13 = totalMass * physMass->productsOfInertia[1];"
    "const float i12 = totalMass * physMass->productsOfInertia[0];"
    "const float i33 = totalMass * selectedI33;"
    "const float i22 = totalMass * selectedI22;"
    "const float i11 = totalMass * selectedI11;"
    "Phys_TryFinalizeMassTensorNoReport(")
    require_contains("${_brush_mass}" "${_brush_expression}"
        "brush inertia retains exact float expression order")
endforeach()
forbid_matches("${_brush_mass}" "${_reporting_pattern}"
    "brush mass construction remains silent")

# Initial impact is part of CG/DynEnt ownership publication. The silent core
# validates the exact body/user-data pair and computes every force/torque value
# in temporaries; only a compact suffix mutates accumulators and wake state.
extract_slice(
    "${_source}"
    "bool __cdecl Phys_TryObjBulletImpactLockedNoReport("
    "int __cdecl Phys_IndexFromODEWorld("
    _bullet_impact
    "silent bullet-impact transaction")
require_ordered("${_bullet_impact}"
    "Pool_TryValidateAllocatedNoReport(\n            bodyStorage, &odeGlob.bodyPool, body)"
    "body->world == physGlob.world[candidate]"
    "the exact body pool address is validated before world dereference")
require_ordered("${_bullet_impact}"
    "Phys_TryValidateBodyUserDataBindingsLockedNoReport()"
    "Phys_TryFinalizeMassTensorNoReport("
    "reciprocal body/user-data ownership precedes tensor validation")
require_ordered("${_bullet_impact}"
    "Phys_TryFinalizeMassTensorNoReport("
    "const float forceScale ="
    "body tensor validation precedes impact arithmetic")
require_ordered("${_bullet_impact}"
    "// All fallible work is complete. Publish exactly the dBodyAddForceAtPos,"
    "body->facc[component] = newForceAccumulator[component];"
    "force publication begins only after every finite calculation")
require_ordered("${_bullet_impact}"
    "body->facc[component] = newForceAccumulator[component];"
    "body->flags &= ~dxBodyDisabled;"
    "force and torque publish before wake state")
require_ordered("${_bullet_impact}"
    "body->flags &= ~dxBodyDisabled;"
    "physGlob.worldData[bodyWorldIndex].timeLastUpdate;"
    "the body's actual world supplies its sleep timestamp")
require_contains("${_bullet_impact}"
    "g_phys_msecStep[worldIndex]"
    "the parameter world retains historical impulse scaling")
forbid_matches("${_bullet_impact}" "${_reporting_pattern}"
    "bullet impact cannot report while caller owns PHYSICS")
forbid_matches("${_bullet_impact}"
    "Sys_(Enter|Leave)CriticalSection[ \\t\\r\\n]*\\("
    "caller-locked bullet impact cannot alter lock depth")
forbid_matches("${_bullet_impact}"
    "(^|[\\r\\n])[ \\t]*(dBody|Phys_ObjAddForce)[A-Za-z0-9_]*[ \\t\\r\\n]*\\("
    "bullet impact cannot invoke assertion-bearing mutation helpers")

extract_slice(
    "${_bullet_impact}"
    "bool __cdecl Phys_TryObjBulletImpactLockedNoReport("
    "// All fallible work is complete. Publish exactly the dBodyAddForceAtPos,"
    _bullet_prepare
    "bullet-impact prepare pass")
foreach(_premature_publication IN ITEMS
    "body->facc[component] ="
    "body->tacc[component] ="
    "body->flags &="
    "body->adis_stepsleft ="
    "body->adis_timeleft ="
    "userData->timeLastAsleep =")
    string(FIND "${_bullet_prepare}" "${_premature_publication}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Bullet-impact state mutates before commit: ${_premature_publication}")
    endif()
endforeach()

# ODE's exact occupancy preflights use a single static bounded joint workspace.
# Creation validates the prospective free-head against every geom/joint alias;
# destruction validates all body, geom, joint, and list ownership before commit.
extract_slice(
    "${_ode}"
    "struct ODENoReportJointValidationWorkspace"
    "static bool ODE_ValidateBodyDestroyNoReport("
    _ode_preflight
    "ODE fixed-pool topology preflight")
require_ordered("${_ode_preflight}"
    "struct ODENoReportJointValidationWorkspace"
    "static ODENoReportJointValidationWorkspace odeNoReportJointWorkspace{};"
    "joint topology scratch has static storage")
foreach(_ode_invariant IN ITEMS
    "ODE_NoReportBodyAllocationCandidateHasNoAliases("
    "geom->body == candidate"
    "joint.node[0].body == candidate"
    "storage.itemSize != sizeof(dxBody) || storage.itemCount != 512"
    "ODE_NoReportIndexWorldJoints("
    "ODE_NoReportIndexBodyJointList("
    "auto &workspace = odeNoReportJointWorkspace;"
    "[body, &workspace](const dxJoint &joint)")
    require_contains("${_ode_preflight}" "${_ode_invariant}"
        "ODE preflight classifies exact global body/joint occupancy")
endforeach()
forbid_matches("${_ode_preflight}" "${_reporting_pattern}"
    "ODE topology preflight is intrinsically silent")

extract_slice(
    "${_ode}"
    "poolmutationstatus_t ODE_TryBodyCreateNoReport("
    "dxJointGroup *__cdecl dGetContactJointGroup("
    _ode_create
    "ODE silent body creation")
require_ordered("${_ode_create}"
    "*outBody = nullptr;"
    "ODE_NoReportBodyAllocationCandidateHasNoAliases("
    "body output is cleared before prospective free-head validation")
require_ordered("${_ode_create}"
    "ODE_NoReportBodyAllocationCandidateHasNoAliases("
    "Pool_TryAllocNoReport("
    "all alias checks precede body pool mutation")
require_ordered("${_ode_create}"
    "if (allocation.status != poolmutationstatus_t::Success)"
    "*outBody = ODE_InitializeAllocatedBody(world, body);"
    "failed pool allocation cannot enter the world list")
forbid_matches("${_ode_create}" "${_reporting_pattern}"
    "ODE body creation remains silent")

extract_slice(
    "${_ode}"
    "odebodycleanupstatus_t ODE_TryBodyDestroyNoReport("
    "#endif\n}"
    _ode_destroy
    "ODE silent body destruction")
require_ordered("${_ode_destroy}"
    "ODE_ValidateBodyDestroyNoReport(body)"
    "while (body->geom)"
    "complete topology validation precedes the first geom mutation")
require_ordered("${_ode_destroy}"
    "while (body->geom)"
    "removeObjectFromList(body);"
    "owned geoms are retired before world unlink")
require_ordered("${_ode_destroy}"
    "removeObjectFromList(body);"
    "Pool_TryFreeNoReport("
    "world unlink precedes body slot release")
forbid_matches("${_ode_destroy}" "${_reporting_pattern}"
    "ODE body destruction remains silent")

# Global geom validation recognizes only exact published roots or uniquely
# owned transform children. Silent construction clears outputs, validates
# attachment topology before allocation, and silently destroys a fresh geom if
# attachment fails.
extract_slice(
    "${_geom}"
    "poolmutationstatus_t ODE_TryCreateGeomNoReport("
    "poolmutationstatus_t ODE_TryGeomTransformSetGeomNoReport("
    _geom_create
    "ODE silent custom-geom creation")
require_ordered("${_geom_create}"
    "*outGeom = nullptr;"
    "ODE_NoReportAttachmentTargetsAreValid(space, body)"
    "geom output is cleared before attachment validation")
require_ordered("${_geom_create}"
    "ODE_TryAllocateGeomNoReport(&storage)"
    "ODE_TryAttachGeomNoReport(geom, space, body)"
    "allocation precedes checked attachment")
require_ordered("${_geom_create}"
    "ODE_TryAttachGeomNoReport(geom, space, body)"
    "ODE_TryGeomDestructNoReport(geom)"
    "failed attachment silently returns its allocation")
require_ordered("${_geom_create}"
    "ODE_TryGeomDestructNoReport(geom)"
    "*outGeom = geom;"
    "ownership publishes only after attachment succeeds")
forbid_matches("${_geom_create}" "${_reporting_pattern}"
    "custom-geom creation remains silent")

extract_slice(
    "${_geom}"
    "odegeomcleanupstatus_t ODE_TryGeomDestructNoReport("
    "bool ODE_TryValidateGeomDestructNoReport("
    _geom_destroy
    "ODE silent geom destruction")
require_ordered("${_geom_destroy}"
    "ODE_NoReportGlobalGeomListsAreValid()"
    "ODE_ValidateGeomDestructNoReport(geom)"
    "global occupancy validates before target ownership")
require_ordered("${_geom_destroy}"
    "ODE_ValidateGeomDestructNoReport(geom)"
    "ODE_CommitGeomDestructNoReport(geom)"
    "all geom validation precedes commit")
forbid_matches("${_geom_destroy}" "${_reporting_pattern}"
    "geom destruction remains silent")

function(require_silent_constructor source start end description)
    extract_slice("${source}" "${start}" "${end}"
        _constructor "ODE silent ${description} constructor")
    require_ordered("${_constructor}"
        "*outGeom = nullptr;"
        "ODE_TryValidateGeomAttachmentNoReport(space, body)"
        "${description} output is cleared before topology validation")
    require_ordered("${_constructor}"
        "ODE_TryAllocateGeomNoReport(&storage)"
        "ODE_TryAttachGeomNoReport(geom, space, body)"
        "${description} allocation precedes attachment")
    require_ordered("${_constructor}"
        "ODE_TryAttachGeomNoReport(geom, space, body)"
        "ODE_TryGeomDestructNoReport(geom)"
        "${description} attachment failure silently rolls back")
    forbid_matches("${_constructor}" "${_reporting_pattern}"
        "${description} construction remains silent")
endfunction()

require_silent_constructor(
    "${_box}"
    "poolmutationstatus_t ODE_TryCreateBoxNoReport("
    "//void dGeomBoxSetLengths ("
    "box")
require_silent_constructor(
    "${_transform}"
    "poolmutationstatus_t ODE_TryCreateGeomTransformNoReport("
    "void dGeomTransformSetGeom ("
    "transform")

message(STATUS "Physics rollback source invariants verified")
