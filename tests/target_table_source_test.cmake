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
read_normalized(
    "${SOURCE_ROOT}/src/universal/q_shared.h" _q_shared_header
    "info-string API")
read_normalized(
    "${SOURCE_ROOT}/src/universal/q_shared.cpp" _q_shared
    "info-string implementation")
read_normalized(
    "${SOURCE_ROOT}/src/universal/info_string.h" _info_string
    "dependency-light info-string contracts")
read_normalized(
    "${SOURCE_ROOT}/src/game/g_utils.cpp" _sp_utils
    "SP material registry")
read_normalized(
    "${SOURCE_ROOT}/src/game_mp/g_utils_mp.cpp" _mp_utils
    "MP material registry")

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
    "inline bool IsStrictDecimalFloatToken("
    "inline bool TryParseFloatToken("
    "inline bool TryParseFloatTokenFallback("
    "constexpr std::size_t kMaxNumericTokenLength = 1023;"
    "if (tokenLength == kMaxNumericTokenLength)"
    "token[tokenLength] = '\\0';"
    "newlocale(LC_NUMERIC_MASK, \"C\", nullptr)"
    "std::feholdexcept(&savedEnvironment)"
    "std::fesetround(FE_TONEAREST)"
    "const float parsed = strtof_l(token, &parsedEnd, locale.handle);"
    "const int restoreResult = std::fesetenv(&savedEnvironment);"
    "parseErrno == ERANGE && parsed == 0.0f"
    "if constexpr (requires(Float &candidate) { std::from_chars( value, end, candidate, std::chars_format::general); })"
    "std::from_chars( value, end, parsed, std::chars_format::general)"
    "!CanEncodeLegacyOffsetComponent(parsed)"
    "!TryParseFloatToken(value, tokenEnd, &parsed[component])"
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

require_contains(
    _q_shared_header
    "bool __cdecl Info_TrySetValueForKey(char *s, const char *key, const char *value);"
    "the checked regular setter must report publication failure")
require_contains(
    _q_shared_header
    "void __cdecl Info_SetValueForKey(char *s, const char *key, const char *value);"
    "the legacy regular setter must retain its ABI")
extract_slice(
    _q_shared "bool __cdecl Info_TrySetValueForKey("
    "bool __cdecl Info_SetValueForKey_Big("
    _regular_info_set "failure-atomic regular info-string setter")
foreach(_required IN ITEMS
    "char scratch[MAX_INFO_STRING];"
    "if (!info_string::TrySetValueForKey( s, MAX_INFO_STRING, scratch, sizeof(scratch), key, value))"
    "return false;"
    "return true;"
    "void __cdecl Info_SetValueForKey("
    "(void)Info_TrySetValueForKey(s, key, value);")
    require_contains(
        _regular_info_set "${_required}"
        "checked core delegation and legacy ABI wrapper")
endforeach()
foreach(_forbidden IN ITEMS
    "Info_RemoveKey(s, key)"
    "<= 0x400"
    "memcpy(&s[strlen(s)]")
    forbid_contains(
        _regular_info_set "${_forbidden}"
        "regular setter must not remove or append in published storage")
endforeach()

extract_slice(
    _info_string "inline bool TrySetValueForKey("
    "constexpr bool CanAppendPreformattedSuffix("
    _checked_info_core "dependency-light checked info setter")
foreach(_required IN ITEMS
    "current == scratch"
    "std::memchr(current, '\\0', capacity)"
    "!IsWellFormed(current)"
    "key[keyLength] == '\\\\'"
    "rawValueLength < capacity"
    "bool removedFirstMatch = false;"
    "const bool hadLeadingDelimiter = *current == '\\\\';"
    "removedFirstInputPair = firstInputPair;"
    "hasRetainedPair || hadLeadingDelimiter || removedFirstInputPair"
    "matches && !removedFirstMatch"
    "pairLength >= capacity - outputLength"
    "*output = '\\0';"
    "std::memmove(current, scratch, outputLength + 1);"
    "return true;")
    require_contains(
        _checked_info_core "${_required}"
        "bounded exact-key replacement core")
endforeach()
string(FIND "${_checked_info_core}" "std::memchr(current, '\\0', capacity)" _core_bound)
string(FIND "${_checked_info_core}" "pairLength >= capacity - outputLength" _core_capacity)
string(FIND "${_checked_info_core}" "std::memmove(current, scratch, outputLength + 1);" _core_commit)
if(_core_bound EQUAL -1
    OR _core_capacity LESS_EQUAL _core_bound
    OR _core_commit LESS_EQUAL _core_capacity)
    message(FATAL_ERROR
        "Checked info replacement must validate bounds before commit")
endif()

foreach(_utils_var IN ITEMS _sp_utils _mp_utils)
    extract_slice(
        ${_utils_var} "int __cdecl G_MaterialIndex("
        "int __cdecl G_ModelIndex("
        _material_index "bounded material-name copy")
    foreach(_required IN ITEMS
        "if (!name || !name[0])"
        "if (strlen(name) >= sizeof("
        "Com_Error( ERR_DROP,"
        "I_strncpyz("
        "I_strlwr(")
        require_contains(
            _material_index "${_required}"
            "material registry rejects overlong names before copying")
    endforeach()
    foreach(_forbidden IN ITEMS
        "do {"
        "v5 - name"
        "*v3++ = *v4++")
        forbid_contains(
            _material_index "${_forbidden}"
            "material registry must not use the legacy unbounded copy")
    endforeach()
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
    "char stagedConfigString[MAX_INFO_STRING]{};"
    "if (!BuildTargetConfig("
    "AttachTargetEntry(&targGlob.targets[targetIndex], ent);"
    "SV_SetConfigstring(CS_TARGETS + targetIndex, stagedConfigString);")
    require_contains(
        _target_set "${_required}" "validated target producer domain")
endforeach()
string(FIND "${_target_set}" "IsOrdinaryLiveEntity(ent, &entityNumber)" _producer_validate)
string(FIND "${_target_set}" "GetTargetIdx(ent)" _producer_lookup)
string(FIND "${_target_set}" "if (!BuildTargetConfig(" _producer_stage)
string(FIND "${_target_set}" "AttachTargetEntry(" _producer_attach)
string(FIND "${_target_set}" "SV_SetConfigstring(CS_TARGETS + targetIndex, stagedConfigString);" _producer_wire)
if(_producer_validate EQUAL -1
    OR _producer_lookup LESS_EQUAL _producer_validate
    OR _producer_stage LESS_EQUAL _producer_lookup
    OR _producer_attach LESS_EQUAL _producer_stage
    OR _producer_wire LESS_EQUAL _producer_attach)
    message(FATAL_ERROR
        "Target producer must validate and stage identity before publication")
endif()
forbid_contains(
    _target_set "va(\"%i\", ent->s.number)"
    "target producer must serialize the validated staged entity number")

extract_slice(
    _targets "int OrdinaryEntityLimit()" "void DetachTargetEntry("
    _target_wire_stage "failure-atomic target wire staging")
foreach(_required IN ITEMS
    "level.num_entities < ENTITYNUM_WORLD ? level.num_entities : ENTITYNUM_WORLD;"
    "target_protocol::ParseConfig( configString, OrdinaryEntityLimit(), parsed)"
    "parsed->entityNumber != expectedEntityNumber"
    "char currentConfigString[MAX_INFO_STRING];"
    "I_strncpyz( stagedConfigString, currentConfigString, MAX_INFO_STRING);"
    "!Info_TrySetValueForKey(stagedConfigString, key, valueString)"
    "return ValidateStagedTargetConfig( stagedConfigString, expectedEntityNumber, parsed);"
    "const int encodedOffset[3]"
    "!Info_TrySetValueForKey( configString, \"ent\", va(\"%i\", entityNumber))"
    "|| !Info_TrySetValueForKey( configString, \"flags\", va(\"%i\", flags))"
    "if (!ValidateStagedTargetConfig( configString, entityNumber, &parsed))")
    require_contains(
        _target_wire_stage "${_required}"
        "all target wire writes are checked before native publication")
endforeach()
extract_slice(
    _targets "bool StageTargetScalarConfig(" "bool BuildTargetConfig("
    _scalar_wire_stage "scalar target wire staging")
string(FIND "${_scalar_wire_stage}" "SV_GetConfigstring(" _wire_fetch)
string(FIND "${_scalar_wire_stage}" "I_strncpyz(" _wire_copy)
string(FIND "${_scalar_wire_stage}" "!Info_TrySetValueForKey(stagedConfigString" _wire_replace)
string(FIND "${_scalar_wire_stage}" "return ValidateStagedTargetConfig(" _wire_parse)
if(_wire_fetch EQUAL -1
    OR _wire_copy LESS_EQUAL _wire_fetch
    OR _wire_replace LESS_EQUAL _wire_copy
    OR _wire_parse LESS_EQUAL _wire_replace)
    message(FATAL_ERROR
        "Target scalar updates must fetch, stage, replace, then parse")
endif()

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
    "if (!ent) return;"
    "strlen(materialName) >= kSpMaterialNameCapacity"
    "Scr_ParamError(1u, \"Target shader name is too long\");"
    "materialIndex = G_MaterialIndex(materialName);"
    "!target_protocol::IsValidMaterialIndex(materialIndex)"
    "!IsPublishedMaterialIndex(materialIndex)"
    "return;"
    "if (!StageTargetScalarConfig("
    "|| parsed.materialIndex != materialIndex"
    "target.materialIndex = materialIndex;")
    require_contains(_shader "${_required}" "validated shader publication")
endforeach()
string(FIND "${_shader}" "gentity_s *const ent = Scr_GetEntity(0);" _shader_entity)
string(FIND "${_shader}" "if (!ent) return;" _shader_entity_guard)
string(FIND "${_shader}" "const int targetIndex = GetTargetIdx(ent);" _shader_lookup)
string(FIND "${_shader}" "strlen(materialName) >= kSpMaterialNameCapacity" _shader_length)
string(FIND "${_shader}" "materialIndex = G_MaterialIndex(materialName);" _shader_resolve)
string(FIND "${_shader}" "if (!StageTargetScalarConfig(" _shader_stage)
string(FIND "${_shader}" "target.materialIndex = materialIndex;" _shader_publish)
if(_shader_entity EQUAL -1
    OR _shader_entity_guard LESS_EQUAL _shader_entity
    OR _shader_lookup LESS_EQUAL _shader_entity_guard
    OR _shader_length EQUAL -1
    OR _shader_resolve LESS_EQUAL _shader_length
    OR _shader_stage LESS_EQUAL _shader_resolve
    OR _shader_publish LESS_EQUAL _shader_stage)
    message(FATAL_ERROR
        "Target shader must bound, stage, and validate before publication")
endif()

extract_slice(
    _targets "void __cdecl Scr_Target_SetOffscreenShader()"
    "void __cdecl Scr_Target_GetArray()"
    _offscreen_shader "transactional offscreen shader setter")
foreach(_required IN ITEMS
    "if (!ent) return;"
    "strlen(materialName) >= kSpMaterialNameCapacity"
    "Scr_ParamError(1u, \"Target offscreen shader name is too long\");"
    "materialIndex = G_MaterialIndex(materialName);"
    "!target_protocol::IsValidMaterialIndex(materialIndex)"
    "!IsPublishedMaterialIndex(materialIndex)"
    "return;"
    "if (!StageTargetScalarConfig("
    "|| parsed.offscreenMaterialIndex != materialIndex"
    "target.offscreenMaterialIndex = materialIndex;")
    require_contains(
        _offscreen_shader "${_required}" "validated offscreen publication")
endforeach()
string(FIND "${_offscreen_shader}" "gentity_s *const ent = Scr_GetEntity(0);" _offscreen_entity)
string(FIND "${_offscreen_shader}" "if (!ent) return;" _offscreen_entity_guard)
string(FIND "${_offscreen_shader}" "const int targetIndex = GetTargetIdx(ent);" _offscreen_lookup)
string(FIND "${_offscreen_shader}" "strlen(materialName) >= kSpMaterialNameCapacity" _offscreen_length)
string(FIND "${_offscreen_shader}" "materialIndex = G_MaterialIndex(materialName);" _offscreen_resolve)
string(FIND "${_offscreen_shader}" "if (!StageTargetScalarConfig(" _offscreen_stage)
string(FIND "${_offscreen_shader}" "target.offscreenMaterialIndex = materialIndex;" _offscreen_publish)
if(_offscreen_entity EQUAL -1
    OR _offscreen_entity_guard LESS_EQUAL _offscreen_entity
    OR _offscreen_lookup LESS_EQUAL _offscreen_entity_guard
    OR _offscreen_length EQUAL -1
    OR _offscreen_resolve LESS_EQUAL _offscreen_length
    OR _offscreen_stage LESS_EQUAL _offscreen_resolve
    OR _offscreen_publish LESS_EQUAL _offscreen_stage)
    message(FATAL_ERROR
        "Offscreen shader must bound, stage, and validate before publication")
endif()

extract_slice(
    _targets "void __cdecl Scr_Target_SetAttackMode()"
    "void __cdecl Scr_Target_SetJavelinOnly()"
    _attack_mode "failure-atomic attack-mode setter")
foreach(_required IN ITEMS
    "if (!ent) return;"
    "int stagedFlags = targGlob.targets[targetIndex].flags;"
    "if (!StageTargetScalarConfig("
    "|| parsed.flags != stagedFlags"
    "targGlob.targets[targetIndex].flags = stagedFlags;"
    "SV_SetConfigstring(CS_TARGETS + targetIndex, stagedConfigString);")
    require_contains(
        _attack_mode "${_required}"
        "attack-mode wire validation precedes native publication")
endforeach()
string(FIND "${_attack_mode}" "gentity_s *const ent = Scr_GetEntity(0);" _attack_entity)
string(FIND "${_attack_mode}" "if (!ent) return;" _attack_entity_guard)
string(FIND "${_attack_mode}" "const int targetIndex = GetTargetIdx(ent);" _attack_lookup)
string(FIND "${_attack_mode}" "if (!StageTargetScalarConfig(" _attack_stage)
string(FIND "${_attack_mode}" "targGlob.targets[targetIndex].flags = stagedFlags;" _attack_publish)
if(_attack_entity EQUAL -1
    OR _attack_entity_guard LESS_EQUAL _attack_entity
    OR _attack_lookup LESS_EQUAL _attack_entity_guard
    OR _attack_stage EQUAL -1
    OR _attack_publish LESS_EQUAL _attack_stage)
    message(FATAL_ERROR
        "Attack-mode setter must stage before changing live flags")
endif()

extract_slice(
    _targets "void __cdecl Scr_Target_SetJavelinOnly()"
    "SV_SetConfigstring(CS_TARGETS + targetIndex, stagedConfigString); }"
    _javelin_mode "failure-atomic Javelin-only setter")
foreach(_required IN ITEMS
    "if (!ent) return;"
    "int stagedFlags = targGlob.targets[targetIndex].flags;"
    "if (!StageTargetScalarConfig("
    "|| parsed.flags != stagedFlags"
    "targGlob.targets[targetIndex].flags = stagedFlags;")
    require_contains(
        _javelin_mode "${_required}"
        "Javelin-only wire validation precedes native publication")
endforeach()
string(FIND "${_javelin_mode}" "gentity_s *const ent = Scr_GetEntity(0);" _javelin_entity)
string(FIND "${_javelin_mode}" "if (!ent) return;" _javelin_entity_guard)
string(FIND "${_javelin_mode}" "const int targetIndex = GetTargetIdx(ent);" _javelin_lookup)
string(FIND "${_javelin_mode}" "if (!StageTargetScalarConfig(" _javelin_stage)
string(FIND "${_javelin_mode}" "targGlob.targets[targetIndex].flags = stagedFlags;" _javelin_publish)
if(_javelin_entity EQUAL -1
    OR _javelin_entity_guard LESS_EQUAL _javelin_entity
    OR _javelin_lookup LESS_EQUAL _javelin_entity_guard
    OR _javelin_stage EQUAL -1
    OR _javelin_publish LESS_EQUAL _javelin_stage)
    message(FATAL_ERROR
        "Javelin-only setter must stage before changing live flags")
endif()

foreach(_required IN ITEMS
    "target->ent->flags &= ~FL_TARGET;"
    "ClearTargetEntry(target);"
    "void ClearLiveTargetFlags()"
    "ent->flags |= FL_TARGET;"
    "if (!target_protocol::CanEncodeLegacyOffset(requestedOffset))"
    "static_cast<int>(offset[0])"
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
foreach(_required IN ITEMS
    "gentity_s *const targetEntity = Scr_GetEntity(0);"
    "if (!targetEntity) return 0;"
    "const gentity_s *const player = Scr_GetEntity(1);"
    "if (!player) return 0;"
    "if (!player->client)")
    require_contains(
        _screen_pos "${_required}"
        "screen-position entity validation")
endforeach()
string(FIND "${_screen_pos}" "gentity_s *const targetEntity = Scr_GetEntity(0);" _screen_target)
string(FIND "${_screen_pos}" "if (!targetEntity) return 0;" _screen_target_guard)
string(FIND "${_screen_pos}" "const gentity_s *const player = Scr_GetEntity(1);" _screen_player)
string(FIND "${_screen_pos}" "if (!player) return 0;" _screen_player_guard)
string(FIND "${_screen_pos}" "if (!player->client)" _screen_player_client)
if(_screen_target EQUAL -1
    OR _screen_target_guard LESS_EQUAL _screen_target
    OR _screen_player LESS_EQUAL _screen_target_guard
    OR _screen_player_guard LESS_EQUAL _screen_player
    OR _screen_player_client LESS_EQUAL _screen_player_guard)
    message(FATAL_ERROR
        "Screen-position helper must null-check each script entity before use")
endif()

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
    "TestCheckedInfoValueReplacement();"
    "TestFailureAtomicTargetConfigStaging();"
    "TryPublishMissingMaterialModel("
    "info_string::TrySetValueForKey("
    "BuildPaddedTargetConfig(&exactStorage, 1017)"
    "BuildPaddedTargetConfig(&rejectedStorage, currentLength)"
    "for (const std::size_t currentLength : {1018u, 1019u})"
    "rejectedStorage == before && liveMaterial == 17"
    "the checked setter must allow exactly 1023 content bytes"
    "a 1024-byte result must fail without changing its source"
    "replacement must remove the first old value and shrink to fit"
    "an empty clean value must remove the existing key"
    "checked replacement must retain legacy delimiter cleaning"
    "replacement must preserve an optional missing leading delimiter"
    "removing a leading first pair must retain legacy delimiter placement"
    "an overlong value must preserve an existing value atomically"
    "\\\\ent\\\\1\\\\mat\\\\2\\\\mat\\\\3"
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

extract_slice(
    _workflow "- name: Build shell: pwsh run: >"
    "- name: Enforce FX archive stack budgets"
    _measured_x86_build "measured Windows x86 explicit build targets")
require_contains(
    _measured_x86_build "kisakcod-target-table-tests"
    "measured Windows x86 explicitly builds the target-table runtime contract")

extract_slice(
    _workflow "- name: Run portable transaction tests"
    "- uses: actions/upload-artifact@v4"
    _measured_x86_tests "measured Windows x86 ctest selection")
require_contains(
    _measured_x86_tests
    "target-table-(parse-and-layout-contracts|source-invariants)"
    "measured Windows x86 selects both target-table contracts")

message(STATUS "Target-table source contract passed")
