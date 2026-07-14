cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

set(_archive_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive.cpp")
set(_restore_control_header_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_restore_control.h")
set(_restore_control_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_restore_control.cpp")
set(_system_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_system.cpp")
foreach(_required_source IN ITEMS
    "${_archive_source_path}"
    "${_restore_control_header_path}"
    "${_restore_control_source_path}"
    "${_system_source_path}")
    if(NOT EXISTS "${_required_source}")
        message(FATAL_ERROR "FX source not found: ${_required_source}")
    endif()
endforeach()
file(READ "${_archive_source_path}" _archive_source)
file(READ "${_restore_control_header_path}" _restore_control_header)
file(READ "${_restore_control_source_path}" _restore_control_source)
file(READ "${_system_source_path}" _system_source)

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
    "FX_ReconstructRetiredArchivePhysicsLocked("
    "bool FX_ArchiveRetiredTokenTargetsMatch("
    _reconstruct_physics_scope
    "retired-body reconstruction")
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
    "fx::archive::BuildPhysicsRetirementPlan("
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
    "fx::physics::ValidateDisjointOwnership("
    "sidecars must be distinct before ownership comparison")
require_ordered(
    "${_drain_helper}"
    "fx::physics::ValidateDisjointOwnership("
    "fx::physics::SnapshotOwnership("
    "all pairwise ownership must be disjoint before body inspection")
require_ordered(
    "${_drain_helper}"
    "fx::physics::SnapshotOwnership("
    "Phys_TryValidateBodyDestroyLockedNoReport("
    "each selected sidecar must snapshot before native destroy preflight")
require_ordered(
    "${_drain_helper}"
    "Phys_TryValidateBodyDestroyLockedNoReport("
    "fx::physics::TakeFirst("
    "every selected native body must preflight before the first Take")
require_ordered(
    "${_drain_helper}"
    "fx::physics::TakeFirst("
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
    "FX_RetireArchivePhysicsLocked("
    "FX_ReconstructRetiredArchivePhysicsLocked("
    _retire_helper
    "bounded retirement helper")
require_ordered(
    "${_retire_helper}"
    "Phys_TryValidateBodyDestroyLockedNoReport("
    "fx::physics::Take("
    "every selected retirement must preflight before the first Take")
require_ordered(
    "${_retire_helper}"
    "fx::physics::Take("
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
        "bounded retirement must remain checked and non-reporting")
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
    "FX_PublishArchiveSafeEmptyStateLocked(system)"
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
    "bool __cdecl FX_PublishArchiveSafeEmptyStateLocked("
    _safe_empty_preflight
    "safe-empty graph preflight")
require_ordered(
    "${_safe_empty_preflight}"
    "FX_ValidateArchiveExclusiveState(system)"
    "FX_CanResetSystemGraphUnderExclusiveClaim(system)"
    "safe-empty graph preflight must validate exclusivity before reset prerequisites")

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

# The shared validator is the proof that the archive owner still holds gate 2
# and iterator -1. Publication copies preserve -1 and validate that proof on
# both sides of the desired and rollback graph memcpy operations.
extract_slice(
    "${_system_source}"
    "bool __cdecl FX_ValidateArchiveExclusiveState("
    "bool __cdecl FX_EndArchive("
    _exclusive_validator
    "archive-exclusive validator")
require_ordered(
    "${_exclusive_validator}"
    "Sys_AtomicLoad(gate) == 2"
    "FX_CurrentThreadOwnsArchive(system)"
    "archive-exclusive validation must require the acquired gate")
require_ordered(
    "${_exclusive_validator}"
    "FX_CurrentThreadOwnsArchive(system)"
    "Sys_AtomicLoad(&system->iteratorCount) == -1"
    "archive-exclusive validation must require the preserved exclusive iterator")

extract_slice(
    "${_archive_source}"
    "void __cdecl FX_Restore(int32_t clientIndex, MemoryFile *memFile)"
    "void __cdecl FX_RestoreEffectDefTable("
    _restore_source
    "FX_Restore transaction")
require_absent(
    "${_restore_source}"
    "FX_RestoreArchiveExclusiveState("
    "graph publication must never reacquire overwritten iterator state")
require_position(
    "${_restore_source}"
    "Sys_AtomicLoad(&rollbackSystem.iteratorCount) == -1"
    _rollback_snapshot_exclusive
    "the rollback graph image must preserve iterator -1")

extract_slice(
    "${_archive_source}"
    "FX_PerformArchiveRestoreControlOperation("
    "void __cdecl FX_Restore("
    _restore_adapter
    "restore controller production adapter")
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
    "Z_Free(rollbackBuffers, 10);"
    "archive exclusion must be released before staging memory is freed")
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
foreach(_non_success_outcome IN ITEMS OriginalRestored SafeEmptyPublished)
    require_absent(
        "${_restore_source}"
        "RestoreControlOutcome::${_non_success_outcome}"
        "recovery outcomes must not be treated as restore success")
endforeach()

message(STATUS "FX archive full-capacity physics transaction contract passed")
