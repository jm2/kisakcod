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

require_pool_source_contains(
    "universal/pool_allocator.h"
    "RUNTIME_SIZE(pooldata_t, 0x8, 0x10);"
    "retail pool metadata must retain its x86 layout while widening natively")
require_pool_source_contains(
    "universal/pool_allocator.h"
    "RUNTIME_OFFSET(pooldata_t, activeCount, 0x4, 0x8);"
    "pool active-count offset must be explicit on both pointer widths")
require_pool_source_contains(
    "universal/pool_allocator.h"
    "struct poolstorage_t"
    "each operation must receive the backing extent needed for bounded validation")
require_pool_source_contains(
    "universal/pool_allocator.h"
    "poolcountresult_t __cdecl Pool_GetFreeCount("
    "free-count validation must return status and value without an aliasable output")
require_pool_source_contains(
    "universal/pool_allocator.h"
    "poolindexresult_t __cdecl Pool_GetSlotIndex("
    "leak diagnostics must return checked indices without aliasable outputs")
require_pool_source_not_contains(
    "universal/pool_allocator.h"
    "outCount"
    "pool count queries must not expose aliasable output pointers")
require_pool_source_not_contains(
    "universal/pool_allocator.h"
    "outNext"
    "pool link queries must not expose aliasable output pointers")
require_pool_source_not_contains(
    "universal/pool_allocator.h"
    "outIndex"
    "pool index queries must not expose aliasable output pointers")

require_pool_source_contains(
    "universal/pool_allocator.cpp"
    "std::memcpy(item, &next, sizeof(next));"
    "free-list links must be written at native pointer width without typed aliasing")
require_pool_source_contains(
    "universal/pool_allocator.cpp"
    "std::memcpy(&next, item, sizeof(next));"
    "free-list links must be read at native pointer width without typed aliasing")
require_pool_source_contains(
    "universal/pool_allocator.cpp"
    "storage.itemSize < sizeof(void *)"
    "every slot must hold a complete native link")
require_pool_source_contains(
    "universal/pool_allocator.cpp"
    "storage.itemSize * storage.itemCount does not overflow"
    "pool extent multiplication must be checked")
require_pool_source_contains(
    "universal/pool_allocator.cpp"
    "Pool_MetadataIsSeparate"
    "pool links must not overwrite their metadata")
require_pool_source_matches(
    "universal/pool_allocator.cpp"
    "if[ \t]*\\(!Pool_MetadataIsSeparate\\(storage, pooldata\\)\\)[ \t\r\n]*return false;[ \t\r\n]*pooldata->firstFree[ \t]*=[ \t]*nullptr;"
    "overlapping metadata must be rejected before initialization writes")
require_pool_source_contains(
    "universal/pool_allocator.cpp"
    "free-list length matches inactive slot count"
    "free-list validation must be bounded by the descriptor and active count")
require_pool_source_not_contains(
    "universal/pool_allocator.cpp"
    "->next"
    "raw object-lifetime-violating freenode dereferences must not return")
require_pool_source_not_contains(
    "universal/pool_allocator.cpp"
    "*(uint32_t *)"
    "free-list pointers must never be stored through a 32-bit integer")
require_pool_source_not_contains(
    "universal/pool_allocator.cpp"
    "*(void **)"
    "free-list links must never be read through an unaligned pointer lvalue")

require_pool_source_contains(
    "physics/phys_local.h"
    "RUNTIME_SIZE(PhysObjUserData, 0x70, 0x78);"
    "physics userdata records must use their native 64-bit stride")
require_pool_source_contains(
    "physics/phys_local.h"
    "RUNTIME_OFFSET(PhysObjUserData, body, 0xC, 0x10);"
    "physics userdata body ownership must retain its exact target offset")
require_pool_source_contains(
    "physics/phys_ode.cpp"
    "Pool_StorageFor(physGlob.userData)"
    "userdata pool operations must derive their extent from the typed array")
require_pool_source_match_count(
    "physics/phys_ode.cpp"
    "Pool_StorageFor\\(physGlob[.]userData\\)"
    4
    "all userdata init/allocate/free/count operations must use the typed extent")
require_pool_source_contains(
    "physics/phys_ode.cpp"
    "static_cast<int>(trackedSize)"
    "memory tracking must use the native PhysGlob size after a checked conversion")
require_pool_source_not_contains(
    "physics/phys_ode.cpp"
    "0x70u, 0x200u"
    "the x86-only userdata stride must not return")

require_pool_source_contains(
    "physics/ode/ode.cpp"
    "traversedFreeCount >= ODE_GEOM_POOL_COUNT"
    "ODE leak traversal must have a hard visit bound")
require_pool_source_contains(
    "physics/ode/ode.cpp"
    "Pool_GetSlotIndex("
    "ODE leak traversal must reject foreign and interior nodes")
require_pool_source_contains(
    "physics/ode/ode.cpp"
    "Pool_NextFree("
    "ODE leak traversal must use the checked link reader")
require_pool_source_matches(
    "physics/ode/ode.cpp"
    "bool[ \t]+__cdecl[ \t]+ODE_Init[ \t]*\\([^)]*\\)[ \t\r\n]*\\{"
    "ODE pool initialization must report failure")
require_pool_source_not_contains(
    "physics/ode/ode.cpp"
    "*(void **)ff"
    "ODE leak traversal must not dereference raw link storage")
require_pool_source_not_contains(
    "physics/ode/ode.cpp"
    "(dxGeomTransform*)ff -"
    "ODE leak traversal must not subtract pointers into fabricated object arrays")

require_pool_source_contains(
    "EffectsCore/fx_archive.cpp"
    "Pool_GetFreeCount("
    "FX archive capacity must fail closed on invalid physics pool state")
require_pool_source_contains(
    "physics/ode/collision_kernel.cpp"
    "ODE_CollisionGeomPoolStorage()"
    "ODE collision allocation must retain checked geom bounds")
require_pool_source_match_count(
    "physics/ode/collision_kernel.cpp"
    "ODE_CollisionGeomPoolStorage\\(\\)"
    4
    "the geom descriptor definition plus both frees and allocation must remain")
require_pool_source_match_count(
    "physics/ode/ode.cpp"
    "ODE_BodyPoolStorage\\(\\)"
    6
    "the body descriptor must cover leak count, init, allocation, and both frees")

message(STATUS "Native-width pool source invariants verified")
