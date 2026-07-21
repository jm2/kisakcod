cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

function(read_normalized RELATIVE_PATH OUT_VARIABLE DESCRIPTION)
    set(_path "${SOURCE_ROOT}/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR
            "Missing client-target source (${DESCRIPTION}): ${_path}")
    endif()
    file(READ "${_path}" _source)
    string(REGEX REPLACE "[ \t\r\n]+" " " _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(extract_slice
    SOURCE_VARIABLE START_MARKER END_MARKER OUT_VARIABLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${START_MARKER}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR
            "Missing start of client-target slice (${DESCRIPTION}): "
            "'${START_MARKER}'")
    endif()

    string(SUBSTRING "${${SOURCE_VARIABLE}}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${END_MARKER}" _end)
    if(_end LESS_EQUAL 0)
        message(FATAL_ERROR
            "Missing ordered end of client-target slice (${DESCRIPTION}): "
            "'${END_MARKER}'")
    endif()

    string(SUBSTRING "${_tail}" 0 ${_end} _slice)
    set(${OUT_VARIABLE} "${_slice}" PARENT_SCOPE)
endfunction()

function(extract_tail SOURCE_VARIABLE START_MARKER OUT_VARIABLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${START_MARKER}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR
            "Missing start of client-target tail (${DESCRIPTION}): "
            "'${START_MARKER}'")
    endif()
    string(SUBSTRING "${${SOURCE_VARIABLE}}" ${_start} -1 _tail)
    set(${OUT_VARIABLE} "${_tail}" PARENT_SCOPE)
endfunction()

function(require_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing client-target invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden client-target regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_ordered SOURCE_VARIABLE FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${FIRST}" _first)
    string(FIND "${${SOURCE_VARIABLE}}" "${SECOND}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered client-target invariant (${DESCRIPTION}): "
            "'${FIRST}' must precede '${SECOND}'")
    endif()
endfunction()

function(require_count SOURCE_VARIABLE NEEDLE EXPECTED DESCRIPTION)
    set(_remaining "${${SOURCE_VARIABLE}}")
    string(LENGTH "${NEEDLE}" _needle_length)
    if(_needle_length EQUAL 0)
        message(FATAL_ERROR "Empty client-target count needle (${DESCRIPTION})")
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
    if(NOT _count EQUAL EXPECTED)
        message(FATAL_ERROR
            "Unexpected client-target invariant count (${DESCRIPTION}): "
            "expected ${EXPECTED}, found ${_count}")
    endif()
endfunction()

read_normalized(
    "src/bgame/bg_target_protocol.h" _protocol
    "shared target protocol")
read_normalized(
    "src/client/client.h" _client_header
    "client configstring storage")
read_normalized(
    "src/client/cl_cgame.cpp" _client
    "client configstring consumers")
read_normalized(
    "src/cgame/cg_servercmds.cpp" _commands
    "cgame configstring and reticle commands")
read_normalized(
    "src/cgame/cg_vehicle_hud.cpp" _vehicle
    "cgame target storage and HUD consumers")
read_normalized(
    "src/cgame/cg_main.h" _cg_header
    "cgame target ABI")
read_normalized(
    "src/server/sv_init.cpp" _server
    "server configstring accessors")
read_normalized(
    "tests/target_table_tests.cpp" _fixture
    "target protocol runtime fixture")
read_normalized(
    "tests/CMakeLists.txt" _tests
    "portable test manifest")
read_normalized(
    ".github/workflows/ci.yml" _workflow
    "measured Windows x86 workflow")

# The command payload parser must be shared, strict, and failure-atomic. This
# keeps permissive CRT conversions out of both producer and consumer paths.
foreach(_required IN ITEMS
    "struct LockOnPayload"
    "inline bool TryParseLockOnPayload("
    "info_string::TryParseSignedDecimalToken( entityToken, &parsed.entityNumber)"
    "info_string::TryParseSignedDecimalToken( durationToken, &parsed.durationMilliseconds)"
    "parsed.entityNumber >= 0"
    "parsed.entityNumber < worldEntityNumber"
    "parsed.durationMilliseconds >= 0"
    "parsed.entityNumber == noneEntityNumber"
    "parsed.durationMilliseconds == 0"
    "*output = parsed;")
    require_contains(
        _protocol "${_required}"
        "strict, failure-atomic ret_lock_on payload parsing")
endforeach()
require_ordered(
    _protocol
    "if (!isActivePayload && !isClearPayload) return false;"
    "*output = parsed;"
    "lock-on output is published only after complete validation")
foreach(_required IN ITEMS
    "constexpr std::size_t kMaxConfigInfoLength = 1023;"
    "inline bool HasBoundedConfigLength("
    "length <= kMaxConfigInfoLength"
    "if (info[length] == '\\0') return true;"
    "if (!detail::HasBoundedConfigLength(info) || !info_string::IsWellFormed(info))")
    require_contains(
        _protocol "${_required}"
        "target config parsing is bounded to the producer wire capacity")
endforeach()

foreach(_required IN ITEMS
    "TestLockOnPayloadParsing();"
    "failed lock-on payload parsing must leave the destination unchanged"
    "the exact NONE/zero clear sentinel must parse"
    "WORLD cannot name a lock-on entity"
    "positive entity overflow must fail"
    "positive duration overflow must fail"
    "entity trailing junk must fail"
    "duration trailing junk must fail"
    "the MP ordinary-entity endpoint must parse"
    "the exact MP NONE/zero sentinel must parse"
    "the MP WORLD slot must not name a lock-on entity")
    require_contains(
        _fixture "${_required}"
        "runtime lock-on grammar and failure-atomicity coverage")
endforeach()
require_contains(
    _fixture
    "a target config larger than the producer wire buffer must be rejected"
    "runtime coverage rejects overlong target configstrings")
require_contains(
    _fixture
    "a full producer-sized buffer without a terminator must be rejected"
    "runtime coverage rejects unterminated producer-sized target buffers")

# One named bound owns the client storage shape and every access validates
# against it before indexing.
foreach(_required IN ITEMS
    "constexpr uint32_t CLIENT_CONFIGSTRING_COUNT = 2815;"
    "uint16_t configstrings[CLIENT_CONFIGSTRING_COUNT];")
    require_contains(
        _client_header "${_required}"
        "named client configstring storage bound")
endforeach()

extract_slice(
    _client "void CL_ConfigstringModified()" "void __cdecl CL_Restart()"
    _cl_config_modified "CL_ConfigstringModified")
foreach(_required IN ITEMS
    "info_string::TryParseSignedDecimalToken(indexToken, &index)"
    "index < 0"
    "static_cast<uint32_t>(index) >= CLIENT_CONFIGSTRING_COUNT"
    "const uint16_t oldString = clients[0].configstrings[index];"
    "clients[0].configstrings[index] = 0;"
    "SL_RemoveRefToString(oldString);"
    "const uint32_t newString = SL_GetString_( newValue, 0, MT_TYPE_CONFIG_STRING);"
    "clients[0].configstrings[index] = static_cast<uint16_t>(newString);")
    require_contains(
        _cl_config_modified "${_required}"
        "strict and full-table-compatible client configstring replacement")
endforeach()
require_ordered(
    _cl_config_modified
    "static_cast<uint32_t>(index) >= CLIENT_CONFIGSTRING_COUNT"
    "clients[0].configstrings[index]"
    "the client index check precedes the first storage access")
require_ordered(
    _cl_config_modified
    "clients[0].configstrings[index] = 0;"
    "SL_RemoveRefToString(oldString);"
    "the old handle is unpublished before its reference is released")
require_ordered(
    _cl_config_modified
    "SL_RemoveRefToString(oldString);"
    "const uint32_t newString = SL_GetString_( newValue, 0, MT_TYPE_CONFIG_STRING);"
    "the old slot is reusable before replacement interning")
require_ordered(
    _cl_config_modified
    "const uint32_t newString = SL_GetString_( newValue, 0, MT_TYPE_CONFIG_STRING);"
    "clients[0].configstrings[index] = static_cast<uint16_t>(newString);"
    "only a validated replacement is published")
foreach(_forbidden IN ITEMS "atol(" "atoi(" "sscanf(")
    forbid_contains(
        _cl_config_modified "${_forbidden}"
        "permissive configstring index parsing")
endforeach()

extract_slice(
    _client
    "const char *__cdecl CL_GetConfigString("
    "snd_alias_t *__cdecl CL_PickSoundAlias("
    _cl_get_config "CL_GetConfigString")
foreach(_required IN ITEMS
    "if (localClientNum)"
    "CL_GetConfigString: invalid local client %d"
    "if (configStringIndex >= CLIENT_CONFIGSTRING_COUNT)"
    "Com_Error( ERR_DROP, \"CL_GetConfigString: configstring index %u is outside [0, %u)\""
    "const uint16_t stringHandle = clients[0].configstrings[configStringIndex];"
    "if (!stringHandle)"
    "CL_GetConfigString: configstring index %u is not initialized"
    "const char *const value = SL_ConvertToString(stringHandle);"
    "if (!value)"
    "CL_GetConfigString: configstring index %u has no value"
    "return value;")
    require_contains(
        _cl_get_config "${_required}"
        "runtime-bounded client configstring lookup")
endforeach()
require_ordered(
    _cl_get_config
    "if (configStringIndex >= CLIENT_CONFIGSTRING_COUNT)"
    "clients[0].configstrings[configStringIndex]"
    "client lookup validates the numeric bound before indexing")
require_ordered(
    _cl_get_config
    "if (!stringHandle)"
    "SL_ConvertToString(stringHandle)"
    "client lookup rejects an absent handle before conversion")
require_count(
    _cl_get_config "return \"\";" 4
    "every reported client lookup error returns before further access")

# Both cgame command entry points must reject malformed indices/payloads before
# any configstring fetch or reticle-state mutation.
extract_slice(
    _commands
    "void __cdecl CG_ConfigStringModifiedInternal("
    "void __cdecl CG_ConfigStringModified("
    _cg_config_internal "CG_ConfigStringModifiedInternal")
require_contains(
    _cg_config_internal
    "stringIndex >= CLIENT_CONFIGSTRING_COUNT"
    "cgame validates the configstring index at the internal boundary")
extract_slice(
    _cg_config_internal
    "if (stringIndex >= CLIENT_CONFIGSTRING_COUNT)"
    "ConfigString = CL_GetConfigString(localClientNum, stringIndex);"
    _cg_config_internal_guard "cgame internal configstring range guard")
require_contains(
    _cg_config_internal_guard "return;"
    "the range guard stops after reporting an invalid internal index")
require_ordered(
    _cg_config_internal
    "stringIndex >= CLIENT_CONFIGSTRING_COUNT"
    "CL_GetConfigString(localClientNum, stringIndex)"
    "cgame range validation precedes configstring fetch")

extract_slice(
    _commands
    "void __cdecl CG_ConfigStringModified("
    "void __cdecl CG_ShutdownPhysics("
    _cg_config_command "CG_ConfigStringModified")
foreach(_required IN ITEMS
    "info_string::TryParseSignedDecimalToken("
    "stringIndex < 0"
    "static_cast<unsigned int>(stringIndex) >= CLIENT_CONFIGSTRING_COUNT"
    "CG_ConfigStringModifiedInternal( localClientNum, static_cast<unsigned int>(stringIndex));")
    require_contains(
        _cg_config_command "${_required}"
        "strict cgame configstring command parsing")
endforeach()
foreach(_forbidden IN ITEMS "atol(" "atoi(" "sscanf(")
    forbid_contains(
        _cg_config_command "${_forbidden}"
        "permissive cgame configstring command parsing")
endforeach()
extract_slice(
    _cg_config_command
    "int stringIndex = 0;"
    "CG_ConfigStringModifiedInternal("
    _cg_config_command_guard "cgame configstring command range guard")
require_ordered(
    _cg_config_command_guard
    "info_string::TryParseSignedDecimalToken("
    "return;"
    "strict configstring parsing precedes the guard return")
require_ordered(
    _cg_config_command
    "info_string::TryParseSignedDecimalToken("
    "CG_ConfigStringModifiedInternal("
    "strict configstring parsing precedes internal dispatch")

# The extracted handler also makes it possible to audit this one command
# independently of the legacy generated dispatch tree around it.
extract_slice(
    _commands
    "void CG_ReticleLockOnCommand("
    "void __cdecl CG_DispatchServerCommand("
    _reticle_command "ret_lock_on command handler")
foreach(_required IN ITEMS
    "Cmd_Argc() != 3"
    "bg::target_protocol::LockOnPayload payload"
    "bg::target_protocol::TryParseLockOnPayload("
    "ENTITYNUM_WORLD"
    "ENTITYNUM_NONE"
    "payload.entityNumber = ENTITYNUM_NONE;"
    "payload.durationMilliseconds = 0;"
    "CG_ReticleStartLockOn( localClientNum, payload.entityNumber, payload.durationMilliseconds);")
    require_contains(
        _reticle_command "${_required}"
        "strict ret_lock_on arity, parsing, and safe fallback")
endforeach()
foreach(_forbidden IN ITEMS "atol(" "atoi(" "sscanf(")
    forbid_contains(
        _reticle_command "${_forbidden}"
        "permissive ret_lock_on payload parsing")
endforeach()
require_count(
    _reticle_command "CG_ReticleStartLockOn(" 1
    "the strict helper publishes the lock state exactly once")
require_ordered(
    _reticle_command
    "bg::target_protocol::TryParseLockOnPayload("
    "CG_ReticleStartLockOn("
    "complete lock-on parsing precedes the sole publication")
require_ordered(
    _reticle_command
    "payload.entityNumber = ENTITYNUM_NONE;"
    "CG_ReticleStartLockOn("
    "malformed lock-on fallback is normalized before publication")
require_contains(
    _commands "CG_ReticleLockOnCommand(localClientNum);"
    "the dispatch tree delegates ret_lock_on to the audited handler")
extract_slice(
    _commands
    "void __cdecl CG_DispatchServerCommand("
    "void __cdecl CG_ExecuteNewServerCommands("
    _server_command_dispatch "server-command dispatch")
require_count(
    _server_command_dispatch "CG_ReticleLockOnCommand(localClientNum);" 1
    "the live dispatch contains exactly one strict ret_lock_on handoff")
require_count(
    _server_command_dispatch "\"ret_lock_on\"" 1
    "the live dispatch contains exactly one ret_lock_on command match")
require_ordered(
    _server_command_dispatch
    "\"ret_lock_on\""
    "CG_ReticleLockOnCommand(localClientNum);"
    "the ret_lock_on match precedes its strict handler")
forbid_contains(
    _server_command_dispatch "CG_ReticleStartLockOn("
    "the dispatch cannot bypass strict ret_lock_on payload parsing")
forbid_contains(
    _reticle_command "2175"
    "ret_lock_on uses ENTITYNUM_NONE instead of a duplicated sentinel")

# Keep targetInfo_t's legacy 28-byte runtime shape explicit on both pointer
# widths while using a named exact 32-entry array bound.
foreach(_required IN ITEMS
    "RUNTIME_SIZE(targetInfo_t, 0x1C, 0x1C);"
    "RUNTIME_OFFSET(targetInfo_t, entNum, 0x0, 0x0);"
    "RUNTIME_OFFSET(targetInfo_t, offset, 0x4, 0x4);"
    "RUNTIME_OFFSET(targetInfo_t, materialIndex, 0x10, 0x10);"
    "RUNTIME_OFFSET(targetInfo_t, offscreenMaterialIndex, 0x14, 0x14);"
    "RUNTIME_OFFSET(targetInfo_t, flags, 0x18, 0x18);"
    "targetInfo_t targets[bg::target_protocol::kMaxTargets];")
    require_contains(
        _cg_header "${_required}"
        "targetInfo_t ABI and exact target-array extent")
endforeach()
require_contains(
    _protocol "constexpr int kMaxTargets = 32;"
    "the shared exact target count remains 32")

extract_slice(
    _vehicle
    "centity_s *CG_TryGetLiveOrdinaryEntity("
    "bool CG_IsValidWeaponIndex("
    _live_entity "live ordinary-entity lookup")
foreach(_required IN ITEMS
    "entityNumber < 0 || entityNumber >= ENTITYNUM_WORLD"
    "CG_GetEntity( localClientNum, static_cast<uint32_t>(entityNumber))"
    "!entity->nextValid"
    "entity->nextState.number != entityNumber"
    "return nullptr;"
    "return entity;")
    require_contains(
        _live_entity "${_required}"
        "numeric, snapshot-valid, identity-checked entity lookup")
endforeach()
require_ordered(
    _live_entity
    "entityNumber < 0 || entityNumber >= ENTITYNUM_WORLD"
    "CG_GetEntity( localClientNum, static_cast<uint32_t>(entityNumber))"
    "numeric entity validation precedes CG_GetEntity array access")

extract_slice(
    _vehicle
    "bool CG_IsValidWeaponIndex("
    "} // namespace"
    _weapon_index "weapon-index validator")
foreach(_required IN ITEMS
    "weaponIndex > 0"
    "static_cast<unsigned int>(weaponIndex) < BG_GetNumWeapons()")
    require_contains(
        _weapon_index "${_required}"
        "weapon definition indices stay within the live table")
endforeach()

extract_slice(
    _vehicle
    "void __cdecl CG_DrawVehicleTargets("
    "void __cdecl CG_DrawJavelinTargets("
    _draw_vehicle_targets "vehicle target rendering")
extract_slice(
    _vehicle
    "void __cdecl CG_DrawJavelinTargets("
    "void CG_DrawPipOnAStickReticle("
    _draw_javelin_targets "Javelin target rendering")
extract_slice(
    _vehicle
    "void __cdecl CG_InitVehicleReticle("
    "void __cdecl CG_ReticleStartLockOn("
    _init_reticle "vehicle-reticle initialization")
foreach(_required IN ITEMS
    "vehReticleLockOnEntNum = ENTITYNUM_NONE;"
    "vehReticleLockOnStartTime = 0;"
    "vehReticleLockOnDuration = 0;")
    require_contains(
        _init_reticle "${_required}"
        "reticle lock state starts in the exact clear representation")
endforeach()

extract_slice(
    _vehicle
    "void __cdecl CG_ReticleStartLockOn("
    "int __cdecl CG_GetTargetPos("
    _start_lock "CG_ReticleStartLockOn")
foreach(_required IN ITEMS
    "targetEntNum == ENTITYNUM_NONE && msecDuration == 0"
    "targetEntNum >= 0"
    "targetEntNum < ENTITYNUM_WORLD"
    "msecDuration >= 0"
    "if (!isClear && !isActive)"
    "targetEntNum = ENTITYNUM_NONE;"
    "msecDuration = 0;")
    require_contains(
        _start_lock "${_required}"
        "direct lock-state publication preserves payload invariants")
endforeach()

extract_slice(
    _vehicle
    "int __cdecl CG_GetTargetPos("
    "void CG_DrawBouncingDiamond("
    _get_target_pos "CG_GetTargetPos")
extract_slice(
    _vehicle
    "void CG_DrawBouncingDiamond("
    "void __cdecl CG_DrawVehicleReticle("
    _bouncing_reticle "bouncing-diamond reticle")

foreach(_slice IN ITEMS
    _draw_vehicle_targets _draw_javelin_targets _get_target_pos)
    require_contains(
        ${_slice}
        "targetIndex < bg::target_protocol::kMaxTargets"
        "typed iteration covers exactly the target array")
    forbid_contains(
        ${_slice} "uintptr_t"
        "target traversal must not use cross-member pointer bounds")
    forbid_contains(
        ${_slice} "shellshock"
        "target traversal must not terminate against an adjacent member")
    forbid_contains(
        ${_slice} "CG_GetEntity("
        "target consumers cannot bypass the live ordinary-entity gate")
endforeach()

foreach(_required IN ITEMS
    "const targetInfo_t &target = cgArray[0].targets[targetIndex];"
    "CG_TryGetLiveOrdinaryEntity(localClientNum, target.entNum)"
    "Material *material = defaultMaterial;"
    "char materialName[96];"
    "static_cast<unsigned int>(sizeof(materialName))"
    "Material *const registeredMaterial = Material_RegisterHandle(materialName, 7)"
    "material = registeredMaterial;"
    "color, material);")
    require_contains(
        _draw_vehicle_targets "${_required}"
        "typed, live-entity target drawing with retained material fallback")
endforeach()

foreach(_required IN ITEMS
    "CG_TryGetLiveOrdinaryEntity(localClientNum, lockedEntityNumber)"
    "const targetInfo_t *target = nullptr;"
    "target = &cgArray[0].targets[targetIndex];"
    "if (!target) return;")
    require_contains(
        _draw_javelin_targets "${_required}"
        "bounded live Javelin target lookup")
endforeach()

foreach(_required IN ITEMS
    "if (!outPos) return 0;"
    "CG_TryGetLiveOrdinaryEntity(localClientNum, targetEntNum)"
    "const targetInfo_t *target = nullptr;"
    "const float position[3]"
    "outPos[0] = position[0];"
    "outPos[1] = position[1];"
    "outPos[2] = position[2];")
    require_contains(
        _get_target_pos "${_required}"
        "failure-atomic target-position lookup")
endforeach()
require_ordered(
    _get_target_pos
    "const float position[3]"
    "outPos[0] = position[0];"
    "all target-position validation and staging precede publication")

foreach(_required IN ITEMS
    "targetEntNum != ENTITYNUM_NONE"
    "CG_GetTargetPos(localClientNum, targetEntNum, &worldPos[0])"
    "CG_TryGetLiveOrdinaryEntity(localClientNum, targetEntNum)"
    "if (!ent) return;")
    require_contains(
        _bouncing_reticle "${_required}"
        "bouncing-reticle fallback stays within live ordinary entities")
endforeach()

extract_slice(
    _vehicle
    "void CG_DrawPipOnAStickReticle("
    "void __cdecl CG_InitVehicleReticle("
    _pip_reticle "pip-on-a-stick reticle")
extract_slice(
    _vehicle
    "void __cdecl CG_DrawVehicleReticle("
    "void __cdecl CG_TargetsChanged("
    _vehicle_reticle "vehicle reticle dispatcher")
foreach(_slice IN ITEMS _pip_reticle _vehicle_reticle)
    require_contains(
        ${_slice} "CG_TryGetLiveOrdinaryEntity("
        "reticle entity access uses the live ordinary-entity gate")
    require_contains(
        ${_slice} "CG_IsValidWeaponIndex("
        "reticle weapon access validates the weapon table bound")
    forbid_contains(
        ${_slice} "CG_GetEntity("
        "reticle consumers cannot bypass the live ordinary-entity gate")
endforeach()
forbid_contains(
    _bouncing_reticle "CG_GetEntity("
    "bouncing-reticle fallback cannot bypass the live ordinary-entity gate")

extract_tail(
    _vehicle "void __cdecl CG_TargetsChanged("
    _targets_changed "CG_TargetsChanged")
foreach(_required IN ITEMS
    "const unsigned int firstTargetConfig = CS_TARGETS;"
    "firstTargetConfig + static_cast<unsigned int>(bg::target_protocol::kMaxTargets)"
    "num < firstTargetConfig || num >= targetConfigEnd"
    "return;"
    "const unsigned int targetIndex = num - firstTargetConfig;"
    "const char *const configString = CL_GetConfigString(localClientNum, num);"
    "targetInfo_t staged"
    "staged.entNum = ENTITYNUM_NONE;"
    "staged.materialIndex = bg::target_protocol::kNoMaterial;"
    "staged.offscreenMaterialIndex = bg::target_protocol::kNoMaterial;"
    "bg::target_protocol::ParseConfig("
    "ENTITYNUM_WORLD"
    "cgArray[0].targets[targetIndex] = staged;")
    require_contains(
        _targets_changed "${_required}"
        "range-first, staged target config parsing and publication")
endforeach()
require_ordered(
    _targets_changed
    "num < firstTargetConfig || num >= targetConfigEnd"
    "CL_GetConfigString(localClientNum, num)"
    "target config range validation precedes configstring fetch")
require_ordered(
    _targets_changed
    "targetInfo_t staged"
    "cgArray[0].targets[targetIndex] = staged;"
    "a fully staged target replaces the live entry once")
require_count(
    _targets_changed
    "cgArray[0].targets[targetIndex] = staged;"
    3
    "empty, malformed, and valid configs each publish a complete stage")
foreach(_forbidden IN ITEMS "atol(" "atoi(" "sscanf(" "Info_ValueForKey(")
    forbid_contains(
        _targets_changed "${_forbidden}"
        "legacy partial target parsing")
endforeach()

forbid_contains(
    _vehicle "2175"
    "ENTITYNUM_NONE must replace the duplicated numeric sentinel")

# Map initialization visits every target config slot after the cg array reset;
# empty slots therefore publish the same fully cleared staged representation.
extract_tail(
    _commands "void __cdecl CG_MapInit("
    _map_init "CG_MapInit target initialization")
foreach(_required IN ITEMS
    "memset(cgArray, 0, sizeof(cgArray));"
    "for (int targetConfig = CS_TARGETS;"
    "targetConfig < CS_TARGETS + bg::target_protocol::kMaxTargets;"
    "CG_TargetsChanged(0, targetConfig);")
    require_contains(
        _map_init "${_required}"
        "every target slot is explicitly initialized through staged clearing")
endforeach()

# Server configstring bounds use the actual array extent and every error path
# returns before indexing. In particular, index == count must be rejected.
extract_slice(
    _server "void __cdecl SV_GetConfigstring("
    "unsigned int __cdecl SV_GetConfigstringConst("
    _sv_get "SV_GetConfigstring")
extract_slice(
    _server "unsigned int __cdecl SV_GetConfigstringConst("
    "void __cdecl SV_InitReliableCommandsForClient("
    _sv_get_const "SV_GetConfigstringConst")
extract_slice(
    _server "void __cdecl SV_SetConfigstring("
    "void SV_SaveSystemInfo("
    _sv_set "SV_SetConfigstring")

foreach(_slice IN ITEMS _sv_get _sv_get_const _sv_set)
    require_contains(
        ${_slice} "index >= ARRAY_COUNT(sv.configstrings)"
        "server configstring access rejects the one-past-end index")
    require_ordered(
        ${_slice}
        "index >= ARRAY_COUNT(sv.configstrings)"
        "sv.configstrings[index]"
        "server numeric validation precedes array access")
endforeach()
foreach(_required IN ITEMS
    "if (!buffer) { Com_Error(ERR_DROP, \"SV_GetConfigstring: buffer is null\"); return; }"
    "if (bufferSize < 1) { Com_Error(ERR_DROP, \"SV_GetConfigstring: bufferSize == %i\", bufferSize); return; }"
    "if (index >= ARRAY_COUNT(sv.configstrings)) { Com_Error(ERR_DROP, \"SV_GetConfigstring: bad index %u\", index); return; }"
    "if (!sv.configstrings[index]) { Com_Error(ERR_DROP, \"SV_GetConfigstring: configstring %u is not initialized\", index); return; }")
    require_contains(
        _sv_get "${_required}"
        "SV_GetConfigstring stops after each reported validation failure")
endforeach()
foreach(_required IN ITEMS
    "if (index >= ARRAY_COUNT(sv.configstrings)) { Com_Error(ERR_DROP, \"SV_GetConfigstringConst: bad index %u\", index); return 0; }"
    "if (!sv.configstrings[index]) { Com_Error(ERR_DROP, \"SV_GetConfigstringConst: configstring %u is not initialized\", index); return 0; }")
    require_contains(
        _sv_get_const "${_required}"
        "SV_GetConfigstringConst stops after each reported failure")
endforeach()
require_contains(
    _sv_set
    "if (index >= ARRAY_COUNT(sv.configstrings)) { Com_Error(ERR_DROP, \"SV_SetConfigstring: bad index %u\", index); return; }"
    "SV_SetConfigstring stops after rejecting the array bound")
foreach(_required IN ITEMS
    "const unsigned __int16 oldString = sv.configstrings[index];"
    "sv.configstrings[index] = 0;"
    "SL_RemoveRefToString(oldString);"
    "sv.configstrings[index] = static_cast<unsigned __int16>(newString);")
    require_contains(
        _sv_set "${_required}"
        "full-table-compatible server configstring replacement")
endforeach()
require_ordered(
    _sv_set
    "sv.configstrings[index] = 0;"
    "SL_RemoveRefToString(oldString);"
    "the server unpublishes the old handle before releasing it")
require_ordered(
    _sv_set
    "SL_RemoveRefToString(oldString);"
    "const unsigned int newString = index < 1114u"
    "the server releases capacity before replacement interning")
require_ordered(
    _sv_set
    "const unsigned int newString = index < 1114u"
    "sv.configstrings[index] = static_cast<unsigned __int16>(newString);"
    "the server publishes only a validated replacement")
foreach(_required IN ITEMS
    "SV_SendServerCommand(svs.clients, \"cs %u %s\", index, val);"
    "SV_SendServerCommand(clients, \"%s %u %s\", v14, index, v15);")
    require_contains(
        _sv_set "${_required}"
        "server configstring wire commands use the unsigned index type")
endforeach()

# Keep this contract in both the portable matrix and the measured Windows x86
# execution gate so the source-only SP/client paths cannot silently regress.
foreach(_required IN ITEMS
    "NAME target-client-source-invariants"
    "target_client_source_test.cmake")
    require_contains(
        _tests "${_required}"
        "portable client-target source-contract wiring")
endforeach()
extract_slice(
    _workflow
    "- name: Run portable transaction tests"
    "- uses: actions/upload-artifact@v4"
    _measured_x86 "measured Windows x86 ctest selection")
require_count(
    _measured_x86 "target-client-source-invariants" 1
    "measured Windows x86 selects the client-target source contract")

message(STATUS "Client-target source contract passed")
