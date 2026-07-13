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

require_position(
    "${_archive_source}"
    "void FX_DestroyCreatedArchivePhysicsLocked("
    _helpers_begin
    "archive physics cleanup must expose a lock-owning helper")
require_position(
    "${_archive_source}"
    "void __cdecl FX_Restore(int32_t clientIndex, MemoryFile *memFile)"
    _restore_begin
    "FX restore implementation must exist")
if(NOT _helpers_begin LESS _restore_begin)
    message(FATAL_ERROR
        "archive physics helpers must precede FX_Restore")
endif()

math(EXPR _helpers_length "${_restore_begin} - ${_helpers_begin}")
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
    "${_restore_source}" "FX_CaptureReplacedArchivePhysicsLocked("
    _capture "restore must capture old bodies under the transaction lock")
require_position(
    "${_restore_source}" "FX_CreateArchivePhysicsLocked("
    _create "restore must create staged bodies under the transaction lock")
require_position(
    "${_restore_source}" "FX_DestroyReplacedArchivePhysicsLocked("
    _commit_destroy "commit must retire old bodies under the transaction lock")
require_position(
    "${_restore_source}" "FX_DestroyCreatedArchivePhysicsLocked("
    _rollback_destroy "rollback must retire staged bodies under the transaction lock")
require_position(
    "${_restore_source}" "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    _physics_leave "restore must finish the physics transaction")
require_position(
    "${_restore_source}" "FX_EndArchive(system);"
    _archive_end "restore must release archive exclusion")

if(NOT _archive_begin LESS _snapshot_enter
    OR NOT _snapshot_enter LESS _snapshot_leave
    OR NOT _snapshot_leave LESS _physics_enter
    OR NOT _physics_enter LESS _capture
    OR NOT _capture LESS _create
    OR NOT _create LESS _commit_destroy
    OR NOT _create LESS _rollback_destroy
    OR NOT _commit_destroy LESS _physics_leave
    OR NOT _rollback_destroy LESS _physics_leave
    OR NOT _physics_leave LESS _archive_end)
    message(FATAL_ERROR
        "FX archive body construction/publication/rollback lock order regressed")
endif()

message(STATUS "FX archive physics transaction lock contract passed")
