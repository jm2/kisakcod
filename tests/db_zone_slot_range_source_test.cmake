cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_registry_path "${SOURCE_ROOT}/src/database/db_registry.cpp")
set(_asset_header_path "${SOURCE_ROOT}/src/xanim/xanim.h")

foreach(_path IN ITEMS "${_registry_path}" "${_asset_header_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing zone-slot range source: ${_path}")
    endif()
endforeach()

file(READ "${_registry_path}" _registry)
file(READ "${_asset_header_path}" _asset_header)

foreach(_var IN ITEMS _registry _asset_header)
    string(REGEX REPLACE "[ \t\r\n]+" " " _normalized "${${_var}}")
    set(${_var} "${_normalized}")
endforeach()

function(require_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing zone-slot invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_not_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden zone-slot regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(extract_slice SOURCE_VAR START END OUT_VAR DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${START}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR "Missing start of ${DESCRIPTION}: '${START}'")
    endif()
    string(SUBSTRING "${${SOURCE_VAR}}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${END}" _end)
    if(_end LESS_EQUAL 0)
        message(FATAL_ERROR "Missing ordered end of ${DESCRIPTION}: '${END}'")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_end} _slice)
    set(${OUT_VAR} "${_slice}" PARENT_SCOPE)
endfunction()

# The physical zone table has one reserved/default entry plus the 32 live
# fast-file entries tracked by g_zoneHandles. This keeps the ABI-sized storage
# unchanged while making the usable range explicit.
require_contains(
    _asset_header
    "ASSET_TYPE_COUNT = 0x21,"
    "the backing-zone count remains 33")
require_contains(
    _registry
    "XZone g_zones[ASSET_TYPE_COUNT]{ 0 }; uint8_t g_zoneHandles[32];"
    "33 physical slots back a 32-entry live-zone table")
require_contains(
    _registry
    "static_assert(ARRAY_COUNT(g_zones) == ARRAY_COUNT(g_zoneHandles) + 1);"
    "the reserved-slot relationship is compile-time checked")

extract_slice(
    _registry
    "char *__cdecl DB_ReferencedFFChecksums()"
    "char *__cdecl DB_ReferencedFFNameList()"
    _checksum_list
    "DB_ReferencedFFChecksums")
extract_slice(
    _registry
    "char *__cdecl DB_ReferencedFFNameList()"
    "void __cdecl Hunk_OverrideDataForFile"
    _name_list
    "DB_ReferencedFFNameList")

# ARRAY_COUNT(g_zones) is 33, so both walks visit exactly 1..32: slot zero is
# excluded and the highest usable slot is no longer silently omitted.
foreach(_var IN ITEMS _checksum_list _name_list)
    require_contains(
        ${_var}
        "for (i = 1; i < static_cast<int32_t>(ARRAY_COUNT(g_zones)); ++i)"
        "referenced fast-file reporting covers slots 1 through 32")
    require_not_contains(
        ${_var}
        "for (i = 0;"
        "reserved slot zero cannot enter referenced fast-file reporting")
endforeach()

extract_slice(
    _registry
    "int32_t __cdecl DB_TryLoadXFileInternal"
    "void __cdecl DB_BuildOSPath"
    _allocation
    "DB_TryLoadXFileInternal")
foreach(_marker IN ITEMS
    "g_zoneIndex = 0;"
    "for (i = 1; i < 0x21; ++i)"
    "g_zoneIndex = i;"
    "if (!g_zoneIndex)"
    "g_zoneCount < 0 || g_zoneCount >= 32"
    "g_zoneHandles[g_zoneCount] = g_zoneIndex;")
    require_contains(
        _allocation "${_marker}" "allocation is limited to live slots 1 through 32")
endforeach()

extract_slice(
    _registry
    "void __cdecl DB_LoadXZone"
    "void __cdecl DB_LoadZone_f"
    _queue
    "DB_LoadXZone")
require_contains(
    _queue
    "g_zoneCount < 0 || g_zoneCount >= 32"
    "the live-zone count is capped at 32")
require_contains(
    _queue
    "zoneInfoCount > static_cast<uint32_t>(32 - g_zoneCount)"
    "queued loads cannot exceed the remaining live-zone capacity")

extract_slice(
    _registry
    "bool __cdecl DB_IsXAssetDefault"
    "int32_t __cdecl DB_GetAllXAssetOfType_FastFile"
    _default_check
    "DB_IsXAssetDefault")
require_contains(
    _default_check
    "return assetEntry->entry.zoneIndex == 0;"
    "zone slot zero remains the default-asset sentinel")

extract_slice(
    _registry
    "void __cdecl DB_UnloadXZone(uint32_t zoneIndex"
    "void __cdecl DB_ShutdownXAssets"
    _unload
    "DB_UnloadXZone")
require_contains(
    _unload
    "iassert(zoneIndex);"
    "the reserved/default slot cannot be unloaded as a live zone")
