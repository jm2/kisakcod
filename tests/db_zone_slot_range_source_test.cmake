cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_registry_path "${SOURCE_ROOT}/src/database/db_registry.cpp")
set(_asset_header_path "${SOURCE_ROOT}/src/xanim/xanim.h")
set(_shared_header_path "${SOURCE_ROOT}/src/universal/q_shared.h")
set(_shared_source_path "${SOURCE_ROOT}/src/universal/q_shared.cpp")
set(_info_helper_path "${SOURCE_ROOT}/src/universal/info_string.h")
set(_dvar_source_path "${SOURCE_ROOT}/src/universal/dvar.cpp")
set(_qcommon_header_path "${SOURCE_ROOT}/src/qcommon/qcommon.h")
set(_files_source_path "${SOURCE_ROOT}/src/universal/com_files.cpp")
set(_mp_server_path "${SOURCE_ROOT}/src/server_mp/sv_main_mp.cpp")
set(_sp_init_path "${SOURCE_ROOT}/src/server/sv_init.cpp")
set(_sp_server_path "${SOURCE_ROOT}/src/server/sv_main.cpp")
set(_zone_slots_path "${SOURCE_ROOT}/src/database/db_zone_slots.h")
set(_helper_path "${SOURCE_ROOT}/src/database/db_referenced_fastfile.h")
set(_fixture_path "${SOURCE_ROOT}/tests/db_referenced_fastfile_tests.cpp")
set(_manifest_path "${SOURCE_ROOT}/scripts/common_files.cmake")
set(_tests_path "${SOURCE_ROOT}/tests/CMakeLists.txt")
set(_ci_path "${SOURCE_ROOT}/.github/workflows/ci.yml")

foreach(_path IN ITEMS
    "${_registry_path}"
    "${_asset_header_path}"
    "${_shared_header_path}"
    "${_shared_source_path}"
    "${_info_helper_path}"
    "${_dvar_source_path}"
    "${_qcommon_header_path}"
    "${_files_source_path}"
    "${_mp_server_path}"
    "${_sp_init_path}"
    "${_sp_server_path}"
    "${_zone_slots_path}"
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
file(READ "${_shared_header_path}" _shared_header)
file(READ "${_shared_source_path}" _shared_source)
file(READ "${_info_helper_path}" _info_helper)
file(READ "${_dvar_source_path}" _dvar_source)
file(READ "${_qcommon_header_path}" _qcommon_header)
file(READ "${_files_source_path}" _files_source)
file(READ "${_mp_server_path}" _mp_server)
file(READ "${_sp_init_path}" _sp_init)
file(READ "${_sp_server_path}" _sp_server)
file(READ "${_zone_slots_path}" _zone_slots)
file(READ "${_helper_path}" _helper)
file(READ "${_fixture_path}" _fixture)
file(READ "${_manifest_path}" _manifest)
file(READ "${_tests_path}" _tests)
file(READ "${_ci_path}" _ci)

foreach(_var IN ITEMS
    _registry
    _asset_header
    _shared_header
    _shared_source
    _info_helper
    _dvar_source
    _qcommon_header
    _files_source
    _mp_server
    _sp_init
    _sp_server
    _zone_slots
    _helper
    _fixture
    _manifest
    _tests
    _ci)
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

# The neutral slot header, not the unrelated asset-type enum, owns the physical
# registry range and the reserved/default-to-usable relationship.
require_contains(
    _asset_header
    "ASSET_TYPE_COUNT = 0x21,"
    "the independent retail asset-type enum remains ABI-unchanged")
foreach(_forbidden IN ITEMS
    "database.h"
    "db_registry"
    "xanim/"
    "qcommon/"
    "XZone"
    "ASSET_TYPE_COUNT"
    "std::string"
    "std::vector"
    "malloc("
    "calloc("
    "realloc("
    "operator new")
    require_not_contains(
        _zone_slots "${_forbidden}" "zone-slot truth remains neutral")
endforeach()
foreach(_marker IN ITEMS
    "namespace db::zone_slots"
    "inline constexpr std::size_t kDefaultZoneSlot = 0;"
    "inline constexpr std::size_t kFirstUsableZoneSlot = 1;"
    "inline constexpr std::size_t kUsableZoneSlotCount = 32;"
    "inline constexpr std::size_t kPhysicalZoneSlotCount = 33;"
    "kFirstUsableZoneSlot == kDefaultZoneSlot + 1"
    "kPhysicalZoneSlotCount == kFirstUsableZoneSlot + kUsableZoneSlotCount"
    "constexpr bool IsUsableZoneSlot(const std::size_t slot) noexcept"
    "slot >= kFirstUsableZoneSlot"
    "slot < kPhysicalZoneSlotCount")
    require_contains(
        _zone_slots "${_marker}" "canonical neutral zone-slot contract")
endforeach()
foreach(_marker IN ITEMS
    "#include \"db_zone_slots.h\""
    "XZone g_zones[db::zone_slots::kPhysicalZoneSlotCount]{ 0 };"
    "uint8_t g_zoneHandles[db::zone_slots::kUsableZoneSlotCount];"
    "ARRAY_COUNT(g_zones) == db::zone_slots::kPhysicalZoneSlotCount"
    "ARRAY_COUNT(g_zoneHandles) == db::zone_slots::kUsableZoneSlotCount"
    "sizeof(g_zones) <= static_cast<std::size_t>(INT_MAX)"
    "sizeof(g_zoneHandles) <= static_cast<std::size_t>(INT_MAX)"
    "sizeof(g_zoneNameList) <= static_cast<std::size_t>(INT_MAX)"
    "g_zones, static_cast<int>(sizeof(g_zones)), \"g_zones\", 10);"
    "static_cast<int>(sizeof(g_zoneHandles))"
    "static_cast<int>(sizeof(g_zoneNameList))")
    require_contains(
        _registry "${_marker}" "registry storage consumes canonical slot truth")
endforeach()
require_contains(
    _registry
    "static_assert(ARRAY_COUNT(g_zones) == ARRAY_COUNT(g_zoneHandles) + 1);"
    "the reserved-slot relationship is compile-time checked")
foreach(_forbidden IN ITEMS
    "g_zones[ASSET_TYPE_COUNT]"
    "g_zoneHandles[32]")
    require_not_contains(
        _registry "${_forbidden}" "registry extents cannot recouple to magic values")
endforeach()
require_contains(
    _shared_header
    "#define BIG_INFO_VALUE 8192"
    "SYSTEMINFO values retain their 8192-byte protocol capacity")
require_contains(
    _registry
    "char g_zoneNameList[BIG_INFO_VALUE];"
    "referenced fast-file output uses the SYSTEMINFO value capacity")
require_contains(
    _registry
    "g_zoneNameList, static_cast<int>(sizeof(g_zoneNameList)), \"g_zoneNameList\", 10);"
    "static allocation tracking follows the expanded native buffer")
require_not_contains(
    _registry
    "g_zoneNameList[2080]"
    "the saturating legacy output buffer cannot return")
require_not_contains(
    _registry
    "fs_gameDirVar->current.integer"
    "semantic string dvars must never be read through their 32-bit integer member")

# The executable walk/formatter remains header-only and structural. It must not
# import the registry, XZone, dvar, qcommon strings, or a dynamic container.
foreach(_forbidden IN ITEMS
    "database.h"
    "db_registry"
    "xanim/"
    "qcommon/"
    "XZone"
    "dvar_s"
    "std::string"
    "std::vector"
    "malloc("
    "calloc("
    "realloc("
    "operator new")
    require_not_contains(
        _helper "${_forbidden}" "the range helper stays production-neutral")
endforeach()
require_contains(
    _helper
    "#include \"db_zone_slots.h\""
    "the formatter consumes canonical neutral slot truth")
require_contains(
    _helper
    "#include <universal/info_string.h>"
    "the database formatter consumes the shared token-safety primitive")
foreach(_marker IN ITEMS
    "static_assert(N == db::zone_slots::kPhysicalZoneSlotCount);"
    "for (std::size_t slot = db::zone_slots::kFirstUsableZoneSlot; slot < N; ++slot)"
    "if (zone.name[0] && !isExcluded(zone.name))"
    "visit(slot, zone);")
    require_contains(
        _helper "${_marker}" "the pure helper owns the exact usable-slot walk")
endforeach()
foreach(_forbidden IN ITEMS
    "kDefaultZoneSlot ="
    "kFirstFastFileZoneSlot"
    "kLiveFastFileZoneCount"
    "kZoneSlotCount")
    require_not_contains(
        _helper "${_forbidden}" "the formatter cannot own duplicate slot constants")
endforeach()

extract_slice(
    _helper
    "template <typename Integer>"
    "// Zone is intentionally structural"
    _decimal_formatter
    "FormatSignedDecimal")
foreach(_marker IN ITEMS
    "bool FormatSignedDecimal("
    "static_assert(std::is_integral_v<Integer>);"
    "static_assert(std::is_signed_v<Integer>);"
    "std::to_chars(output, output + capacity - 1, value, 10);"
    "if (result.ec != std::errc{})"
    "return false;"
    "return true;")
    require_contains(
        _decimal_formatter
        "${_marker}"
        "portable bounded signed-decimal formatting")
endforeach()
require_contains(
    _decimal_formatter
    [=[*result.ptr = '\0';]=]
    "signed decimal output reserves and writes its NUL")
require_not_contains(
    _decimal_formatter
    "_itoa"
    "MS-only integer conversion cannot return")

extract_slice(
    _helper
    "// The destination, zone names, and mod directory must not overlap."
    "} // namespace db::referenced_fastfile"
    _name_formatter
    "FormatReferencedFastFileNames")
foreach(_marker IN ITEMS
    "bool FormatReferencedFastFileNames("
    "std::array<SelectedName, db::zone_slots::kUsableZoneSlotCount> selected{};"
    "const std::size_t outputLimit = capacity - 1;"
    "detail::TryAccumulateLength("
    "selected[selectedCount++] = {"
    "if (!fits) return false;"
    "char *cursor = output;"
    "std::memcpy(cursor, part, length);"
    "return true;")
    require_contains(
        _name_formatter
        "${_marker}"
        "fixed-storage failure-atomic name formatting")
endforeach()
foreach(_marker IN ITEMS
    "info_string::IsSafeUnquotedPathTokenComponent(zone.name)"
    "!modDirectory"
    "!*modDirectory"
    "info_string::IsSafeUnquotedPathTokenComponent( modDirectory)")
    require_contains(
        _name_formatter
        "${_marker}"
        "selected zone and mod-directory tokens are validated before sizing")
endforeach()
require_contains(
    _name_formatter
    [=[*cursor = '\0';]=]
    "successful name output is explicitly terminated")
require_ordered(
    _name_formatter
    "ForEachReferencedFastFile("
    "if (!fits) return false;"
    "the complete selection is preflighted before failure returns")
require_ordered(
    _name_formatter
    "if (!fits) return false;"
    "char *cursor = output;"
    "no destination write occurs before successful preflight")
require_ordered(
    _name_formatter
    "append(modDirectory, modDirectoryLength);"
    "*cursor++ = '/';"
    "the mod directory precedes its slash")
require_ordered(
    _name_formatter
    "*cursor++ = '/';"
    "append(entry.name, entry.nameLength);"
    "the mod prefix precedes the zone name")

require_contains(
    _registry
    "#include \"db_referenced_fastfile.h\""
    "the registry uses the production-neutral helper")
require_contains(
    _manifest
    "database/db_referenced_fastfile.h"
    "the helper is part of the database source manifest")
require_contains(
    _manifest
    "database/db_zone_slots.h"
    "the canonical slot header is part of the database source manifest")
require_contains(
    _manifest
    "universal/info_string.h"
    "the shared protocol helper is part of the universal source manifest")

# The shared info-string primitives are dependency-free, allocation-free, and
# usable by both the engine and portable runtime tests.
foreach(_forbidden IN ITEMS
    "qcommon/"
    "database/"
    "dvar_s"
    "Com_Error"
    "Com_Printf"
    "std::string"
    "std::vector"
    "malloc("
    "calloc("
    "realloc("
    "operator new")
    require_not_contains(
        _info_helper "${_forbidden}" "the info helper remains engine-neutral")
endforeach()
foreach(_marker IN ITEMS
    "inline bool IsSafeUnquotedValueComponent("
    "if (!value) return false;"
    "*cursor <= static_cast<unsigned char>(' ')"
    "cursor[1] == static_cast<unsigned char>('/')"
    "cursor[1] == static_cast<unsigned char>('*')"
    "inline bool IsSafeUnquotedPathTokenComponent("
    "value[0] != '/'"
    "value[0] != '*'"
    "value[length - 1] != '/'"
    "inline bool HasExactKey("
    "std::memcmp(info, key, keyLength) == 0"
    "constexpr bool CanAppendPreformattedSuffix("
    "suffixLength < capacity - currentLength"
    [=[std::memchr(current, '\0', capacity)]=]
    "std::memmove( current + currentLength, suffix, suffixLength + 1);"
    "return true;")
    require_contains(
        _info_helper "${_marker}" "bounded shared info-string behavior")
endforeach()
require_contains(
    _info_helper
    "*cursor == static_cast<unsigned char>('\\\\')"
    "backslash is rejected as an info-string delimiter")
require_contains(
    _info_helper
    [=[*cursor == static_cast<unsigned char>(';')]=]
    "semicolon is rejected as a command delimiter")
require_contains(
    _info_helper
    [=[*cursor == static_cast<unsigned char>('"')]=]
    "quotes are rejected from unquoted tokens")
require_not_contains(
    _info_helper
    "strcat("
    "the shared append cannot use an unbounded string copy")

# The production big setter builds replacements in scratch storage, commits
# only after a strict NUL-reserving append, and reports every failure to its
# caller. Empty sanitized values intentionally commit key removal.
require_contains(
    _shared_header
    "bool __cdecl Info_SetValueForKey_Big(char *s, const char *key, const char *value);"
    "big info insertion exposes status")
extract_slice(
    _shared_source
    "bool __cdecl Info_SetValueForKey_Big"
    "bool __cdecl ParseConfigStringToStruct"
    _big_setter
    "Info_SetValueForKey_Big")
foreach(_marker IN ITEMS
    "bool valueComplete = false;"
    "valueComplete = true;"
    "if (!valueComplete)"
    "return false;"
    "hasCleanValue = cleanValue[0] != 0;"
    "Com_sprintf(newi, BIG_INFO_STRING, \"\\\\%s\\\\%s\", key, cleanValue);"
    "len <= 0 || len >= BIG_INFO_STRING"
    "memcpy(cleanValue, s, strlen(s) + 1);"
    "Info_RemoveKey_Big(cleanValue, key);"
    "if (!hasCleanValue)"
    "info_string::AppendPreformattedSuffix( cleanValue, BIG_INFO_STRING, newi)"
    "memcpy(s, cleanValue, strlen(cleanValue) + 1);"
    "return true;")
    require_contains(
        _big_setter "${_marker}" "complete transactional big-info replacement")
endforeach()
foreach(_marker IN ITEMS
    "c != '\\\\'"
    "c != ';'"
    "c != '\"'"
    "Can't use keys with a"
    "Info buffer length exceeded, not including key/value pair in response."
    "Info string length exceeded. key: %s value: %s Info string: %s")
    require_contains(
        _big_setter "${_marker}" "legacy sanitization and diagnostics remain")
endforeach()
require_ordered(
    _big_setter
    "Com_sprintf(newi, BIG_INFO_STRING"
    "memcpy(cleanValue, s, strlen(s) + 1);"
    "the replacement suffix is formatted before scratch is reused")
require_ordered(
    _big_setter
    "Info_RemoveKey_Big(cleanValue, key);"
    "info_string::AppendPreformattedSuffix( cleanValue, BIG_INFO_STRING, newi)"
    "the old pair is removed only from the candidate before append")
extract_slice(
    _big_setter
    "if (!info_string::AppendPreformattedSuffix("
    "return true;"
    _big_append_commit
    "successful big-info append commit")
require_ordered(
    _big_append_commit
    "info_string::AppendPreformattedSuffix( cleanValue, BIG_INFO_STRING, newi)"
    "memcpy(s, cleanValue, strlen(cleanValue) + 1);"
    "a successful append precedes destination commit")
foreach(_forbidden IN ITEMS
    "Info_RemoveKey_Big(s, key)"
    "strcat("
    "<= BIG_INFO_STRING")
    require_not_contains(
        _big_setter "${_forbidden}" "replacement and append stay failure-atomic")
endforeach()

# Big Dvar aggregation carries setter failure in per-call state while
# Dvar_ForEach owns the read lock. Reporting is deferred until after unlock.
foreach(_marker IN ITEMS
    "char *__cdecl Dvar_InfoString_Big(int bit);"
    "char *__cdecl Dvar_InfoString_Big(int bit, bool *complete);")
    require_contains(
        _qcommon_header "${_marker}" "legacy and checked big-info APIs coexist")
endforeach()
extract_slice(
    _dvar_source
    "namespace { struct DvarInfoStringBigContext"
    "char *__cdecl Dvar_InfoString_Big(int bit, bool *complete)"
    _big_callback
    "Dvar_InfoStringSingle_Big")
foreach(_marker IN ITEMS
    "uint32_t flags; bool complete;"
    "if (!context.complete || (context.flags & dvar->flags) == 0) return;"
    "context.complete = Info_SetValueForKey_Big(info2, dvar->name, value);")
    require_contains(
        _big_callback "${_marker}" "each selected Dvar insertion propagates status")
endforeach()
require_not_contains(
    _big_callback
    "Com_Error("
    "the Dvar callback cannot longjmp while the read lock is held")
extract_slice(
    _dvar_source
    "char *__cdecl Dvar_InfoString_Big(int bit, bool *complete)"
    "char *__cdecl Dvar_InfoString_Big(int bit)"
    _checked_big_builder
    "checked Dvar_InfoString_Big")
foreach(_marker IN ITEMS
    "DvarInfoStringBigContext context{"
    "Dvar_ForEach(Dvar_InfoStringSingle_Big, &context);"
    "*complete = context.complete;"
    "return info2;")
    require_contains(
        _checked_big_builder "${_marker}" "checked aggregate build completion")
endforeach()
require_ordered(
    _checked_big_builder
    "Dvar_ForEach(Dvar_InfoStringSingle_Big, &context);"
    "*complete = context.complete;"
    "completion is exposed only after Dvar_ForEach unlocks")
require_not_contains(
    _checked_big_builder
    "Com_Error("
    "the checked builder itself never errors under iteration")
extract_slice(
    _dvar_source
    "char *__cdecl Dvar_InfoString_Big(int bit)"
    "void __cdecl Dvar_ForEach(void(__cdecl *callback)"
    _legacy_big_builder
    "legacy Dvar_InfoString_Big")
foreach(_marker IN ITEMS
    "Dvar_InfoString_Big(bit, &complete)"
    "if (!complete)"
    "Com_Error("
    "result[0] = 0;"
    "return result;")
    require_contains(
        _legacy_big_builder "${_marker}" "legacy callers fail closed after unlock")
endforeach()
require_ordered(
    _legacy_big_builder
    "Dvar_InfoString_Big(bit, &complete)"
    "result[0] = 0;"
    "legacy fail-closed clearing follows the completed checked build")
require_ordered(
    _legacy_big_builder
    "result[0] = 0;"
    "Com_Error("
    "legacy global output is cleared before ERR_DROP longjmps")
extract_slice(
    _dvar_source
    "void __cdecl Dvar_ForEach(void(__cdecl *callback)"
    "bool __cdecl CompareDvars"
    _dvar_foreach
    "Dvar_ForEach")
require_ordered(
    _dvar_foreach
    "Sys_LockRead(&g_dvarCritSect);"
    "callback(sortedDvars[dvarIter], userData);"
    "callbacks execute under the established read lock")
require_ordered(
    _dvar_foreach
    "callback(sortedDvars[dvarIter], userData);"
    "Sys_UnlockRead(&g_dvarCritSect);"
    "iteration unlocks after every callback returns")

# The filesystem domain consumes the native pointer lane and rejects every
# delimiter, tokenizer comment, traversal, and noncanonical mod path.
extract_slice(
    _files_source
    "bool __cdecl FS_GameDirDomainFunc"
    "void FS_RegisterDvars"
    _game_dir_domain
    "FS_GameDirDomainFunc")
foreach(_marker IN ITEMS
    "const char *const gameDir = newValue.string;"
    "if (!gameDir) return false;"
    "if (!*gameDir) return true;"
    "info_string::IsSafeUnquotedPathTokenComponent(gameDir)"
    "I_strnicmp(gameDir, \"mods\", 4)"
    "gameDir[4] != '/' || !gameDir[5]"
    "strstr(gameDir, \"..\")"
    "strstr(gameDir, \"::\")"
    "return true;")
    require_contains(
        _game_dir_domain "${_marker}" "native safe fs_game validation")
endforeach()
require_not_contains(
    _game_dir_domain
    "newValue.integer"
    "fs_game validation cannot truncate or forge the string pointer")

# Every SP/MP systeminfo publication consumes checked aggregation and returns
# before publication on failure. MP additionally verifies critical round trips
# and emits one canonical explicit-empty fs_game pair.
extract_slice(
    _sp_init
    "void SV_SaveSystemInfo()"
    "void __cdecl SV_SetExpectedHunkUsage"
    _sp_save_systeminfo
    "SP SV_SaveSystemInfo")
foreach(_marker IN ITEMS
    "Dvar_InfoString_Big(8, &complete)"
    "if (!complete)"
    "SYSTEMINFO cannot be represented within protocol limits"
    "I_strncpyz(str, systemInfo, 0x2000);"
    "SV_SetConfigstring(1, str);"
    "dvar_modifiedFlags &= ~8u;")
    require_contains(
        _sp_save_systeminfo "${_marker}" "checked SP initial systeminfo publication")
endforeach()
require_contains(
    _sp_save_systeminfo
    "return;"
    "SP initial failure exits before publication")
require_not_contains(
    _sp_save_systeminfo
    "Dvar_InfoString_Big(8)"
    "SP initial publication cannot use unchecked aggregation")
require_ordered(
    _sp_save_systeminfo
    "if (!complete)"
    "SV_SetConfigstring(1, str);"
    "SP publication follows aggregate completion")
require_ordered(
    _sp_save_systeminfo
    "SV_SetConfigstring(1, str);"
    "dvar_modifiedFlags &= ~8u;"
    "SP clears modification only after publication")
extract_slice(
    _sp_server
    "void __cdecl SV_PreFrame()"
    "int __cdecl SV_RunFrame"
    _sp_preframe
    "SP SV_PreFrame")
foreach(_marker IN ITEMS
    "Dvar_InfoString_Big(8, &complete)"
    "if (!complete)"
    "SYSTEMINFO cannot be represented within protocol limits"
    "SV_SetConfigstring(1u, v2);"
    "dvar_modifiedFlags &= ~8u;")
    require_contains(
        _sp_preframe "${_marker}" "checked SP modified systeminfo publication")
endforeach()
require_contains(
    _sp_preframe
    "return;"
    "SP modified failure exits before publication")
require_not_contains(
    _sp_preframe
    "Dvar_InfoString_Big(8)"
    "SP modified publication cannot use unchecked aggregation")
require_ordered(
    _sp_preframe
    "if (!complete)"
    "SV_SetConfigstring(1u, v2);"
    "SP modified publication follows aggregate completion")
require_ordered(
    _sp_preframe
    "SV_SetConfigstring(1u, v2);"
    "dvar_modifiedFlags &= ~8u;"
    "SP modified flag clears only after publication")

extract_slice(
    _mp_server
    "void __cdecl SV_SetSystemInfoConfig()"
    "void __cdecl SV_PreFrame()"
    _mp_systeminfo
    "MP SV_SetSystemInfoConfig")
foreach(_marker IN ITEMS
    "Dvar_InfoString_Big(8, &complete)"
    "if (!complete)"
    "Info_ValueForKey(v0, \"sv_referencedFFCheckSums\")"
    "sv_referencedFFCheckSums->current.string"
    "Info_ValueForKey(v0, \"sv_referencedFFNames\")"
    "sv_referencedFFNames->current.string"
    "I_strncpyz(dest, v0, 0x2000);"
    "!*fs_gameDirVar->current.string"
    "info_string::AppendPreformattedSuffix( dest, sizeof(dest), \"\\\\fs_game\\\\\")"
    "info_string::HasExactKey(dest, \"fs_game\")"
    "Info_ValueForKey(dest, \"fs_game\")"
    "fs_gameDirVar->current.string"
    "SV_SetConfigstring(1, dest);"
    "dvar_modifiedFlags &= ~8u;")
    require_contains(
        _mp_systeminfo "${_marker}" "complete verified MP systeminfo publication")
endforeach()
require_contains(
    _mp_systeminfo
    "return;"
    "MP representation failure exits before publication")
require_not_contains(
    _mp_systeminfo
    "Dvar_InfoString_Big(8)"
    "MP publication cannot use unchecked aggregation")
require_ordered(
    _mp_systeminfo
    "if (!complete)"
    "Info_ValueForKey(v0, \"sv_referencedFFCheckSums\")"
    "aggregate completion precedes field round trips")
require_ordered(
    _mp_systeminfo
    "Info_ValueForKey(v0, \"sv_referencedFFCheckSums\")"
    "Info_ValueForKey(v0, \"sv_referencedFFNames\")"
    "rotating Info_ValueForKey results are consumed in separate checks")
require_ordered(
    _mp_systeminfo
    "info_string::HasExactKey(dest, \"fs_game\")"
    "SV_SetConfigstring(1, dest);"
    "explicit fs_game presence is verified before publication")
require_ordered(
    _mp_systeminfo
    "SV_SetConfigstring(1, dest);"
    "dvar_modifiedFlags &= ~8u;"
    "MP clears modification only after verified publication")
foreach(_forbidden IN ITEMS
    "fs_gameDirVar->current.integer"
    "I_strncat(dest, 1024"
    "strlen(dest) + strlen")
    require_not_contains(
        _mp_systeminfo "${_forbidden}" "legacy truncating fs_game fallback")
endforeach()

extract_slice(
    _registry
    "void __cdecl DB_BuildOSPath_Mod"
    "bool __cdecl DB_ModFileExists"
    _mod_path
    "DB_BuildOSPath_Mod")
foreach(_marker IN ITEMS
    "if (!*fs_gameDirVar->current.string)"
    "string = fs_gameDirVar->current.string;"
    "Com_sprintf(filename, size, \"%s\\\\%s\\\\%s.ff\"")
    require_contains(
        _mod_path
        "${_marker}"
        "mod-path construction uses the native-width string dvar")
endforeach()
require_ordered(
    _mod_path
    "if (!*fs_gameDirVar->current.string)"
    "string = fs_gameDirVar->current.string;"
    "mod-path construction validates the string before using it")

extract_slice(
    _registry
    "bool __cdecl DB_ModFileExists"
    "static void DB_WaitForRecoveryAndClaimAssets"
    _mod_exists
    "DB_ModFileExists")
foreach(_marker IN ITEMS
    "if (!*fs_gameDirVar->current.string)"
    "return 0;"
    "DB_BuildOSPath_Mod(\"mod\", 0x100u, filename);")
    require_contains(
        _mod_exists
        "${_marker}"
        "mod-file probing uses native-width string truthiness")
endforeach()
require_ordered(
    _mod_exists
    "if (!*fs_gameDirVar->current.string)"
    "DB_BuildOSPath_Mod(\"mod\", 0x100u, filename);"
    "an empty mod directory is rejected before path construction")

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
    "db::referenced_fastfile::FormatSignedDecimal("
    "checksum reporting uses bounded portable integer conversion")
foreach(_marker IN ITEMS
    "zone.fileSize,"
    "zoneSizeStr,"
    "ARRAY_COUNT(zoneSizeStr)"
    "bool checksumFormatFailed = false;"
    "g_zoneNameList[0] = 0;"
    "Com_Error(ERR_DROP, \"Could not format a referenced fast-file size\");"
    "I_strncat(g_zoneNameList, BIG_INFO_VALUE, zoneSizeStr);")
    require_contains(
        _checksum_list
        "${_marker}"
        "portable checksum formatting and fail-closed publication")
endforeach()
foreach(_forbidden IN ITEMS "_itoa(" "itoa(" "2080")
    require_not_contains(
        _checksum_list
        "${_forbidden}"
        "nonportable or legacy-capacity checksum formatting")
endforeach()
require_contains(
    _name_list
    "db::referenced_fastfile::FormatReferencedFastFileNames("
    "name reporting uses the capacity-aware shared formatter")
require_contains(
    _name_list
    "fs_gameDirVar->current.string,"
    "mod-zone prefixes use the native-width dvar string pointer")
foreach(_marker IN ITEMS
    "g_zoneNameList,"
    "ARRAY_COUNT(g_zoneNameList)"
    "g_zoneNameList[0] = 0;"
    "Com_Error("
    "ERR_DROP,"
    "Referenced fast-file name list cannot be represented within SYSTEMINFO protocol limits")
    require_contains(
        _name_list
        "${_marker}"
        "oversized name lists fail explicitly and remain unpublished")
endforeach()
require_not_contains(
    _name_list
    "fs_gameDirVar->current.integer"
    "the dvar pointer cannot be truncated through its integer member")
foreach(_forbidden IN ITEMS "I_strncat(" "2080")
    require_not_contains(
        _name_list
        "${_forbidden}"
        "name reporting cannot truncate incrementally")
endforeach()
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
    "#include \"database/db_zone_slots.h\""
    "void TestZoneSlotConstants()"
    "static_assert(kDefaultZoneSlot == 0);"
    "static_assert(kFirstUsableZoneSlot == 1);"
    "static_assert(kUsableZoneSlotCount == 32);"
    "static_assert(kPhysicalZoneSlotCount == 33);"
    "static_assert(!IsUsableZoneSlot(kDefaultZoneSlot));"
    "static_assert(IsUsableZoneSlot(kPhysicalZoneSlotCount - 1));"
    "!IsUsableZoneSlot((std::numeric_limits<std::size_t>::max)())"
    "ZoneFixture zones[db::zone_slots::kPhysicalZoneSlotCount]{};"
    "zones[db::zone_slots::kDefaultZoneSlot]"
    "zones[db::zone_slots::kPhysicalZoneSlotCount - 2]"
    "zones[db::zone_slots::kPhysicalZoneSlotCount - 1]"
    "visited[1] == 31"
    "visited[2] == 32"
    "visitedFileSizes == std::array<std::int32_t, 3>{-1, 31, 32}"
    "FormatSignedDecimal("
    "std::strcmp(exact.data(), \"-2147483648\") == 0"
    "exact.back() == "
    "sizeof(expected) - 1"
    "tooSmall == unchanged"
    "emptyOutput[0] == "
    "zones[db::zone_slots::kPhysicalZoneSlotCount - 1].name = \"localized_slot32\";"
    "std::strcmp(localizedOutput.data(), \"mods/example/slot31\") == 0"
    "constexpr std::size_t kLegacyCapacity = 2080;"
    "constexpr std::size_t kSystemInfoCapacity = 8192;"
    "db::zone_slots::kUsableZoneSlotCount * (kModPrefixLength + kZoneNameLength)"
    "legacyOutput == unchangedLegacyOutput"
    "the complete 32-zone mod list must fit the SYSTEMINFO value buffer"
    "std::strlen(systemInfoOutput.data()) == kExpectedLength"
    "systemInfoOutput[kExpectedLength] == "
    "void TestInfoStringPredicates()"
    "IsSafeUnquotedValueComponent(nullptr)"
    "bad//value"
    "bad/*value"
    "void TestRejectedNameComponents()"
    "invalid referenced-name input must leave output unchanged"
    "a mod zone requires a nonempty mod directory"
    "mods/example/"
    "void TestInfoStringAppend()"
    "an append ending at capacity minus one must succeed"
    "capacity one must represent an empty string and its terminator"
    "static_assert(!CanAppendPreformattedSuffix(maximum - 1, 1, maximum));"
    "static_assert(CanAppendPreformattedSuffix(maximum - 1, 0, maximum));"
    "void TestExactKeyDetection()"
    "an explicitly present empty value must retain key presence"
    "text appearing only as a value must not be reported as a key"
    "localized_unsafe name"
    "an excluded localized name need not be token-safe"
    "void TestSystemInfoSuffixAssembly()"
    "representable referenced pairs must assemble without alteration"
    "constexpr std::size_t kSystemInfoCapacity = 8192;"
    "an aggregate reaching exactly 8192 bytes must be rejected"
    "aggregate == unchangedAggregate")
    require_contains(
        _fixture
        "${_marker}"
        "portable range, conversion, saturation, and atomicity coverage")
endforeach()
foreach(_forbidden IN ITEMS
    "kFirstFastFileZoneSlot"
    "kLiveFastFileZoneCount"
    "kZoneSlotCount")
    require_not_contains(
        _fixture "${_forbidden}" "runtime tests use canonical neutral slot names")
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
    "if (*fs_gameDirVar->current.string && DB_ShouldLoadFromModDir(zoneName))"
    "DB_BuildOSPath_Mod(zoneName, 256, filename);")
    require_contains(
        _allocation
        "${_marker}"
        "mod fast-file loading uses native-width string truthiness")
endforeach()
require_ordered(
    _allocation
    "if (*fs_gameDirVar->current.string && DB_ShouldLoadFromModDir(zoneName))"
    "DB_BuildOSPath_Mod(zoneName, 256, filename);"
    "mod fast-file loading validates the native string before path construction")
foreach(_marker IN ITEMS
    "g_zoneIndex = 0;"
    "for (i = static_cast<int32_t>(db::zone_slots::kFirstUsableZoneSlot); i < static_cast<int32_t>(db::zone_slots::kPhysicalZoneSlotCount); ++i)"
    "g_zoneIndex = i;"
    "if (!db::zone_slots::IsUsableZoneSlot(g_zoneIndex))"
    "static_cast<int32_t>( db::zone_slots::kUsableZoneSlotCount)"
    "g_zoneHandles[g_zoneCount] = g_zoneIndex;")
    require_contains(
        _allocation "${_marker}" "allocation is limited to live slots 1 through 32")
endforeach()
foreach(_forbidden IN ITEMS
    "for (i = 1; i < 0x21; ++i)"
    "g_zoneCount >= 32")
    require_not_contains(
        _allocation "${_forbidden}" "allocation cannot regress to magic slot bounds")
endforeach()

extract_slice(
    _registry
    "void __cdecl DB_LoadXZone"
    "void __cdecl DB_LoadZone_f"
    _queue
    "DB_LoadXZone")
require_contains(
    _queue
    "static_cast<int32_t>(db::zone_slots::kUsableZoneSlotCount)"
    "the live-zone count is capped at 32")
require_contains(
    _queue
    "static_cast<int32_t>(db::zone_slots::kUsableZoneSlotCount) - g_zoneCount"
    "queued loads cannot exceed the remaining live-zone capacity")
foreach(_forbidden IN ITEMS
    "g_zoneCount >= 32"
    "static_cast<uint32_t>(32 - g_zoneCount)")
    require_not_contains(
        _queue "${_forbidden}" "queue admission cannot use magic slot capacity")
endforeach()

extract_slice(
    _registry
    "bool __cdecl DB_IsXAssetDefault"
    "int32_t __cdecl DB_GetAllXAssetOfType_FastFile"
    _default_check
    "DB_IsXAssetDefault")
require_contains(
    _default_check
    "return assetEntry->entry.zoneIndex == db::zone_slots::kDefaultZoneSlot;"
    "zone slot zero remains the default-asset sentinel")

extract_slice(
    _registry
    "void __cdecl DB_UnloadXZone(uint32_t zoneIndex"
    "void __cdecl DB_ShutdownXAssets"
    _unload
    "DB_UnloadXZone")
require_contains(
    _unload
    "iassert(db::zone_slots::IsUsableZoneSlot(zoneIndex));"
    "the reserved/default slot cannot be unloaded as a live zone")
