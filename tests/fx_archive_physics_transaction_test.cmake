cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

set(_archive_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive.cpp")
set(_physics_batch_control_header_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_physics_batch_control.h")
set(_physics_batch_control_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_physics_batch_control.cpp")
set(_restore_control_header_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_restore_control.h")
set(_restore_control_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_restore_control.cpp")
set(_archive_gate_header_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_gate_control.h")
set(_archive_gate_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_gate_control.cpp")
set(_system_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_system.cpp")
set(_common_files_path
    "${SOURCE_ROOT}/scripts/common_files.cmake")
foreach(_required_source IN ITEMS
    "${_archive_source_path}"
    "${_physics_batch_control_header_path}"
    "${_physics_batch_control_source_path}"
    "${_restore_control_header_path}"
    "${_restore_control_source_path}"
    "${_archive_gate_header_path}"
    "${_archive_gate_source_path}"
    "${_system_source_path}"
    "${_common_files_path}")
    if(NOT EXISTS "${_required_source}")
        message(FATAL_ERROR "FX source not found: ${_required_source}")
    endif()
endforeach()
file(READ "${_archive_source_path}" _archive_source)
file(READ "${_physics_batch_control_header_path}"
    _physics_batch_control_header)
file(READ "${_physics_batch_control_source_path}"
    _physics_batch_control_source)
file(READ "${_restore_control_header_path}" _restore_control_header)
file(READ "${_restore_control_source_path}" _restore_control_source)
file(READ "${_archive_gate_header_path}" _archive_gate_header)
file(READ "${_archive_gate_source_path}" _archive_gate_source)
file(READ "${_system_source_path}" _system_source)
file(READ "${_common_files_path}" _common_files)

function(require_position source needle out_position description)
    string(FIND "${source}" "${needle}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "${description}: missing '${needle}'")
    endif()
    set(${out_position} ${_position} PARENT_SCOPE)
endfunction()

function(require_absent source needle description)
    string(FIND "${source}" "${needle}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR "${description}: found forbidden '${needle}'")
    endif()
endfunction()

function(require_ordered source first second description)
    require_position("${source}" "${first}" _first "${description}")
    require_position("${source}" "${second}" _second "${description}")
    if(NOT _first LESS _second)
        message(FATAL_ERROR "${description}: operations are out of order")
    endif()
endfunction()

function(extract_slice source begin_marker end_marker out_slice description)
    require_position("${source}" "${begin_marker}" _begin "${description} begin")
    string(SUBSTRING "${source}" ${_begin} -1 _tail)
    string(FIND "${_tail}" "${end_marker}" _relative_end)
    if(_relative_end EQUAL -1 OR _relative_end EQUAL 0)
        message(FATAL_ERROR "${description}: invalid source boundaries")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_relative_end} _slice)
    set(${out_slice} "${_slice}" PARENT_SCOPE)
endfunction()

function(require_occurrence_count source needle expected description)
    set(_remaining "${source}")
    set(_count 0)
    string(LENGTH "${needle}" _needle_length)
    while(TRUE)
        string(FIND "${_remaining}" "${needle}" _position)
        if(_position EQUAL -1)
            break()
        endif()
        math(EXPR _count "${_count} + 1")
        math(EXPR _next "${_position} + ${_needle_length}")
        string(SUBSTRING "${_remaining}" ${_next} -1 _remaining)
    endwhile()
    if(NOT _count EQUAL expected)
        message(FATAL_ERROR
            "${description}: expected ${expected} occurrences of '${needle}', found ${_count}")
    endif()
endfunction()

function(require_before_and_after source needle pivot description)
    require_position("${source}" "${pivot}" _pivot "${description} pivot")
    string(LENGTH "${pivot}" _pivot_length)
    string(SUBSTRING "${source}" 0 ${_pivot} _before)
    math(EXPR _after_begin "${_pivot} + ${_pivot_length}")
    string(SUBSTRING "${source}" ${_after_begin} -1 _after)
    require_position("${_before}" "${needle}" _before_position
        "${description} before publication")
    require_position("${_after}" "${needle}" _after_position
        "${description} after publication")
endfunction()

foreach(_forbidden_pointer_codec
    "FX_DecodeArchivedPhysicsBody"
    "FX_EncodeArchivedPhysicsBody"
    "originalPhysObjId")
    require_absent(
        "${_archive_source}"
        "${_forbidden_pointer_codec}"
        "FX archive must not treat a token as a body address")
endforeach()

require_absent(
    "${_archive_source}"
    "(void)Phys_TryDestroyBodyLockedNoReport("
    "archive rollback must never discard native cleanup failure")
require_absent(
    "${_archive_source}"
    "FX_FailArchivePhysicsCleanupLocked"
    "archive helpers must propagate cleanup ownership through controller status")
require_absent(
    "${_archive_source}"
    "FX archive native-body cleanup failed after ownership transfer"
    "archive helpers must not terminate outside the centralized controller fail-stop")

require_occurrence_count(
    "${_archive_source}"
    "if (bodyStatus == PhysBodyModelCreateStatus::CleanupFailed)"
    2
    "both desired creation and retired-body reconstruction must classify retained cleanup ownership")
extract_slice(
    "${_archive_source}"
    "FX_PerformArchivePhysicsReconstructionBatchOperation("
    "FX_ReconstructRetiredArchivePhysicsLocked("
    _reconstruct_physics_scope
    "retired-body reconstruction callback")
extract_slice(
    "${_reconstruct_physics_scope}"
    "if (bodyStatus != PhysBodyModelCreateStatus::Success"
    "const fx::physics::TokenResult bound"
    _reconstruct_failure_scope
    "retired-body reconstruction failure")
require_ordered(
    "${_reconstruct_failure_scope}"
    "if (bodyStatus == PhysBodyModelCreateStatus::CleanupFailed)"
    "return Status::UnsafeFailure;"
    "retained reconstruction ownership must become an unsafe controller result")
require_ordered(
    "${_reconstruct_failure_scope}"
    "return Status::UnsafeFailure;"
    "return createdBody"
    "cleanup failure must be classified before ordinary creation failure")
require_ordered(
    "${_reconstruct_failure_scope}"
    "? Status::UnsafeFailure"
    ": Status::RecoverableFailure;"
    "unexpected retained reconstruction ownership must remain unsafe")
require_position(
    "${_reconstruct_physics_scope}"
    "fx::physics::BindWithScratch("
    _reconstruct_bind_scratch
    "retired-body reconstruction must bind with caller scratch")
require_absent(
    "${_reconstruct_physics_scope}"
    "fx::physics::Bind("
    "retired-body reconstruction must not allocate wrapper scratch")
extract_slice(
    "${_reconstruct_physics_scope}"
    "if (!bound)"
    "entry.reconstructedToken = bound.token;"
    _reconstruct_bind_failure_scope
    "retired-body reconstruction bind failure")
require_ordered(
    "${_reconstruct_bind_failure_scope}"
    "bound.status == fx::physics::SidecarStatus::DuplicateBody"
    "return Status::UnsafeFailure;"
    "duplicate reconstructed-body ownership must fail unsafe")
require_ordered(
    "${_reconstruct_bind_failure_scope}"
    "return Status::UnsafeFailure;"
    "Phys_TryDestroyBodyLockedNoReport("
    "duplicate-body failure must retain ambiguous ownership rather than destroying another registration")
require_ordered(
    "${_reconstruct_physics_scope}"
    "return Status::RecoverableFailure;"
    "entry.reconstructedToken = bound.token;"
    "recoverable reconstruction failure must precede current-entry mutation")

extract_slice(
    "${_archive_source}"
    "FX_CreateArchivePhysicsLocked("
    "struct FxArchiveRestoreControlContext"
    _create_physics_scope
    "desired archive physics creation")
extract_slice(
    "${_create_physics_scope}"
    "if (bodyStatus != PhysBodyModelCreateStatus::Success"
    "const fx::physics::TokenResult bound"
    _create_failure_scope
    "desired archive physics creation failure")
require_ordered(
    "${_create_failure_scope}"
    "if (bodyStatus == PhysBodyModelCreateStatus::CleanupFailed)"
    "return Status::UnsafeFailure;"
    "retained desired ownership must become an unsafe controller result")
require_ordered(
    "${_create_failure_scope}"
    "return Status::UnsafeFailure;"
    "return createdBody"
    "cleanup failure must be classified before ordinary desired creation failure")
require_ordered(
    "${_create_failure_scope}"
    "? Status::UnsafeFailure"
    ": Status::RecoverableFailure;"
    "unexpected retained desired ownership must remain unsafe")
foreach(_create_scratch_operation IN ITEMS
    "fx::physics::ValidateDisjointOwnershipWithScratch("
    "fx::physics::BindWithScratch("
    "fx::physics::TakeWithScratch(")
    require_position(
        "${_create_physics_scope}"
        "${_create_scratch_operation}"
        _create_scratch_position
        "desired archive physics must use caller-owned sidecar scratch")
endforeach()
foreach(_create_stack_wrapper IN ITEMS
    "fx::physics::ValidateDisjointOwnership("
    "fx::physics::Bind("
    "fx::physics::Take(")
    require_absent(
        "${_create_physics_scope}"
        "${_create_stack_wrapper}"
        "desired archive physics must not allocate wrapper scratch")
endforeach()

foreach(_forbidden_direct_physics_api IN ITEMS
    "Phys_ObjDestroy("
    "Pool_ValidateFull("
    "Pool_GetFreeCount(")
    require_absent(
        "${_archive_source}"
        "${_forbidden_direct_physics_api}"
        "archive helpers reachable under PHYSICS must use silent checked primitives")
endforeach()

# A zero-body restore is independent of native world/space and pool state. Both
# capacity consumers must calculate demand first, accept the exact zero tuple,
# and only then query the silent native capacity helper for nonempty restores.
extract_slice(
    "${_archive_source}"
    "FxArchivePhysicsCapacityStatus FX_ArchivePhysicsCapacityAvailableLocked("
    "bool FX_BuildArchivePhysicsRetirementPlanLocked("
    _capacity_available
    "archive capacity check")
require_ordered(
    "${_capacity_available}"
    "FX_GetArchivePhysicsDemand("
    "if (required.bodies == 0"
    "capacity must derive demand before its zero-demand fast path")
require_ordered(
    "${_capacity_available}"
    "if (required.bodies == 0"
    "FX_GetArchivePhysicsFreeCapacityLocked(&available)"
    "zero demand must not require a native capacity query")

extract_slice(
    "${_archive_source}"
    "bool FX_BuildArchivePhysicsRetirementPlanLocked("
    "FX_RetireArchivePhysicsLocked("
    _retirement_planner
    "archive retirement planner")
require_ordered(
    "${_retirement_planner}"
    "FX_GetArchivePhysicsDemand("
    "if (desired.bodies == 0"
    "retirement planning must derive demand before its zero-demand fast path")
require_ordered(
    "${_retirement_planner}"
    "if (desired.bodies == 0"
    "FX_GetArchivePhysicsFreeCapacityLocked(&freeCapacity)"
    "zero-demand planning must not inspect native capacity")
require_ordered(
    "${_retirement_planner}"
    "FX_GetArchivePhysicsFreeCapacityLocked(&freeCapacity)"
    "fx::archive::BuildPhysicsRetirementPlanWithScratch("
    "nonempty planning must use the validated silent capacity snapshot")

extract_slice(
    "${_archive_source}"
    "bool FX_GetArchivePhysicsFreeCapacityLocked("
    "FxArchivePhysicsCapacityStatus FX_ArchivePhysicsCapacityAvailableLocked("
    _free_capacity_helper
    "silent capacity helper")
require_position(
    "${_free_capacity_helper}"
    "Phys_TryGetFreeResourceCapacityLockedNoReport("
    _silent_capacity
    "native capacity must use the locked no-report API")
foreach(_forbidden_capacity_api IN ITEMS
    "Pool_ValidateFull("
    "Pool_GetFreeCount("
    "Com_Error("
    "MyAssertHandler("
    "Com_Print")
    require_absent(
        "${_free_capacity_helper}"
        "${_forbidden_capacity_api}"
        "capacity inspection must be silent")
endforeach()

# Destructive cleanup is an all-or-nothing three-sidecar operation. Validate
# containers and pairwise ownership, snapshot and preflight every selected
# native body, and only then begin detaching registrations.
extract_slice(
    "${_archive_source}"
    "FX_DrainArchivePhysicsSidecarsLocked("
    "enum class FxArchivePhysicsCapacityStatus"
    _drain_helper
    "multi-sidecar drain helper")
require_ordered(
    "${_drain_helper}"
    "if (sidecars[first] == sidecars[second])"
    "fx::physics::ValidateDisjointOwnershipWithScratch("
    "sidecars must be distinct before ownership comparison")
require_ordered(
    "${_drain_helper}"
    "fx::physics::ValidateDisjointOwnershipWithScratch("
    "fx::physics::SnapshotOwnershipWithScratch("
    "all pairwise ownership must be disjoint before body inspection")
require_ordered(
    "${_drain_helper}"
    "fx::physics::SnapshotOwnershipWithScratch("
    "Phys_TryValidateBodyDestroyLockedNoReport("
    "each selected sidecar must snapshot before native destroy preflight")
require_ordered(
    "${_drain_helper}"
    "Phys_TryValidateBodyDestroyLockedNoReport("
    "fx::physics::TakeFirstWithScratch("
    "every selected native body must preflight before the first Take")
require_ordered(
    "${_drain_helper}"
    "fx::physics::TakeFirstWithScratch("
    "Phys_TryDestroyBodyLockedNoReport("
    "drain must detach ownership before checked native destruction")
require_occurrence_count(
    "${_drain_helper}"
    "if (!drainSidecar[index])"
    2
    "both inspection and drain passes must honor the selected sidecar set")
foreach(_forbidden_destroy IN ITEMS
    "Phys_ObjDestroy("
    "dBodyDestroy("
    "Pool_Free("
    "Com_Error("
    "MyAssertHandler("
    "Com_Print")
    require_absent(
        "${_drain_helper}"
        "${_forbidden_destroy}"
        "multi-sidecar drain must remain checked and non-reporting")
endforeach()

extract_slice(
    "${_archive_source}"
    "FX_PerformArchivePhysicsRetirementBatchOperation("
    "FX_RetireArchivePhysicsLocked("
    _retire_helper
    "bounded retirement callback")
require_ordered(
    "${_retire_helper}"
    "Phys_TryValidateBodyDestroyLockedNoReport("
    "fx::physics::TakeWithScratch("
    "every selected retirement must preflight before the first Take")
require_ordered(
    "${_retire_helper}"
    "fx::physics::TakeWithScratch("
    "Phys_TryDestroyBodyLockedNoReport("
    "retirement must detach sidecar ownership before destruction")
foreach(_forbidden_destroy IN ITEMS
    "Phys_ObjDestroy("
    "dBodyDestroy("
    "Pool_Free("
    "Com_Error("
    "MyAssertHandler("
    "Com_Print")
    require_absent(
        "${_retire_helper}"
        "${_forbidden_destroy}"
        "bounded retirement callback must remain checked and non-reporting")
endforeach()

# Production wrappers retain their recoverable input classification while the
# engine-free batch controller owns the only retirement/reconstruction pass
# sequencing. The callbacks are deliberately narrow: PHYSICS remains owned by
# FX_Restore and no reporting, heap/Z allocation, or lock transition can occur
# inside an entry operation. Reconstruction intentionally allocates native
# fixed-pool resources through its checked callback.
require_occurrence_count(
    "${_archive_source}"
    "#include \"fx_archive_physics_batch_control.h\""
    1
    "production archive must include the portable physics batch controller")
foreach(_batch_build_file IN ITEMS
    "EffectsCore/fx_archive_physics_batch_control.cpp"
    "EffectsCore/fx_archive_physics_batch_control.h")
    require_occurrence_count(
        "${_common_files}"
        "${_batch_build_file}"
        1
        "production EFFECTSCORE sources must include the physics batch controller")
endforeach()

foreach(_engine_type IN ITEMS
    "FxArchivePhysicsEntry"
    "BodySidecar"
    "dxBody"
    "Phys_Try")
    require_absent(
        "${_physics_batch_control_header}"
        "${_engine_type}"
        "portable physics batch API must remain engine-type-free")
endforeach()

extract_slice(
    "${_physics_batch_control_source}"
    "RestoreControlOperationStatus RunArchivePhysicsBatch("
    "} // namespace"
    _physics_batch_driver
    "portable physics batch driver")
require_occurrence_count(
    "${_physics_batch_driver}"
    "perform("
    2
    "physics batch driver must have one preflight and one commit callback site")
require_occurrence_count(
    "${_physics_batch_driver}"
    "const ArchivePhysicsBatchPerformCallback perform = callbacks.perform;"
    1
    "physics batch driver must snapshot its callback before either pass")
require_occurrence_count(
    "${_physics_batch_driver}"
    "for (std::size_t index = 0; index < selectedCount; ++index)"
    2
    "physics batch driver must retain separate preflight and commit passes")
require_ordered(
    "${_physics_batch_driver}"
    "SelectionIsValid(planIndices, selectedCount, entryCount)"
    "preflightOperation,\n                planIndices[index]);"
    "the complete selection must validate before the first preflight callback")
require_ordered(
    "${_physics_batch_driver}"
    "preflightOperation,\n                planIndices[index]);"
    "commitOperation,\n                planIndices[index]);"
    "every preflight callback site must precede the commit pass")
require_ordered(
    "${_physics_batch_driver}"
    "commitOperation,\n                planIndices[index]);"
    "++completedCount;"
    "the completed prefix may advance only after a successful commit callback")

extract_slice(
    "${_archive_source}"
    "FX_RetireArchivePhysicsLocked("
    "struct FxArchivePhysicsReconstructionBatchContext"
    _retirement_batch_wrapper
    "production retirement batch wrapper")
extract_slice(
    "${_archive_source}"
    "FX_ReconstructRetiredArchivePhysicsLocked("
    "bool FX_ArchiveRetiredTokenTargetsMatch("
    _reconstruction_batch_wrapper
    "production reconstruction batch wrapper")
require_occurrence_count(
    "${_archive_source}"
    "fx::archive::RunArchivePhysicsRetirementBatch("
    1
    "production retirement must route through the portable batch controller")
require_occurrence_count(
    "${_archive_source}"
    "fx::archive::RunArchivePhysicsReconstructionBatch("
    1
    "production reconstruction must route through the portable batch controller")
require_ordered(
    "${_retirement_batch_wrapper}"
    "return Status::RecoverableFailure;"
    "fx::archive::RunArchivePhysicsRetirementBatch("
    "retirement wrapper arguments must remain recoverable before delegation")
require_ordered(
    "${_retirement_batch_wrapper}"
    "FX_ArchivePhysicsBatchSelectionIsValidForWrapper("
    "fx::archive::RunArchivePhysicsRetirementBatch("
    "retirement must preserve wrapper classification for invalid selections")
require_ordered(
    "${_reconstruction_batch_wrapper}"
    "return Status::RecoverableFailure;"
    "fx::archive::RunArchivePhysicsReconstructionBatch("
    "reconstruction wrapper arguments must remain recoverable before delegation")
require_ordered(
    "${_reconstruction_batch_wrapper}"
    "FX_ArchivePhysicsBatchSelectionIsValidForWrapper("
    "fx::archive::RunArchivePhysicsReconstructionBatch("
    "reconstruction must preserve wrapper classification for invalid selections")
require_ordered(
    "${_reconstruction_batch_wrapper}"
    "std::size_t reconstructedCount = 0;"
    "&reconstructedCount);"
    "reconstruction must provide disjoint exact-prefix output storage")

foreach(_batch_wrapper_scope_name IN ITEMS
    _retirement_batch_wrapper
    _reconstruction_batch_wrapper)
    require_absent(
        "${${_batch_wrapper_scope_name}}"
        "for ("
        "production wrapper must not retain an inline physics batch loop")
    foreach(_forbidden_wrapper_primitive IN ITEMS
        "Phys_TryValidateBodyDestroyLockedNoReport("
        "Phys_TryDestroyBodyLockedNoReport("
        "Phys_TryCreateBodyFromStateAndXModelLockedNoReport("
        "fx::physics::TakeWithScratch("
        "fx::physics::BindWithScratch(")
        require_absent(
            "${${_batch_wrapper_scope_name}}"
            "${_forbidden_wrapper_primitive}"
            "production wrapper must delegate entry operations to its callback")
    endforeach()
endforeach()

foreach(_batch_callback_scope_name IN ITEMS
    _retire_helper
    _reconstruct_physics_scope)
    foreach(_forbidden_callback_operation IN ITEMS
        "Sys_EnterCriticalSection("
        "Sys_LeaveCriticalSection("
        "Com_Error("
        "Com_Print"
        "MyAssertHandler("
        "Sys_Error("
        "std::abort("
        "Z_Malloc("
        "Z_Free(")
        require_absent(
            "${${_batch_callback_scope_name}}"
            "${_forbidden_callback_operation}"
            "physics batch callbacks must remain non-reporting and caller-locked")
    endforeach()
endforeach()
foreach(_forbidden_batch_driver_operation IN ITEMS
    "Sys_EnterCriticalSection("
    "Sys_LeaveCriticalSection("
    "Com_Error("
    "Com_Print"
    "MyAssertHandler("
    "Sys_Error("
    "std::abort("
    "Z_Malloc("
    "Z_Free(")
    require_absent(
        "${_physics_batch_driver}"
        "${_forbidden_batch_driver_operation}"
        "portable physics batch sequencing must remain non-reporting and lock-free")
endforeach()

extract_slice(
    "${_archive_source}"
    "FX_PublishArchivePhysicsSafeEmptyLocked("
    "bool FX_AppendArchivePhysicsEntry("
    _safe_empty_helper
    "safe-empty recovery helper")
require_ordered(
    "${_safe_empty_helper}"
    "FX_ValidateArchiveExclusiveState(system)"
    "FX_CanPublishArchiveSafeEmptyStateLocked(system)"
    "safe-empty recovery must preflight graph publication under archive exclusivity")
require_ordered(
    "${_safe_empty_helper}"
    "FX_CanPublishArchiveSafeEmptyStateLocked(system)"
    "FX_DrainArchivePhysicsSidecarsLocked("
    "safe-empty recovery must prove graph reset viability before draining")
require_ordered(
    "${_safe_empty_helper}"
    "FX_DrainArchivePhysicsSidecarsLocked("
    "FX_PublishArchiveSafeEmptyStateLockedWithScratch("
    "safe-empty recovery must atomically drain all physics before graph reset")
require_ordered(
    "${_safe_empty_helper}"
    "const Status drainStatus ="
    "if (drainStatus != Status::Success)"
    "safe-empty recovery must retain the drain's tri-state result")
require_ordered(
    "${_safe_empty_helper}"
    "if (drainStatus != Status::Success)"
    "return drainStatus;"
    "safe-empty recovery must propagate unsafe drain failure unchanged")
require_position(
    "${_safe_empty_helper}"
    "FxArchiveRestorePhysicsScratch *const scratch"
    _safe_empty_workspace_scratch
    "safe-empty recovery must receive the complete transaction scratch")
require_ordered(
    "${_safe_empty_helper}"
    "&scratch->ownership.sidecar"
    "&scratch->poolGraph"
    "safe-empty recovery must pass both sidecar and pool scratch to graph publication")
foreach(_required_sidecar IN ITEMS
    "{liveSidecar, stagedSidecar, rollbackSidecar}"
    "{true, true, true}")
    require_position(
        "${_safe_empty_helper}"
        "${_required_sidecar}"
        _sidecar_position
        "safe-empty recovery must select all three distinct sidecars")
endforeach()
foreach(_forbidden_safe_empty IN ITEMS
    "FX_DrainArchivePhysicsSidecarLocked("
    "FX_RestoreArchiveExclusiveState("
    "Com_Error("
    "MyAssertHandler("
    "Com_Print")
    require_absent(
        "${_safe_empty_helper}"
        "${_forbidden_safe_empty}"
        "safe-empty recovery must be atomic and non-reporting")
endforeach()

extract_slice(
    "${_system_source}"
    "bool __cdecl FX_CanPublishArchiveSafeEmptyStateLocked("
    "bool __cdecl FX_PublishArchiveSafeEmptyStateLockedWithScratch("
    _safe_empty_preflight
    "safe-empty graph preflight")
require_ordered(
    "${_safe_empty_preflight}"
    "FX_ValidateArchiveExclusiveState(system)"
    "FX_CanResetSystemGraphUnderExclusiveClaim(system)"
    "safe-empty graph preflight must validate exclusivity before reset prerequisites")

extract_slice(
    "${_system_source}"
    "bool __cdecl FX_PublishArchiveSafeEmptyStateLockedWithScratch("
    "bool __cdecl FX_PublishArchiveSafeEmptyStateLocked("
    _safe_empty_publication
    "safe-empty scratch publication")
foreach(_safe_empty_scratch IN ITEMS
    "BodySidecarValidationScratch *const sidecarScratch"
    "FxPoolAllocationGraphScratch *const poolGraphScratch")
    require_position(
        "${_safe_empty_publication}"
        "${_safe_empty_scratch}"
        _safe_empty_scratch_position
        "safe-empty publication must receive both caller scratch types")
endforeach()
require_ordered(
    "${_safe_empty_publication}"
    "fx::physics::ValidateWithScratch(sidecar, sidecarScratch)"
    "Sys_EnterCriticalSection(CRITSECT_FX_ALLOC);"
    "safe-empty publication must validate physics with caller scratch before graph mutation")
require_ordered(
    "${_safe_empty_publication}"
    "FX_ResetSystemGraphUnderExclusiveClaim(system)"
    "FxValidatePoolAllocationGraphWithScratch("
    "safe-empty publication must validate the reset graph with caller scratch")
require_ordered(
    "${_safe_empty_publication}"
    "poolGraphScratch"
    "system->isInitialized = true;"
    "safe-empty publication must finish caller-scratch graph validation before publication")
foreach(_safe_empty_stack_wrapper IN ITEMS
    "fx::physics::Validate(sidecar)"
    "FxValidatePoolAllocationGraph(")
    require_absent(
        "${_safe_empty_publication}"
        "${_safe_empty_stack_wrapper}"
        "safe-empty transaction publication must not allocate wrapper scratch")
endforeach()

extract_slice(
    "${_system_source}"
    "bool FX_CanResetSystemGraphUnderExclusiveClaim("
    "fx::physics::SidecarStatus FX_ResetSystemGraphUnderExclusiveClaim("
    _graph_reset_preflight
    "safe-empty canonical graph preflight")
require_ordered(
    "${_graph_reset_preflight}"
    "FX_GetOwnedSystemBuffers(system)"
    "system->effects == buffers->effects"
    "graph reset must resolve canonical storage before pointer validation")
foreach(_canonical_graph_pointer IN ITEMS
    "system->effects == buffers->effects"
    "system->elems == buffers->elems"
    "system->trailElems == buffers->trailElems"
    "system->trails == buffers->trails"
    "system->visState == buffers->visState"
    "system->deferredElems == buffers->deferredElems")
    require_position(
        "${_graph_reset_preflight}"
        "${_canonical_graph_pointer}"
        _canonical_pointer_position
        "graph reset must reject noncanonical system-buffer pointers")
endforeach()

extract_slice(
    "${_system_source}"
    "fx::physics::SidecarStatus FX_ResetSystemGraphUnderExclusiveClaim("
    "fx::physics::SidecarStatus FX_ResetSystemUnderLifecycleClaim("
    _canonical_safe_empty_reset
    "canonical safe-empty visibility reset")
require_ordered(
    "${_canonical_safe_empty_reset}"
    "system->visStateBufferRead = system->visState;"
    "system->visStateBufferWrite = system->visState + 1;"
    "safe-empty publication must restore canonical read-zero/write-one roles")
require_ordered(
    "${_canonical_safe_empty_reset}"
    "system->visStateBufferWrite = system->visState + 1;"
    "return fx::physics::SidecarStatus::Success;"
    "safe-empty visibility canonicalization must precede successful reset")

# The shared validator is the proof that the controller remains fully acquired
# with an Exclusive gate and iterator -1. Publication copies preserve -1 and
# validate that proof on both sides of graph memcpy operations.
extract_slice(
    "${_system_source}"
    "bool __cdecl FX_ValidateArchiveExclusiveState("
    "bool __cdecl FX_EndArchive("
    _exclusive_validator
    "archive-exclusive validator")
require_ordered(
    "${_exclusive_validator}"
    "ArchiveGateOwnerPhase::Acquired"
    "ArchiveGateValue::Exclusive"
    "archive-exclusive validation must require the fully acquired controller phase")
require_ordered(
    "${_exclusive_validator}"
    "ArchiveGateValue::Exclusive"
    "FX_CurrentThreadOwnsArchive(system)"
    "archive-exclusive validation must require the acquired gate before owner identity")
require_ordered(
    "${_exclusive_validator}"
    "FX_CurrentThreadOwnsArchive(system)"
    "Sys_AtomicLoad(&system->iteratorCount) == -1"
    "archive-exclusive validation must require the preserved exclusive iterator")

foreach(_archive_owner_phase IN ITEMS
    Idle
    Pending
    PendingExclusive
    Acquired
    ExclusiveGateOnly)
    require_position(
        "${_archive_gate_header}"
        "    ${_archive_owner_phase},"
        _archive_owner_phase_position
        "archive controller must retain its ${_archive_owner_phase} ownership phase")
endforeach()

extract_slice(
    "${_archive_gate_source}"
    "ArchiveGateControlStatus AcquireArchiveGate("
    "ArchiveGateControlStatus ReleaseArchiveGate("
    _archive_gate_acquire_controller
    "portable archive gate acquire controller")
require_ordered(
    "${_archive_gate_acquire_controller}"
    "ArchiveGateControlOperation::ClaimPending"
    "state->phase = ArchiveGateOwnerPhase::Pending;"
    "archive acquisition must record Pending only after the gate claim succeeds")
require_ordered(
    "${_archive_gate_acquire_controller}"
    "state->phase = ArchiveGateOwnerPhase::Pending;"
    "ArchiveGateControlOperation::TryAcquireIterator"
    "archive acquisition must close iterator admission before trying exclusivity")
require_ordered(
    "${_archive_gate_acquire_controller}"
    "state->phase = ArchiveGateOwnerPhase::PendingExclusive;"
    "ArchiveGateControlOperation::PromoteExclusive"
    "archive acquisition must record iterator ownership before gate promotion")
require_ordered(
    "${_archive_gate_acquire_controller}"
    "state->phase = ArchiveGateOwnerPhase::Acquired;"
    "ArchiveGateControlOperation::ValidateAdmission"
    "archive acquisition must record complete ownership before admission validation")

extract_slice(
    "${_archive_gate_source}"
    "ArchiveGateControlStatus ReleaseArchiveGate("
    "ArchiveGateControlStatus AbandonArchiveGateForError("
    _archive_gate_release_controller
    "portable archive gate release controller")
require_ordered(
    "${_archive_gate_release_controller}"
    "ArchiveGateControlOperation::ValidateExclusive"
    "ReleaseIteratorForCleanup(state, callbacks)"
    "normal archive release must validate before dropping iterator ownership")
require_position(
    "${_archive_gate_release_controller}"
    "if (ReleaseIteratorForCleanup(state, callbacks)\n        != ArchiveGateControlStatus::Success)\n    {\n        return ArchiveGateControlStatus::UnsafeFailure;\n    }\n    return ReopenOwnedGate(state, callbacks);"
    _ordered_normal_archive_release
    "normal archive release must drop iterator ownership before reopening admission")
require_position(
    "${_archive_gate_release_controller}"
    "if (state->phase == ArchiveGateOwnerPhase::ExclusiveGateOnly)\n        return ReopenOwnedGate(state, callbacks);"
    _gate_only_release_retry
    "normal release must resume a partial cleanup at its gate-only phase")

extract_slice(
    "${_archive_gate_source}"
    "ArchiveGateControlStatus AbandonArchiveGateForError("
    "bool ArchiveGateOwnerMatches("
    _archive_gate_error_controller
    "portable archive gate error controller")
require_ordered(
    "${_archive_gate_error_controller}"
    "ReleaseIteratorForCleanup(state, callbacks)"
    "ArchiveGateControlOperation::ClearArchivingForError"
    "error abandon must release iterator ownership before clearing archive state")
require_ordered(
    "${_archive_gate_error_controller}"
    "ArchiveGateControlOperation::ClearArchivingForError"
    "return ReopenOwnedGate(state, callbacks);"
    "error abandon must clear archive state before reopening admission")
require_absent(
    "${_archive_gate_error_controller}"
    "ClearOwner(state)"
    "error abandon must retain retry state until gate reopening succeeds")

extract_slice(
    "${_archive_gate_source}"
    "ArchiveGateControlStatus ReopenOwnedGate("
    "ArchiveGateControlStatus ReleaseIteratorForCleanup("
    _archive_gate_reopen_controller
    "portable archive gate reopen helper")
require_ordered(
    "${_archive_gate_reopen_controller}"
    "Invoke(callbacks, operation)"
    "ClearOwner(state);"
    "archive ownership may clear only after its gate reopen callback succeeds")

extract_slice(
    "${_archive_source}"
    "bool FX_ArchiveVisibilitySelectorsMatch("
    "struct FxArchiveRestoreControlContext"
    _visibility_selector_matcher
    "archive visibility selector matcher")
foreach(_selector_match_marker IN ITEMS
    "system && buffers"
    "system->visState == buffers->visState"
    "FX_VisibilitySelectorsRoundTrip("
    "&buffers->visState[0]"
    "&buffers->visState[1]"
    "system->visStateBufferRead"
    "system->visStateBufferWrite"
    "expectedSelectors")
    require_position(
        "${_visibility_selector_matcher}"
        "${_selector_match_marker}"
        _selector_match_position
        "archive graph admission must round-trip exact visibility roles")
endforeach()
require_ordered(
    "${_visibility_selector_matcher}"
    "system && buffers"
    "system->visState == buffers->visState"
    "selector matching must reject null inputs before dereferencing either graph")
require_ordered(
    "${_visibility_selector_matcher}"
    "system->visState == buffers->visState"
    "FX_VisibilitySelectorsRoundTrip("
    "selector matching must prove base-buffer identity before role derivation")

extract_slice(
    "${_archive_source}"
    "struct FxArchiveRestoreControlContext"
    "// The sidecars are last"
    _restore_context
    "archive restore controller context")
require_ordered(
    "${_restore_context}"
    "FxSystemBuffers *desiredBuffers;"
    "FxVisibilityBufferSelectors desiredVisibilitySelectors{};"
    "the controller context must own the desired selector pair beside its graph")
require_ordered(
    "${_restore_context}"
    "FxSystemBuffers *originalBuffers;"
    "FxVisibilityBufferSelectors originalVisibilitySelectors{};"
    "the controller context must own the rollback selector pair beside its graph")

extract_slice(
    "${_archive_source}"
    "struct FxArchiveRestoreTransactionWorkspace final"
    "static_assert(std::is_nothrow_default_constructible_v<"
    _restore_workspace
    "archive restore transaction workspace")
set(_previous_workspace_member "struct FxArchiveRestoreTransactionWorkspace final")
foreach(_workspace_member IN ITEMS
    "FxSystem rollbackSystem{};"
    "FxArchiveRestoreControlContext control{};"
    "FxArchiveRestorePhysicsScratch physicsScratch{};"
    "fx::physics::BodySidecar stagedPhysicsSidecar{};"
    "fx::physics::BodySidecar rollbackPhysicsSidecar{};")
    require_ordered(
        "${_restore_workspace}"
        "${_previous_workspace_member}"
        "${_workspace_member}"
        "the restore workspace must concretely own ordered transaction state")
    set(_previous_workspace_member "${_workspace_member}")
endforeach()
foreach(_workspace_lifetime_guard IN ITEMS
    "FxArchiveRestoreTransactionWorkspace() noexcept = default;"
    "~FxArchiveRestoreTransactionWorkspace() noexcept = default;"
    "const FxArchiveRestoreTransactionWorkspace &) = delete;")
    require_position(
        "${_restore_workspace}"
        "${_workspace_lifetime_guard}"
        _workspace_lifetime_position
        "the restore workspace must have explicit noncopying lifetime semantics")
endforeach()

extract_slice(
    "${_archive_source}"
    "struct FxArchiveRestorePhysicsScratch"
    "constexpr int FX_ARCHIVE_SYSTEM_SIZE"
    _restore_scratch
    "archive restore caller scratch")
foreach(_scratch_member IN ITEMS
    "FxArchivePhysicsOwnershipScratch ownership{};"
    "retirementCandidates{};"
    "PhysicsRetirementPlanScratch retirementPlanner{};"
    "FxPoolAllocationGraphScratch poolGraph{};")
    require_position(
        "${_restore_scratch}"
        "${_scratch_member}"
        _scratch_member_position
        "the heap workspace must own every bounded restore scratch image")
endforeach()

extract_slice(
    "${_archive_source}"
    "void __cdecl FX_Restore(int32_t clientIndex, MemoryFile *memFile)"
    "FxEffect *__cdecl FX_EffectFromHandle("
    _restore_source
    "FX_Restore transaction")
require_absent(
    "${_restore_source}"
    "FX_RestoreArchiveExclusiveState("
    "graph publication must never reacquire overwritten iterator state")
require_position(
    "${_restore_source}"
    "Sys_AtomicLoad(&rollbackSystem.iteratorCount) == -1\n                && FX_ValidateArchiveExclusiveState(system);"
    _rollback_snapshot_exclusive
    "the rollback graph image must preserve iterator -1")

extract_slice(
    "${_archive_source}"
    "bool FX_RebuildArchivePoolAllocationStates("
    "[[noreturn]] void FX_DropInvalidEffectHandle("
    _archive_pool_rebuild
    "archive pool-state preflight")
require_position(
    "${_archive_pool_rebuild}"
    "FxPoolAllocationGraphScratch *const scratch"
    _archive_pool_scratch_parameter
    "archive graph preflight must receive caller-owned scratch")
require_ordered(
    "${_archive_pool_rebuild}"
    "FxPoolRebuildAllocationStateLocked<FxTrailElem, MAX_TRAIL_ELEMS>("
    "FxValidatePoolAllocationGraphWithScratch("
    "archive preflight must rebuild every allocation sidecar before graph validation")
require_absent(
    "${_archive_pool_rebuild}"
    "FxValidatePoolAllocationGraph("
    "archive preflight must not allocate the legacy graph wrapper frame")

extract_slice(
    "${_restore_source}"
    "FxPoolAllocationGraphScratch *const poolGraphScratch ="
    "if (!FX_FixupEffectDefHandlesNoDrop("
    _pool_graph_preflight_lifetime
    "archive pool-graph scratch lifetime")
require_ordered(
    "${_pool_graph_preflight_lifetime}"
    "AllocateArchiveRestoreWorkspace<"
    "if (!poolGraphScratch)"
    "pool graph scratch allocation must be checked before validation")
require_ordered(
    "${_pool_graph_preflight_lifetime}"
    "if (!poolGraphScratch)"
    "FX_RebuildArchivePoolAllocationStates("
    "archive pool reconstruction must not run after allocation failure")
require_ordered(
    "${_pool_graph_preflight_lifetime}"
    "FX_RebuildArchivePoolAllocationStates("
    "DestroyArchiveRestoreWorkspace("
    "archive graph scratch must be destroyed immediately after preflight")
require_ordered(
    "${_pool_graph_preflight_lifetime}"
    "DestroyArchiveRestoreWorkspace("
    "if (!restoredPoolStateValid)"
    "archive graph scratch must be gone before invalid pool state is reported")
require_ordered(
    "${_pool_graph_preflight_lifetime}"
    "if (!restoredPoolStateValid)"
    "\"Invalid FX pool state in archive\""
    "invalid pool state may report only after scratch destruction")
require_absent(
    "${_pool_graph_preflight_lifetime}"
    "FxPoolAllocationGraphScratch poolGraphScratch{};"
    "archive preflight must not recreate graph scratch on the restore stack")
require_ordered(
    "${_restore_source}"
    "DestroyArchiveRestoreWorkspace("
    "FX_AllocateArchiveRestoreTransactionWorkspace()"
    "short-lived graph scratch must be destroyed before transaction workspace allocation")
require_ordered(
    "${_restore_source}"
    "FX_AllocateArchiveRestoreTransactionWorkspace()"
    "FX_BeginArchive(system, restoreGeneration)"
    "the checked restore workspace must exist before archive ownership is acquired")
extract_slice(
    "${_restore_source}"
    "if (!FX_BeginArchive(system, restoreGeneration))"
    "// Nothing below this point performs archive I/O"
    _begin_archive_failure
    "archive begin failure cleanup")
require_ordered(
    "${_begin_archive_failure}"
    "FX_DestroyArchiveRestoreTransactionWorkspace("
    "Z_Free(rollbackBuffers, 10);"
    "begin failure must destroy the empty workspace before staging storage")
require_ordered(
    "${_begin_archive_failure}"
    "FX_DestroyArchiveRestoreTransactionWorkspace("
    "Com_Error(ERR_DROP"
    "begin failure must destroy the empty workspace before reporting")

foreach(_former_restore_stack_local IN ITEMS
    "FxSystem rollbackSystem{};"
    "FxArchiveRestoreControlContext restoreContext{};"
    "fx::physics::BodySidecar stagedPhysicsSidecar"
    "fx::physics::BodySidecar rollbackPhysicsSidecar"
    "FxArchiveRestorePhysicsScratch physicsScratch{};"
    "FxPoolAllocationGraphScratch poolGraphScratch{};")
    require_absent(
        "${_restore_source}"
        "${_former_restore_stack_local}"
        "FX_Restore must not recreate transaction workspace members on its stack")
endforeach()
require_ordered(
    "${_restore_source}"
    "FxSystem &rollbackSystem = restoreWorkspace->rollbackSystem;"
    "FxArchiveRestoreControlContext &restoreContext ="
    "FX_Restore must bind rollback and control state from the heap workspace")
require_ordered(
    "${_restore_source}"
    "&restoreWorkspace->stagedPhysicsSidecar"
    "&restoreWorkspace->rollbackPhysicsSidecar"
    "FX_Restore must bind both transaction sidecars from the heap workspace")
require_ordered(
    "${_restore_source}"
    "&restoreWorkspace->rollbackPhysicsSidecar"
    "&restoreWorkspace->physicsScratch"
    "FX_Restore must bind caller scratch after its workspace sidecars")

# The desired pair is derived solely from validated Disk32 relocation equality,
# then resolved into the staged buffer image. No relocated address is ever
# reinterpreted as a native live pointer.
extract_slice(
    "${_restore_source}"
    "const bool readVisStateValid ="
    "bool physicsDataValid = true;"
    _desired_selector_staging
    "desired visibility selector staging")
require_ordered(
    "${_desired_selector_staging}"
    "if (!readVisStateValid || !writeVisStateValid"
    "const FxVisibilityBufferSelectors desiredVisibilitySelectors{"
    "both relocated addresses must validate before selector construction")
extract_slice(
    "${_restore_source}"
    "const FxVisibilityBufferSelectors desiredVisibilitySelectors{"
    "if (!FX_TryResolveVisibilitySelectors("
    _desired_selector_pair
    "desired visibility selector pair")
foreach(_desired_equality_marker IN ITEMS
    "relocatedReadAddress == secondLiveVisAddress\n            ? std::uint8_t{1}\n            : std::uint8_t{0}"
    "relocatedWriteAddress == secondLiveVisAddress\n            ? std::uint8_t{1}\n            : std::uint8_t{0}")
    require_position(
        "${_desired_selector_pair}"
        "${_desired_equality_marker}"
        _desired_equality_position
        "validated second-slot equality must map to one and first-slot equality to zero")
endforeach()
require_ordered(
    "${_desired_selector_staging}"
    "FX_TryResolveVisibilitySelectors("
    "&restoredSystem.visStateBufferRead"
    "desired read/write roles must resolve into staged storage before binding")
foreach(_desired_staged_slot IN ITEMS
    "&restoredBuffers->visState[0]"
    "&restoredBuffers->visState[1]")
    require_position(
        "${_desired_selector_staging}"
        "${_desired_staged_slot}"
        _desired_staged_slot_position
        "desired selectors must resolve only against restored buffers")
endforeach()
require_ordered(
    "${_desired_selector_staging}"
    "&restoredSystem.visStateBufferRead"
    "&restoredSystem.visStateBufferWrite"
    "staged read and write roles must retain their order")
require_ordered(
    "${_desired_selector_staging}"
    "&restoredSystem.visStateBufferWrite"
    "FX_ArchiveVisibilitySelectorsMatch("
    "the staged graph must round-trip after both selector bindings")
foreach(_forbidden_desired_pointer_publish IN ITEMS
    "reinterpret_cast<const FxVisState *>"
    "reinterpret_cast<FxVisState *>"
    "restoredSystem.visStateBufferRead = context."
    "restoredSystem.visStateBufferWrite = context.")
    require_absent(
        "${_desired_selector_staging}"
        "${_forbidden_desired_pointer_publish}"
        "restore must never publish relocated or foreign staged pointers")
endforeach()

# Capture selector values, the rollback system, and its buffers in one coherent
# FX_ALLOC interval. The rollback graph is immediately relinked and rebound to
# its own storage before that interval closes.
extract_slice(
    "${_restore_source}"
    "FxVisibilityBufferSelectors originalVisibilitySelectors{};"
    "// The archive owner keeps iterator exclusivity"
    _original_selector_snapshot
    "original visibility selector snapshot")
require_ordered(
    "${_original_selector_snapshot}"
    "Sys_EnterCriticalSection(CRITSECT_FX_ALLOC);"
    "FX_TryDeriveVisibilitySelectorPair("
    "original selectors must be captured only after allocator exclusion")
foreach(_live_original_marker IN ITEMS
    "&systemBuffers->visState[0]"
    "&systemBuffers->visState[1]"
    "system->visStateBufferRead"
    "system->visStateBufferWrite"
    "&originalVisibilitySelectors")
    require_position(
        "${_original_selector_snapshot}"
        "${_live_original_marker}"
        _live_original_position
        "original selectors must derive from exact live-buffer roles")
endforeach()
require_ordered(
    "${_original_selector_snapshot}"
    "FX_TryDeriveVisibilitySelectorPair("
    "std::memcpy(&rollbackSystem, system, sizeof(rollbackSystem));"
    "live selector derivation must precede rollback image capture")
require_ordered(
    "${_original_selector_snapshot}"
    "std::memcpy(&rollbackSystem, system, sizeof(rollbackSystem));"
    "std::memcpy(\n            rollbackBuffers,"
    "the rollback system and buffers must share one coherent snapshot")
require_ordered(
    "${_original_selector_snapshot}"
    "std::memcpy(\n            rollbackBuffers,"
    "FX_LinkSystemBuffers(&rollbackSystem, rollbackBuffers);"
    "rollback base pointers must relink only after both images are copied")
require_ordered(
    "${_original_selector_snapshot}"
    "FX_LinkSystemBuffers(&rollbackSystem, rollbackBuffers);"
    "FX_TryResolveVisibilitySelectors("
    "rollback visibility roles must resolve after base relinking")
foreach(_rollback_staged_slot IN ITEMS
    "&rollbackBuffers->visState[0]"
    "&rollbackBuffers->visState[1]")
    require_position(
        "${_original_selector_snapshot}"
        "${_rollback_staged_slot}"
        _rollback_staged_slot_position
        "rollback selectors must resolve only against rollback buffers")
endforeach()
require_ordered(
    "${_original_selector_snapshot}"
    "&rollbackSystem.visStateBufferRead"
    "&rollbackSystem.visStateBufferWrite"
    "rollback read and write roles must retain their order")
require_ordered(
    "${_original_selector_snapshot}"
    "&rollbackSystem.visStateBufferWrite"
    "FX_ArchiveVisibilitySelectorsMatch("
    "rollback selector bindings must round-trip before snapshot admission")
require_ordered(
    "${_original_selector_snapshot}"
    "FX_ArchiveVisibilitySelectorsMatch("
    "Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);"
    "rollback selector admission must finish inside the coherent snapshot")

# Both selector values are copied into the heap-owned controller context before
# the portable restore controller can dispatch any operation.
require_ordered(
    "${_restore_source}"
    "restoreContext.desiredVisibilitySelectors ="
    "restoreContext.originalVisibilitySelectors ="
    "controller selector pairs must retain desired then original ownership")
require_ordered(
    "${_restore_source}"
    "restoreContext.originalVisibilitySelectors ="
    "fx::archive::RunRestoreControl(restoreCallbacks)"
    "both selector pairs must be assigned before controller dispatch")

extract_slice(
    "${_archive_source}"
    "FX_PerformArchiveRestoreControlOperation("
    "void __cdecl FX_Restore("
    _restore_adapter
    "restore controller production adapter")
extract_slice(
    "${_restore_adapter}"
    "case Operation::CaptureOriginal:"
    "case Operation::PlanRetirement:"
    _capture_original
    "restore original-graph admission")
set(_desired_staged_roundtrip
    "FX_ArchiveVisibilitySelectorsMatch(\n                context.desiredSystem,\n                context.desiredBuffers,\n                context.desiredVisibilitySelectors)")
set(_original_staged_roundtrip
    "FX_ArchiveVisibilitySelectorsMatch(\n                context.originalSystem,\n                context.originalBuffers,\n                context.originalVisibilitySelectors)")
set(_live_original_roundtrip
    "FX_ArchiveVisibilitySelectorsMatch(\n                context.system,\n                context.systemBuffers,\n                context.originalVisibilitySelectors)")
require_ordered(
    "${_capture_original}"
    "${_desired_staged_roundtrip}"
    "${_original_staged_roundtrip}"
    "CaptureOriginal must preflight desired before rollback staged selectors")
require_ordered(
    "${_capture_original}"
    "${_original_staged_roundtrip}"
    "${_live_original_roundtrip}"
    "CaptureOriginal must preflight rollback staged before live selectors")
require_ordered(
    "${_capture_original}"
    "${_live_original_roundtrip}"
    "FX_ArchiveEffectRingIsValid(context.system)"
    "all selector round trips must precede original graph traversal")
require_ordered(
    "${_capture_original}"
    "${_live_original_roundtrip}"
    "FX_CollectArchivePhysicsEntries("
    "all selector round trips must precede original semantic collection")

extract_slice(
    "${_restore_adapter}"
    "case Operation::PublishDesiredGraph:"
    "case Operation::ValidateDesiredState:"
    _desired_publication
    "desired graph publication")
require_ordered(
    "${_desired_publication}"
    "Sys_AtomicStore(&context.desiredSystem->iteratorCount, -1);"
    "sizeof(*context.system));"
    "the desired graph image must publish iterator -1")
require_before_and_after(
    "${_desired_publication}"
    "FX_ValidateArchiveExclusiveState(context.system)"
    "sizeof(*context.system));"
    "desired graph publication must preserve archive exclusivity")
require_absent(
    "${_desired_publication}"
    "iteratorCount, 0"
    "desired graph publication must never create a nonexclusive window")
require_ordered(
    "${_desired_publication}"
    "const bool stagedSelectorsValid ="
    "const bool liveSelectorsResolved ="
    "desired staged roles must validate before live role resolution")
foreach(_desired_live_slot IN ITEMS
    "&context.systemBuffers->visState[0]"
    "&context.systemBuffers->visState[1]")
    require_position(
        "${_desired_publication}"
        "${_desired_live_slot}"
        _desired_live_slot_position
        "desired publication must resolve pointers against live buffers")
endforeach()
require_ordered(
    "${_desired_publication}"
    "const bool liveSelectorsResolved ="
    "if (!stagedSelectorsValid || !liveSelectorsResolved)"
    "desired publication must resolve both roles before its failure branch")
set(_desired_buffer_copy
    "std::memcpy(\n                context.systemBuffers,\n                context.desiredBuffers,")
set(_desired_system_copy
    "std::memcpy(\n                context.system,\n                context.desiredSystem,")
require_ordered(
    "${_desired_publication}"
    "if (!stagedSelectorsValid || !liveSelectorsResolved)"
    "${_desired_buffer_copy}"
    "desired selector preflight must finish before the first graph copy")
require_ordered(
    "${_desired_publication}"
    "${_desired_buffer_copy}"
    "${_desired_system_copy}"
    "desired publication must copy buffers before the system image")
require_ordered(
    "${_desired_publication}"
    "${_desired_system_copy}"
    "FX_LinkSystemBuffers(context.system, context.systemBuffers);"
    "desired base pointers must relink only after both graph copies")
require_ordered(
    "${_desired_publication}"
    "FX_LinkSystemBuffers(context.system, context.systemBuffers);"
    "context.system->visStateBufferRead = liveReadState;"
    "desired read role must bind only after base-pointer relinking")
require_ordered(
    "${_desired_publication}"
    "context.system->visStateBufferRead = liveReadState;"
    "context.system->visStateBufferWrite = liveWriteState;"
    "desired live selector roles must publish in read/write order")
require_ordered(
    "${_desired_publication}"
    "context.system->visStateBufferWrite = liveWriteState;"
    "desiredExclusiveState =\n                FX_ArchiveVisibilitySelectorsMatch("
    "desired live selector roles must round-trip after publication")
require_ordered(
    "${_desired_publication}"
    "desiredExclusiveState =\n                FX_ArchiveVisibilitySelectorsMatch("
    "Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);"
    "desired selector round-trip must finish inside FX_ALLOC")
foreach(_forbidden_desired_staged_pointer IN ITEMS
    "context.system->visStateBufferRead = context.desiredSystem"
    "context.system->visStateBufferWrite = context.desiredSystem"
    "reinterpret_cast<const FxVisState *>"
    "reinterpret_cast<FxVisState *>")
    require_absent(
        "${_desired_publication}"
        "${_forbidden_desired_staged_pointer}"
        "desired publication must not copy or reinterpret staged selector pointers")
endforeach()

extract_slice(
    "${_restore_adapter}"
    "case Operation::ValidateDesiredState:"
    "case Operation::ValidateDiscardedOriginalPhysics:"
    _desired_graph_admission
    "desired graph post-publication admission")
require_ordered(
    "${_desired_graph_admission}"
    "FX_ArchiveVisibilitySelectorsMatch("
    "FX_RebuildPoolAllocationStatesNoReport(context.system)"
    "desired selector round-trip must lead graph admission")

extract_slice(
    "${_restore_adapter}"
    "case Operation::PublishOriginalGraph:"
    "case Operation::ValidateOriginalGraph:"
    _rollback_publication
    "rollback graph publication")
require_before_and_after(
    "${_rollback_publication}"
    "FX_ValidateArchiveExclusiveState(context.system)"
    "sizeof(*context.system));"
    "rollback graph publication must preserve archive exclusivity")
require_absent(
    "${_rollback_publication}"
    "iteratorCount, 0"
    "rollback graph publication must never create a nonexclusive window")
require_ordered(
    "${_rollback_publication}"
    "const bool stagedSelectorsValid ="
    "const bool liveSelectorsResolved ="
    "rollback staged roles must validate before live role resolution")
foreach(_original_live_slot IN ITEMS
    "&context.systemBuffers->visState[0]"
    "&context.systemBuffers->visState[1]")
    require_position(
        "${_rollback_publication}"
        "${_original_live_slot}"
        _original_live_slot_position
        "rollback publication must resolve pointers against live buffers")
endforeach()
set(_original_buffer_copy
    "std::memcpy(\n                context.systemBuffers,\n                context.originalBuffers,")
set(_original_system_copy
    "std::memcpy(\n                context.system,\n                context.originalSystem,")
require_ordered(
    "${_rollback_publication}"
    "if (!stagedSelectorsValid || !liveSelectorsResolved)"
    "${_original_buffer_copy}"
    "rollback selector preflight must finish before the first graph copy")
require_ordered(
    "${_rollback_publication}"
    "${_original_buffer_copy}"
    "${_original_system_copy}"
    "rollback publication must copy buffers before the system image")
require_ordered(
    "${_rollback_publication}"
    "${_original_system_copy}"
    "FX_LinkSystemBuffers(context.system, context.systemBuffers);"
    "rollback base pointers must relink only after both graph copies")
require_ordered(
    "${_rollback_publication}"
    "FX_LinkSystemBuffers(context.system, context.systemBuffers);"
    "context.system->visStateBufferRead = liveReadState;"
    "rollback read role must bind only after base-pointer relinking")
require_ordered(
    "${_rollback_publication}"
    "context.system->visStateBufferRead = liveReadState;"
    "context.system->visStateBufferWrite = liveWriteState;"
    "rollback live selector roles must publish in read/write order")
require_ordered(
    "${_rollback_publication}"
    "context.system->visStateBufferWrite = liveWriteState;"
    "context.originalGraphPublished = true;"
    "rollback publication must mark graph mutation before fallible postchecks")
require_ordered(
    "${_rollback_publication}"
    "context.originalGraphPublished = true;"
    "originalExclusiveState =\n                FX_ArchiveVisibilitySelectorsMatch("
    "rollback mutation state must publish before selector round-trip")
require_ordered(
    "${_rollback_publication}"
    "originalExclusiveState =\n                FX_ArchiveVisibilitySelectorsMatch("
    "Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);"
    "rollback selector round-trip must finish inside FX_ALLOC")
foreach(_forbidden_original_staged_pointer IN ITEMS
    "context.system->visStateBufferRead = context.originalSystem"
    "context.system->visStateBufferWrite = context.originalSystem"
    "reinterpret_cast<const FxVisState *>"
    "reinterpret_cast<FxVisState *>")
    require_absent(
        "${_rollback_publication}"
        "${_forbidden_original_staged_pointer}"
        "rollback publication must not copy or reinterpret staged selector pointers")
endforeach()

extract_slice(
    "${_restore_adapter}"
    "case Operation::ValidateOriginalGraph:"
    "case Operation::PublishSafeEmpty:"
    _original_graph_admission
    "original graph post-publication admission")
require_ordered(
    "${_original_graph_admission}"
    "FX_ArchiveVisibilitySelectorsMatch("
    "(!context.originalGraphPublished"
    "original selector round-trip must lead graph admission")

# Keep the production adapter as a thin, one-case-per-operation translation
# layer. The portable controller owns sequencing and terminal-state choice.
require_occurrence_count(
    "${_restore_adapter}"
    "switch (operation)"
    1
    "the production adapter must use one operation dispatch")
foreach(_adapter_operation IN ITEMS
    CaptureOriginal
    PlanRetirement
    RetireOriginal
    PreparePhysicsReplacement
    CreateDesiredPhysics
    ValidateDesiredPhysics
    PublishPhysicsReplacement
    PublishDesiredGraph
    ValidateDesiredState
    ValidateDiscardedOriginalPhysics
    DrainNonLivePhysics
    RollbackPhysicsReplacement
    ValidateOriginalTokensInSnapshot
    ValidateOriginalTokensInLiveGraph
    ReconstructRetiredOriginalPhysics
    PatchOriginalTokensInSnapshot
    PatchOriginalTokensInLiveGraph
    ValidateOriginalPhysics
    PublishOriginalGraph
    ValidateOriginalGraph
    PublishSafeEmpty)
    require_occurrence_count(
        "${_restore_adapter}"
        "case Operation::${_adapter_operation}:"
        1
        "the production adapter must map every controller operation exactly once")
endforeach()
require_occurrence_count(
    "${_restore_adapter}"
    "FX_ValidatePoolAllocationGraphStateWithScratch("
    3
    "all restore graph validation states must reuse the workspace pool scratch")
foreach(_adapter_scratch_operation IN ITEMS
    "FX_CaptureArchivePhysicsRollbackRecipesLockedWithScratch("
    "fx::physics::PrepareReplacementWithScratch("
    "FX_ValidateArchivePhysicsOwnershipLockedWithScratch("
    "fx::physics::PublishReplacementWithScratch("
    "fx::physics::RollbackReplacementWithScratch(")
    require_position(
        "${_restore_adapter}"
        "${_adapter_scratch_operation}"
        _adapter_scratch_position
        "the restore controller adapter must use caller-owned sidecar scratch")
endforeach()
foreach(_adapter_stack_wrapper IN ITEMS
    "FX_ValidatePoolAllocationGraphState(context.system)"
    "FX_ValidateArchivePhysicsOwnershipLocked("
    "fx::physics::PrepareReplacement("
    "fx::physics::PublishReplacement("
    "fx::physics::RollbackReplacement(")
    require_absent(
        "${_restore_adapter}"
        "${_adapter_stack_wrapper}"
        "the restore controller adapter must not allocate wrapper scratch")
endforeach()

extract_slice(
    "${_restore_control_header}"
    "enum class RestoreControlOperationStatus"
    "enum class RestoreControlOutcome"
    _operation_status_declaration
    "restore controller operation status")
foreach(_operation_status IN ITEMS Success RecoverableFailure UnsafeFailure)
    require_position(
        "${_operation_status_declaration}"
        "${_operation_status}"
        _operation_status_position
        "restore controller must expose tri-state operation results")
endforeach()
extract_slice(
    "${_restore_control_header}"
    "enum class RestoreControlOutcome"
    "struct RestoreControlCallbacks"
    _outcome_declaration
    "restore controller outcome")
foreach(_outcome IN ITEMS
    DesiredPublished
    OriginalRestored
    SafeEmptyPublished
    UnsafeFailure)
    require_position(
        "${_outcome_declaration}"
        "${_outcome}"
        _outcome_position
        "restore controller must expose every terminal ownership state")
endforeach()
foreach(_controller_sequence IN ITEMS
    PrepareOperations
    DesiredPublicationOperations
    CommitOperations
    LiveGraphRecoveryOperations
    SnapshotRecoveryOperations)
    require_occurrence_count(
        "${_restore_control_source}"
        "${_controller_sequence}"
        2
        "restore controller must define and dispatch each transaction sequence")
endforeach()
extract_slice(
    "${_restore_control_source}"
    "SequenceResult RunSequence("
    "RestoreControlOutcome PublishSafeEmpty("
    _sequence_driver
    "restore controller sequence driver")
extract_slice(
    "${_sequence_driver}"
    "default:"
    "return SequenceResult::Success;"
    _invalid_status_scope
    "invalid callback status handling")
require_position(
    "${_invalid_status_scope}"
    "return SequenceResult::UnsafeFailure;"
    _invalid_status_failure
    "invalid callback status must fail closed")
extract_slice(
    "${_restore_control_source}"
    "RestoreControlOutcome RunRestoreControl("
    "} // namespace fx::archive"
    _controller_driver
    "restore controller driver")
require_ordered(
    "${_controller_driver}"
    "if (!callbacks.context || !callbacks.perform)"
    "return RestoreControlOutcome::UnsafeFailure;"
    "invalid controller callbacks must fail closed")
require_ordered(
    "${_controller_driver}"
    "RunSequence(callbacks, PrepareOperations)"
    "RunSequence(callbacks, DesiredPublicationOperations)"
    "preparation must precede desired graph publication")
require_ordered(
    "${_controller_driver}"
    "RunSequence(callbacks, DesiredPublicationOperations)"
    "RunSequence(callbacks, CommitOperations)"
    "desired graph validation must precede commit cleanup")

foreach(_obsolete_inline_control IN ITEMS
    livePhysicsCaptured
    retirementPlanned
    retirementComplete
    replacementPrepared
    replacementCreated
    stagedPhysicsValid
    replacementPublished
    restoreSucceeded
    validCommittedState
    oldStateRestored
    safeEmptyPublished
    safeTerminalState)
    require_absent(
        "${_restore_source}"
        "${_obsolete_inline_control}"
        "FX_Restore must not retain the obsolete inline controller branch tree")
endforeach()

require_occurrence_count(
    "${_restore_source}"
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS)"
    1
    "FX_Restore must acquire one continuous PHYSICS interval")
require_occurrence_count(
    "${_restore_source}"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS)"
    1
    "FX_Restore must release one continuous PHYSICS interval")
require_occurrence_count(
    "${_restore_source}"
    "fx::archive::RunRestoreControl(restoreCallbacks)"
    1
    "FX_Restore must delegate to the portable controller exactly once")
require_position(
    "${_restore_source}"
    "fx::archive::RunRestoreControl(restoreCallbacks)"
    _controller_call
    "restore controller invocation")
string(SUBSTRING
    "${_restore_source}" ${_controller_call} -1
    _restore_completion)
require_ordered(
    "${_restore_source}"
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "fx::archive::RunRestoreControl(restoreCallbacks)"
    "the controller must run only after PHYSICS acquisition")
require_ordered(
    "${_restore_completion}"
    "fx::archive::RunRestoreControl(restoreCallbacks)"
    "if (restoreOutcome == fx::archive::RestoreControlOutcome::UnsafeFailure)"
    "the controller outcome must drive centralized fail-stop handling")
require_ordered(
    "${_restore_completion}"
    "if (restoreOutcome == fx::archive::RestoreControlOutcome::UnsafeFailure)"
    "Sys_Error(\"FX archive restore could not recover a safe runtime state\")"
    "unsafe controller outcome must fail-stop")
require_ordered(
    "${_restore_completion}"
    "Sys_Error(\"FX archive restore could not recover a safe runtime state\")"
    "std::abort();"
    "unsafe restore must terminate after reporting the fatal condition")
require_ordered(
    "${_restore_completion}"
    "std::abort();"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "unrecoverable restore must fail-stop while native physics remains excluded")
require_ordered(
    "${_restore_completion}"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "FX_EndArchive(system)"
    "archive admission may reopen only after a safe terminal state exists")
require_ordered(
    "${_restore_completion}"
    "std::abort();"
    "FX_EndArchive(system)"
    "unsafe restore must terminate before archive admission can reopen")
require_ordered(
    "${_restore_completion}"
    "std::abort();"
    "Z_Free(rollbackBuffers, 10);"
    "unsafe restore must terminate before transaction scratch can be released")
extract_slice(
    "${_restore_completion}"
    "if (restoreOutcome == fx::archive::RestoreControlOutcome::UnsafeFailure)"
    "    }\n\n    Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    _unsafe_restore_branch
    "unsafe restore fail-stop branch")
foreach(_unsafe_cleanup IN ITEMS
    "FX_DestroyArchiveRestoreTransactionWorkspace("
    "FX_FreeArchiveRestoreWorkspaceMemory("
    "Z_Free("
    "Sys_LeaveCriticalSection("
    "FX_EndArchive("
    "system->isArchiving = false")
    require_absent(
        "${_unsafe_restore_branch}"
        "${_unsafe_cleanup}"
        "unsafe restore must retain workspace, locks, and archive admission until termination")
endforeach()
require_position(
    "${_restore_source}"
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    _physics_enter
    "restore PHYSICS acquisition")
require_position(
    "${_restore_source}"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    _physics_leave
    "restore PHYSICS release")
if(NOT _physics_enter LESS _physics_leave)
    message(FATAL_ERROR "FX_Restore PHYSICS interval is inverted")
endif()
math(EXPR _physics_length "${_physics_leave} - ${_physics_enter}")
string(SUBSTRING
    "${_restore_source}" ${_physics_enter} ${_physics_length}
    _physics_transaction)
foreach(_forbidden_physics_call IN ITEMS
    "Phys_ObjDestroy("
    "Pool_ValidateFull("
    "Pool_GetFreeCount("
    "Com_Error("
    "MyAssertHandler("
    "Com_Print"
    "MemFile_"
    "Z_Malloc("
    "Z_Free("
    "Phys_GetStateFromBody("
    "Phys_TryCreateBodyFromStateAndXModel("
    "FX_RebuildPoolAllocationStates(system)"
    "FX_RestoreArchiveExclusiveState(")
    require_absent(
        "${_physics_transaction}"
        "${_forbidden_physics_call}"
        "the PHYSICS transaction must not allocate, perform I/O, report, or call asserting helpers")
endforeach()
foreach(_required_no_report_call IN ITEMS
    "Phys_TryGetFreeResourceCapacityLockedNoReport("
    "Phys_TryValidateBodyDestroyLockedNoReport("
    "Phys_TryDestroyBodyLockedNoReport("
    "Phys_TryCreateBodyFromStateAndXModelLockedNoReport("
    "FX_RebuildPoolAllocationStatesNoReport(")
    require_position(
        "${_archive_source}"
        "${_required_no_report_call}"
        _no_report_position
        "archive transaction must use its no-report primitive")
endforeach()

string(SUBSTRING "${_restore_source}" ${_physics_leave} -1 _after_physics)
require_ordered(
    "${_after_physics}"
    "FX_EndArchive(system);"
    "FX_DestroyArchiveRestoreTransactionWorkspace(restoreWorkspace)"
    "safe restore cleanup must destroy the workspace only after archive release")
require_ordered(
    "${_after_physics}"
    "FX_DestroyArchiveRestoreTransactionWorkspace(restoreWorkspace)"
    "Z_Free(rollbackBuffers, 10);"
    "safe restore cleanup must destroy the workspace before staging memory is freed")
require_ordered(
    "${_after_physics}"
    "Z_Free(restoredBuffers, 10);"
    "if (restoreOutcome"
    "terminal outcome must be checked only after transaction storage is released")
require_position(
    "${_after_physics}"
    "!= fx::archive::RestoreControlOutcome::DesiredPublished"
    _desired_outcome_check
    "only desired publication may report restore success")
require_ordered(
    "${_after_physics}"
    "!= fx::archive::RestoreControlOutcome::DesiredPublished"
    "Com_Error("
    "all non-desired terminal states must report ERR_DROP")
require_occurrence_count(
    "${_restore_source}"
    "RestoreControlOutcome::DesiredPublished"
    1
    "DesiredPublished must be the sole nonfatal restore outcome")
require_occurrence_count(
    "${_restore_source}"
    "RestoreControlOutcome::UnsafeFailure"
    1
    "UnsafeFailure must have one centralized fail-stop")
require_occurrence_count(
    "${_restore_source}"
    "FX_DestroyArchiveRestoreTransactionWorkspace("
    2
    "workspace destruction must occur only on begin failure and safe terminal cleanup")
foreach(_non_success_outcome IN ITEMS OriginalRestored SafeEmptyPublished)
    require_absent(
        "${_restore_source}"
        "RestoreControlOutcome::${_non_success_outcome}"
        "recovery outcomes must not be treated as restore success")
endforeach()

message(STATUS "FX archive full-capacity physics transaction contract passed")
