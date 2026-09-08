cmake_minimum_required(VERSION 3.16)

# Source invariants for the ki-v4m phys_obj_id owner-bound and rollback
# rework. The engine TUs scanned here are built ONLY by the Windows x86
# CI legs, so the Linux portable build cannot compile-verify them; these
# text-level invariants pin the assessed defects closed:
#   1. the dynent sidecar owner-key stride bound is rejected through a
#      release-effective Com_Error path (not an assert-only handler),
#      before the entity lists are published;
#   2. every failed-bind site destroys the freshly created body;
#   3. the cpose creation Assign and the breakable-piece Bind run inside
#      their documented CRITSECT_PHYSICS spans.

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_load_obj_path "${SOURCE_ROOT}/src/DynEntity/DynEntity_load_obj.cpp")
set(_pieces_path "${SOURCE_ROOT}/src/DynEntity/DynEntity_pieces.cpp")
set(_ents_mp_path "${SOURCE_ROOT}/src/cgame_mp/cg_ents_mp.cpp")
set(_snapshot_mp_path "${SOURCE_ROOT}/src/cgame_mp/cg_snapshot_mp.cpp")
foreach(_path IN ITEMS
    "${_load_obj_path}" "${_pieces_path}"
    "${_ents_mp_path}" "${_snapshot_mp_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing phys_obj_id owner-bound source: ${_path}")
    endif()
endforeach()
file(READ "${_load_obj_path}" _load_obj)
file(READ "${_pieces_path}" _pieces)
file(READ "${_ents_mp_path}" _ents_mp)
file(READ "${_snapshot_mp_path}" _snapshot_mp)

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
        message(FATAL_ERROR "Missing phys_obj_id owner-bound invariant: ${description}")
    endif()
endfunction()

function(require_ordered haystack first second description)
    string(FIND "${haystack}" "${first}" _first)
    if(_first EQUAL -1)
        message(FATAL_ERROR "Missing first phys_obj_id owner-bound invariant: ${description}")
    endif()
    string(SUBSTRING "${haystack}" ${_first} -1 _tail)
    string(FIND "${_tail}" "${second}" _second)
    if(_second LESS_EQUAL 0)
        message(FATAL_ERROR
            "Missing or unordered phys_obj_id owner-bound invariant: ${description}")
    endif()
endfunction()

function(forbid_contains haystack needle description)
    string(FIND "${haystack}" "${needle}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden phys_obj_id owner-bound behavior (${description}): ${needle}")
    endif()
endfunction()

# --- MP def loader: the per-draw-type stride bound must be release-effective.
extract_slice(
    "${_load_obj}"
    "for (drawTypea = 0; drawTypea < 2; ++drawTypea)"
    "cm.dynEntPoseList[drawTypea] = "
    _mp_stride
    "MP def-loader stride gate")
require_contains("${_mp_stride}"
    "kDynEntPhysObjIdOwnerPerDrawType"
    "MP def-loader gates on the shared owner-key stride constant")
require_ordered("${_mp_stride}"
    "Com_Error("
    "ERR_DROP"
    "MP def-loader rejects oversize counts via ERR_DROP before publication")
forbid_contains("${_mp_stride}"
    "MyAssertHandler"
    "MP def-loader stride gate cannot be assert-only")

# --- SP save loader: same release-effective bound plus bind-failure rollback.
extract_slice(
    "${_load_obj}"
    "void __cdecl DynEnt_LoadEntities(MemoryFile *memFile)"
    "#endif // KISAK_SP"
    _sp_loader
    "SP save loader")
require_ordered("${_sp_loader}"
    "kDynEntPhysObjIdOwnerPerDrawType"
    "ERR_DROP"
    "SP save loader rejects oversize save counts via ERR_DROP before reads")
forbid_contains("${_sp_loader}"
    "MyAssertHandler"
    "SP save loader stride gate cannot be assert-only")
require_ordered("${_sp_loader}"
    "bind.status != phys_obj_id::Status::Success"
    "Phys_ObjDestroy(PHYS_WORLD_DYNENT, physObjIdBody);"
    "SP save-loader failed bind destroys the fresh body")
require_ordered("${_sp_loader}"
    "Phys_ObjDestroy(PHYS_WORLD_DYNENT, physObjIdBody);"
    "dynEntClient->physObjId = 0;"
    "SP save-loader clears the field only after the body is destroyed")
forbid_contains("${_sp_loader}"
    "leak the body"
    "SP save loader must not document a deliberate leak")

# --- Breakable pieces: Bind inside the lock span; failed bind rolls back.
extract_slice(
    "${_pieces}"
    "DynEntPiecesPhysSpawnResult spawnResult"
    "if (spawnResult.cleanupFailed)"
    _piece_spawn
    "breakable-piece spawn transaction")
require_ordered("${_piece_spawn}"
    "g_breakablePieceBodySidecar.Bind("
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "breakable-piece Bind runs inside the physics lock span")
require_ordered("${_piece_spawn}"
    "bindFailed = true;"
    "Phys_TryDestroyBodyLockedNoReport("
    "breakable-piece failed bind destroys the fresh body")
forbid_contains("${_pieces}"
    "let the engine drain the body on shutdown"
    "breakable-piece failed bind must not defer cleanup to shutdown")

# --- MP cpose creation: Assign inside the lock span; failed assign rolls back.
extract_slice(
    "${_ents_mp}"
    "bool assignFailed = false;"
    "if (cleanupFailed"
    _cpose_create
    "cpose creation transaction")
require_ordered("${_cpose_create}"
    "CG_CPosePhysObjId_Assign(cent, physObjId)"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "cpose creation Assign runs inside the physics lock span")
require_ordered("${_cpose_create}"
    "assignFailed = true;"
    "Phys_TryDestroyBodyLockedNoReport("
    "cpose failed assign destroys the fresh body under the same lock")

# --- Shutdown comment: the legacy dead-token condition must be stated
# --- accurately (dead tokens entered the block only under TR_PHYSICS).
require_contains("${_snapshot_mp}"
    "physObjId != -1 || trType == TR_PHYSICS"
    "shutdown comment quotes the legacy dead-token condition")
forbid_contains("${_snapshot_mp}"
    "the field is unconditionally"
    "shutdown comment must not misstate the legacy reset condition")

message(STATUS "phys_obj_id owner-bound source invariants verified")
