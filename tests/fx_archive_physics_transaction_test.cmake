cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

set(_archive_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive.cpp")
if(NOT EXISTS "${_archive_source_path}")
    message(FATAL_ERROR
        "FX archive source not found: ${_archive_source_path}")
endif()
file(READ "${_archive_source_path}" _archive_source)

function(require_position source needle out_position description)
    string(FIND "${source}" "${needle}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "${description}: missing '${needle}'")
    endif()
    set(${out_position} ${_position} PARENT_SCOPE)
endfunction()

foreach(_forbidden_pointer_codec
    "FX_DecodeArchivedPhysicsBody"
    "FX_EncodeArchivedPhysicsBody"
    "originalPhysObjId")
    string(FIND
        "${_archive_source}" "${_forbidden_pointer_codec}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "FX archive must not treat the per-owner token as a body address: ${_forbidden_pointer_codec}")
    endif()
endforeach()

foreach(_required_token_operation
    "fx::physics::TokenFromLegacyField("
    "fx::physics::TokenToLegacyField("
    "fx::physics::ValidateSemanticOwnership("
    "fx::physics::PrepareReplacement("
    "fx::physics::PublishReplacement("
    "fx::physics::RollbackReplacement(")
    string(FIND
        "${_archive_source}" "${_required_token_operation}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "FX archive sidecar transaction is missing ${_required_token_operation}")
    endif()
endforeach()

require_position(
    "${_archive_source}"
    "bool FX_DrainArchivePhysicsSidecarLocked("
    _helpers_begin
    "archive physics cleanup must expose a lock-owning helper")
require_position(
    "${_archive_source}"
    "bool FX_CreateArchivePhysicsLocked("
    _create_helper_begin
    "archive physics creation helper must exist")
require_position(
    "${_archive_source}"
    "bool FX_GetArchiveModelGeomCount("
    _drain_helper_end
    "archive physics drain helper boundary must exist")
require_position(
    "${_archive_source}"
    "void __cdecl FX_Restore(int32_t clientIndex, MemoryFile *memFile)"
    _restore_begin
    "FX restore implementation must exist")
if(NOT _helpers_begin LESS _drain_helper_end
    OR NOT _drain_helper_end LESS _create_helper_begin
    OR NOT _create_helper_begin LESS _restore_begin)
    message(FATAL_ERROR
        "archive physics helpers must precede FX_Restore")
endif()

math(EXPR _helpers_length "${_drain_helper_end} - ${_helpers_begin}")
string(SUBSTRING
    "${_archive_source}" ${_helpers_begin} ${_helpers_length}
    _helper_source)
foreach(_unexpected_lock
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);")
    string(FIND "${_helper_source}" "${_unexpected_lock}" _lock_position)
    if(NOT _lock_position EQUAL -1)
        message(FATAL_ERROR
            "Locked archive physics helpers must not change transaction lock ownership")
    endif()
endforeach()

math(EXPR _create_helper_length
    "${_restore_begin} - ${_create_helper_begin}")
string(SUBSTRING
    "${_archive_source}"
    ${_create_helper_begin}
    ${_create_helper_length}
    _create_helper_source)
require_position(
    "${_create_helper_source}"
    "FX_ArchivePhysicsCapacityAvailableLocked("
    _capacity
    "restore must reject insufficient ODE capacity before construction")
require_position(
    "${_create_helper_source}"
    "Phys_TryCreateBodyFromStateAndXModel("
    _create_body
    "restore must transactionally construct a complete body")
require_position(
    "${_create_helper_source}"
    "fx::physics::Bind("
    _bind
    "restore must bind every native body to its owner slot")
require_position(
    "${_create_helper_source}"
    "entry.elem->physObjId ="
    _write_token
    "restore must replace serialized bits with a fresh token")
if(NOT _capacity LESS _create_body
    OR NOT _create_body LESS _bind
    OR NOT _bind LESS _write_token)
    message(FATAL_ERROR
        "FX archive create/capacity/bind/token ordering regressed")
endif()

string(SUBSTRING "${_archive_source}" ${_restore_begin} -1 _restore_tail)
require_position(
    "${_restore_tail}"
    "void __cdecl FX_RestoreEffectDefTable("
    _restore_end
    "FX restore boundary must remain identifiable")
string(SUBSTRING "${_restore_tail}" 0 ${_restore_end} _restore_source)

string(REGEX MATCHALL
    "Sys_EnterCriticalSection\\(CRITSECT_PHYSICS\\)"
    _physics_enters
    "${_restore_source}")
string(REGEX MATCHALL
    "Sys_LeaveCriticalSection\\(CRITSECT_PHYSICS\\)"
    _physics_leaves
    "${_restore_source}")
list(LENGTH _physics_enters _physics_enter_count)
list(LENGTH _physics_leaves _physics_leave_count)
if(NOT _physics_enter_count EQUAL 1 OR NOT _physics_leave_count EQUAL 1)
    message(FATAL_ERROR
        "FX_Restore must own exactly one continuous physics critical section")
endif()

require_position(
    "${_restore_source}" "if (!FX_BeginArchive(system))"
    _archive_begin "restore must acquire archive exclusion")
require_position(
    "${_restore_source}" "Sys_EnterCriticalSection(CRITSECT_FX_ALLOC);"
    _snapshot_enter "restore must drain and snapshot the FX allocator")
require_position(
    "${_restore_source}" "Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);"
    _snapshot_leave "restore must finish the allocator drain interval")
require_position(
    "${_restore_source}" "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    _physics_enter "restore must begin the physics transaction")
require_position(
    "${_restore_source}" "fx::physics::PrepareReplacement("
    _prepare "restore must prepare a generation-related replacement")
require_position(
    "${_restore_source}" "FX_CreateArchivePhysicsLocked("
    _create "restore must create staged bodies under the transaction lock")
require_position(
    "${_restore_source}" "fx::physics::PublishReplacement("
    _publish_sidecar "sidecar replacement must precede graph publication")
require_position(
    "${_restore_source}" "memcpy(systemBuffers, restoredBuffers"
    _publish_graph "restore must publish the staged graph")
require_position(
    "${_restore_source}" "validCommittedState = restoredExclusiveState"
    _validate_commit "restore must validate the published pair")
require_position(
    "${_restore_source}" "FX_DrainArchivePhysicsSidecarLocked("
    _commit_drain "old bodies must be drained only after validation")
require_position(
    "${_restore_source}" "fx::physics::RollbackReplacement("
    _rollback_sidecar "failed publication must restore sidecar ownership")
require_position(
    "${_restore_source}" "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    _physics_leave "restore must finish the physics transaction")
require_position(
    "${_restore_source}" "FX_EndArchive(system);"
    _archive_end "restore must release archive exclusion")

string(SUBSTRING "${_restore_source}" ${_rollback_sidecar} -1 _rollback_tail)
require_position(
    "${_rollback_tail}" "rollbackBuffers,"
    _rollback_copy_relative
    "old graph publication must follow sidecar rollback")
math(EXPR _rollback_copy
    "${_rollback_sidecar} + ${_rollback_copy_relative}")

if(NOT _archive_begin LESS _snapshot_enter
    OR NOT _snapshot_enter LESS _snapshot_leave
    OR NOT _snapshot_leave LESS _physics_enter
    OR NOT _physics_enter LESS _prepare
    OR NOT _prepare LESS _create
    OR NOT _create LESS _publish_sidecar
    OR NOT _publish_sidecar LESS _publish_graph
    OR NOT _publish_graph LESS _validate_commit
    OR NOT _validate_commit LESS _commit_drain
    OR NOT _validate_commit LESS _rollback_sidecar
    OR NOT _rollback_sidecar LESS _rollback_copy
    OR NOT _commit_drain LESS _physics_leave
    OR NOT _rollback_copy LESS _physics_leave
    OR NOT _physics_leave LESS _archive_end)
    message(FATAL_ERROR
        "FX archive sidecar/graph commit and rollback ordering regressed")
endif()

require_position(
    "${_archive_source}"
    "bufferSnapshot->elems[ownerIndex].item.physObjId ="
    _save_marker
    "save must serialize a stable non-pointer owner marker")

message(STATUS "FX archive physics sidecar transaction contract passed")
