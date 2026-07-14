cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

set(_archive_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive.cpp")
set(_system_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_system.cpp")
foreach(_required_source IN ITEMS
    "${_archive_source_path}"
    "${_system_source_path}")
    if(NOT EXISTS "${_required_source}")
        message(FATAL_ERROR "FX source not found: ${_required_source}")
    endif()
endforeach()
file(READ "${_archive_source_path}" _archive_source)
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
require_position(
    "${_archive_source}"
    "Sys_Error(\"FX archive native-body cleanup failed after ownership transfer\")"
    _cleanup_fail_stop
    "post-transfer native cleanup must fail-stop under archive ownership")

require_occurrence_count(
    "${_archive_source}"
    "if (bodyStatus == PhysBodyModelCreateStatus::CleanupFailed)"
    2
    "both desired creation and retired-body reconstruction must classify retained cleanup ownership")
extract_slice(
    "${_archive_source}"
    "bool FX_ReconstructRetiredArchivePhysicsLocked("
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
    "FX_FailArchivePhysicsCleanupLocked();"
    "retained reconstruction ownership must fail-stop before ordinary rollback")
require_ordered(
    "${_reconstruct_failure_scope}"
    "FX_FailArchivePhysicsCleanupLocked();"
    "return false;"
    "retained reconstruction ownership must not return recoverably")

extract_slice(
    "${_archive_source}"
    "bool FX_CreateArchivePhysicsLocked("
    "void __cdecl FX_Restore("
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
    "FX_FailArchivePhysicsCleanupLocked();"
    "retained desired ownership must fail-stop before ordinary restore failure")
require_ordered(
    "${_create_failure_scope}"
    "FX_FailArchivePhysicsCleanupLocked();"
    "created = false;"
    "retained desired ownership must not enter recoverable rollback")

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
    "bool FX_RetireArchivePhysicsLocked("
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
    "bool FX_DrainArchivePhysicsSidecarsLocked("
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
    "bool FX_RetireArchivePhysicsLocked("
    "bool FX_ReconstructRetiredArchivePhysicsLocked("
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
    "bool FX_PublishArchivePhysicsSafeEmptyLocked("
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
    "${_restore_source}"
    "if (replacementPublished)"
    "validCommittedState = restoredExclusiveState"
    _desired_publication
    "desired graph publication")
require_ordered(
    "${_desired_publication}"
    "Sys_AtomicStore(&restoredSystem.iteratorCount, -1);"
    "memcpy(system, &restoredSystem, sizeof(*system));"
    "the desired graph image must publish iterator -1")
require_before_and_after(
    "${_desired_publication}"
    "FX_ValidateArchiveExclusiveState(system)"
    "memcpy(system, &restoredSystem, sizeof(*system));"
    "desired graph publication must preserve archive exclusivity")
require_absent(
    "${_desired_publication}"
    "iteratorCount, 0"
    "desired graph publication must never create a nonexclusive window")

extract_slice(
    "${_restore_source}"
    "if (rollbackPhysicsValid)"
    "oldStateRestored = stagedDrained"
    _rollback_publication
    "rollback graph publication")
require_before_and_after(
    "${_rollback_publication}"
    "FX_ValidateArchiveExclusiveState(system)"
    "&rollbackSystem,"
    "rollback graph publication must preserve archive exclusivity")
require_absent(
    "${_rollback_publication}"
    "iteratorCount, 0"
    "rollback graph publication must never create a nonexclusive window")

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
require_ordered(
    "${_restore_source}"
    "const bool safeTerminalState ="
    "Sys_Error(\"FX archive restore could not recover a safe runtime state\")"
    "unrecoverable restore must classify terminal safety before fail-stop")
require_ordered(
    "${_restore_source}"
    "Sys_Error(\"FX archive restore could not recover a safe runtime state\")"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "unrecoverable restore must fail-stop while native physics remains excluded")
require_ordered(
    "${_restore_source}"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "FX_EndArchive(system)"
    "archive admission may reopen only after a safe terminal state exists")
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
    "FX_RebuildPoolAllocationStatesNoReport(system)")
    require_position(
        "${_archive_source}"
        "${_required_no_report_call}"
        _no_report_position
        "archive transaction must use its no-report primitive")
endforeach()

require_ordered(
    "${_restore_source}"
    "FX_CaptureArchivePhysicsRollbackRecipesLocked("
    "FX_BuildArchivePhysicsRetirementPlanLocked("
    "rollback recipes must precede retirement planning")
require_ordered(
    "${_restore_source}"
    "FX_RetireArchivePhysicsLocked("
    "fx::physics::PrepareReplacement("
    "retirement must finish before replacement provenance changes")
require_ordered(
    "${_restore_source}"
    "fx::physics::PrepareReplacement("
    "FX_CreateArchivePhysicsLocked("
    "replacement provenance must precede construction")
require_ordered(
    "${_restore_source}"
    "FX_CreateArchivePhysicsLocked("
    "fx::physics::PublishReplacement("
    "replacement construction must finish before sidecar publication")
require_ordered(
    "${_restore_source}"
    "fx::physics::RollbackReplacement("
    "FX_ReconstructRetiredArchivePhysicsLocked("
    "sidecar rollback and discarded-body drain must precede reconstruction")

string(SUBSTRING "${_restore_source}" ${_physics_leave} -1 _after_physics)
require_ordered(
    "${_after_physics}"
    "FX_EndArchive(system);"
    "Z_Free(rollbackBuffers, 10);"
    "archive exclusion must be released before staging memory is freed")
require_ordered(
    "${_after_physics}"
    "Z_Free(restoredBuffers, 10);"
    "Com_Error("
    "all transaction storage must be freed before ERR_DROP")

message(STATUS "FX archive full-capacity physics transaction contract passed")
