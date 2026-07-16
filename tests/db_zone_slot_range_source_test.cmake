cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_registry_path "${SOURCE_ROOT}/src/database/db_registry.cpp")
set(_asset_header_path "${SOURCE_ROOT}/src/xanim/xanim.h")
set(_helper_path "${SOURCE_ROOT}/src/database/db_referenced_fastfile.h")
set(_fixture_path "${SOURCE_ROOT}/tests/db_referenced_fastfile_tests.cpp")
set(_manifest_path "${SOURCE_ROOT}/scripts/common_files.cmake")
set(_tests_path "${SOURCE_ROOT}/tests/CMakeLists.txt")
set(_ci_path "${SOURCE_ROOT}/.github/workflows/ci.yml")

foreach(_path IN ITEMS
    "${_registry_path}"
    "${_asset_header_path}"
    "${_helper_path}"
    "${_fixture_path}"
    "${_manifest_path}"
    "${_tests_path}"
    "${_ci_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing zone-slot range source: ${_path}")
    endif()
endforeach()

file(READ "${_registry_path}" _registry)
file(READ "${_asset_header_path}" _asset_header)
file(READ "${_helper_path}" _helper)
file(READ "${_fixture_path}" _fixture)
file(READ "${_manifest_path}" _manifest)
file(READ "${_tests_path}" _tests)
file(READ "${_ci_path}" _ci)

foreach(_var IN ITEMS _registry _asset_header _helper _fixture _manifest _tests _ci)
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

function(require_ordered SOURCE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${FIRST}" _first)
    string(FIND "${${SOURCE_VAR}}" "${SECOND}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered zone-slot invariant (${DESCRIPTION})")
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

# The executable walk/formatter remains header-only and structural. It must not
# import the registry, XZone, dvar, qcommon strings, or a dynamic container.
foreach(_forbidden IN ITEMS
    "database.h"
    "db_registry"
    "xanim/"
    "qcommon/"
    "universal/"
    "XZone"
    "dvar_s"
    "std::string"
    "std::vector")
    require_not_contains(
        _helper "${_forbidden}" "the range helper stays production-neutral")
endforeach()
foreach(_marker IN ITEMS
    "inline constexpr std::size_t kDefaultZoneSlot = 0;"
    "inline constexpr std::size_t kFirstFastFileZoneSlot = 1;"
    "inline constexpr std::size_t kLiveFastFileZoneCount = 32;"
    "kFirstFastFileZoneSlot + kLiveFastFileZoneCount;"
    "static_assert(N == kZoneSlotCount);"
    "for (std::size_t slot = kFirstFastFileZoneSlot; slot < N; ++slot)"
    "if (zone.name[0] && !isExcluded(zone.name))"
    "visit(slot, zone);"
    "void EmitReferencedFastFileNames("
    "bool emitted = false;")
    require_contains(
        _helper "${_marker}" "the pure helper owns the exact usable-slot walk")
endforeach()
require_ordered(
    _helper "if (emitted) emit(\" \");" "if (zone.modZone)"
    "inter-zone separators precede the next name")
require_ordered(
    _helper "if (zone.modZone)" "emit(modDirectory);"
    "mod-zone formatting begins with the requested directory")
require_ordered(
    _helper "emit(modDirectory);" "emit(\"/\");"
    "the mod directory precedes its slash")
require_ordered(
    _helper "emit(\"/\");" "emit(zone.name);"
    "the mod prefix precedes the zone name")
require_ordered(
    _helper "emit(zone.name);" "emitted = true;"
    "the separator state publishes only after a name")

require_contains(
    _registry
    "#include \"db_referenced_fastfile.h\""
    "the registry uses the production-neutral helper")
require_contains(
    _manifest
    "database/db_referenced_fastfile.h"
    "the helper is part of the database source manifest")

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

# Both output paths use the executable 1..32 walk and retain the retail
# localized-prefix comparison. The native pointer member is required on 64-bit.
require_contains(
    _checksum_list
    "db::referenced_fastfile::ForEachReferencedFastFile("
    "checksum reporting uses the shared usable-slot walk")
require_contains(
    _checksum_list
    "_itoa(zone.fileSize, zoneSizeStr, 0xAu);"
    "checksum reporting retains retail decimal conversion")
require_contains(
    _name_list
    "db::referenced_fastfile::EmitReferencedFastFileNames("
    "name reporting uses the shared formatter")
require_contains(
    _name_list
    "fs_gameDirVar->current.string,"
    "mod-zone prefixes use the native-width dvar string pointer")
require_not_contains(
    _name_list
    "fs_gameDirVar->current.integer"
    "the dvar pointer cannot be truncated through its integer member")
foreach(_var IN ITEMS _checksum_list _name_list)
    require_contains(
        ${_var}
        "return I_strncmp(zoneName, \"localized_\", v0) == 0;"
        "retail localized fast-file exclusion is preserved")
    require_not_contains(
        ${_var}
        "for (i = 0;"
        "reserved slot zero cannot enter referenced fast-file reporting")
endforeach()

# Portable behavior coverage executes the production helper rather than a
# source-shaped reimplementation.
foreach(_marker IN ITEMS
    "zones[kDefaultZoneSlot] = {\"slot0-default\", false};"
    "zones[31] = {\"slot31\", false};"
    "zones[32] = {\"slot32\", false};"
    "visited[1] == 31"
    "visited[2] == 32"
    "output == \"mods/example/slot31 slot32\""
    "zones[32].name = \"localized_slot32\";"
    "output == \"mods/example/slot31\"")
    require_contains(
        _fixture "${_marker}" "portable range and formatting behavior coverage")
endforeach()
foreach(_marker IN ITEMS
    "add_executable(kisakcod-db-referenced-fastfile-tests db_referenced_fastfile_tests.cpp)"
    "target_compile_features(kisakcod-db-referenced-fastfile-tests PRIVATE cxx_std_20)"
    "kisakcod_test_warnings(kisakcod-db-referenced-fastfile-tests)"
    "NAME database-referenced-fastfile-format"
    "COMMAND kisakcod-db-referenced-fastfile-tests")
    require_contains(
        _tests "${_marker}" "portable behavior test registration")
endforeach()
foreach(_marker IN ITEMS
    "kisakcod-db-referenced-fastfile-tests"
    "database-referenced-fastfile-format")
    require_contains(
        _ci "${_marker}" "measured Windows x86 CI integration")
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
