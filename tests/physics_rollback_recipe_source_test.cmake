cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_header_path "${SOURCE_ROOT}/src/physics/phys_local.h")
set(_source_path "${SOURCE_ROOT}/src/physics/phys_ode.cpp")
foreach(_path IN ITEMS "${_header_path}" "${_source_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing physics rollback source: ${_path}")
    endif()
endforeach()
file(READ "${_header_path}" _header)
file(READ "${_source_path}" _source)

function(require_regex haystack pattern description)
    string(REGEX MATCH "${pattern}" _match "${haystack}")
    if(_match STREQUAL "")
        message(FATAL_ERROR
            "Missing physics rollback invariant: ${description}")
    endif()
endfunction()

function(forbid_regex haystack pattern description)
    string(REGEX MATCH "${pattern}" _match "${haystack}")
    if(NOT _match STREQUAL "")
        message(FATAL_ERROR
            "Forbidden physics rollback behavior (${description}): ${_match}")
    endif()
endfunction()

function(extract_slice source start_marker end_marker out_var description)
    string(FIND "${source}" "${start_marker}" _start)
    string(FIND "${source}" "${end_marker}" _end)
    if(_start EQUAL -1 OR _end EQUAL -1 OR NOT _start LESS _end)
        message(FATAL_ERROR "Could not isolate ${description}")
    endif()
    math(EXPR _length "${_end} - ${_start}")
    string(SUBSTRING "${source}" ${_start} ${_length} _slice)
    set(${out_var} "${_slice}" PARENT_SCOPE)
endfunction()

function(require_regex_ordered haystack first second description)
    string(REGEX MATCH "${first}" _first_match "${haystack}")
    if(_first_match STREQUAL "")
        message(FATAL_ERROR
            "Missing first ordered rollback invariant: ${description}")
    endif()
    string(FIND "${haystack}" "${_first_match}" _first_position)
    string(LENGTH "${_first_match}" _first_length)
    math(EXPR _tail_start "${_first_position} + ${_first_length}")
    string(SUBSTRING "${haystack}" ${_tail_start} -1 _tail)
    string(REGEX MATCH "${second}" _second_match "${_tail}")
    if(_second_match STREQUAL "")
        message(FATAL_ERROR
            "Missing or unordered rollback invariant: ${description}")
    endif()
endfunction()

foreach(_status_api IN ITEMS
    Phys_TryGetBodyModelResourceDemand
    Phys_TryGetFreeResourceCapacityLockedNoReport
    Phys_TryCaptureBodyStateLocked
    Phys_TryBuildBodyRollbackRecipeLocked
    Phys_TryValidateBodyDestroyLockedNoReport
    Phys_TryDestroyBodyLockedNoReport)
    require_regex(
        "${_header}"
        "\\[\\[nodiscard\\]\\][ \t\r\n]+PhysBodyRollbackStatus[ \t\r\n]+__cdecl[ \t\r\n]+${_status_api}\\([^;]*\\)[ \t\r\n]*noexcept[ \t\r\n]*;"
        "${_status_api} is a nodiscard noexcept status API")
endforeach()

extract_slice(
    "${_source}"
    "Phys_TryGetFreeResourceCapacityLockedNoReport("
    "PhysBodyRollbackStatus __cdecl Phys_TryCaptureBodyStateLocked("
    _capacity_scope
    "silent fixed-pool free-capacity query")
require_regex_ordered("${_capacity_scope}"
    "\\*outCapacity[ \t\r\n]*=[ \t\r\n]*\\{\\}[ \t\r\n]*;"
    "Phys_RollbackPoolIsValid\\(bodyStorage"
    "capacity output is cleared before pool inspection")
foreach(_capacity_pool IN ITEMS
    "Phys_RollbackPoolIsValid\\(bodyStorage"
    "Phys_RollbackPoolIsValid\\([ \t\r\n]*userDataStorage"
    "Phys_RollbackPoolIsValid\\(geomStorage")
    require_regex("${_capacity_scope}" "${_capacity_pool}"
        "capacity query silently full-validates every fixed pool")
endforeach()
forbid_regex("${_capacity_scope}"
    "(Pool_ValidateFull|Pool_GetFreeCount|MyAssertHandler|Com_Print[A-Za-z0-9_]*|iassert|vassert|fprintf)[ \t\r\n]*\\("
    "capacity query cannot report through diagnostic pool APIs")

extract_slice(
    "${_source}"
    "PhysBodyRollbackStatus Phys_TryValidateGlobalTopologyLockedNoReport("
    "bool Phys_RollbackTransformHasUniqueInnerOwnership("
    _global_topology_scope
    "global no-report native topology classification")
foreach(_global_capacity_guard IN ITEMS
    "bodyStorage\\.itemCount[ \t\r\n]*>[ \t\r\n]*snapshot\\.worldForBody\\.size\\(\\)"
    "userDataStorage\\.itemCount[ \t\r\n]*>[ \t\r\n]*snapshot\\.bodyForUserData\\.size\\(\\)"
    "geomStorage\\.itemCount[ \t\r\n]*>[ \t\r\n]*snapshot\\.spaceForGeom\\.size\\(\\)")
    require_regex("${_global_topology_scope}" "${_global_capacity_guard}"
        "pool descriptors fit every fixed-capacity topology snapshot")
endforeach()
require_regex_ordered("${_global_topology_scope}"
    "bodyStorage\\.itemCount[ \t\r\n]*>[ \t\r\n]*snapshot\\.worldForBody\\.size\\(\\)"
    "snapshot\\.worldForBody\\[bodyIndex\\]"
    "body descriptor capacity is proven before snapshot indexing")
require_regex_ordered("${_global_topology_scope}"
    "userDataStorage\\.itemCount[ \t\r\n]*>[ \t\r\n]*snapshot\\.bodyForUserData\\.size\\(\\)"
    "snapshot\\.bodyForUserData\\[userDataIndex\\]"
    "user-data descriptor capacity is proven before snapshot indexing")
foreach(_global_invariant IN ITEMS
    "worldForBody\\.fill\\(PHYS_ROLLBACK_NO_OWNER\\)"
    "bodyForUserData\\.fill\\(PHYS_ROLLBACK_NO_OWNER\\)"
    "spaceForGeom\\.fill\\(PHYS_ROLLBACK_NO_OWNER\\)"
    "bodyForGeom\\.fill\\(PHYS_ROLLBACK_NO_OWNER\\)"
    "transformForInner\\.fill\\(PHYS_ROLLBACK_NO_OWNER\\)"
    "snapshot\\.spaceForGeom\\[geomIndex\\][ \t\r\n]*!=[ \t\r\n]*snapshot\\.worldForBody\\[bodyIndex\\]"
    "const bool isOuter"
    "const bool isInner"
    "!isOuter[ \t\r\n]*&&[ \t\r\n]*!isInner")
    require_regex("${_global_topology_scope}" "${_global_invariant}"
        "every allocated native slot has one reciprocal owner classification")
endforeach()
forbid_regex("${_global_topology_scope}"
    "(Pool_Free|Phys_ObjDestroy|ODE_GeomDestruct|dBodyDestroy|MyAssertHandler|Com_Print[A-Za-z0-9_]*|iassert|vassert|fprintf)[ \t\r\n]*\\("
    "global topology preflight is silent and non-destructive")

extract_slice(
    "${_source}"
    "PhysBodyRollbackStatus Phys_TryBuildBodyDestroyPlanLocked("
    "void Phys_ReturnValidatedPoolSlotNoReport(\n    const poolstorage_t storage,"
    _destroy_preflight_scope
    "non-mutating body-destroy preflight")
foreach(_destroy_invariant IN ITEMS
    "Phys_TryValidateGlobalTopologyLockedNoReport\\("
    "Phys_ValidateLiveBodyOwnershipLocked\\("
    "body->firstjoint"
    "transform->cleanup[ \t\r\n]*!=[ \t\r\n]*1"
    "resources\\[innerIndex\\]"
    "candidate->pos[ \t\r\n]*==[ \t\r\n]*body->info\\.pos"
    "candidate->R[ \t\r\n]*==[ \t\r\n]*body->info\\.R"
    "topology\\.transformForInner\\[innerIndex\\][ \t\r\n]*!=[ \t\r\n]*outerIndex")
    require_regex("${_destroy_preflight_scope}" "${_destroy_invariant}"
        "destroy preflight proves exact native ownership")
endforeach()
forbid_regex("${_destroy_preflight_scope}"
    "(Pool_Free|Phys_ReturnValidatedPoolSlotNoReport|Phys_ObjDestroy|ODE_GeomDestruct|dBodyDestroy|MyAssertHandler|Com_Print[A-Za-z0-9_]*|iassert|vassert|fprintf)[ \t\r\n]*\\("
    "destroy preflight is silent and non-destructive")

extract_slice(
    "${_source}"
    "void Phys_CommitBodyDestroyPlanLockedNoReport("
    "} // namespace"
    _destroy_commit_scope
    "no-report body-destroy commit")
require_regex_ordered("${_destroy_commit_scope}"
    "\\*backlink[ \t\r\n]*=[ \t\r\n]*next"
    "Phys_ReturnValidatedPoolSlotNoReport\\("
    "all native links are removed before any slot is returned")
foreach(_destroy_release IN ITEMS
    "&odeGlob\\.geomPool"
    "&odeGlob\\.bodyPool"
    "&physGlob\\.userDataPool")
    require_regex("${_destroy_commit_scope}" "${_destroy_release}"
        "destroy commit returns every owned fixed-pool resource")
endforeach()
forbid_regex("${_destroy_commit_scope}"
    "(Pool_Free|Phys_ObjDestroy|ODE_GeomDestruct|dBodyDestroy|dSpaceRemove|MyAssertHandler|Com_Print[A-Za-z0-9_]*|iassert|vassert|fprintf)[ \t\r\n]*\\("
    "destroy commit has no assertion/report-capable callees")

extract_slice(
    "${_source}"
    "bool Phys_TryBuildRoundedMassTensorNoReport("
    "static PhysBodyModelCreateStatus Phys_TryBuildCollisionFromXModel("
    _inertia_scope
    "no-report inertial tensor setup")
require_regex_ordered("${_inertia_scope}"
    "Phys_RollbackMassTensorIsReconstructible\\(\\*physMass\\)"
    "const double determinant"
    "inertia inputs are validated before matrix inversion")
require_regex_ordered("${_inertia_scope}"
    "const double leadingMinor"
    "!\\(leadingMinor[ \t\r\n]*>[ \t\r\n]*0\\.0\\)"
    "rounded inertia uses the complete Sylvester positive-definite test")
require_regex_ordered("${_inertia_scope}"
    "const float inverseMass"
    "userData->translation\\[component\\]"
    "all fallible inertia calculations precede mutation")
foreach(_float_parity IN ITEMS
    "dSqrt\\(sum\\)"
    "dRecip\\(lower\\[row \\* 4 \\+ row\\]\\)"
    "dRecip\\(totalMass\\)")
    require_regex("${_inertia_scope}" "${_float_parity}"
        "silent inversion preserves ODE float operation parity")
endforeach()
forbid_regex("${_inertia_scope}"
    "(Phys_ObjSetInertialTensor|Phys_AdjustForNewCenterOfMass|Phys_MassSetBrushTotal|dBodySetMass|dMassSetParameters|MyAssertHandler|Com_Print[A-Za-z0-9_]*|iassert|vassert|fprintf)[ \t\r\n]*\\("
    "no-report inertia path cannot reach legacy diagnostic helpers")

extract_slice(
    "${_source}"
    "static bool Phys_DestroyFreshBodyResourceNoReport("
    "static PhysBodyModelCreateStatus Phys_TryInitializeBodyMass("
    _fresh_body_rollback_scope
    "fresh body no-report rollback callback")
require_regex("${_fresh_body_rollback_scope}"
    "static[ \t\r\n]+bool[ \t\r\n]+Phys_DestroyFreshBodyResourceNoReport\\("
    "fresh body rollback publishes its cleanup result")
require_regex_ordered("${_fresh_body_rollback_scope}"
    "return[ \t\r\n]+false[ \t\r\n]*;"
    "Phys_ReturnValidatedPoolSlotNoReport\\("
    "fresh body rollback rejects failed ownership preflight before release")
require_regex("${_fresh_body_rollback_scope}"
    "Phys_ReturnValidatedPoolSlotNoReport\\("
    "fresh body rollback returns its validated pool slot directly")
require_regex_ordered("${_fresh_body_rollback_scope}"
    "Phys_ReturnValidatedPoolSlotNoReport\\("
    "return[ \t\r\n]+true[ \t\r\n]*;"
    "fresh body rollback reports success only after exact pool release")
extract_slice(
    "${_fresh_body_rollback_scope}"
    "context->world->firstbody ="
    "return true;"
    _fresh_body_commit_scope
    "fresh body rollback commit tail")
forbid_regex("${_fresh_body_commit_scope}"
    "return[ \t\r\n]+false[ \t\r\n]*;"
    "fresh body rollback cannot reject after its first topology mutation")
forbid_regex("${_fresh_body_rollback_scope}"
    "return[ \t\r\n]*;"
    "fresh body rollback cannot silently discard cleanup status")
forbid_regex("${_fresh_body_rollback_scope}"
    "(Pool_Free|dBodyDestroy|MyAssertHandler|Com_Print[A-Za-z0-9_]*|iassert|vassert|fprintf)[ \t\r\n]*\\("
    "fresh body rollback cannot report through legacy destruction")

extract_slice(
    "${_source}"
    "static bool Phys_DestroyFreshPrimaryGeomResourceNoReport("
    "static PhysBodyModelCreateStatus Phys_TryBodyAddGeomAndSetMass("
    _fresh_geom_rollback_scope
    "fresh geom no-report rollback callback")
require_regex("${_fresh_geom_rollback_scope}"
    "static[ \t\r\n]+bool[ \t\r\n]+Phys_DestroyFreshPrimaryGeomResourceNoReport\\("
    "fresh geom rollback publishes its cleanup result")
require_regex_ordered("${_fresh_geom_rollback_scope}"
    "return[ \t\r\n]+false[ \t\r\n]*;"
    "Phys_ReturnValidatedPoolSlotNoReport\\("
    "fresh geom rollback rejects failed ownership preflight before release")
require_regex("${_fresh_geom_rollback_scope}"
    "Phys_ReturnValidatedPoolSlotNoReport\\("
    "fresh geom rollback returns its validated pool slot directly")
require_regex_ordered("${_fresh_geom_rollback_scope}"
    "Phys_ReturnValidatedPoolSlotNoReport\\("
    "return[ \t\r\n]+true[ \t\r\n]*;"
    "fresh geom rollback reports success only after exact pool release")
extract_slice(
    "${_fresh_geom_rollback_scope}"
    "context->space->first = geom->next;"
    "return true;"
    _fresh_geom_commit_scope
    "fresh geometry rollback commit tail")
forbid_regex("${_fresh_geom_commit_scope}"
    "return[ \t\r\n]+false[ \t\r\n]*;"
    "fresh geom rollback cannot reject after its first topology mutation")
forbid_regex("${_fresh_geom_rollback_scope}"
    "return[ \t\r\n]*;"
    "fresh geom rollback cannot silently discard cleanup status")
forbid_regex("${_fresh_geom_rollback_scope}"
    "(Pool_Free|ODE_GeomDestruct|dSpaceRemove|MyAssertHandler|Com_Print[A-Za-z0-9_]*|iassert|vassert|fprintf)[ \t\r\n]*\\("
    "fresh geom rollback cannot report through legacy destruction")
require_regex(
    "${_header}"
    "\\[\\[nodiscard\\]\\][ \t\r\n]+PhysBodyModelCreateStatus[ \t\r\n]+__cdecl[ \t\r\n]+Phys_TryCreateBodyFromStateAndXModelLockedNoReport\\([^;]*\\)[ \t\r\n]*noexcept[ \t\r\n]*;"
    "locked archive reconstruction is nodiscard and noexcept")
require_regex("${_header}" "struct[ \t\r\n]+PhysBodyResourceDemand"
    "explicit body/user-data/geom resource demand")
require_regex("${_header}" "struct[ \t\r\n]+PhysBodyRollbackRecipe"
    "state/model/demand rollback recipe")
require_regex("${_header}"
    "PhysBodyModelCreateStatus[^}]*CleanupFailed"
    "archive reconstruction exposes an unrecoverable cleanup status")
require_regex("${_header}" "Caller owns CRITSECT_PHYSICS"
    "transaction lock ownership contract")

extract_slice(
    "${_source}"
    "bool Phys_RollbackPoolIsValid("
    "PhysBodyRollbackStatus Phys_ValidateModelResourceDemand("
    _pool_scope
    "silent full pool validation")
foreach(_pool_invariant IN ITEMS
    "control\\.initMagic[ \t\r\n]*!=[ \t\r\n]*PHYS_ROLLBACK_POOL_MAGIC"
    "control\\.boundData[ \t\r\n]*!=[ \t\r\n]*poolData"
    "allocatedCount[ \t\r\n]*!=[ \t\r\n]*activeCount"
    "std::array<bool,[ \t\r\n]*ODE_GEOM_POOL_COUNT>[ \t\r\n]+visitedFree"
    "std::memcmp\\(slot,[ \t\r\n]*&expectedNext"
    "control\\.slotState\\[slot\\][ \t\r\n]*!=[ \t\r\n]*POOL_SLOT_ALLOCATED")
    require_regex("${_pool_scope}" "${_pool_invariant}"
        "pool metadata, allocation bitmap, and free-list agree")
endforeach()
forbid_regex(
    "${_pool_scope}"
    "(Pool_ValidateFull|MyAssertHandler|Com_Print[A-Za-z0-9_]*|iassert|vassert|fprintf)[ \t\r\n]*\\("
    "fallible pool validation must be silent")

extract_slice(
    "${_source}"
    "void Phys_GetCanonicalModelBoxBounds("
    "bool Phys_RollbackVectorIsBounded("
    _fallback_box_scope
    "canonical default-model collision bounds")
foreach(_degenerate_axis IN ITEMS 0 1 2)
    require_regex("${_fallback_box_scope}"
        "outMaxs\\[${_degenerate_axis}\\][ \t\r\n]*==[ \t\r\n]*outMins\\[${_degenerate_axis}\\]"
        "fallback box detects exact degeneracy on axis ${_degenerate_axis}")
endforeach()
require_regex_ordered("${_fallback_box_scope}"
    "outMaxs\\[0\\][ \t\r\n]*==[ \t\r\n]*outMins\\[0\\]"
    "outMins\\[component\\][ \t\r\n]*=[ \t\r\n]*-PHYS_MODEL_FALLBACK_BOX_HALF_LENGTH"
    "the whole fallback box is canonicalized before validation")
require_regex_ordered("${_fallback_box_scope}"
    "Phys_GetCanonicalModelBoxBounds\\(model,[ \t\r\n]*outMins,[ \t\r\n]*outMaxs\\)"
    "outMins\\[component\\][ \t\r\n]*>=[ \t\r\n]*outMaxs\\[component\\]"
    "strict ordering is checked only after legacy fallback canonicalization")
forbid_regex("${_fallback_box_scope}"
    "(MyAssertHandler|Com_Print[A-Za-z0-9_]*|iassert|vassert|fprintf)[ \t\r\n]*\\("
    "fallback box canonicalization and validation are silent")

extract_slice(
    "${_source}"
    "PhysBodyRollbackStatus Phys_ValidateModelResourceDemand("
    "PhysBodyRollbackStatus Phys_ValidateLiveBodyOwnershipLocked("
    _model_scope
    "model resource-demand preflight")
require_regex_ordered("${_model_scope}"
    "\\*outDemand[ \t\r\n]*=[ \t\r\n]*\\{\\}[ \t\r\n]*;"
    "if[ \t\r\n]*\\([ \t\r\n]*!model"
    "demand output is cleared before model inspection")
require_regex("${_model_scope}"
    "PhysBodyResourceDemand[ \t\r\n]+demand[ \t\r\n]*\\{[ \t\r\n]*1[ \t\r\n]*,[ \t\r\n]*1[ \t\r\n]*,[ \t\r\n]*0[ \t\r\n]*\\}"
    "every reconstruction reserves one body and one user-data slot")
require_regex("${_model_scope}"
    "geomList\\.count[ \t\r\n]*!=[ \t\r\n]*0[ \t\r\n]*&&[ \t\r\n]*!geomList\\.geoms"
    "a zero-count PhysGeomList preserves the legacy no-collision body")
require_regex("${_model_scope}"
    "Phys_TryGetValidatedModelBoxBounds\\(\\*model,[ \t\r\n]*mins,[ \t\r\n]*maxs\\)"
    "default-model demand validates canonical fallback-box bounds")
foreach(_model_type IN ITEMS PHYS_GEOM_NONE PHYS_GEOM_BOX PHYS_GEOM_CYLINDER)
    require_regex("${_model_scope}" "case[ \t]+${_model_type}[ \t\r\n]*:"
        "canonical model type ${_model_type}")
endforeach()
require_regex_ordered("${_model_scope}"
    "case[ \t]+PHYS_GEOM_NONE"
    "if[ \t\r\n]*\\([ \t\r\n]*!geom\\.brush"
    "brush type requires brush identity")
require_regex_ordered("${_model_scope}"
    "case[ \t]+PHYS_GEOM_BOX"
    "required[ \t\r\n]*=[ \t\r\n]*2"
    "box reconstruction owns primitive and transform slots")
require_regex_ordered("${_model_scope}"
    "case[ \t]+PHYS_GEOM_CYLINDER"
    "required[ \t\r\n]*=[ \t\r\n]*2"
    "cylinder reconstruction owns primitive and transform slots")
require_regex("${_model_scope}"
    "demand\\.geomCount[ \t\r\n]*>[ \t\r\n]*ODE_GEOM_POOL_COUNT[ \t\r\n]*-[ \t\r\n]*required"
    "geom demand is overflow checked")
require_regex("${_model_scope}"
    "Phys_RollbackMassTensorIsReconstructible\\(geomList\\.mass\\)"
    "model inertia is reconstructible without downstream diagnostics")
require_regex_ordered("${_model_scope}"
    "geom\\.brush->mins\\[component\\]"
    "geom\\.brush->maxs\\[component\\]"
    "brush bounds are checked before no-report reconstruction")

extract_slice(
    "${_source}"
    "PhysBodyRollbackStatus Phys_ValidateLiveBodyOwnershipLocked("
    "const float *Phys_RollbackModelGeomCenter("
    _ownership_scope
    "live body ownership validation")
require_regex_ordered("${_ownership_scope}"
    "Phys_TryGetExactPoolSlotIndex\\([ \t\r\n]*bodyStorage"
    "Phys_RollbackPoolIsValid\\([ \t\r\n]*bodyStorage"
    "foreign body pointers are range-proven before pool traversal")
require_regex_ordered("${_ownership_scope}"
    "bodyStorage\\.itemCount[ \t\r\n]*>[ \t\r\n]*PHYS_BODY_POOL_COUNT"
    "visitedBodies\\[cursorIndex\\]"
    "body descriptor capacity is proven before fixed cycle-set indexing")
require_regex("${_ownership_scope}"
    "std::array<bool,[ \t\r\n]*PHYS_BODY_POOL_COUNT>[ \t\r\n]+visitedBodies"
    "world traversal has a fixed cycle bound")
require_regex_ordered("${_ownership_scope}"
    "cursor->world[ \t\r\n]*!=[ \t\r\n]*world"
    "cursor->tome"
    "every body has exact world and backlink ownership")
require_regex_ordered("${_ownership_scope}"
    "Phys_TryGetExactPoolSlotIndex\\([ \t\r\n]*userDataStorage"
    "static_cast<PhysObjUserData[ \t]*\\*>\\(rawUserData\\)"
    "user data is range-proven before typed dereference")
require_regex("${_ownership_scope}"
    "userData->body[ \t\r\n]*!=[ \t\r\n]*body"
    "user-data back ownership is exact")

extract_slice(
    "${_source}"
    "bool Phys_RollbackStateCenterMatchesModel("
    "PhysBodyRollbackStatus Phys_ValidateTargetSpaceTopologyLocked("
    _state_center_scope
    "canonical rollback center validation")
require_regex_ordered("${_state_center_scope}"
    "Phys_TryGetValidatedModelBoxBounds\\("
    "Vec3Avg\\(defaultMins,[ \t\r\n]*defaultMaxs,[ \t\r\n]*defaultCenter\\)"
    "fallback-box center derives from canonical collision bounds")

extract_slice(
    "${_source}"
    "PhysBodyRollbackStatus Phys_ValidateTargetSpaceTopologyLocked("
    "bool Phys_RollbackTransformHasUniqueInnerOwnership("
    _space_scope
    "target-space topology validation")
require_regex_ordered("${_space_scope}"
    "space->count[ \t\r\n]*<[ \t\r\n]*0"
    "space->count\\)[ \t\r\n]*>[ \t\r\n]*geomStorage\\.itemCount"
    "target-space count is bounded")
require_regex("${_space_scope}"
    "Phys_TryGetExactPoolSlotIndex\\([ \t\r\n]*geomStorage,[ \t\r\n]*cursor"
    "every target-space member is an exact geom-pool slot")
require_regex_ordered("${_space_scope}"
    "cursor->parent_space[ \t\r\n]*!=[ \t\r\n]*space"
    "cursor->tome"
    "target-space membership and backlinks are exact")
require_regex("${_space_scope}"
    "traversedCount[ \t\r\n]*==[ \t\r\n]*static_cast<std::size_t>\\(space->count\\)"
    "bounded traversal reconciles the complete target-space count")

extract_slice(
    "${_source}"
    "bool Phys_RollbackTransformHasUniqueInnerOwnership("
    "bool Phys_RollbackTransformPoseMatchesModel("
    _transform_owner_scope
    "unique transform ownership proof")
require_regex("${_transform_owner_scope}"
    "index[ \t\r\n]*<[ \t\r\n]*geomStorage\\.itemCount"
    "every allocated geom slot is considered as a potential owner")
require_regex("${_transform_owner_scope}"
    "candidate->type[ \t\r\n]*!=[ \t\r\n]*dGeomTransformClass"
    "only allocated transforms can own inner geoms")
require_regex("${_transform_owner_scope}"
    "transform->obj[ \t\r\n]*!=[ \t\r\n]*inner"
    "transform owner identity is compared directly")
require_regex_ordered("${_transform_owner_scope}"
    "transform[ \t\r\n]*!=[ \t\r\n]*expectedOwner"
    "ownerCount[ \t\r\n]*!=[ \t\r\n]*1"
    "exactly one expected allocated transform owns an inner geom")

extract_slice(
    "${_source}"
    "bool Phys_RollbackTransformPoseMatchesModel("
    "PhysBodyRollbackStatus Phys_ValidateBodyGeomTopologyLocked("
    _identity_scope
    "model collision identity validators")
foreach(_identity_invariant IN ITEMS
    "Phys_AxisToOdeMatrix3\\(geom\\.orientation,[ \t\r\n]*expectedRotation\\)"
    "transform\\.localR"
    "transform\\.localPos"
    "Vec3Add\\(expectedOffset"
    "Vec3Sub\\(expectedOffset"
    "dGeomBoxGetLengths\\(box,[ \t\r\n]*actualLengths\\)"
    "classData\\.direction[ \t\r\n]*==[ \t\r\n]*1"
    "classData\\.radius[ \t\r\n]*==[ \t\r\n]*modelGeom\\.halfLengths\\[1\\]"
    "classData\\.halfHeight[ \t\r\n]*==[ \t\r\n]*modelGeom\\.halfLengths\\[0\\]")
    require_regex("${_identity_scope}" "${_identity_invariant}"
        "live collision class/pose/dimension/brush identity")
endforeach()
require_regex_ordered("${_identity_scope}"
    "classData\\.u\\.brush"
    "modelGeom\\.brush"
    "live brush identity is pointer-exact")

extract_slice(
    "${_source}"
    "PhysBodyRollbackStatus Phys_ValidateBodyGeomTopologyLocked("
    "} // namespace"
    _geom_scope
    "body geom destroyability validation")
require_regex_ordered("${_geom_scope}"
    "Phys_RollbackPoolIsValid\\([ \t\r\n]*geomStorage"
    "Phys_ValidateTargetSpaceTopologyLocked\\("
    "full geom pool validation precedes complete target-space validation")
require_regex("${_geom_scope}"
    "spaceMembership\\[outerIndex\\]"
    "each retiring outer geom is a proven target-space member")
require_regex("${_geom_scope}"
    "transform->cleanup[ \t\r\n]*!=[ \t\r\n]*1"
    "transform destruction owns its inner geom")
foreach(_detached_field IN ITEMS
    "inner->body"
    "inner->body_next"
    "inner->next"
    "inner->tome"
    "inner->parent_space")
    require_regex("${_geom_scope}" "${_detached_field}"
        "nested inner geom detachment field ${_detached_field}")
endforeach()
require_regex_ordered("${_geom_scope}"
    "inner->body"
    "inner->parent_space"
    "nested inner geoms are detached from body and space topology")
require_regex("${_geom_scope}"
    "Phys_RollbackTransformHasUniqueInnerOwnership\\("
    "inner geom destruction has one exact transform owner")
require_regex("${_geom_scope}"
    "Phys_RollbackPrimitiveMatchesModel\\("
    "primitive class, pose, and dimensions match the supplied model")
require_regex("${_geom_scope}"
    "Phys_RollbackBrushMatchesModel\\("
    "brush collision identity matches the supplied model")
require_regex("${_geom_scope}"
    "resourceCount[ \t\r\n]*!=[ \t\r\n]*expectedDemand\\.geomCount"
    "live topology matches exact reconstruction demand")
require_regex_ordered("${_geom_scope}"
    "Phys_TryGetValidatedModelBoxBounds\\("
    "Vec3Sub\\(expectedMaxs,[ \t\r\n]*expectedMins,[ \t\r\n]*expectedLengths\\)"
    "default-box topology matches canonical fallback dimensions")

extract_slice(
    "${_source}"
    "constexpr std::size_t PHYS_BODY_POOL_COUNT"
    "static PhysBodyModelCreateStatus Phys_TryBuildCollisionFromXModel("
    _preflight_scope
    "complete rollback preflight implementation")
forbid_regex(
    "${_preflight_scope}"
    "(Phys_ObjDestroy|ODE_GeomDestruct|dBodyDestroy|dWorldDestroy|dSpaceDestroy|dSpaceAdd|dSpaceRemove)[ \t\r\n]*\\("
    "preflight must not destroy or relink native resources")
forbid_regex(
    "${_preflight_scope}"
    "(Pool_Alloc|Pool_Free|dBodyCreate|dCreate[A-Za-z0-9_]*|Phys_TryCreateBody[A-Za-z0-9_]*)[ \t\r\n]*\\("
    "preflight must not allocate or create resources")
forbid_regex(
    "${_preflight_scope}"
    "(MyAssertHandler|Com_Print[A-Za-z0-9_]*|iassert|vassert|fprintf)[ \t\r\n]*\\("
    "preflight must not report or assert")
forbid_regex(
    "${_preflight_scope}"
    "Sys_(Enter|Leave)CriticalSection[ \t\r\n]*\\("
    "preflight honors rather than mutates caller lock ownership")

extract_slice(
    "${_source}"
    "PhysBodyRollbackStatus __cdecl Phys_TryCaptureBodyStateLocked("
    "PhysBodyRollbackStatus __cdecl Phys_TryBuildBodyRollbackRecipeLocked("
    _capture_scope
    "fallible live-body state capture")
require_regex_ordered("${_capture_scope}"
    "\\*outState[ \t\r\n]*=[ \t\r\n]*\\{\\}[ \t\r\n]*;"
    "Phys_ValidateLiveBodyOwnershipLocked\\("
    "capture output is cleared before ownership validation")
require_regex_ordered("${_capture_scope}"
    "Phys_ValidateLiveBodyOwnershipLocked\\("
    "body->info\\.pos\\[component\\]"
    "native body fields are read only after ownership validation")
require_regex_ordered("${_capture_scope}"
    "Phys_RollbackBodyStateIsValid\\(captured\\)"
    "\\*outState[ \t\r\n]*=[ \t\r\n]*captured"
    "only a completely validated state is published")

extract_slice(
    "${_source}"
    "PhysBodyRollbackStatus __cdecl Phys_TryBuildBodyRollbackRecipeLocked("
    "static PhysBodyModelCreateStatus Phys_TryBuildCollisionFromXModel("
    _recipe_scope
    "destructive retirement preflight")
require_regex_ordered("${_recipe_scope}"
    "\\*outRecipe[ \t\r\n]*=[ \t\r\n]*\\{\\}[ \t\r\n]*;"
    "Phys_TryGetBodyModelResourceDemand\\("
    "recipe output is cleared before fallible validation")
require_regex_ordered("${_recipe_scope}"
    "Phys_TryCaptureBodyStateLocked\\("
    "body->firstjoint"
    "joint inspection follows proven live-body ownership")
require_regex("${_recipe_scope}"
    "PhysBodyRollbackStatus::NonReconstructibleJoints"
    "attached joints are rejected")
require_regex_ordered("${_recipe_scope}"
    "Phys_ValidateBodyGeomTopologyLocked\\("
    "outRecipe->state[ \t\r\n]*=[ \t\r\n]*state"
    "recipe publication follows complete destroyability validation")

extract_slice(
    "${_source}"
    "static PhysBodyModelCreateStatus Phys_TryCreateBodyFromStateInternal("
    "dxBody *__cdecl Phys_CreateBodyFromState("
    _body_create_scope
    "transactional state-only body creation")
require_regex_ordered("${_body_create_scope}"
    "Phys_TryDestroyBodyLockedNoReport\\(worldIndex,[ \t\r\n]*body\\)"
    "PhysBodyModelCreateStatus::CleanupFailed"
    "state-only no-report rollback must expose native cleanup failure")
forbid_regex("${_body_create_scope}"
    "\\(void\\)[ \t\r\n]*Phys_TryDestroyBodyLockedNoReport"
    "state-only no-report rollback cannot discard cleanup failure")

extract_slice(
    "${_source}"
    "Phys_TryCreateBodyFromStateAndXModelInternal("
    "PhysBodyModelCreateStatus __cdecl Phys_TryCreateBodyFromStateAndXModel("
    _create_internal_scope
    "shared transactional state/model creation")
require_regex_ordered("${_create_internal_scope}"
    "\\*outBody[ \t\r\n]*=[ \t\r\n]*nullptr"
    "Phys_TryCreateBodyFromStateInternal\\("
    "creation output is empty before allocation")
require_regex_ordered("${_create_internal_scope}"
    "Phys_RollbackBodyStateIsValid\\(\\*state\\)"
    "Phys_TryCreateBodyFromStateInternal\\("
    "strict rollback state validation precedes allocation")
require_regex_ordered("${_create_internal_scope}"
    "Phys_TryGetBodyModelResourceDemand\\(model,[ \t\r\n]*&demand\\)"
    "Phys_TryCreateBodyFromStateInternal\\("
    "strict model validation precedes allocation")
require_regex_ordered("${_create_internal_scope}"
    "Phys_TryValidateGlobalTopologyLockedNoReport\\("
    "Phys_TryCreateBodyFromStateInternal\\("
    "global native topology is validated before allocation")
require_regex_ordered("${_create_internal_scope}"
    "Phys_TryGetFreeResourceCapacityLockedNoReport\\("
    "Phys_TryCreateBodyFromStateInternal\\("
    "exact fixed-pool capacity is validated before allocation")
require_regex_ordered("${_create_internal_scope}"
    "Phys_TryBuildRoundedMassTensorNoReport\\("
    "Phys_TryCreateBodyFromStateInternal\\("
    "rounded float inertia is proven reconstructible before allocation")
require_regex_ordered("${_create_internal_scope}"
    "Phys_TryBuildCollisionFromXModel\\("
    "reportFailure\\)"
    "diagnostic policy is shared by body and collision creation")
require_regex("${_create_internal_scope}"
    "Phys_ReleaseCreatedBodyResources\\(body,[ \t\r\n]*userData\\)"
    "partial collision construction is rolled back")
require_regex_ordered("${_create_internal_scope}"
    "Phys_TryDestroyBodyLockedNoReport\\(worldIndex,[ \t\r\n]*body\\)"
    "PhysBodyModelCreateStatus::CleanupFailed"
    "collision rollback must expose native cleanup failure")
forbid_regex("${_create_internal_scope}"
    "\\(void\\)[ \t\r\n]*Phys_TryDestroyBodyLockedNoReport"
    "collision rollback cannot discard native cleanup failure")
require_regex("${_source}"
    "reportFailure[ \t\r\n]*\\?[ \t\r\n]*Phys_DestroyBodyResource[ \t\r\n]*:[ \t\r\n]*Phys_DestroyFreshBodyResourceNoReport"
    "strict body companion rollback selects the no-report callback")
require_regex("${_source}"
    "reportFailure[ \t\r\n]*\\?[ \t\r\n]*Phys_DestroyPrimaryGeomResource[ \t\r\n]*:[ \t\r\n]*Phys_DestroyFreshPrimaryGeomResourceNoReport"
    "strict geom companion rollback selects the no-report callback")

extract_slice(
    "${_source}"
    "PhysBodyModelCreateStatus __cdecl\nPhys_TryCreateBodyFromStateAndXModelLockedNoReport("
    "PhysBodyModelCreateStatus __cdecl Phys_TryCreateBodyFromPresetAndXModel("
    _locked_create_scope
    "locked no-report reconstruction API")
require_regex("${_locked_create_scope}"
    "Phys_TryCreateBodyFromStateAndXModelInternal\\([^;]*false[ \t\r\n]*,[ \t\r\n]*true\\)"
    "locked reconstruction disables diagnostics and enables strict validation")
forbid_regex(
    "${_locked_create_scope}"
    "(MyAssertHandler|Com_Print[A-Za-z0-9_]*|iassert|vassert|fprintf)[ \t\r\n]*\\("
    "locked reconstruction entry point is silent")

extract_slice(
    "${_source}"
    "static PhysBodyModelCreateStatus Phys_TryBuildCollisionFromXModel("
    "void __cdecl Phys_ObjSetCollisionFromXModel("
    _collision_builder_scope
    "shared model collision builder")
require_regex("${_collision_builder_scope}"
    "Phys_GetCanonicalModelBoxBounds\\(\\*model,[ \t\r\n]*mins,[ \t\r\n]*maxs\\)"
    "creation uses the same canonical fallback bounds as archive validation")
require_regex("${_collision_builder_scope}"
    "geomList->count[ \t\r\n]*!=[ \t\r\n]*0[ \t\r\n]*&&[ \t\r\n]*!geomList->geoms"
    "creation preserves a zero-count no-collision PhysGeomList")

message(STATUS "Physics rollback recipe source invariants verified")
