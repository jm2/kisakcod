cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_fx_system_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_system.cpp")
set(_fx_draw_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_draw.cpp")
set(_fx_sort_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_sort.cpp")
set(_fx_archive_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive.cpp")
set(_fx_update_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_update.cpp")
set(_fx_pool_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_pool.h")
set(_fx_sidecar_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_physics_sidecar.h")
set(_phys_ode_path
    "${SOURCE_ROOT}/src/physics/phys_ode.cpp")

foreach(_source_path
    "${_fx_system_path}"
    "${_fx_draw_path}"
    "${_fx_sort_path}"
    "${_fx_archive_path}"
    "${_fx_update_path}"
    "${_fx_pool_path}"
    "${_fx_sidecar_path}"
    "${_phys_ode_path}")
    if(NOT EXISTS "${_source_path}")
        message(FATAL_ERROR "Missing live FX physics source: ${_source_path}")
    endif()
endforeach()

file(READ "${_fx_system_path}" _fx_system_source)
file(READ "${_fx_draw_path}" _fx_draw_source)
file(READ "${_fx_sort_path}" _fx_sort_source)
file(READ "${_fx_archive_path}" _fx_archive_source)
file(READ "${_fx_update_path}" _fx_update_source)
file(READ "${_fx_pool_path}" _fx_pool_source)
file(READ "${_fx_sidecar_path}" _fx_sidecar_source)
file(READ "${_phys_ode_path}" _phys_ode_source)

function(extract_source_slice
    SOURCE_VAR START_MARKER END_MARKER OUT_VAR DESCRIPTION)
    set(_source "${${SOURCE_VAR}}")
    string(FIND "${_source}" "${START_MARKER}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR
            "Missing start of ${DESCRIPTION}: '${START_MARKER}'")
    endif()

    string(SUBSTRING "${_source}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${END_MARKER}" _relative_end)
    if(_relative_end LESS_EQUAL 0)
        message(FATAL_ERROR
            "Missing ordered end of ${DESCRIPTION}: '${END_MARKER}'")
    endif()

    string(SUBSTRING
        "${_tail}" 0 ${_relative_end} _slice)
    set(${OUT_VAR} "${_slice}" PARENT_SCOPE)
endfunction()

function(extract_source_tail
    SOURCE_VAR START_MARKER OUT_VAR DESCRIPTION)
    set(_source "${${SOURCE_VAR}}")
    string(FIND "${_source}" "${START_MARKER}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR
            "Missing start of ${DESCRIPTION}: '${START_MARKER}'")
    endif()
    string(SUBSTRING "${_source}" ${_start} -1 _tail)
    set(${OUT_VAR} "${_tail}" PARENT_SCOPE)
endfunction()

function(require_slice_contains SLICE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SLICE_VAR}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing live FX physics invariant (${DESCRIPTION})")
    endif()
endfunction()

function(require_slice_not_contains SLICE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SLICE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden live FX physics regression (${DESCRIPTION})")
    endif()
endfunction()

function(require_slice_matches SLICE_VAR PATTERN DESCRIPTION)
    string(REGEX MATCH
        "${PATTERN}" _match "${${SLICE_VAR}}")
    if(_match STREQUAL "")
        message(FATAL_ERROR
            "Missing live FX physics invariant (${DESCRIPTION})")
    endif()
endfunction()

function(require_slice_not_matches SLICE_VAR PATTERN DESCRIPTION)
    string(REGEX MATCH
        "${PATTERN}" _match "${${SLICE_VAR}}")
    if(NOT _match STREQUAL "")
        message(FATAL_ERROR
            "Forbidden live FX physics regression (${DESCRIPTION})")
    endif()
endfunction()

function(require_slice_ordered SLICE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SLICE_VAR}}" "${FIRST}" _first)
    string(FIND "${${SLICE_VAR}}" "${SECOND}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered live FX physics invariant "
            "(${DESCRIPTION})")
    endif()
endfunction()

function(require_literal_count
    SLICE_VAR NEEDLE EXPECTED_COUNT DESCRIPTION)
    set(_remaining "${${SLICE_VAR}}")
    string(LENGTH "${NEEDLE}" _needle_length)
    if(_needle_length EQUAL 0)
        message(FATAL_ERROR "Cannot count an empty FX physics invariant")
    endif()

    set(_count 0)
    while(TRUE)
        string(FIND "${_remaining}" "${NEEDLE}" _position)
        if(_position EQUAL -1)
            break()
        endif()
        math(EXPR _count "${_count} + 1")
        math(EXPR _next "${_position} + ${_needle_length}")
        string(SUBSTRING "${_remaining}" ${_next} -1 _remaining)
    endwhile()

    if(NOT _count EQUAL EXPECTED_COUNT)
        message(FATAL_ERROR
            "Unexpected live FX physics invariant count "
            "(${DESCRIPTION}): expected ${EXPECTED_COUNT}, "
            "found ${_count}")
    endif()
endfunction()

# The registry remains external to frozen FxSystem/FxSystemBuffers storage.
require_slice_contains(
    _fx_system_source
    "fx::physics::BodySidecar fx_physicsBodySidecars[\n    std::size(fx_poolAllocationStates)];"
    "native body ownership must use an external per-system registry")
require_slice_contains(
    _fx_system_source
    "\"FX physics sidecars must match the system pool\""
    "the sidecar registry extent must remain tied to the system pool")
require_slice_contains(
    _fx_system_source
    "FX_GetPhysicsBodySidecar("
    "live paths must resolve the registry through a checked system getter")
require_slice_contains(
    _fx_sidecar_source
    "ValidateVacantOwner("
    "live allocation/free preflight must retain its O(1) vacancy primitive")
require_slice_contains(
    _fx_system_source
    "bool __cdecl FX_ThreadOwnsEffectLock("
    "draw traversal guards must use a public read-only effect-lock query")
require_slice_contains(
    _fx_system_source
    "return FX_CurrentThreadOwnsEffectLock(system, effect);"
    "the public effect-lock query must preserve generation-aware TLS ownership")

# Lifecycle mutation closes archive/kill admission, atomically owns the
# iterator word, and releases iterator-exclusive ownership only at the end.
extract_source_slice(
    _fx_system_source
    "FxLifecycleClaimStatus FX_BeginLifecycleClaim("
    "bool FX_EndLifecycleClaim("
    _begin_lifecycle_source
    "FX_BeginLifecycleClaim")
require_slice_ordered(
    _begin_lifecycle_source
    "Sys_EnterCriticalSection(CRITSECT_FX_ALLOC);"
    "FxIteratorTryBeginExclusive(&system->iteratorCount)"
    "lifecycle admission must drain the allocator before claiming iterators")
require_slice_ordered(
    _begin_lifecycle_source
    "FxIteratorTryBeginExclusive(&system->iteratorCount)"
    "claim->system = system;"
    "a lifecycle claim must publish its system only after iterator exclusion")
extract_source_slice(
    _begin_lifecycle_source
    "claim->system = system;"
    "return FxLifecycleClaimStatus::Success;"
    _lifecycle_revalidation_source
    "lifecycle post-claim revalidation")
require_slice_ordered(
    _lifecycle_revalidation_source
    "(void)FxIteratorEndExclusive(&system->iteratorCount);"
    "(void)Sys_AtomicCompareExchange(killGate, 0, 1);"
    "failed lifecycle revalidation must release iterators before external gates")

extract_source_slice(
    _fx_system_source
    "bool FX_EndLifecycleClaim("
    "bool FX_ClearSystemForShutdownLocked("
    _end_lifecycle_source
    "FX_EndLifecycleClaim")
require_slice_ordered(
    _end_lifecycle_source
    "FxIteratorEndExclusive(&claim->system->iteratorCount)"
    "Sys_AtomicCompareExchange(claim->killGate, 0, 1)"
    "lifecycle release must publish iterator availability before kill admission")
require_slice_ordered(
    _end_lifecycle_source
    "Sys_AtomicCompareExchange(claim->killGate, 0, 1)"
    "Sys_AtomicCompareExchange(claim->archiveGate, 0, 2)"
    "lifecycle gates must reopen in their established release order")

extract_source_slice(
    _fx_system_source
    "bool FX_ClearSystemForShutdownLocked("
    "fx::physics::SidecarStatus FX_DrainPhysicsBodySidecarLocked("
    _shutdown_clear_source
    "FX_ClearSystemForShutdownLocked")
require_slice_contains(
    _shutdown_clear_source
    "Sys_AtomicLoad(&system->iteratorCount) != -1"
    "shutdown clearing requires iterator-exclusive ownership")
require_slice_contains(
    _shutdown_clear_source
    "iteratorBytes + sizeof(system->iteratorCount)"
    "shutdown clear must skip the owned iterator word")
require_slice_not_contains(
    _shutdown_clear_source
    "std::memset(bytes, 0, sizeof(FxSystem))"
    "shutdown must not clear the exclusive iterator word")

# Every iterator admission snapshots the lifecycle generation before it can
# wait. After acquiring iterator ownership it rejects a generation change or
# an uninitialized system instead of crossing reset/shutdown.
extract_source_slice(
    _fx_draw_source
    "void __cdecl FX_BeginIteratingOverEffects_Cooperative("
    "bool __cdecl FX_DowngradeEffectKillExclusiveToCooperative("
    _cooperative_admission_source
    "cooperative FX iterator admission")
require_slice_ordered(
    _cooperative_admission_source
    "const std::uint32_t admissionGeneration ="
    "for (;;)"
    "cooperative admission must capture generation before waiting")
extract_source_tail(
    _cooperative_admission_source
    "for (;;)"
    _cooperative_admission_loop
    "cooperative FX iterator wait loop")
require_slice_ordered(
    _cooperative_admission_loop
    "FX_WaitForArchiveGate(system);"
    "FxIteratorBeginCooperative(&system->iteratorCount);"
    "cooperative admission must wait for lifecycle gates before ownership")
require_slice_ordered(
    _cooperative_admission_loop
    "FxIteratorBeginCooperative(&system->iteratorCount);"
    "const std::uint32_t currentGeneration ="
    "cooperative admission must resample generation after ownership")
require_slice_contains(
    _cooperative_admission_loop
    "currentGeneration == admissionGeneration"
    "cooperative admission must reject a lifecycle crossing")
require_slice_contains(
    _cooperative_admission_loop
    "const bool initialized = system->isInitialized != 0;"
    "cooperative admission must reject an uninitialized system")
require_slice_ordered(
    _cooperative_admission_loop
    "FxIteratorEndCooperative(&system->iteratorCount, &remaining)"
    "if (currentGeneration != admissionGeneration)"
    "cooperative rejection must release iterator ownership before reporting")

extract_source_slice(
    _fx_sort_source
    "void __cdecl FX_WaitBeginIteratingOverEffects_Exclusive("
    "bool __cdecl FX_FirstEffectIsFurther("
    _sort_admission_source
    "sort-exclusive FX iterator admission")
require_slice_ordered(
    _sort_admission_source
    "const std::uint32_t admissionGeneration ="
    "for (;;)"
    "sort admission must capture generation before waiting")
extract_source_tail(
    _sort_admission_source
    "for (;;)"
    _sort_admission_loop
    "sort-exclusive FX iterator wait loop")
require_slice_ordered(
    _sort_admission_loop
    "FX_WaitForArchiveGate(system);"
    "FxIteratorWaitBeginExclusive(&system->iteratorCount);"
    "sort admission must wait for lifecycle gates before ownership")
require_slice_ordered(
    _sort_admission_loop
    "FxIteratorWaitBeginExclusive(&system->iteratorCount);"
    "const std::uint32_t currentGeneration ="
    "sort admission must resample generation after ownership")
require_slice_contains(
    _sort_admission_loop
    "currentGeneration == admissionGeneration"
    "sort admission must reject a lifecycle crossing")
require_slice_contains(
    _sort_admission_loop
    "const bool initialized = system->isInitialized != 0;"
    "sort admission must reject an uninitialized system")
require_slice_ordered(
    _sort_admission_loop
    "FxIteratorEndExclusive(&system->iteratorCount)"
    "if (currentGeneration != admissionGeneration)"
    "sort rejection must release iterator ownership before reporting")

extract_source_slice(
    _fx_system_source
    "bool __cdecl FX_BeginEffectKillExclusive("
    "bool __cdecl FX_EndEffectKillExclusive("
    _kill_admission_source
    "kill-exclusive FX iterator admission")
require_slice_ordered(
    _kill_admission_source
    "fx_effectKillExclusiveThreadState.generation\n            != FX_GetCooperativeIteratorGeneration("
    "fx_effectKillExclusiveThreadState = {};"
    "kill admission must clear stale exclusive thread ownership")
require_slice_ordered(
    _kill_admission_source
    "fx_effectKillExclusiveThreadState = {};"
    "if (fx_effectKillExclusiveThreadState.system\n        || fx_effectRestartRetainThreadState.system"
    "kill admission must clear stale ownership before nested-owner rejection")
require_slice_ordered(
    _kill_admission_source
    "const std::uint32_t admissionGeneration ="
    "while (Sys_AtomicCompareExchange(killGate, 1, 0) != 0)"
    "kill admission must capture generation before waiting on its gate")
require_slice_ordered(
    _kill_admission_source
    "FX_WaitForArchiveGate(system);"
    "FxIteratorWaitBeginExclusive(&system->iteratorCount);"
    "kill admission must wait for archive before iterator ownership")
require_slice_ordered(
    _kill_admission_source
    "FxIteratorWaitBeginExclusive(&system->iteratorCount);"
    "const std::uint32_t currentGeneration ="
    "kill admission must resample generation after ownership")
require_slice_contains(
    _kill_admission_source
    "currentGeneration == admissionGeneration"
    "kill admission must reject a lifecycle crossing")
require_slice_contains(
    _kill_admission_source
    "currentGeneration != admissionGeneration\n            || !system->isInitialized"
    "kill admission must reject changed or uninitialized lifecycle state")

extract_source_slice(
    _fx_system_source
    "bool __cdecl FX_BeginArchive("
    "bool __cdecl FX_RestoreArchiveExclusiveState("
    _archive_admission_source
    "archive-exclusive FX iterator admission")
require_slice_ordered(
    _archive_admission_source
    "const std::uint32_t admissionGeneration ="
    "FxIteratorWaitBeginExclusive(&system->iteratorCount);"
    "archive admission must capture generation before waiting")
require_slice_ordered(
    _archive_admission_source
    "FxIteratorWaitBeginExclusive(&system->iteratorCount);"
    "const bool admissionValid = gateClosed && system->isInitialized"
    "archive admission must validate initialized state after ownership")
require_slice_contains(
    _archive_admission_source
    "== admissionGeneration;"
    "archive admission must reject a lifecycle generation change")
require_slice_ordered(
    _archive_admission_source
    "if (admissionValid)"
    "(void)FxIteratorEndExclusive(&system->iteratorCount);"
    "invalid archive admission must release iterator ownership")

extract_source_slice(
    _fx_system_source
    "bool FX_PhysicsOwnerIsVacantLocked("
    "bool FX_CurrentThreadOwnsAnyEffectLock("
    _vacancy_helper_source
    "FX_PhysicsOwnerIsVacantLocked")
require_slice_contains(
    _vacancy_helper_source
    "*outStatus = fx::physics::ValidateVacantOwner(sidecar, ownerIndex);"
    "live owner admission must use the checked O(1) vacancy primitive")

# Model physics creation/configuration/binding finishes under PHYSICS before
# FX_SpawnElem publishes either graph link.
extract_source_slice(
    _fx_system_source
    "FxModelPhysicsSpawnResult FX_TrySpawnModelPhysics(\n    FxSystem *const system,"
    "bool __cdecl FX_SpawnModelPhysics("
    _spawn_physics_source
    "FX_TrySpawnModelPhysics")
require_literal_count(
    _spawn_physics_source
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    1
    "model spawn must acquire one continuous physics transaction")
require_slice_ordered(
    _spawn_physics_source
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "FX_PhysicsOwnerIsVacantLocked("
    "model spawn must lock before owner preflight")
require_slice_ordered(
    _spawn_physics_source
    "FX_PhysicsOwnerIsVacantLocked("
    "Phys_TryCreateBodyFromPresetAndXModel("
    "model spawn must prove owner vacancy before native creation")
require_slice_ordered(
    _spawn_physics_source
    "Phys_TryCreateBodyFromPresetAndXModel("
    "Phys_ObjSetAngularVelocity(body, angularVelocity);"
    "native body construction must finish before final configuration")
require_slice_ordered(
    _spawn_physics_source
    "Phys_ObjSetAngularVelocity(body, angularVelocity);"
    "const fx::physics::TokenResult binding = fx::physics::Bind("
    "native body configuration must precede sidecar binding")
require_slice_ordered(
    _spawn_physics_source
    "const fx::physics::TokenResult binding = fx::physics::Bind("
    "elem->physObjId = fx::physics::TokenToLegacyField(binding.token);"
    "sidecar binding must precede token publication")
require_slice_contains(
    _spawn_physics_source
    "constexpr float MAX_FX_PHYSICS_ANGULAR_VELOCITY = 65536.0f;"
    "live model spawn must share the archive angular-velocity bound")
require_slice_ordered(
    _spawn_physics_source
    "for (const float component : angularVelocity)"
    "if (!std::isfinite(component)"
    "every angular-velocity component must receive finite validation")
require_slice_ordered(
    _spawn_physics_source
    "component > MAX_FX_PHYSICS_ANGULAR_VELOCITY"
    "Phys_TryCreateBodyFromPresetAndXModel("
    "angular-velocity validation must precede native body allocation")

extract_source_slice(
    _spawn_physics_source
    "if (!binding)"
    "elem->physObjId = fx::physics::TokenToLegacyField(binding.token);"
    _spawn_bind_failure_source
    "model-spawn bind failure")
require_slice_ordered(
    _spawn_bind_failure_source
    "binding.status != fx::physics::SidecarStatus::DuplicateBody"
    "Phys_ObjDestroy(PHYS_WORLD_FX, body);"
    "duplicate ownership rejection must not destroy another slot's body")
require_slice_ordered(
    _spawn_bind_failure_source
    "Phys_ObjDestroy(PHYS_WORLD_FX, body);"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "failed binding must destroy the unowned body before unlock")

extract_source_slice(
    _spawn_physics_source
    "elem->physObjId = fx::physics::TokenToLegacyField(binding.token);"
    "return result;"
    _spawn_success_source
    "model-spawn success")
require_slice_ordered(
    _spawn_success_source
    "elem->physObjId = fx::physics::TokenToLegacyField(binding.token);"
    "result.outcome = FxModelPhysicsSpawnOutcome::Success;"
    "success must follow token publication")
require_slice_ordered(
    _spawn_success_source
    "result.outcome = FxModelPhysicsSpawnOutcome::Success;"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "successful spawn must retain PHYSICS through token publication")
require_slice_not_contains(
    _spawn_physics_source
    "effect->firstElemHandle"
    "the physics helper must not publish the live effect graph")

extract_source_slice(
    _phys_ode_source
    "PhysBodyModelCreateStatus __cdecl Phys_TryCreateBodyFromPresetAndXModel("
    "void __cdecl Phys_ObjSetAngularVelocity("
    _preset_body_create_source
    "Phys_TryCreateBodyFromPresetAndXModel")
foreach(_required_preset_bound
    "physPreset->mass < 0.0001f"
    "physPreset->mass > 1000000.0f"
    "!std::isfinite(physPreset->mass)"
    "!std::isfinite(physPreset->bounce)"
    "physPreset->bounce < 0.0f || physPreset->bounce > 1.0f"
    "physPreset->friction >= 0.0f"
    "physPreset->friction <= 10000.0f"
    "physPreset->friction\n                == (std::numeric_limits<float>::max)()"
    "physPreset->type < 0 || physPreset->type >= 50")
    require_slice_contains(
        _preset_body_create_source
        "${_required_preset_bound}"
        "live preset construction must retain archive-compatible bounds")
endforeach()
require_slice_contains(
    _preset_body_create_source
    "constexpr float MAX_PHYSICS_VECTOR_COMPONENT = 1048576.0f;"
    "position and linear velocity must retain a finite component bound")
foreach(_required_vector_bound
    "position[index] < -MAX_PHYSICS_VECTOR_COMPONENT"
    "position[index] > MAX_PHYSICS_VECTOR_COMPONENT"
    "velocity[index] < -MAX_PHYSICS_VECTOR_COMPONENT"
    "velocity[index] > MAX_PHYSICS_VECTOR_COMPONENT")
    require_slice_contains(
        _preset_body_create_source
        "${_required_vector_bound}"
        "position and linear velocity must be bounded before construction")
endforeach()
require_slice_ordered(
    _preset_body_create_source
    "if (!std::isfinite(position[index])"
    "BodyState state{};"
    "position/velocity validation must precede BodyState construction")
require_slice_ordered(
    _preset_body_create_source
    "if (!std::isfinite(quat[index]))"
    "BodyState state{};"
    "quaternion validation must precede BodyState construction")
require_slice_ordered(
    _preset_body_create_source
    "double quaternionMagnitudeSquared = 0.0;"
    "if (!std::isfinite(quaternionMagnitudeSquared)"
    "quaternion magnitude must be accumulated before validation")
require_slice_contains(
    _preset_body_create_source
    "quaternionMagnitudeSquared\n            < static_cast<double>((std::numeric_limits<float>::min)())"
    "degenerate quaternions must be rejected")
require_slice_contains(
    _preset_body_create_source
    "quaternionMagnitudeSquared\n            > static_cast<double>((std::numeric_limits<float>::max)())"
    "overflowing quaternion magnitudes must be rejected")
require_slice_ordered(
    _preset_body_create_source
    "if (!std::isfinite(quaternionMagnitudeSquared)"
    "BodyState state{};"
    "quaternion magnitude validation must precede state construction")
require_slice_ordered(
    _preset_body_create_source
    "BodyState state{};"
    "QuatToAxis(quat, state.rotation);"
    "validated quaternion input must precede rotation construction")
require_slice_ordered(
    _preset_body_create_source
    "QuatToAxis(quat, state.rotation);"
    "for (const auto &row : state.rotation)"
    "rotation output must receive finite validation")
require_slice_ordered(
    _preset_body_create_source
    "for (const auto &row : state.rotation)"
    "Phys_TryCreateBodyFromStateAndXModel("
    "finite rotation validation must precede transactional native construction")

extract_source_slice(
    _fx_system_source
    "void __cdecl FX_SpawnElem("
    "FxPool<FxElem> *__cdecl FX_AllocElem("
    _spawn_elem_source
    "FX_SpawnElem")
require_slice_ordered(
    _spawn_elem_source
    "FX_TrySpawnModelPhysics("
    "if (publishElem)"
    "model physics must complete before graph publication is admitted")
require_slice_ordered(
    _spawn_elem_source
    "if (publishElem)"
    "nextElemInEffect->item.prevElemHandleInEffect ="
    "the old head backlink must remain inside the publication commit")
require_slice_ordered(
    _spawn_elem_source
    "nextElemInEffect->item.prevElemHandleInEffect ="
    "effect->firstElemHandle[elemClass] = elemHandle;"
    "the new head must publish only after its backlink")

# Draw traversal holds the owning effect lock from the first effect-owned
# handle read through the last pool-backed payload use. The cooperative
# iterator pins effect slots; the per-effect lock prevents an updater from
# freeing and reallocating an element/trail slot behind a stale draw handle.
extract_source_slice(
    _fx_draw_source
    "void __cdecl FX_DrawNonSpriteElems("
    "void __cdecl FX_BeginIteratingOverEffects_Cooperative("
    _draw_non_sprite_loop_source
    "FX_DrawNonSpriteElems")
require_slice_contains(
    _draw_non_sprite_loop_source
    "if (FX_LockEffect(system, effect))"
    "class-one draw must skip effects that cannot be lifetime-locked")
require_slice_ordered(
    _draw_non_sprite_loop_source
    "if (FX_LockEffect(system, effect))"
    "FX_DrawNonSpriteEffect(system, effect, 1u, system->msecDraw);"
    "class-one traversal must begin only after effect-lock acquisition")
require_slice_ordered(
    _draw_non_sprite_loop_source
    "FX_DrawNonSpriteEffect(system, effect, 1u, system->msecDraw);"
    "FX_UnlockEffect(system, effect);"
    "class-one traversal must finish before releasing its effect lock")

extract_source_slice(
    _fx_draw_source
    "void __cdecl FX_DrawNonSpriteEffect("
    "void __cdecl FX_DrawElement_Setup_1_("
    _draw_non_sprite_effect_source
    "FX_DrawNonSpriteEffect")
extract_source_slice(
    _fx_draw_source
    "void __cdecl FX_DrawSpotLightEffect("
    "void __cdecl FX_DrawSpriteElems("
    _draw_spotlight_effect_source
    "FX_DrawSpotLightEffect")
extract_source_slice(
    _fx_draw_source
    "void __cdecl FX_DrawTrailsForEffect("
    "void __cdecl FX_DrawTrail("
    _draw_trails_effect_source
    "FX_DrawTrailsForEffect")
extract_source_slice(
    _fx_draw_source
    "void __cdecl FX_DrawSpriteEffect("
    "void __cdecl FX_GenerateVerts("
    _draw_sprite_effect_source
    "FX_DrawSpriteEffect")
foreach(_owned_draw_traversal
    _draw_non_sprite_effect_source
    _draw_spotlight_effect_source
    _draw_trails_effect_source
    _draw_sprite_effect_source)
    require_slice_contains(
        ${_owned_draw_traversal}
        "!FX_CurrentThreadOwnsCooperativeIterator(system)"
        "every effect-owned draw traversal must require cooperative lifetime ownership")
    require_slice_contains(
        ${_owned_draw_traversal}
        "!FX_ThreadOwnsEffectLock(system, effect)"
        "every effect-owned draw traversal must require its per-effect lock")
endforeach()

extract_source_slice(
    _fx_draw_source
    "void __cdecl FX_DrawSpriteElems("
    "void __cdecl FX_DrawTrailsForEffect("
    _draw_sprite_loop_source
    "FX_DrawSpriteElems")
require_slice_ordered(
    _draw_sprite_loop_source
    "if (!FX_LockEffect(system, effect))"
    "FX_DrawSpriteEffect(system, effect, drawTime);"
    "sprite traversal must skip a stale effect before reading its graph")
require_slice_ordered(
    _draw_sprite_loop_source
    "FX_DrawSpriteEffect(system, effect, drawTime);"
    "FX_DrawNonSpriteEffect(system, effect, 2u, drawTime);"
    "sprite and cloud traversal must share one effect lifetime interval")
require_slice_ordered(
    _draw_sprite_loop_source
    "FX_DrawNonSpriteEffect(system, effect, 2u, drawTime);"
    "if (effect->firstTrailHandle != 0xFFFF)"
    "trail-list admission must be sampled while the effect is locked")
require_slice_ordered(
    _draw_sprite_loop_source
    "if (effect->firstTrailHandle != 0xFFFF)"
    "FX_UnlockEffect(system, effect);"
    "sprite/cloud traversal must release only after trail admission is captured")
extract_source_slice(
    _draw_sprite_loop_source
    "if (numTrailEffects > 0)"
    "FX_EndIteratingOverEffects_Cooperative(system);"
    _draw_trail_loop_source
    "FX_DrawSpriteElems trail pass")
require_slice_ordered(
    _draw_trail_loop_source
    "if (FX_LockEffect(system, effecta))"
    "FX_DrawTrailsForEffect(system, effecta, drawTime);"
    "saved trail handles must be re-locked before dereferencing effect state")
require_slice_ordered(
    _draw_trail_loop_source
    "FX_DrawTrailsForEffect(system, effecta, drawTime);"
    "FX_UnlockEffect(system, effecta);"
    "trail traversal must finish before releasing its effect lock")

extract_source_slice(
    _fx_draw_source
    "void __cdecl FX_DrawSpotLight("
    "void __cdecl FX_DrawSpotLightEffect("
    _draw_spotlight_loop_source
    "FX_DrawSpotLight")
require_slice_ordered(
    _draw_spotlight_loop_source
    "const bool effectLocked = FX_LockEffect(system, v1);"
    "FxSpotLightStateSnapshot lockedSnapshot{};"
    "spotlight state must be resampled only after effect-lock acquisition")
require_slice_ordered(
    _draw_spotlight_loop_source
    "FX_DrawSpotLightEffect(system, v1, msecDraw);"
    "FX_UnlockEffect(system, v1);"
    "spotlight traversal must finish before releasing its effect lock")

# The late physics-token read is itself a lifetime boundary. Keep a direct
# ownership guard here even though its current model-draw caller is reached
# through FX_DrawNonSpriteEffect; the helper remains publicly declared.
extract_source_slice(
    _fx_draw_source
    "void __cdecl FX_SetPlacementFromPhysics("
    "void __cdecl FX_DrawElem_Light("
    _draw_physics_source
    "FX_SetPlacementFromPhysics")
require_slice_contains(
    _draw_physics_source
    "!draw || !draw->system || !draw->effect || !draw->elem"
    "physics draw must retain an explicit effect owner")
require_slice_contains(
    _draw_physics_source
    "!FX_CurrentThreadOwnsCooperativeIterator(draw->system)"
    "physics token reads must require cooperative effect lifetime")
require_slice_contains(
    _draw_physics_source
    "!FX_ThreadOwnsEffectLock(draw->system, draw->effect)"
    "physics token reads must require the owning effect lock")
require_slice_ordered(
    _draw_physics_source
    "!FX_ThreadOwnsEffectLock(draw->system, draw->effect)"
    "FxPoolItemIndex<FxElem, MAX_ELEMS>("
    "physics draw must establish effect lifetime before deriving its pool owner")
require_slice_ordered(
    _draw_physics_source
    "!FX_ThreadOwnsEffectLock(draw->system, draw->effect)"
    "fx::physics::TokenFromLegacyField(draw->elem->physObjId)"
    "physics draw must establish effect lifetime before sampling its token")

# Draw reads the token, resolves it, and consumes the native body within the
# same PHYSICS interval. Error reporting remains outside that interval.
require_literal_count(
    _draw_physics_source
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    1
    "draw must acquire PHYSICS exactly once")
require_literal_count(
    _draw_physics_source
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    1
    "draw must release PHYSICS exactly once")
require_slice_ordered(
    _draw_physics_source
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "fx::physics::TokenFromLegacyField(draw->elem->physObjId)"
    "draw must sample the pool-backed token only after locking physics")
require_slice_ordered(
    _draw_physics_source
    "fx::physics::TokenFromLegacyField(draw->elem->physObjId)"
    "fx::physics::Resolve("
    "draw token sampling must immediately feed checked resolution")
require_slice_ordered(
    _draw_physics_source
    "fx::physics::Resolve("
    "Phys_ObjGetInterpolatedState("
    "draw must resolve before dereferencing the native body")
require_slice_ordered(
    _draw_physics_source
    "Phys_ObjGetInterpolatedState("
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "draw must retain PHYSICS through native body use")
extract_source_slice(
    _draw_physics_source
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    _draw_lock_source
    "draw physics lock interval")
require_slice_not_contains(
    _draw_lock_source
    "Com_Error("
    "draw must not longjmp while PHYSICS is held")
extract_source_tail(
    _draw_physics_source
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    _draw_after_unlock_source
    "draw post-unlock reporting")
require_slice_contains(
    _draw_after_unlock_source
    "Com_Error("
    "draw resolution errors must be reported after unlock")

# The status-returning element-pool wrapper takes archive-aware FX_ALLOC. All
# live callers invoke it while already holding PHYSICS, establishing the sole
# nested order PHYSICS -> FX_ALLOC.
extract_source_slice(
    _fx_system_source
    "template <typename BEFORE_PUBLISH>\nFxPoolMutationStatus FX_FreePool_Generic_FxElem_Status_("
    "template <typename BEFORE_PUBLISH>\nbool __cdecl FX_FreePool_Generic_FxElem_("
    _elem_free_wrapper_source
    "status-returning FX element free wrapper")
require_slice_ordered(
    _elem_free_wrapper_source
    "FX_EnterArchiveAwarePoolCriticalSection();"
    "FxPoolFreeLocked<FxElem, MAX_ELEMS>("
    "element free must acquire archive-aware allocator ownership first")
require_slice_ordered(
    _elem_free_wrapper_source
    "FxPoolFreeLocked<FxElem, MAX_ELEMS>("
    "Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);"
    "element free must release FX_ALLOC after the pool transaction")
require_slice_not_contains(
    _elem_free_wrapper_source
    "Com_Error("
    "the nested free wrapper must return status instead of longjmp")
require_literal_count(
    _fx_system_source
    "FX_FreePool_Generic_FxElem_Status_("
    4
    "the definition and three PHYSICS-owning live callers must stay accounted")

extract_source_slice(
    _fx_pool_source
    "template <typename ITEM_TYPE, std::size_t LIMIT, typename BEFORE_PUBLISH>\nFxPoolMutationStatus FxPoolFreeLocked("
    "template <typename ITEM_TYPE, std::size_t LIMIT>\nFxPoolMutationStatus FxPoolFreeLocked("
    _pool_free_source
    "fallible FxPoolFreeLocked")
require_slice_ordered(
    _pool_free_source
    "FxPoolCanFreeLocked<ITEM_TYPE, LIMIT>("
    "std::forward<BEFORE_PUBLISH>(beforePublish)()"
    "pool validation must precede the ownership callback")
require_slice_ordered(
    _pool_free_source
    "return FxPoolMutationStatus::BeforePublishRejected;"
    "pool[freedIndex].item = ITEM_TYPE{};"
    "callback rejection must precede free-slot publication")

extract_source_slice(
    _fx_system_source
    "void __cdecl FX_FreeElem("
    "void __cdecl FX_FreeTrailElem("
    _free_elem_source
    "FX_FreeElem")
require_literal_count(
    _free_elem_source
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    1
    "element free must acquire PHYSICS exactly once")
require_literal_count(
    _free_elem_source
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    1
    "element free must release PHYSICS exactly once")
require_slice_ordered(
    _free_elem_source
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "FX_FreePool_Generic_FxElem_Status_("
    "element free must hold PHYSICS before archive-aware FX_ALLOC")
require_slice_ordered(
    _free_elem_source
    "FX_FreePool_Generic_FxElem_Status_("
    "const fx::physics::BodyResult body = fx::physics::Take("
    "body ownership transfer must occur in the pre-publication callback")
require_slice_ordered(
    _free_elem_source
    "FX_FreePool_Generic_FxElem_Status_("
    "FX_PhysicsOwnerIsVacantLocked("
    "non-physics vacancy proof must occur in the pre-publication callback")
require_slice_ordered(
    _free_elem_source
    "const fx::physics::BodyResult body = fx::physics::Take("
    "nextElem->item.prevElemHandleInEffect =\n                    releasedElem.prevElemHandleInEffect;"
    "body ownership must be removed before graph unlink")
require_slice_ordered(
    _free_elem_source
    "FX_PhysicsOwnerIsVacantLocked("
    "nextElem->item.prevElemHandleInEffect =\n                    releasedElem.prevElemHandleInEffect;"
    "vacancy must be proved before graph unlink")
require_slice_ordered(
    _free_elem_source
    "FX_FreePool_Generic_FxElem_Status_("
    "Phys_ObjDestroy(PHYS_WORLD_FX, releasedBody);"
    "native destruction must follow successful pool publication")
require_slice_ordered(
    _free_elem_source
    "Phys_ObjDestroy(PHYS_WORLD_FX, releasedBody);"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "native destruction must finish before releasing PHYSICS")
extract_source_slice(
    _free_elem_source
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    _free_elem_lock_source
    "element-free lock interval")
require_slice_not_contains(
    _free_elem_lock_source
    "Com_Error("
    "element free must not longjmp while PHYSICS/FX_ALLOC is held")
require_slice_not_contains(
    _free_elem_lock_source
    "MyAssertHandler("
    "element free must not assert while PHYSICS/FX_ALLOC is held")
extract_source_tail(
    _free_elem_source
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    _free_elem_after_unlock_source
    "element-free post-unlock reporting")
require_slice_ordered(
    _free_elem_after_unlock_source
    "if (poolStatus != FxPoolMutationStatus::Success)"
    "Com_Error("
    "element free errors must be reported after both locks are released")

# Unpublished rollback and spotlight free are the other status-wrapper users.
# They must prove vacancy under the same nested lock order.
extract_source_slice(
    _fx_system_source
    "bool FX_RollbackUnpublishedElem("
    "void __cdecl FX_SpawnElem("
    _rollback_elem_source
    "FX_RollbackUnpublishedElem")
require_slice_ordered(
    _rollback_elem_source
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "FX_FreePool_Generic_FxElem_Status_("
    "unpublished rollback must hold PHYSICS before FX_ALLOC")
require_slice_ordered(
    _rollback_elem_source
    "FX_FreePool_Generic_FxElem_Status_("
    "FX_PhysicsOwnerIsVacantLocked("
    "unpublished rollback must prove vacancy before slot publication")
require_slice_ordered(
    _rollback_elem_source
    "FX_PhysicsOwnerIsVacantLocked("
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "unpublished rollback must retain PHYSICS through pool publication")
require_slice_not_contains(
    _rollback_elem_source
    "Com_Error("
    "unpublished rollback must return status for reporting after unlock")

extract_source_slice(
    _fx_system_source
    "void __cdecl FX_FreeSpotLightElem("
    "double __cdecl FX_GetClientVisibility("
    _free_spotlight_source
    "FX_FreeSpotLightElem")
require_slice_ordered(
    _free_spotlight_source
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "FX_FreePool_Generic_FxElem_Status_("
    "spotlight free must hold PHYSICS before FX_ALLOC")
require_slice_ordered(
    _free_spotlight_source
    "FX_FreePool_Generic_FxElem_Status_("
    "FX_PhysicsOwnerIsVacantLocked("
    "spotlight free must prove vacancy in its pool callback")
require_slice_ordered(
    _free_spotlight_source
    "FX_PhysicsOwnerIsVacantLocked("
    "system->activeSpotLightElemHandle = FX_INVALID_HANDLE;"
    "spotlight vacancy must be proved before clearing graph ownership")
require_slice_ordered(
    _free_spotlight_source
    "Sys_AtomicStore(&system->activeSpotLightElemCount, 0);"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "spotlight ownership must be cleared before pool publication completes")
require_slice_ordered(
    _free_spotlight_source
    "FX_PhysicsOwnerIsVacantLocked("
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "spotlight free must retain PHYSICS through pool publication")
extract_source_slice(
    _free_spotlight_source
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    _free_spotlight_lock_source
    "spotlight-free lock interval")
require_slice_not_contains(
    _free_spotlight_lock_source
    "Com_Error("
    "spotlight free must not report while PHYSICS/FX_ALLOC is held")

# Reset/shutdown drain every native body exactly through the sidecar before
# rebuilding free lists, allocation bitmaps, or zeroing frozen storage.
extract_source_slice(
    _fx_system_source
    "fx::physics::SidecarStatus FX_DrainPhysicsBodySidecarLocked("
    "fx::physics::SidecarStatus FX_ResetSystemUnderLifecycleClaim("
    _drain_sidecar_source
    "FX_DrainPhysicsBodySidecarLocked")
require_slice_not_contains(
    _drain_sidecar_source
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "the locked drain helper must not recursively acquire PHYSICS")
require_slice_not_contains(
    _drain_sidecar_source
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "the locked drain helper must not release its caller's PHYSICS lock")
require_literal_count(
    _drain_sidecar_source
    "fx::physics::TakeFirst(sidecar);"
    1
    "lifecycle drain must have one bounded sidecar transfer point")
extract_source_tail(
    _drain_sidecar_source
    "std::size_t destroyedCount = 0;"
    _active_drain_source
    "initialized sidecar drain")
require_slice_ordered(
    _active_drain_source
    "destroyedCount < fx::physics::BODY_LIMIT"
    "fx::physics::TakeFirst(sidecar);"
    "sidecar draining must be bounded before each transfer")
require_slice_ordered(
    _active_drain_source
    "fx::physics::TakeFirst(sidecar);"
    "Phys_ObjDestroy(PHYS_WORLD_FX, body.body);"
    "each transferred body must be destroyed exactly once")
require_slice_ordered(
    _active_drain_source
    "Phys_ObjDestroy(PHYS_WORLD_FX, body.body);"
    "if (sidecar->ActiveCount() != 0)"
    "drain completion must verify no body remains")
require_slice_ordered(
    _active_drain_source
    "if (sidecar->ActiveCount() != 0)"
    "return fx::physics::ResetEmpty(sidecar);"
    "empty reset must advance generations only after the drain completes")

extract_source_slice(
    _fx_system_source
    "fx::physics::SidecarStatus FX_ResetSystemUnderLifecycleClaim("
    "} // namespace"
    _reset_under_claim_source
    "FX_ResetSystemUnderLifecycleClaim")
require_slice_contains(
    _reset_under_claim_source
    "Sys_AtomicLoad(&system->iteratorCount) != -1"
    "bulk reset must require iterator-exclusive lifecycle ownership")
require_slice_ordered(
    _reset_under_claim_source
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "FX_DrainPhysicsBodySidecarLocked(system);"
    "bulk reset must lock before draining native body ownership")
require_slice_ordered(
    _reset_under_claim_source
    "FX_DrainPhysicsBodySidecarLocked(system);"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "bulk reset must retain PHYSICS through the complete drain")
require_slice_ordered(
    _reset_under_claim_source
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "system->firstFreeElem = 0;"
    "bulk reset must drain before rebuilding the element free list")
require_slice_ordered(
    _reset_under_claim_source
    "FX_DrainPhysicsBodySidecarLocked(system);"
    "FxPoolResetAllocationState(&states->elems);"
    "bulk reset must drain before clearing the allocation bitmap")
require_slice_not_contains(
    _reset_under_claim_source
    "Com_Error("
    "bulk reset must return status instead of reporting under lifecycle ownership")
require_slice_not_contains(
    _reset_under_claim_source
    "Sys_AtomicStore(&system->iteratorCount, 0);"
    "bulk reset must preserve iterator-exclusive lifecycle ownership")

extract_source_slice(
    _fx_system_source
    "void __cdecl FX_InitSystem("
    "void __cdecl FX_ResetSystem("
    _init_system_source
    "FX_InitSystem")
require_slice_ordered(
    _init_system_source
    "FX_RegisterDvars();"
    "FX_BeginLifecycleClaim(system, &lifecycleClaim)"
    "init must finish report-capable registration before closing admission")
require_slice_ordered(
    _init_system_source
    "FX_BeginLifecycleClaim(system, &lifecycleClaim)"
    "FX_ClearSystemForShutdownLocked(system);"
    "init must own iterator exclusion before clearing system state")
require_slice_ordered(
    _init_system_source
    "FX_ClearSystemForShutdownLocked(system);"
    "std::memset(systemBuffers, 0, sizeof(*systemBuffers));"
    "init must clear system and buffer state under one lifecycle claim")
require_slice_ordered(
    _init_system_source
    "FX_LinkSystemBuffers(system, systemBuffers);"
    "FX_ResetSystemUnderLifecycleClaim(system);"
    "init must link buffers before the checked bulk reset")
require_slice_ordered(
    _init_system_source
    "FX_ResetSystemUnderLifecycleClaim(system);"
    "system->isInitialized = 1;"
    "init must finish reset before publishing initialized state")
extract_source_tail(
    _init_system_source
    "system->isInitialized = 1;"
    _init_publication_source
    "init final publication")
require_slice_contains(
    _init_publication_source
    "FX_EndLifecycleClaim(&lifecycleClaim)"
    "init must retain lifecycle ownership through final publication")
require_slice_not_contains(
    _init_system_source
    "memset((uint8_t *)system, 0, sizeof(FxSystem));"
    "init must preserve the iterator word while clearing system state")

extract_source_slice(
    _fx_system_source
    "void __cdecl FX_ResetSystem("
    "int32_t __cdecl FX_EffectToHandle("
    _reset_system_source
    "FX_ResetSystem")
require_slice_ordered(
    _reset_system_source
    "FX_BeginLifecycleClaim(system, &lifecycleClaim)"
    "FX_ResetSystemUnderLifecycleClaim(system);"
    "public reset must acquire lifecycle ownership before bulk mutation")
require_slice_ordered(
    _reset_system_source
    "FX_ResetSystemUnderLifecycleClaim(system);"
    "FX_EndLifecycleClaim(&lifecycleClaim);"
    "public reset must retain iterator exclusion through bulk mutation")
require_slice_ordered(
    _reset_system_source
    "FX_EndLifecycleClaim(&lifecycleClaim);"
    "if (resetStatus != fx::physics::SidecarStatus::Success)"
    "public reset must release lifecycle ownership before reporting failure")
require_slice_not_contains(
    _reset_system_source
    "Sys_AtomicStore(&system->iteratorCount, 0);"
    "reset must release iterator exclusion through its lifecycle claim")

extract_source_slice(
    _fx_system_source
    "void __cdecl FX_ShutdownSystem("
    "void __cdecl FX_RelocateSystem("
    _shutdown_system_source
    "FX_ShutdownSystem")
require_slice_ordered(
    _shutdown_system_source
    "Sys_EnterCriticalSection(CRITSECT_PHYSICS);"
    "FX_DrainPhysicsBodySidecarLocked(system);"
    "shutdown must lock before draining native body ownership")
require_slice_ordered(
    _shutdown_system_source
    "FX_DrainPhysicsBodySidecarLocked(system);"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "shutdown must retain PHYSICS through the complete drain")
require_slice_ordered(
    _shutdown_system_source
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICS);"
    "FX_ClearSystemForShutdownLocked(system);"
    "shutdown must drain before clearing frozen system storage")
require_slice_ordered(
    _shutdown_system_source
    "FX_DrainPhysicsBodySidecarLocked(system);"
    "memset((uint8_t *)systemBuffers, 0, sizeof(FxSystemBuffers));"
    "shutdown must drain before clearing frozen pool storage")
extract_source_tail(
    _shutdown_system_source
    "FX_ClearSystemForShutdownLocked(system);"
    _shutdown_after_clear_source
    "shutdown post-clear lifecycle release")
require_slice_contains(
    _shutdown_after_clear_source
    "FX_EndLifecycleClaim(&lifecycleClaim)"
    "shutdown must retain iterator-exclusive ownership through system clearing")
require_slice_not_contains(
    _shutdown_system_source
    "memset((uint8_t *)system, 0, sizeof(FxSystem));"
    "shutdown must not publish an unlocked iterator word during clear")
require_slice_ordered(
    _shutdown_system_source
    "FX_EndLifecycleClaim(&lifecycleClaim)"
    "\"FX shutdown could not drain physics ownership"
    "shutdown must release lifecycle gates before reporting drain failure")

# Frozen physObjId values are generation tokens only. Live/archive code may
# pass them through the bit-preserving helpers, never decode them as addresses.
set(_live_token_sources
    "${_fx_system_source}\n${_fx_draw_source}\n"
    "${_fx_archive_source}\n${_fx_update_source}")
foreach(_forbidden_codec
    "FX_DecodeArchivedPhysicsBody"
    "FX_EncodeArchivedPhysicsBody")
    require_slice_not_contains(
        _live_token_sources
        "${_forbidden_codec}"
        "legacy FX body pointer codecs must not return")
endforeach()
require_slice_not_contains(
    _live_token_sources
    "Phys_ObjCreate("
    "live FX ownership must use fallible typed native construction")
require_slice_not_matches(
    _live_token_sources
    "\\([ \t\r\n]*(const[ \t\r\n]+)?dxBody[ \t\r\n]*\\*[ \t\r\n]*\\)[^;]*physObjId"
    "physObjId must not be C-cast to a native body pointer")
require_slice_not_matches(
    _live_token_sources
    "(reinterpret_cast|static_cast)[ \t\r\n]*<[ \t\r\n]*(const[ \t\r\n]+)?dxBody[ \t\r\n]*\\*>[ \t\r\n]*\\([^;]*physObjId"
    "physObjId must not be C++-cast to a native body pointer")
require_slice_not_matches(
    _live_token_sources
    "physObjId[ \t\r\n]*=[^;]*Phys_Obj"
    "native body creation results must not be stored in the token field")
require_slice_not_matches(
    _live_token_sources
    "Phys_Obj[A-Za-z0-9_]*[ \t\r\n]*\\([^;]*physObjId"
    "native physics calls must not consume the token field directly")
require_slice_contains(
    _fx_system_source
    "fx::physics::TokenToLegacyField(binding.token)"
    "spawn must publish only encoded generation tokens")
require_slice_contains(
    _fx_draw_source
    "fx::physics::TokenFromLegacyField(draw->elem->physObjId)"
    "draw must decode token bits without pointer conversion")
require_slice_contains(
    _fx_archive_source
    "fx::physics::TokenFromLegacyField("
    "archive capture must resolve generation tokens")
require_slice_contains(
    _fx_archive_source
    "fx::physics::TokenToLegacyField("
    "archive restore must publish fresh generation tokens")

message(STATUS "Live FX physics ownership source invariants verified")
