cmake_minimum_required(VERSION 3.16)

function(require_pool_source_contains RELATIVE_PATH NEEDLE DESCRIPTION)
    file(READ "${SOURCE_ROOT}/src/${RELATIVE_PATH}" _source)
    string(FIND "${_source}" "${NEEDLE}" _position)
    if (_position EQUAL -1)
        message(FATAL_ERROR
            "Missing pool invariant (${DESCRIPTION}) in src/${RELATIVE_PATH}")
    endif()
endfunction()

function(require_pool_source_not_contains RELATIVE_PATH NEEDLE DESCRIPTION)
    file(READ "${SOURCE_ROOT}/src/${RELATIVE_PATH}" _source)
    string(FIND "${_source}" "${NEEDLE}" _position)
    if (NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden pool regression (${DESCRIPTION}) in src/${RELATIVE_PATH}")
    endif()
endfunction()

function(require_pool_source_matches RELATIVE_PATH PATTERN DESCRIPTION)
    file(READ "${SOURCE_ROOT}/src/${RELATIVE_PATH}" _source)
    string(REGEX MATCH "${PATTERN}" _match "${_source}")
    if ("${_match}" STREQUAL "")
        message(FATAL_ERROR
            "Missing pool invariant (${DESCRIPTION}) in src/${RELATIVE_PATH}")
    endif()
endfunction()

function(require_pool_source_match_count
    RELATIVE_PATH PATTERN EXPECTED_COUNT DESCRIPTION)
    file(READ "${SOURCE_ROOT}/src/${RELATIVE_PATH}" _source)
    string(REGEX MATCHALL "${PATTERN}" _matches "${_source}")
    list(LENGTH _matches _count)
    if (NOT _count EQUAL EXPECTED_COUNT)
        message(FATAL_ERROR
            "Unexpected pool invariant count (${DESCRIPTION}) in "
            "src/${RELATIVE_PATH}: expected ${EXPECTED_COUNT}, found ${_count}")
    endif()
endfunction()

# Extract by two stable declaration boundaries. This is intentionally more
# precise than global substring counts: a walk in Pool_ValidateFull is wanted,
# while the same walk in Pool_Alloc, Pool_Free, or their fast validator is not.
function(pool_function_slice
    RELATIVE_PATH START_MARKER END_MARKER OUT_VARIABLE)
    file(READ "${SOURCE_ROOT}/src/${RELATIVE_PATH}" _source)
    string(FIND "${_source}" "${START_MARKER}" _start)
    if (_start EQUAL -1)
        message(FATAL_ERROR
            "Could not find function boundary '${START_MARKER}' in "
            "src/${RELATIVE_PATH}")
    endif()
    string(FIND "${_source}" "${END_MARKER}" _end)
    if (_end EQUAL -1 OR _end LESS_EQUAL _start)
        message(FATAL_ERROR
            "Could not find ordered function boundary '${END_MARKER}' in "
            "src/${RELATIVE_PATH}")
    endif()
    math(EXPR _length "${_end} - ${_start}")
    string(SUBSTRING "${_source}" ${_start} ${_length} _body)
    set(${OUT_VARIABLE} "${_body}" PARENT_SCOPE)
endfunction()

function(require_pool_function_contains
    RELATIVE_PATH START_MARKER END_MARKER NEEDLE DESCRIPTION)
    pool_function_slice(
        "${RELATIVE_PATH}" "${START_MARKER}" "${END_MARKER}" _body)
    string(FIND "${_body}" "${NEEDLE}" _position)
    if (_position EQUAL -1)
        message(FATAL_ERROR
            "Missing function invariant (${DESCRIPTION}) in "
            "src/${RELATIVE_PATH} at ${START_MARKER}")
    endif()
endfunction()

function(require_pool_function_not_contains
    RELATIVE_PATH START_MARKER END_MARKER NEEDLE DESCRIPTION)
    pool_function_slice(
        "${RELATIVE_PATH}" "${START_MARKER}" "${END_MARKER}" _body)
    string(FIND "${_body}" "${NEEDLE}" _position)
    if (NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden function regression (${DESCRIPTION}) in "
            "src/${RELATIVE_PATH} at ${START_MARKER}")
    endif()
endfunction()

function(require_pool_function_not_matches
    RELATIVE_PATH START_MARKER END_MARKER PATTERN DESCRIPTION)
    pool_function_slice(
        "${RELATIVE_PATH}" "${START_MARKER}" "${END_MARKER}" _body)
    string(REGEX MATCH "${PATTERN}" _match "${_body}")
    if (NOT "${_match}" STREQUAL "")
        message(FATAL_ERROR
            "Forbidden function regression (${DESCRIPTION}) in "
            "src/${RELATIVE_PATH} at ${START_MARKER}")
    endif()
endfunction()

require_pool_source_contains(
    "universal/pool_allocator.h"
    "RUNTIME_SIZE(pooldata_t, 0x8, 0x10);"
    "retail pool metadata must retain its x86 ABI")
require_pool_source_contains(
    "universal/pool_allocator.h"
    "RUNTIME_OFFSET(pooldata_t, activeCount, 0x4, 0x8);"
    "retail active-count offset must remain explicit")
require_pool_source_contains(
    "universal/pool_allocator.h"
    "using poolslotstate_t = std::uint32_t;"
    "slot-state indices must have a fixed cross-target width")
require_pool_source_contains(
    "universal/pool_allocator.h"
    "constexpr poolslotstate_t POOL_SLOT_END = UINT32_MAX - 1u;"
    "the free-list end sentinel must be public and stable")
require_pool_source_contains(
    "universal/pool_allocator.h"
    "constexpr poolslotstate_t POOL_SLOT_ALLOCATED = UINT32_MAX;"
    "the allocation sentinel must be public and stable")
require_pool_source_contains(
    "universal/pool_allocator.h"
    "struct poolcontrol_t"
    "portable ownership state must live outside retail metadata")
require_pool_source_contains(
    "universal/pool_allocator.h"
    "RUNTIME_SIZE(poolcontrol_t, 0x24, 0x40);"
    "control layout must be explicit on both pointer widths")
require_pool_source_contains(
    "universal/pool_allocator.h"
    "poolcontrol_t *control;"
    "every descriptor must carry its authoritative control table")
require_pool_source_contains(
    "universal/pool_allocator.h"
    "RUNTIME_OFFSET(poolstorage_t, control, 0xC, 0x18);"
    "descriptor control offset must be target-explicit")
require_pool_source_contains(
    "universal/pool_allocator.h"
    "Pool_ControlFor(poolslotstate_t (&slotState)[N]) noexcept"
    "typed sidecar construction must retain the exact element count")
require_pool_source_contains(
    "universal/pool_allocator.h"
    "bool __cdecl Pool_Invalidate("
    "failed or reset owners need an explicit retirement operation")
require_pool_source_contains(
    "universal/pool_allocator.h"
    "bool __cdecl Pool_ValidateFull("
    "O(N) integrity checking must remain an explicit boundary operation")
require_pool_source_not_contains(
    "universal/pool_allocator.h"
    "outCount"
    "count queries must not expose aliasable outputs")
require_pool_source_not_contains(
    "universal/pool_allocator.h"
    "outNext"
    "link queries must not expose aliasable outputs")
require_pool_source_not_contains(
    "universal/pool_allocator.h"
    "outIndex"
    "index queries must not expose aliasable outputs")

require_pool_source_contains(
    "universal/pool_allocator.cpp"
    "storage.control->slotStateCount != storage.itemCount"
    "sidecar extent must exactly match the object pool")
require_pool_source_contains(
    "universal/pool_allocator.cpp"
    "slot-state byte count does not overflow"
    "sidecar range arithmetic must be checked")
require_pool_source_contains(
    "universal/pool_allocator.cpp"
    "Pool_RangesAreSeparate"
    "pool, metadata, control, and sidecar ranges must be disjoint")
require_pool_source_contains(
    "universal/pool_allocator.cpp"
    "Pool_MetadataIsSeparate(pooldata, ranges)"
    "pooldata overlap must be rejected before mutation")
require_pool_source_contains(
    "universal/pool_allocator.cpp"
    "std::memcpy(item, &next, sizeof(next));"
    "compatibility links must be written without typed aliasing")
require_pool_source_contains(
    "universal/pool_allocator.cpp"
    "std::memcmp(item, &expectedNext, sizeof(expectedNext))"
    "compatibility links must be compared as bytes")
require_pool_source_not_contains(
    "universal/pool_allocator.cpp"
    "Pool_ReadNext"
    "untrusted object bytes must not be materialized as pointers")
require_pool_source_not_contains(
    "universal/pool_allocator.cpp"
    "->next"
    "raw freenode lifetime violations must not return")
require_pool_source_not_contains(
    "universal/pool_allocator.cpp"
    "*(uint32_t *)"
    "native compatibility links must never be truncated")
require_pool_source_not_contains(
    "universal/pool_allocator.cpp"
    "*(void **)"
    "unaligned compatibility links must not use pointer lvalues")
require_pool_source_match_count(
    "universal/pool_allocator.cpp"
    "(^|[\r\n])[ \t]*for[ \t]*\\("
    3
    "the only linear walks must be initialization and the two full-validation scans")
require_pool_source_not_contains(
    "universal/pool_allocator.cpp"
    "while ("
    "no hidden allocator helper may add an unbounded walk")
require_pool_source_match_count(
    "universal/pool_allocator.cpp"
    "Pool_ValidateFull\\("
    1
    "full validation must remain explicit rather than called by a hot path")

set(_pool_loop_pattern "(^|[\r\n])[ \t]*(for|while)[ \t]*\\(")
set(_pool_cpp "universal/pool_allocator.cpp")

require_pool_function_not_matches(
    "${_pool_cpp}"
    "bool Pool_FastStateIsValid("
    "} // namespace"
    "${_pool_loop_pattern}"
    "the shared fast validator must remain O(1)")
require_pool_function_not_contains(
    "${_pool_cpp}"
    "bool Pool_FastStateIsValid("
    "} // namespace"
    "Pool_ValidateFull("
    "the fast validator must not call the full validator")
require_pool_function_contains(
    "${_pool_cpp}"
    "bool Pool_FastStateIsValid("
    "} // namespace"
    "control.activeCount != pooldata->activeCount"
    "fast validation must reconcile both active-count mirrors")
require_pool_function_contains(
    "${_pool_cpp}"
    "bool Pool_FastStateIsValid("
    "} // namespace"
    "control.slotState[headIndex]"
    "fast validation must inspect the authoritative head state")
require_pool_function_contains(
    "${_pool_cpp}"
    "bool Pool_FastStateIsValid("
    "} // namespace"
    "Pool_LinkMatches(pooldata->firstFree, expectedNext)"
    "fast validation must reconcile the current compatibility edge")

require_pool_function_not_matches(
    "${_pool_cpp}"
    "void *__cdecl Pool_Alloc("
    "bool __cdecl Pool_Free("
    "${_pool_loop_pattern}"
    "Pool_Alloc must remain O(1)")
require_pool_function_not_contains(
    "${_pool_cpp}"
    "void *__cdecl Pool_Alloc("
    "bool __cdecl Pool_Free("
    "Pool_ValidateFull("
    "Pool_Alloc must not invoke full validation")
require_pool_function_contains(
    "${_pool_cpp}"
    "void *__cdecl Pool_Alloc("
    "bool __cdecl Pool_Free("
    "control.slotState[state.headIndex] = POOL_SLOT_ALLOCATED;"
    "allocation must mark exactly the popped shadow slot")
require_pool_function_contains(
    "${_pool_cpp}"
    "void *__cdecl Pool_Alloc("
    "bool __cdecl Pool_Free("
    "++control.activeCount;"
    "allocation must publish its shadow count")

require_pool_function_not_matches(
    "${_pool_cpp}"
    "bool __cdecl Pool_Free("
    "poolallocresult_t __cdecl Pool_TryAllocNoReport("
    "${_pool_loop_pattern}"
    "Pool_Free must remain O(1)")
require_pool_function_not_contains(
    "${_pool_cpp}"
    "bool __cdecl Pool_Free("
    "poolallocresult_t __cdecl Pool_TryAllocNoReport("
    "Pool_ValidateFull("
    "Pool_Free must not invoke full validation")
require_pool_function_contains(
    "${_pool_cpp}"
    "bool __cdecl Pool_Free("
    "poolallocresult_t __cdecl Pool_TryAllocNoReport("
    "control.slotState[itemIndex] != POOL_SLOT_ALLOCATED"
    "free must reject duplicate ownership in O(1)")
require_pool_function_contains(
    "${_pool_cpp}"
    "bool __cdecl Pool_Free("
    "poolallocresult_t __cdecl Pool_TryAllocNoReport("
    "control.slotState[itemIndex] = state.headIndex;"
    "free must push through the shadow chain")

require_pool_function_contains(
    "${_pool_cpp}"
    "static bool Pool_ValidateFullImpl("
    "bool __cdecl Pool_ValidateFull("
    "for (std::size_t"
    "full validation must explicitly scan bounded state")
require_pool_function_contains(
    "${_pool_cpp}"
    "static bool Pool_ValidateFullImpl("
    "bool __cdecl Pool_ValidateFull("
    "expectedFreeCount"
    "the free-chain walk must be bounded by the inactive count")
require_pool_function_contains(
    "${_pool_cpp}"
    "static bool Pool_ValidateFullImpl("
    "bool __cdecl Pool_ValidateFull("
    "allocatedCount != state.activeCount"
    "full validation must reconcile allocation ownership")
require_pool_function_contains(
    "${_pool_cpp}"
    "static bool Pool_ValidateFullImpl("
    "bool __cdecl Pool_ValidateFull("
    "every free-list link matches slot-state control"
    "full validation must reconcile every compatibility edge")

# Silent transaction primitives share the O(1) fast mutations but suppress
# assertion/report callbacks. Full validation remains deliberately bounded
# O(N), with diagnostic and no-report wrappers selecting the reporting mode.
foreach(_silent_api IN ITEMS
    "Pool_TryAllocNoReport("
    "Pool_TryFreeNoReport("
    "Pool_TryValidateAllocatedNoReport("
    "Pool_TryValidateFullNoReport(")
    require_pool_source_contains(
        "universal/pool_allocator.h"
        "${_silent_api}"
        "silent fixed-pool API must remain publicly declared")
endforeach()
require_pool_function_not_matches(
    "${_pool_cpp}"
    "poolallocresult_t __cdecl Pool_TryAllocNoReport("
    "poolmutationstatus_t __cdecl Pool_TryFreeNoReport("
    "${_pool_loop_pattern}"
    "silent allocation must remain O(1)")
require_pool_function_not_contains(
    "${_pool_cpp}"
    "poolallocresult_t __cdecl Pool_TryAllocNoReport("
    "poolmutationstatus_t __cdecl Pool_TryFreeNoReport("
    "POOL_REJECT("
    "silent allocation cannot report")
require_pool_function_not_matches(
    "${_pool_cpp}"
    "poolmutationstatus_t __cdecl Pool_TryFreeNoReport("
    "poolmutationstatus_t __cdecl Pool_TryValidateAllocatedNoReport("
    "${_pool_loop_pattern}"
    "silent free must remain O(1)")
require_pool_function_not_contains(
    "${_pool_cpp}"
    "poolmutationstatus_t __cdecl Pool_TryFreeNoReport("
    "poolmutationstatus_t __cdecl Pool_TryValidateAllocatedNoReport("
    "POOL_REJECT("
    "silent free cannot report")
require_pool_function_contains(
    "${_pool_cpp}"
    "bool __cdecl Pool_ValidateFull("
    "poolcountresult_t __cdecl Pool_GetFreeCount("
    "Pool_ValidateFullImpl(storage, pooldata, true)"
    "diagnostic full validation explicitly enables reporting")
require_pool_function_contains(
    "${_pool_cpp}"
    "bool __cdecl Pool_ValidateFull("
    "poolcountresult_t __cdecl Pool_GetFreeCount("
    "Pool_ValidateFullImpl(storage, pooldata, false)"
    "silent full validation explicitly suppresses reporting")

require_pool_function_not_matches(
    "${_pool_cpp}"
    "poolcountresult_t __cdecl Pool_GetFreeCount("
    "poolnextresult_t __cdecl Pool_NextFree("
    "${_pool_loop_pattern}"
    "the ordinary count query must remain O(1)")
require_pool_function_not_contains(
    "${_pool_cpp}"
    "poolcountresult_t __cdecl Pool_GetFreeCount("
    "poolnextresult_t __cdecl Pool_NextFree("
    "Pool_ValidateFull("
    "ordinary count queries must not hide a full scan")
require_pool_function_contains(
    "${_pool_cpp}"
    "poolnextresult_t __cdecl Pool_NextFree("
    "poolindexresult_t __cdecl Pool_GetSlotIndex("
    "nextIndex == itemIndex"
    "checked link queries must reject self-cycles")

require_pool_source_contains(
    "physics/phys_local.h"
    "RUNTIME_SIZE(PhysObjUserData, 0x70, 0x78);"
    "physics userdata must retain its native target stride")
require_pool_source_contains(
    "physics/phys_ode.cpp"
    "Pool_ControlFor(physUserDataPoolSlotState)"
    "userdata ownership needs an external sidecar")
require_pool_source_contains(
    "physics/phys_ode.cpp"
    "Pool_StorageFor(physGlob.userData, physUserDataPoolControl)"
    "userdata operations must share their bound descriptor")
require_pool_source_contains(
    "physics/phys_ode.cpp"
    "const bool userDataPoolValid = Pool_ValidateFull("
    "userdata shutdown diagnostics must perform full validation")
require_pool_source_match_count(
    "physics/phys_ode.cpp"
    "Pool_ValidateFull\\("
    1
    "userdata has one explicit full-validation boundary")
require_pool_source_contains(
    "physics/phys_ode.cpp"
    "Pool_Invalidate(userDataStorage, &physGlob.userDataPool)"
    "userdata resets must retire the old binding explicitly")

require_pool_source_contains(
    "physics/ode/ode.cpp"
    "Pool_ControlFor(odeBodyPoolSlotState)"
    "ODE bodies need an external ownership sidecar")
require_pool_source_contains(
    "physics/ode/ode.cpp"
    "Pool_ControlFor(odeGeomPoolSlotState)"
    "ODE geoms need an external ownership sidecar")
require_pool_source_contains(
    "physics/ode/ode.cpp"
    "const bool bodyPoolValid = Pool_ValidateFull("
    "ODE body leak checks must perform full validation")
require_pool_source_contains(
    "physics/ode/ode.cpp"
    "const bool geomPoolValid = Pool_ValidateFull("
    "ODE geom leak checks must perform full validation")
require_pool_source_match_count(
    "physics/ode/ode.cpp"
    "Pool_ValidateFull\\("
    2
    "ODE leak checks must validate both owned pools")
require_pool_source_contains(
    "physics/ode/ode.cpp"
    "Pool_Invalidate(bodyStorage, &odeGlob.bodyPool)"
    "ODE resets must retire body control explicitly")
require_pool_source_contains(
    "physics/ode/ode.cpp"
    "Pool_Invalidate(geomStorage, &odeGlob.geomPool)"
    "ODE resets must retire geom control explicitly")
require_pool_source_contains(
    "physics/ode/ode.cpp"
    "traversedFreeCount >= ODE_GEOM_POOL_COUNT"
    "diagnostic traversal must retain a hard bound")
require_pool_source_not_contains(
    "physics/ode/ode.cpp"
    "*(void **)ff"
    "ODE diagnostics must not dereference raw link storage")

require_pool_source_contains(
    "EffectsCore/fx_archive.cpp"
    "Phys_TryGetFreeResourceCapacityLockedNoReport("
    "archive capacity must use the silent native three-pool validator")
require_pool_source_not_contains(
    "EffectsCore/fx_archive.cpp"
    "Pool_GetFreeCount("
    "archive capacity must not bypass the silent native validator")
require_pool_source_not_contains(
    "EffectsCore/fx_archive.cpp"
    "Pool_ValidateFull("
    "archive capacity must not invoke diagnostic full validation")
foreach(_capacity_pool_storage IN ITEMS
    "ODE_BodyPoolStorage()"
    "Phys_UserDataPoolStorage()"
    "ODE_GeomPoolStorage()")
    require_pool_function_contains(
        "physics/phys_ode.cpp"
        "Phys_TryGetFreeResourceCapacityLockedNoReport("
        "PhysBodyRollbackStatus __cdecl Phys_TryCaptureBodyStateLocked("
        "${_capacity_pool_storage}"
        "silent capacity must inspect every fixed-pool descriptor")
endforeach()
require_pool_source_contains(
    "physics/ode/collision_kernel.cpp"
    "ODE_GeomPoolStorage()"
    "collision allocation must retain the bound geom descriptor")

message(STATUS "External-shadow pool source invariants verified")
