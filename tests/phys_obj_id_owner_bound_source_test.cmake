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
#      their documented CRITSECT_PHYSICS spans;
#   4. the SP save loader clears the serialized physObjId token BEFORE
#      body restoration, so a failed Phys_ObjLoad can never leave a
#      stale token that resolves to another owner's body;
#   5. the SP save loader WriteBind runs inside a CRITSECT_PHYSICS span
#      (the caller G_LoadMainState holds no lock) and leaves the span
#      BEFORE Phys_ObjDestroy, which manages its own locking;
#   6. the SP save saver ReadResolve runs inside a CRITSECT_PHYSICS span
#      (the caller G_SaveMainState holds no lock) and leaves the span
#      BEFORE Phys_ObjSave, which only reads the resolved body.
#   7. the MP cpose sidecar accesses in CG_UpdatePhysicsPose,
#      CG_PreProcess_GetDObj, CG_ShutdownEntity, and CG_Shutdown run
#      inside CRITSECT_PHYSICS spans: the physics-pose guard inspects
#      only the token field sentinels (no Resolve) outside the span,
#      and every Resolve/Release is followed by the span's Leave BEFORE
#      the self-locking Phys_ObjDestroy.
#   8. the SP save loader bounds the declared per-draw-type count by the
#      MAP-ALLOCATED capacity (captured before the save count replaces
#      it) as well as the owner-key stride, both release-effective
#      ERR_DROP paths ahead of any list read;
#   9. both per-draw-type stride gates reject counts AT the stride
#      (>= 4096), matching the legacy map-loader maximum and the
#      "Max is 4095" message text;
#  10. DynEntCl_Shutdown clears DYNENT_CL_ACTIVE under the legacy
#      non-zero-token condition (captured before TakeBody), NOT nested
#      inside the live-body conditional — dead/stale token entities
#      drop the active flag across shutdown — and its TakeBody calls
#      stay inside CRITSECT_PHYSICS spans.

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_load_obj_path "${SOURCE_ROOT}/src/DynEntity/DynEntity_load_obj.cpp")
set(_client_path "${SOURCE_ROOT}/src/DynEntity/DynEntity_client.cpp")
set(_pieces_path "${SOURCE_ROOT}/src/DynEntity/DynEntity_pieces.cpp")
set(_ents_mp_path "${SOURCE_ROOT}/src/cgame_mp/cg_ents_mp.cpp")
set(_snapshot_mp_path "${SOURCE_ROOT}/src/cgame_mp/cg_snapshot_mp.cpp")
set(_main_mp_path "${SOURCE_ROOT}/src/cgame_mp/cg_main_mp.cpp")
foreach(_path IN ITEMS
    "${_load_obj_path}" "${_client_path}" "${_pieces_path}"
    "${_ents_mp_path}" "${_snapshot_mp_path}" "${_main_mp_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing phys_obj_id owner-bound source: ${_path}")
    endif()
endforeach()
file(READ "${_load_obj_path}" _load_obj)
file(READ "${_client_path}" _client)
file(READ "${_pieces_path}" _pieces)
file(READ "${_ents_mp_path}" _ents_mp)
file(READ "${_snapshot_mp_path}" _snapshot_mp)
file(READ "${_main_mp_path}" _main_mp)

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
require_contains("${_mp_stride}"
    ">= phys_obj_id::kDynEntPhysObjIdOwnerPerDrawType"
    "MP def-loader stride gate rejects counts AT the stride (legacy 4095 maximum)")
forbid_contains("${_mp_stride}"
    "> phys_obj_id::kDynEntPhysObjIdOwnerPerDrawType"
    "MP def-loader must not accept the stride-count maps its message rejects")
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
require_contains("${_sp_loader}"
    "count >= phys_obj_id::kDynEntPhysObjIdOwnerPerDrawType"
    "SP save loader stride gate rejects counts AT the stride (legacy 4095 maximum)")
forbid_contains("${_sp_loader}"
    "> phys_obj_id::kDynEntPhysObjIdOwnerPerDrawType"
    "SP save loader must not accept the stride-count save images its message rejects")
require_ordered("${_sp_loader}"
    "const uint16_t allocated = cm.dynEntCount[drawType];"
    "cm.dynEntCount[drawType] = count;"
    "SP save loader captures the map-allocated capacity BEFORE the save count"
    " replaces it")
require_ordered("${_sp_loader}"
    "but the map allocated only [%hu]"
    "MemFile_ReadData(memFile, sizeof(DynEntityPose) * count"
    "SP save loader rejects counts above the allocated capacity BEFORE any"
    " list read (a crafted save cannot overflow the map-sized lists)")
forbid_contains("${_sp_loader}"
    "MyAssertHandler"
    "SP save loader stride gate cannot be assert-only")
require_ordered("${_sp_loader}"
    "bind.status != phys_obj_id::Status::Success"
    "Phys_ObjDestroy(PHYS_WORLD_DYNENT, physObjIdBody);"
    "SP save-loader failed bind destroys the fresh body")
require_ordered("${_sp_loader}"
    "dynEntClient->physObjId = 0;"
    "Phys_ObjLoad(PHYS_WORLD_DYNENT, memFile)"
    "SP save-loader clears the serialized token BEFORE body restoration so a"
    " failed load cannot leave a stale token that resolves to another"
    " owner's body")
require_ordered("${_sp_loader}"
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "phys_obj_id::WriteBind("
    "SP save-loader WriteBind enters CRITSECT_PHYSICS (caller G_LoadMainState"
    " holds no lock)")
require_ordered("${_sp_loader}"
    "phys_obj_id::WriteBind("
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "SP save-loader WriteBind runs inside the physics lock span")
require_ordered("${_sp_loader}"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "Phys_ObjDestroy(PHYS_WORLD_DYNENT, physObjIdBody);"
    "SP save-loader leaves the lock span BEFORE the self-locking"
    " Phys_ObjDestroy rollback")
forbid_contains("${_sp_loader}"
    "leak the body"
    "SP save loader must not document a deliberate leak")

# --- SP save saver: ReadResolve inside the lock span; body save outside it.
extract_slice(
    "${_load_obj}"
    "void DynEnt_SaveEntities(MemoryFile *memFile)"
    "} while (v2);"
    _saver
    "SP save saver")
require_ordered("${_saver}"
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "phys_obj_id::ReadResolve<dxBody>("
    "SP save saver ReadResolve enters CRITSECT_PHYSICS (caller G_SaveMainState"
    " holds no lock)")
require_ordered("${_saver}"
    "phys_obj_id::ReadResolve<dxBody>("
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "SP save saver ReadResolve runs inside the physics lock span")
require_ordered("${_saver}"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "Phys_ObjSave(physObjIdBody, memFile);"
    "SP save saver leaves the lock span BEFORE the body-state save")

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

# --- MP cpose sidecar locking: every Resolve/Release under the physics
# --- lock (Codex P2 rework). The physics-pose guard inspects only the
# --- token FIELD sentinels outside the span; the sidecar Resolve stays
# --- inside it.
extract_slice(
    "${_ents_mp}"
    "void __cdecl CG_UpdatePhysicsPose(centity_s *cent)"
    "char __cdecl CG_ExpiredLaunch"
    _pose_update
    "MP physics-pose update")
require_contains("${_pose_update}"
    "phys_obj_id::IsNull(cent->pose.physObjId) || CG_CPosePhysObjId_IsDead(cent)"
    "physics-pose guard inspects only the token field sentinels (no Resolve)")
require_ordered("${_pose_update}"
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "CG_CPosePhysObjId_GetBody(cent)"
    "physics-pose sidecar Resolve enters CRITSECT_PHYSICS first")
require_ordered("${_pose_update}"
    "CG_CPosePhysObjId_GetBody(cent)"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "physics-pose sidecar Resolve runs inside the physics lock span")

extract_slice(
    "${_ents_mp}"
    "DObj_s *__cdecl CG_PreProcess_GetDObj(int32_t localClientNum, int32_t entIndex, int32_t entType, XModel *model)"
    "if (!obj && model)"
    _preprocess_getdobj
    "MP DObj preprocess teardown")
require_ordered("${_preprocess_getdobj}"
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "CG_CPosePhysObjId_TakeBody(cent)"
    "DObj preprocess Release enters CRITSECT_PHYSICS first")
require_ordered("${_preprocess_getdobj}"
    "CG_CPosePhysObjId_TakeBody(cent)"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "DObj preprocess Release runs inside the physics lock span")
require_ordered("${_preprocess_getdobj}"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "Phys_ObjDestroy(PHYS_WORLD_FX, physObjIdBody);"
    "DObj preprocess leaves the lock span BEFORE the self-locking"
    " Phys_ObjDestroy")

extract_slice(
    "${_snapshot_mp}"
    "void __cdecl CG_ShutdownEntity(int localClientNum, centity_s *cent)"
    "void __cdecl CG_SetInitialSnapshot"
    _shutdown_entity
    "MP entity shutdown")
require_ordered("${_shutdown_entity}"
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "CG_CPosePhysObjId_GetBody(cent)"
    "entity-shutdown outer Resolve enters CRITSECT_PHYSICS first")
require_ordered("${_shutdown_entity}"
    "CG_CPosePhysObjId_GetBody(cent)"
    "CG_CPosePhysObjId_TakeBody(cent)"
    "entity-shutdown Release runs inside the same physics lock span")
require_ordered("${_shutdown_entity}"
    "CG_CPosePhysObjId_TakeBody(cent)"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "entity-shutdown leaves the physics lock span after the Release")
require_ordered("${_shutdown_entity}"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "Phys_ObjDestroy(PHYS_WORLD_FX, physObjIdBody);"
    "entity-shutdown leaves the lock span BEFORE the self-locking"
    " Phys_ObjDestroy")

extract_slice(
    "${_main_mp}"
    "void __cdecl CG_Shutdown(int32_t localClientNum)"
    "Ragdoll_Shutdown();"
    "_client_shutdown"
    "MP client shutdown")
require_ordered("${_client_shutdown}"
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "CG_CPosePhysObjId_TakeBody(cent)"
    "client-shutdown Release enters CRITSECT_PHYSICS first")
require_ordered("${_client_shutdown}"
    "CG_CPosePhysObjId_TakeBody(cent)"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "client-shutdown Release runs inside the physics lock span")
require_ordered("${_client_shutdown}"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "Phys_ObjDestroy(PHYS_WORLD_FX, physObjIdBody);"
    "client-shutdown leaves the lock span BEFORE the self-locking"
    " Phys_ObjDestroy")

# --- DynEntCl_Shutdown: the legacy-condition flag clear plus sealed
# --- lock spans. The clear must follow the legacy non-zero-token
# --- condition (captured BEFORE TakeBody nulls the field), NOT the
# --- live-body conditional — dead/stale token entities drop
# --- DYNENT_CL_ACTIVE across shutdown exactly like they did before the
# --- sidecar rework. The old nesting (clear inside if (physObjIdBody))
# --- is forbidden textually so the regression cannot return.
extract_slice(
    "${_client}"
    "void __cdecl DynEntCl_Shutdown(int32_t localClientNum)"
    "void __cdecl DynEntCl_UnlinkEntity"
    _dynent_shutdown
    "dynent client shutdown")
require_contains("${_dynent_shutdown}"
    "dynEntClient->physObjId != phys_obj_id::INVALID_BODY_TOKEN"
    "dynent shutdown captures the legacy non-zero-token condition (MODEL loop)")
require_contains("${_dynent_shutdown}"
    "dynEntClienta->physObjId != phys_obj_id::INVALID_BODY_TOKEN"
    "dynent shutdown captures the legacy non-zero-token condition (BRUSH loop)")
require_ordered("${_dynent_shutdown}"
    "hadPhysObjIdToken ="
    "dynEntClient->flags &= ~1u;"
    "dynent shutdown MODEL flag clear follows the legacy token condition,"
    " outside the live-body conditional")
require_ordered("${_dynent_shutdown}"
    "dynEntClienta->physObjId != phys_obj_id::INVALID_BODY_TOKEN"
    "dynEntClienta->flags &= ~1u;"
    "dynent shutdown BRUSH flag clear follows the legacy token condition,"
    " outside the live-body conditional")
forbid_contains("${_dynent_shutdown}"
    "Phys_ObjDestroy(PHYS_WORLD_DYNENT, physObjIdBody);
                    dynEntClient->flags &= ~1u;"
    "dynent shutdown MODEL flag clear must not be nested inside the"
    " live-body conditional")
forbid_contains("${_dynent_shutdown}"
    "Phys_ObjDestroy(PHYS_WORLD_DYNENT, physObjIdBody);
                    dynEntClienta->flags &= ~1u;"
    "dynent shutdown BRUSH flag clear must not be nested inside the"
    " live-body conditional")
require_ordered("${_dynent_shutdown}"
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "DynEntPhysObjId_TakeBody(DYNENT_DRAW_MODEL, dynEntId, dynEntClient);"
    "dynent shutdown MODEL Release enters CRITSECT_PHYSICS first")
require_ordered("${_dynent_shutdown}"
    "DynEntPhysObjId_TakeBody(DYNENT_DRAW_MODEL, dynEntId, dynEntClient);"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "dynent shutdown MODEL Release runs inside the physics lock span")
require_ordered("${_dynent_shutdown}"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "Phys_ObjDestroy(PHYS_WORLD_DYNENT, physObjIdBody);"
    "dynent shutdown MODEL leaves the lock span BEFORE the self-locking"
    " Phys_ObjDestroy")
require_ordered("${_dynent_shutdown}"
    "DynEntPhysObjId_TakeBody(DYNENT_DRAW_BRUSH, dynEntIda, dynEntClienta);"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "dynent shutdown BRUSH Release runs inside the physics lock span")

message(STATUS "phys_obj_id owner-bound source invariants verified")
