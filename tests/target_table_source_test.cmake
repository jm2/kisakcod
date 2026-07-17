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
    "constexpr bool IsValidMaterialIndex("
    "materialIndex == kNoMaterial"
    "materialIndex > 0 && materialIndex <= kMaxMaterialIndex"
    "inline bool CanEncodeLegacyOffsetComponent("
    "std::isfinite(component)"
    "std::numeric_limits<int>::min"
    "std::numeric_limits<int>::max"
    "inline bool CanEncodeLegacyOffset("
    "inline bool TryEncodeLockOnDuration("
    "!std::isfinite(seconds)"
    "seconds < 0.0"
    "static_cast<float>(seconds) * 1000.0f"
    "!CanEncodeLegacyOffsetComponent(retailMilliseconds)"
    "*milliseconds = static_cast<int>(retailMilliseconds);"
    "enum class ConfigParseError"
    "duplicate recognized keys"
    "Unknown keys are ignored for forward wire compatibility."
    "inline ConfigParseError ParseConfig("
    "info_string::IsWellFormed(info)"
    "info_string::TryGetExactValueView(info, key, value, length)"
    "std::from_chars(value, end, parsed, 10)"
    "std::from_chars( value, end, parsed[component], std::chars_format::general)"
    "!CanEncodeLegacyOffsetComponent(parsed[component])"
    "parsed.entityNumber < 0"
    "parsed.entityNumber >= maxEntityCount"
    "!IsValidMaterialIndex(parsed.materialIndex)"
    "!IsValidMaterialIndex(parsed.offscreenMaterialIndex)"
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

extract_slice(
    _targets "void __cdecl Scr_Target_Set()" "bool Targ_Remove("
    _target_set "ordinary-entity target producer")
foreach(_required IN ITEMS
    "gentity_s *const ent = Scr_GetEntity(0);"
    "int entityNumber = 0;"
    "if (!IsOrdinaryLiveEntity(ent, &entityNumber))"
    "Scr_ObjectError(\"Target must be a live ordinary entity\");"
    "return;"
    "int targetIndex = GetTargetIdx(ent);"
    "AttachTargetEntry(&targGlob.targets[targetIndex], ent);"
    "Info_SetValueForKey(configString, \"ent\", va(\"%i\", entityNumber));")
    require_contains(
        _target_set "${_required}" "validated target producer domain")
endforeach()
string(FIND "${_target_set}" "IsOrdinaryLiveEntity(ent, &entityNumber)" _producer_validate)
string(FIND "${_target_set}" "GetTargetIdx(ent)" _producer_lookup)
string(FIND "${_target_set}" "AttachTargetEntry(" _producer_attach)
string(FIND "${_target_set}" "Info_SetValueForKey(configString, \"ent\"" _producer_wire)
if(_producer_validate EQUAL -1
    OR _producer_lookup LESS_EQUAL _producer_validate
    OR _producer_attach LESS_EQUAL _producer_lookup
    OR _producer_wire LESS_EQUAL _producer_attach)
    message(FATAL_ERROR
        "Target producer must validate and stage identity before publication")
endif()
forbid_contains(
    _target_set "va(\"%i\", ent->s.number)"
    "target producer must serialize the validated staged entity number")

extract_slice(
    _layout "inline void ClearTargetEntry(" "RUNTIME_SIZE(target_t,"
    _storage_clear "storage-only target initialization")
foreach(_required IN ITEMS
    "target->ent = nullptr;"
    "target->offset[0] = 0.0f;"
    "target->materialIndex = bg::target_protocol::kNoMaterial;"
    "target->offscreenMaterialIndex = bg::target_protocol::kNoMaterial;"
    "target->flags = 0;")
    require_contains(
        _storage_clear "${_required}" "complete storage-only clear")
endforeach()
forbid_contains(
    _storage_clear "target->ent->"
    "storage initialization must never dereference a stale entity pointer")
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
    _targets "void ClearLiveTargetFlags()"
    "bool IsPublishedMaterialIndex("
    _live_flag_clear "authoritative live target-flag reset")
foreach(_required IN ITEMS
    "entityNumber < level.num_entities"
    "entityNumber < ENTITYNUM_WORLD"
    "if (entity.r.inuse) entity.flags &= ~FL_TARGET;")
    require_contains(
        _live_flag_clear "${_required}" "bounded authoritative flag reset")
endforeach()

extract_slice(
    _targets "bool IsPublishedMaterialIndex("
    "bool IsOrdinaryLiveEntity("
    _material_lookup "registered target material lookup")
foreach(_required IN ITEMS
    "materialIndex == target_protocol::kNoMaterial"
    "target_protocol::IsValidMaterialIndex(materialIndex)"
    "CS_SERVER_MATERIALS + materialIndex"
    "return materialConfigString[0] != '\\0';")
    require_contains(
        _material_lookup "${_required}" "published material validation")
endforeach()

extract_slice(
    _targets "bool IsOrdinaryLiveEntity("
    "void __cdecl G_InitTargets()"
    _ordinary_entity "ordinary live entity resolver")
foreach(_required IN ITEMS
    "candidate >= level.num_entities"
    "candidate >= ENTITYNUM_WORLD"
    "gentity_s &liveEntity = level.gentities[candidate];"
    "&liveEntity != ent"
    "!liveEntity.r.inuse"
    "liveEntity.s.number != candidate")
    require_contains(
        _ordinary_entity "${_required}" "bounded ordinary entity identity")
endforeach()
string(FIND "${_ordinary_entity}" "candidate >= level.num_entities" _ordinary_bound)
string(FIND "${_ordinary_entity}" "level.gentities[candidate]" _ordinary_index)
if(_ordinary_bound EQUAL -1 OR _ordinary_index LESS_EQUAL _ordinary_bound)
    message(FATAL_ERROR
        "Ordinary entity resolution must range-check before indexing")
endif()

extract_slice(
    _targets "void __cdecl G_InitTargets()" "void __cdecl G_LoadTargets()"
    _init "stale-safe target initialization")
require_contains(
    _init "ClearTargetEntry(&targGlob.targets[i]);"
    "startup discards storage without dereferencing old pointers")
forbid_contains(
    _init "DetachTargetEntry("
    "startup must not detach through a stale entity pointer")

extract_slice(
    _targets "void __cdecl G_LoadTargets()" "void __cdecl Scr_Target_SetShader()"
    _load "transactional target load")
foreach(_required IN ITEMS
    "StagedTarget staged[kMaxTargets]{};"
    "level.num_entities > MAX_GENTITIES"
    "const int ordinaryEntityLimit = level.num_entities < ENTITYNUM_WORLD ? level.num_entities : ENTITYNUM_WORLD;"
    "target_protocol::ParseConfig( configString, ordinaryEntityLimit, &staged[targetIndex].config)"
    "entityNumber >= level.num_entities"
    "entityNumber >= ENTITYNUM_WORLD"
    "const gentity_s &entity = level.gentities[entityNumber];"
    "!entity.r.inuse || entity.s.number != entityNumber"
    "IsPublishedMaterialIndex(source.materialIndex)"
    "IsPublishedMaterialIndex(source.offscreenMaterialIndex)"
    "staged[previous].config.entityNumber == entityNumber"
    "Com_Error( ERR_DROP,"
    "return;"
    "ClearLiveTargetFlags();"
    "for (target_t &target : targGlob.targets) ClearTargetEntry(&target);"
    "AttachTargetEntry( &target, &level.gentities[source.entityNumber]);"
    "target.flags = source.flags;"
    "targGlob.targetCount = stagedCount;")
    require_contains(_load "${_required}" "validate-before-publish load")
endforeach()
string(FIND "${_load}" "target_protocol::ParseConfig(" _parse_position)
string(FIND "${_load}" "entityNumber >= level.num_entities" _live_bound_position)
string(FIND "${_load}" "const gentity_s &entity = level.gentities[entityNumber];" _entity_index_position)
string(FIND "${_load}" "ClearLiveTargetFlags();" _flag_reset_position)
string(FIND "${_load}" "for (target_t &target : targGlob.targets)" _storage_reset_position)
string(FIND "${_load}" "AttachTargetEntry(" _attach_position)
string(FIND "${_load}" "targGlob.targetCount = stagedCount;" _publish_position)
if(_parse_position EQUAL -1
    OR _live_bound_position LESS_EQUAL _parse_position
    OR _entity_index_position LESS_EQUAL _live_bound_position
    OR _flag_reset_position LESS_EQUAL _entity_index_position
    OR _storage_reset_position LESS_EQUAL _flag_reset_position
    OR _attach_position LESS_EQUAL _storage_reset_position
    OR _publish_position LESS_EQUAL _attach_position)
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

extract_slice(
    _targets "void __cdecl Scr_Target_SetShader()"
    "void __cdecl Scr_Target_SetOffscreenShader()"
    _shader "transactional target shader setter")
foreach(_required IN ITEMS
    "materialIndex = G_MaterialIndex(materialName);"
    "!target_protocol::IsValidMaterialIndex(materialIndex)"
    "!IsPublishedMaterialIndex(materialIndex)"
    "return;"
    "target.materialIndex = materialIndex;")
    require_contains(_shader "${_required}" "validated shader publication")
endforeach()
string(FIND "${_shader}" "materialIndex = G_MaterialIndex(materialName);" _shader_resolve)
string(FIND "${_shader}" "target.materialIndex = materialIndex;" _shader_publish)
if(_shader_resolve EQUAL -1 OR _shader_publish LESS_EQUAL _shader_resolve)
    message(FATAL_ERROR
        "Target shader must resolve and validate before live publication")
endif()

extract_slice(
    _targets "void __cdecl Scr_Target_SetOffscreenShader()"
    "void __cdecl Scr_Target_GetArray()"
    _offscreen_shader "transactional offscreen shader setter")
foreach(_required IN ITEMS
    "materialIndex = G_MaterialIndex(materialName);"
    "!target_protocol::IsValidMaterialIndex(materialIndex)"
    "!IsPublishedMaterialIndex(materialIndex)"
    "return;"
    "target.offscreenMaterialIndex = materialIndex;")
    require_contains(
        _offscreen_shader "${_required}" "validated offscreen publication")
endforeach()
string(FIND "${_offscreen_shader}" "materialIndex = G_MaterialIndex(materialName);" _offscreen_resolve)
string(FIND "${_offscreen_shader}" "target.offscreenMaterialIndex = materialIndex;" _offscreen_publish)
if(_offscreen_resolve EQUAL -1
    OR _offscreen_publish LESS_EQUAL _offscreen_resolve)
    message(FATAL_ERROR
        "Offscreen shader must resolve and validate before live publication")
endif()

foreach(_required IN ITEMS
    "target->ent->flags &= ~FL_TARGET;"
    "ClearTargetEntry(target);"
    "void ClearLiveTargetFlags()"
    "ent->flags |= FL_TARGET;"
    "if (!target_protocol::CanEncodeLegacyOffset(requestedOffset))"
    "static_cast<int>(target.offset[0])"
    "DetachTargetEntry(&targGlob.targets[targetIndex]);"
    "targGlob.targetCount = 0;"
    "target.flags = source.flags;")
    require_contains(_targets "${_required}" "full target lifecycle and wire symmetry")
endforeach()
foreach(_forbidden IN ITEMS
    "ResetTargetEntry("
    "InitializeTargetEntry(")
    forbid_contains(
        _targets "${_forbidden}" "stale-dereferencing lifecycle helper")
endforeach()

extract_slice(
    _targets "int __cdecl ScrGetTargetScreenPos("
    "void __cdecl Scr_Target_IsInCircle()"
    _screen_pos "target screen-position script validation")
require_contains(
    _screen_pos "if (Scr_GetNumParam() < 3)"
    "screen-position helper requires target, player, and FOV")

extract_slice(
    _targets "void __cdecl Scr_Target_StartLockOn()"
    "void __cdecl Scr_Target_ClearLockOn()"
    _start_lock "bounded lock-on command")
foreach(_required IN ITEMS
    "if (Scr_GetNumParam() < 2)"
    "return;"
    "IsOrdinaryLiveEntity(ent, &entityNumber)"
    "target_protocol::TryEncodeLockOnDuration( seconds, &milliseconds)"
    "Scr_ParamError( 1u,"
    "va(\"ret_lock_on %i %i\", entityNumber, milliseconds)")
    require_contains(_start_lock "${_required}" "validated lock-on wire command")
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
    "TestProducerEntityDomainModel();"
    "TestCompleteAndDefaultConfigs();"
    "TestArgumentAndGrammarFailures();"
    "TestEntityFailures();"
    "TestOffsetFailures();"
    "TestScalarRangeFailures();"
    "TestLockOnDurationEncoding();"
    "ClearTargetEntry(&target);"
    "std::uintptr_t{1}"
    "\\\\ent\\\\2174"
    "\\\\ent\\\\2175"
    "\\\\mat\\\\0"
    "\\\\offmat\\\\0"
    "\\\\offs\\\\2147483648"
    "quiet_NaN"
    "infinity"
    "the producer must reject WORLD"
    "the producer must reject NONE"
    "the producer must reject the live bound"
    "the producer must reject a non-inuse entity"
    "the producer must reject a stale pointer"
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
