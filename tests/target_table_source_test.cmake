cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

function(read_normalized PATH OUT_VARIABLE DESCRIPTION)
    if(NOT EXISTS "${PATH}")
        message(FATAL_ERROR "Missing target-table ${DESCRIPTION}: ${PATH}")
    endif()
    file(READ "${PATH}" _source)
    string(REGEX REPLACE "[ \t\r\n]+" " " _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(require_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing target-table invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden target-table regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(extract_slice SOURCE_VARIABLE START END OUT_VARIABLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${START}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR "Missing start of ${DESCRIPTION}: '${START}'")
    endif()
    string(SUBSTRING "${${SOURCE_VARIABLE}}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${END}" _end)
    if(_end LESS_EQUAL 0)
        message(FATAL_ERROR "Missing ordered end of ${DESCRIPTION}: '${END}'")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_end} _slice)
    set(${OUT_VARIABLE} "${_slice}" PARENT_SCOPE)
endfunction()

read_normalized(
    "${SOURCE_ROOT}/src/bgame/bg_target_protocol.h" _protocol
    "shared wire protocol")
read_normalized(
    "${SOURCE_ROOT}/src/game/g_target_table.h" _layout
    "server native layout")
read_normalized(
    "${SOURCE_ROOT}/src/game/g_local.h" _local
    "SP declarations")
read_normalized(
    "${SOURCE_ROOT}/src/game/g_targets.cpp" _targets
    "SP implementation")
read_normalized(
    "${SOURCE_ROOT}/src/game/g_weapon.cpp" _weapon
    "weapon-lock consumer")
read_normalized(
    "${SOURCE_ROOT}/tests/target_table_tests.cpp" _fixture
    "runtime fixture")
read_normalized(
    "${SOURCE_ROOT}/tests/CMakeLists.txt" _tests
    "portable test manifest")
read_normalized(
    "${SOURCE_ROOT}/tests/pointer_truncation.allow" _pointer_allow
    "pointer-truncation allowlist")
read_normalized(
    "${SOURCE_ROOT}/scripts/common_files.cmake" _common_manifest
    "common source manifest")
read_normalized(
    "${SOURCE_ROOT}/scripts/sp/sp_files.cmake" _sp_manifest
    "SP source manifest")
read_normalized(
    "${SOURCE_ROOT}/.github/workflows/ci.yml" _workflow
    "CI workflow")

foreach(_required IN ITEMS
    "namespace bg::target_protocol"
    "constexpr int kMaxTargets = 32;"
    "constexpr int kNoMaterial = -1;"
    "constexpr int kMaxMaterialIndex = 127;"
    "constexpr int kAttackProfileTop = 1;"
    "constexpr int kJavelinOnly = 2;"
    "enum class ConfigParseError"
    "inline ConfigParseError ParseConfig("
    "info_string::IsWellFormed(info)"
    "info_string::TryGetExactValueView(info, key, value, length)"
    "std::from_chars(value, end, parsed, 10)"
    "std::from_chars( value, end, parsed[component], std::chars_format::general)"
    "!std::isfinite(parsed[component])"
    "parsed.entityNumber < 0"
    "parsed.entityNumber >= maxEntityCount"
    "parsed.materialIndex < kNoMaterial"
    "parsed.materialIndex > kMaxMaterialIndex"
    "(parsed.flags & ~kKnownFlags) != 0"
    "*output = parsed;"
    "inline const char *ConfigParseErrorName(")
    require_contains(_protocol "${_required}" "shared bounded wire parser")
endforeach()

foreach(_forbidden IN ITEMS
    "g_local.h"
    "SV_GetConfigstring"
    "SV_SetConfigstring"
    "Scr_Error"
    "Com_Error"
    "malloc("
    "calloc("
    "operator new")
    forbid_contains(
        _protocol "${_forbidden}" "protocol must remain engine-neutral")
endforeach()

foreach(_required IN ITEMS
    "#include <bgame/bg_target_protocol.h>"
    "struct target_t"
    "gentity_s *ent;"
    "RUNTIME_SIZE(target_t, 0x1C, 0x20);"
    "RUNTIME_OFFSET(target_t, offset, 0x4, 0x8);"
    "RUNTIME_OFFSET(target_t, flags, 0x18, 0x1C);"
    "target_t targets[bg::target_protocol::kMaxTargets];"
    "RUNTIME_SIZE(TargetGlob, 0x384, 0x408);"
    "RUNTIME_OFFSET(TargetGlob, targetCount, 0x380, 0x400);")
    require_contains(_layout "${_required}" "server native layout")
endforeach()
foreach(_forbidden IN ITEMS
    "ConfigParseError"
    "ParsedConfig"
    "ParseConfig(")
    forbid_contains(
        _layout "${_forbidden}" "wire parser must live in shared bgame")
endforeach()

require_contains(
    _local "#include \"g_target_table.h\""
    "SP declarations consume the shared native-layout type")
forbid_contains(
    _local "struct target_t"
    "the runtime layout must have one source of truth")
require_contains(
    _common_manifest "\${SRC_DIR}/bgame/bg_target_protocol.h"
    "the common BGame manifest owns the shared wire protocol")
require_contains(
    _sp_manifest "\${SRC_DIR}/game/g_target_table.h"
    "the SP manifest owns the server target-table layout")

extract_slice(
    _targets "void __cdecl G_LoadTargets()" "void __cdecl Scr_Target_SetShader()"
    _load "transactional target load")
foreach(_required IN ITEMS
    "StagedTarget staged[kMaxTargets]{};"
    "target_protocol::ParseConfig( configString, MAX_GENTITIES, &staged[targetIndex].config)"
    "if (!level.gentities[entityNumber].r.inuse)"
    "staged[previous].config.entityNumber == entityNumber"
    "Com_Error( ERR_DROP,"
    "return;"
    "for (target_t &target : targGlob.targets) ResetTargetEntry(&target);"
    "InitializeTargetEntry( &target, &level.gentities[source.entityNumber]);"
    "target.flags = source.flags;"
    "targGlob.targetCount = stagedCount;")
    require_contains(_load "${_required}" "validate-before-publish load")
endforeach()
string(FIND "${_load}" "target_protocol::ParseConfig(" _parse_position)
string(FIND "${_load}" "for (target_t &target : targGlob.targets)" _reset_position)
string(FIND "${_load}" "targGlob.targetCount = stagedCount;" _publish_position)
if(_parse_position EQUAL -1
    OR _reset_position LESS_EQUAL _parse_position
    OR _publish_position LESS_EQUAL _reset_position)
    message(FATAL_ERROR
        "Target-table load must validate all input before reset/publication")
endif()

extract_slice(
    _targets "int __cdecl GetTargetIdx(" "int __cdecl G_TargetGetOffset("
    _lookup "typed target lookup")
foreach(_required IN ITEMS
    "if (!ent) return kMaxTargets;"
    "for (int targetIndex = 0; targetIndex < kMaxTargets; ++targetIndex)"
    "targGlob.targets[targetIndex].ent == ent"
    "return kMaxTargets;")
    require_contains(_lookup "${_required}" "typed native-stride lookup")
endforeach()

foreach(_required IN ITEMS
    "target->ent->flags &= ~FL_TARGET;"
    "target->ent = nullptr;"
    "target->offset[0] = 0.0f;"
    "target->materialIndex = target_protocol::kNoMaterial;"
    "target->offscreenMaterialIndex = target_protocol::kNoMaterial;"
    "target->flags = 0;"
    "ent->flags |= FL_TARGET;"
    "if (!CanEncodeLegacyOffset(requestedOffset))"
    "static_cast<int>(target.offset[0])"
    "ResetTargetEntry(&targGlob.targets[targetIndex]);"
    "targGlob.targetCount = 0;"
    "target.flags = source.flags;")
    require_contains(_targets "${_required}" "full target lifecycle and wire symmetry")
endforeach()

foreach(_forbidden IN ITEMS
    "(unsigned int)&level.gentities"
    "*((unsigned int *)"
    "atol("
    "sscanf("
    "v1 += 7"
    "+= 28"
    "+ 28)"
    "0x380"
    "targets[32]")
    forbid_contains(_targets "${_forbidden}" "ILP32 raw-stride/pointer load")
endforeach()

foreach(_required IN ITEMS
    "const int lockedEntityNumber = ent->client->ps.weapLockedEntnum;"
    "lockedEntityNumber >= 0"
    "lockedEntityNumber < ENTITYNUM_WORLD"
    "level.gentities[lockedEntityNumber].r.inuse"
    "target = &level.gentities[lockedEntityNumber];"
    "target = NULL;"
    "Vec3Clear(offset);")
    require_contains(_weapon "${_required}" "release-safe weapon lock index")
endforeach()
forbid_contains(
    _weapon "level.gentities[ent->client->ps.weapLockedEntnum]"
    "weapon fire must not index before validating the lock entity")

forbid_contains(
    _pointer_allow "game/g_targets.cpp|"
    "the fixed target pointer truncation site must leave the debt ledger")

foreach(_required IN ITEMS
    "add_executable(kisakcod-target-table-tests"
    "target_table_tests.cpp"
    "NAME target-table-parse-and-layout-contracts"
    "NAME target-table-source-invariants"
    "target_table_source_test.cmake")
    require_contains(_tests "${_required}" "portable target-table test wiring")
endforeach()
foreach(_required IN ITEMS
    "#include \"bgame/bg_target_protocol.h\""
    "TestNativePointerLayout();"
    "TestCompleteAndDefaultConfigs();"
    "TestArgumentAndGrammarFailures();"
    "TestEntityFailures();"
    "TestOffsetFailures();"
    "TestScalarRangeFailures();"
    "parse failure must leave the destination unchanged")
    require_contains(_fixture "${_required}" "runtime parser/layout coverage")
endforeach()

require_contains(
    _workflow "kisakcod-target-table-tests"
    "measured Windows x86 builds the target-table runtime contract")
require_contains(
    _workflow "target-table-(parse-and-layout-contracts|source-invariants)"
    "measured Windows x86 runs both target-table contracts")

message(STATUS "Target-table source contract passed")
