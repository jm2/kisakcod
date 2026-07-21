cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

function(load_text RELATIVE_PATH OUT_VARIABLE)
    set(_path "${SOURCE_ROOT}/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing upstream reconciliation input: ${_path}")
    endif()
    file(READ "${_path}" _source)
    string(REPLACE "\r\n" "\n" _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(require_contains HAYSTACK_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${HAYSTACK_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing upstream reconciliation invariant (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains HAYSTACK_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${HAYSTACK_VARIABLE}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden upstream reconciliation regression (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(require_count HAYSTACK_VARIABLE NEEDLE EXPECTED DESCRIPTION)
    set(_remaining "${${HAYSTACK_VARIABLE}}")
    set(_count 0)
    string(LENGTH "${NEEDLE}" _needle_length)
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
            "Unexpected upstream reconciliation count (${DESCRIPTION}): "
            "expected ${EXPECTED}, found ${_count}")
    endif()
endfunction()

function(forbid_numeric_mt_type_arguments RELATIVE_PATH)
    set(_path "${SOURCE_ROOT}/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing MT callsite input: ${_path}")
    endif()
    file(READ "${_path}" _source)
    string(REPLACE "\r\n" "\n" _source "${_source}")
    # Search executable source only: examples in comments and diagnostics must
    # not become accidental build failures.
    string(REGEX REPLACE "\"[^\"]*\"" "\"\"" _source "${_source}")
    string(REGEX REPLACE "'[^']*'" "''" _source "${_source}")
    while(TRUE)
        string(FIND "${_source}" "/*" _comment_start)
        if(_comment_start EQUAL -1)
            break()
        endif()
        string(SUBSTRING "${_source}" 0 ${_comment_start} _prefix)
        math(EXPR _comment_body_start "${_comment_start} + 2")
        string(SUBSTRING "${_source}" ${_comment_body_start} -1 _rest)
        string(FIND "${_rest}" "*/" _comment_end)
        if(_comment_end EQUAL -1)
            set(_source "${_prefix}")
            break()
        endif()
        math(EXPR _suffix_start "${_comment_end} + 2")
        string(SUBSTRING "${_rest}" ${_suffix_start} -1 _suffix)
        set(_source "${_prefix} ${_suffix}")
    endwhile()
    string(REGEX REPLACE "//[^\n]*" " " _source "${_source}")
    string(REGEX MATCH
        "(MT_Alloc|MT_AllocIndex|SL_GetString_|SL_GetStringOfSize|SL_GetLowercaseString_|SL_ConvertToLowercase)[ \t\r\n]*\\([^;]*,[ \t\r\n]*(1|2|3|4|5|6|7|8|9|10|11|12|13|14|15|16|17|18|19|20|21)[uUlL]*[ \t\r\n]*\\)"
        _raw_call "${_source}")
    if(NOT _raw_call STREQUAL "")
        message(FATAL_ERROR
            "Raw memory-tree type argument remains in ${RELATIVE_PATH}: "
            "${_raw_call}")
    endif()
endfunction()

load_text("src/gfx_d3d/r_dpvs.cpp" _r_dpvs)
load_text("src/aim_assist/aim_assist.cpp" _aim_assist)
load_text("src/aim_assist/aim_assist.h" _aim_assist_header)
load_text("src/aim_assist/aim_assist_safety.h" _aim_safety)
load_text("src/qcommon/cmd.cpp" _cmd)
load_text("src/qcommon/cmd_dispatch.h" _cmd_dispatch)
load_text("src/universal/com_angle.cpp" _com_angle)
load_text("src/universal/com_math.cpp" _com_math)
load_text("src/script/scr_memorytree.h" _scr_memorytree_header)
load_text("src/script/scr_memorytree.cpp" _scr_memorytree)
load_text("src/script/scr_stringlist.h" _scr_stringlist_header)
load_text("src/game/g_save.cpp" _g_save)
load_text("src/database/db_load.cpp" _db_load)
load_text("src/database/db_stream_load.cpp" _db_stream_load)
load_text("src/client/cl_cgame.cpp" _cl_cgame)
load_text("src/client/cl_demo.cpp" _cl_demo)
load_text("scripts/common_files.cmake" _common_files)
load_text("tests/CMakeLists.txt" _tests_cmake)
load_text(".github/workflows/ci.yml" _ci)

if(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "rdpvs_stale_def")
    string(REPLACE
        "iassert( !dynEntDefa->xModel );"
        "iassert( !dynEntDef->xModel );"
        _r_dpvs "${_r_dpvs}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "angle_unsigned")
    string(REPLACE
        "return AngleDelta(a1, a2);"
        "return AngleNormalize360(a1 - a2);"
        _com_angle "${_com_angle}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "angle_duplicate")
    string(APPEND _com_math
        "\nfloat AngleSubtract(float a1, float a2) "
        "{ return AngleDelta(a1, a2); }\n")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "angle_global_floor")
    string(REPLACE "std::floor(offset)" "floor(offset)" _com_angle "${_com_angle}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "angle_implicit_narrowing")
    string(REPLACE
        "static_cast<float>(difference * 0.002777777845039964)"
        "difference * 0.002777777845039964"
        _com_angle "${_com_angle}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "aim_no_release_guard")
    string(REPLACE
        "if (!ordinaryEntity)\n            return 0;"
        "if (false)\n            return 0;"
        _aim_assist "${_aim_assist}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "aim_packed_abi")
    string(REPLACE
        "AimAssist_GetWeaponIndex(localClientNum, ps)"
        "AimAssist_GetWeaponIndex(localClientNum, (const playerState_s *)HIDWORD(localClientNum))"
        _aim_assist "${_aim_assist}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "aim_unbounded_weapon")
    string(REPLACE
        "return aim_assist::safety::BoundedWeaponIndex(weapIndex, weaponCount);"
        "return weapIndex;"
        _aim_assist "${_aim_assist}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "cmd_tail_skip")
    string(REPLACE
        "current != nullptr"
        "current->next != nullptr"
        _cmd_dispatch "${_cmd_dispatch}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "cmd_direct_walk")
    string(REPLACE
        "command_dispatch::FindLinkedCommand("
        "LegacyFindCommand("
        _cmd "${_cmd}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "cmd_null_query")
    string(REPLACE "if (!name)" "if (false)" _cmd_dispatch "${_cmd_dispatch}")
elseif(DEFINED CONTRACT_MUTATION AND CONTRACT_MUTATION STREQUAL "cmd_null_node")
    string(REPLACE
        "if (current->name && namesEqual(name, current->name))"
        "if (namesEqual(name, current->name))"
        _cmd_dispatch "${_cmd_dispatch}")
elseif(DEFINED CONTRACT_MUTATION AND NOT CONTRACT_MUTATION STREQUAL "")
    message(FATAL_ERROR "Unknown reconciliation mutation: ${CONTRACT_MUTATION}")
endif()

string(REPLACE "\t" " " _r_dpvs_normalized "${_r_dpvs}")
string(REGEX REPLACE "[ \r\n]+" " " _r_dpvs_normalized
    "${_r_dpvs_normalized}")
require_contains(
    _r_dpvs_normalized
    "dynEntDefa = DynEnt_GetEntityDef(dynEntIndexa, DYNENT_DRAW_BRUSH); iassert( !dynEntDefa->xModel ); iassert( dynEntDefa->brushModel ); bmodel = R_GetBrushModel(dynEntDefa->brushModel);"
    "brush assertions and model lookup use the same fetched definition")

string(REGEX REPLACE "[ \t\r\n]+" " " _aim_normalized "${_aim_assist}")
require_contains(
    _aim_normalized
    "const bool ordinaryEntity = aim_assist::safety::IsOrdinaryEntityNumber( ps->viewlocked_entNum, ENTITYNUM_WORLD); iassert(ordinaryEntity); if (!ordinaryEntity) return 0; weapIndex = CG_GetEntity(localClientNum, ps->viewlocked_entNum)->nextState.weapon;"
    "release guard rejects sentinels and invalid entities before lookup")
require_contains(
    _aim_normalized
    "const uint32_t weaponCount = BG_GetNumWeapons(); bcassert(weapIndex, weaponCount); return aim_assist::safety::BoundedWeaponIndex(weapIndex, weaponCount);"
    "every selected weapon is range-checked in release")
require_count(
    _aim_assist "AimAssist_DrawTargets(localClientNum, ps, red);" 4
    "all four debug overlays pass the real player state")
require_contains(
    _aim_assist
    "void __cdecl AimAssist_DrawTargets(\n    int32_t localClientNum,\n    const playerState_s *ps,"
    "the debug target function exposes the native pointer explicitly")
require_contains(
    _aim_assist_header
    "void __cdecl AimAssist_DrawTargets(\n    int32_t localClientNum,\n    const playerState_s *ps,"
    "the public declaration matches the repaired debug target ABI")
forbid_contains(
    _aim_assist "HIDWORD(localClientNum)"
    "native pointers must not be decoded from a decompiler-packed integer")
require_contains(
    _aim_safety
    "return entityNumber >= 0 && entityNumber < ordinaryEntityEnd;"
    "ordinary-entity validation is signed and excludes both sentinels")
require_contains(
    _aim_safety
    "return weaponIndex < weaponCount ? weaponIndex : 0;"
    "invalid weapons collapse to the no-weapon sentinel")

require_contains(
    _cmd_dispatch
    "for (Command *current = head; current != nullptr; current = current->next)"
    "the shared production walker admits empty lists and visits the tail")
require_contains(
    _cmd_dispatch
    "if (!name)\n        return nullptr;"
    "null lookup names are rejected before comparator dispatch")
require_contains(
    _cmd_dispatch
    "if (current->name && namesEqual(name, current->name))"
    "null registry names are skipped before comparator dispatch")
require_count(
    _cmd "command_dispatch::FindLinkedCommand(" 2
    "server and ordinary dispatch share the tested linked-list walker")
forbid_contains(
    _cmd "itr->next; itr = itr->next"
    "production dispatch must not dereference empty heads or skip tails")

string(REGEX REPLACE "[ \t\r\n]+" " " _mt_header_normalized
    "${_scr_memorytree_header}")
require_contains(
    _mt_header_normalized
    "enum mtType_t : int { MT_TYPE_FIRST = 1, MT_TYPE_THREAD = 1, MT_TYPE_VECTOR = 2, MT_TYPE_NOTETRACK = 3, MT_TYPE_ANIM_TREE = 4, MT_TYPE_SMALL_ANIM_TREE = 5, MT_TYPE_EXTERNAL = 6, MT_TYPE_TEMP = 7, MT_TYPE_SURFACE = 8, MT_TYPE_ANIM_PART = 9, MT_TYPE_MODEL_PART = 10, MT_TYPE_MODEL_PART_MAP = 11, MT_TYPE_DUPLICATE_PARTS = 12, MT_TYPE_MODEL_LIST = 13, MT_TYPE_SCRIPT_PARSE = 14, MT_TYPE_SCRIPT_STRING = 15, MT_TYPE_CLASS = 16, MT_TYPE_TAG_INFO = 17, MT_TYPE_ANIMSCRIPTED = 18, MT_TYPE_CONFIG_STRING = 19, MT_TYPE_DEBUGGER_STRING = 20, MT_TYPE_GENERIC = 21, MT_TYPE_COUNT = 22, };"
    "the debug-accounting categories retain their exact int-backed values")
require_contains(
    _scr_memorytree "constexpr const char *mt_type_names[22]"
    "the debug-name table retains one entry for every category including zero")
require_contains(
    _scr_memorytree_header
    "static_assert(std::is_same_v<std::underlying_type_t<mtType_t>, int>);"
    "the memory-tree type remains int-backed on every target")
require_contains(
    _scr_memorytree "static_assert(kMemoryTreeTypeCount == MT_TYPE_COUNT);"
    "the debug-name table and public category count stay synchronized")
require_contains(
    _scr_memorytree_header "unsigned short MT_AllocIndex(int numBytes, int type);"
    "the public allocator-index ABI remains int-typed")
require_contains(
    _scr_memorytree_header "void* MT_Alloc(int numBytes, int type);"
    "the public allocator ABI remains int-typed")
require_contains(
    _scr_stringlist_header
    "uint32_t SL_GetString_(const char* str, uint32_t user, int type);"
    "the public string-interning ABI remains int-typed")
require_contains(
    _scr_stringlist_header
    "uint32_t SL_GetStringOfSize(const char* str, uint32_t user, uint32_t len, int type);"
    "the sized string-interning ABI remains int-typed")
require_contains(
    _scr_stringlist_header
    "uint32_t SL_GetLowercaseString_(const char* str, uint32_t user, int type);"
    "the lowercase string-interning ABI remains int-typed")
require_contains(
    _scr_stringlist_header
    "uint32_t __cdecl SL_ConvertToLowercase(uint32_t stringValue, uint32_t user, int type);"
    "the lowercase-conversion ABI remains int-typed")

# Exercise the built callsites selected for the symbolic-only curation from
# upstream 1c30dda2. Database loader callsites remain outside this batch because
# their broader upstream diff is superseded by hardened validation; the unbuilt
# generated scr_yacc.cpp intentionally remains untouched.
foreach(_mt_callsite IN ITEMS
    src/bgame/bg_weapons_load_obj.cpp
    src/cgame_mp/cg_ents_mp.cpp
    src/client/cl_cgame.cpp
    src/game/g_animscripted.cpp
    src/game/g_save.cpp
    src/game/g_utils.cpp
    src/game_mp/g_utils_mp.cpp
    src/qcommon/common.cpp
    src/script/scr_animtree.cpp
    src/script/scr_debugger.cpp
    src/script/scr_evaluate.cpp
    src/script/scr_main.cpp
    src/script/scr_readwrite.cpp
    src/script/scr_stringlist.cpp
    src/script/scr_variable.cpp
    src/script/scr_vm.cpp
    src/script/scr_yacc2.cpp
    src/server/sv_init.cpp
    src/server_mp/sv_init_mp.cpp
    src/universal/com_memory.cpp
    src/xanim/dobj.cpp
    src/xanim/xanim.cpp
    src/xanim/xanim_load_obj.cpp
    src/xanim/xmodel_load_obj.cpp)
    forbid_numeric_mt_type_arguments("${_mt_callsite}")
endforeach()
require_contains(
    _g_save "MT_Alloc(112, MT_TYPE_TAG_INFO)"
    "tag-info save restoration uses its named debug-accounting category")
require_contains(
    _g_save "sizeof(animscripted_s),\n                MT_TYPE_ANIMSCRIPTED)"
    "scripted save restoration uses the distinct animscripted category")
require_contains(
    _g_save "static_assert(sizeof(tagInfo_s) == 112);"
    "native64 SP remains closed until tag-info saves have a Disk32 converter")

require_count(
    _db_load "localClientNum < STATIC_MAX_LOCAL_CLIENTS" 2
    "both DObj archive passes cover every compiled local-client slot")
require_count(
    _db_load "handle < CLIENT_DOBJ_HANDLE_MAX" 2
    "both DObj archive passes use the client handle bound")
require_count(
    _db_load "handle < MAX_GENTITIES" 2
    "both DObj archive passes use the server entity bound")
forbid_contains(
    _db_load "handle < 1152"
    "DObj client traversal must not restore a hard-coded handle count")
forbid_contains(
    _db_load "handle < 1024"
    "DObj server traversal must not restore a hard-coded entity count")

string(REGEX REPLACE "[ \t\r\n]+" " " _cl_cgame_normalized
    "${_cl_cgame}")
require_contains(
    _cl_cgame_normalized
    "for (uint32_t index = 0; index < CLIENT_CONFIGSTRING_COUNT; ++index)"
    "client restart traverses the declared configstring array extent")
require_contains(
    _cl_cgame_normalized
    "SL_GetString_( newValue, 0, MT_TYPE_CONFIG_STRING)"
    "configstring replacement uses the named memory-tree category")
require_contains(
    _cl_cgame_normalized
    "SL_GetString_( \"\", 0, MT_TYPE_CONFIG_STRING)"
    "client restart uses the named configstring category")
require_contains(
    _cl_demo "while (v0 < CLIENT_CONFIGSTRING_COUNT);"
    "demo finalization traverses the declared configstring extent")
forbid_contains(
    _cl_demo "v0 < 2815"
    "demo finalization must not restore a hard-coded configstring count")

string(REGEX REPLACE "[ \t\r\n]+" " " _db_load_normalized
    "${_db_load}")
require_contains(
    _db_load_normalized
    "&varMaterialVertexDeclaration->streamCount, disk32::kMaterialVertexDeclarationBytes);"
    "the fast-file loader retains the serialized 32-bit declaration extent")
forbid_contains(
    _db_load "sizeof(MaterialVertexDeclaration)"
    "native host width must not select a fast-file declaration extent")

string(REGEX REPLACE "[ \t\r\n]+" " " _db_stream_load_normalized
    "${_db_stream_load}")
require_contains(
    _db_stream_load_normalized
    "if (g_streamDelayIndex >= ARRAY_COUNT(g_streamDelayArray)) { Com_Error(ERR_DROP, \"Fast-file delayed stream table overflow\"); return; }"
    "delayed-stream overflow is rejected at runtime in release builds")
forbid_contains(
    _db_stream_load
    "bcassert(g_streamDelayIndex, ARRAY_COUNT(g_streamDelayArray));"
    "an assertion alone cannot guard the delayed-stream write")

require_contains(
    _cl_cgame_normalized
    "Com_Error( ERR_DROP, \"CL_GetConfigString: invalid local client %d\", localClientNum); return \"\";"
    "configstring reads reject invalid clients at runtime")
require_contains(
    _cl_cgame_normalized
    "Com_Error( ERR_DROP, \"CL_GetConfigString: configstring index %u is outside [0, %u)\", configStringIndex, CLIENT_CONFIGSTRING_COUNT); return \"\";"
    "configstring reads reject out-of-range indices at runtime")
require_contains(
    _cl_cgame_normalized
    "Com_Error( ERR_DROP, \"CL_GetConfigString: configstring index %u is not initialized\", configStringIndex); return \"\";"
    "configstring reads reject missing handles at runtime")

require_contains(
    _com_angle "float KISAK_CDECL AngleDelta(float a1, float a2)"
    "the signed retail delta implementation is isolated for portable tests")
require_contains(
    _com_angle "return AngleDelta(a1, a2);"
    "AngleSubtract delegates to the canonical signed implementation")
require_contains(
    _com_angle "#include <cmath>"
    "portable C++ math overloads are selected")
require_contains(
    _com_angle
    "scaled = static_cast<float>(difference * 0.002777777845039964);"
    "the original double scale has an explicit staged float conversion")
require_contains(
    _com_angle "offset = static_cast<float>(scaled + 0.5);"
    "the original double offset has an explicit staged float conversion")
require_contains(
    _com_angle "rounded = std::floor(offset);"
    "the float std::floor overload avoids narrowing")
require_contains(
    _com_angle "return static_cast<float>((scaled - rounded) * 360.0);"
    "the original double reconstruction has an explicit return conversion")
forbid_contains(
    _com_angle "#include <math.h>"
    "the C math header cannot reintroduce the double-only overload")
forbid_contains(
    _com_math "AngleDelta(float a1, float a2)"
    "AngleDelta must have one definition")
forbid_contains(
    _com_math "AngleSubtract(float a1, float a2)"
    "AngleSubtract must have one definition")
require_count(
    _common_files "universal/com_angle.cpp" 1
    "the production source manifest contains com_angle exactly once")
require_count(
    _tests_cmake "NAME upstream-reconciliation-angle-math-contracts" 1
    "CTest registers the runtime angle contract")
require_count(
    _tests_cmake "NAME upstream-reconciliation-aim-safety-contracts" 1
    "CTest registers the runtime aim safety contract")
require_count(
    _tests_cmake "NAME upstream-reconciliation-command-dispatch-contracts" 1
    "CTest registers the runtime command dispatch contract")
require_count(
    _tests_cmake "NAME upstream-reconciliation-source-invariants" 1
    "CTest registers this source contract")
require_count(
    _ci "kisakcod-upstream-angle-math-tests" 1
    "measured Windows x86 builds the runtime angle contract")
require_count(
    _ci "kisakcod-upstream-aim-safety-tests" 1
    "measured Windows x86 builds the runtime aim safety contract")
require_count(
    _ci "kisakcod-upstream-command-dispatch-tests" 1
    "measured Windows x86 builds the command dispatch contract")
require_count(
    _ci "upstream-reconciliation-(aim-safety-contracts|angle-math-contracts|command-dispatch-contracts|source-invariants)" 1
    "measured Windows x86 runs all reconciliation contracts")

if(NOT DEFINED CONTRACT_MUTATION OR CONTRACT_MUTATION STREQUAL "")
    foreach(_mutation IN ITEMS
        rdpvs_stale_def
        angle_unsigned
        angle_duplicate
        angle_global_floor
        angle_implicit_narrowing
        aim_no_release_guard
        aim_packed_abi
        aim_unbounded_weapon
        cmd_tail_skip
        cmd_direct_walk
        cmd_null_query
        cmd_null_node)
        execute_process(
            COMMAND "${CMAKE_COMMAND}"
                "-DSOURCE_ROOT=${SOURCE_ROOT}"
                "-DCONTRACT_MUTATION=${_mutation}"
                -P "${CMAKE_CURRENT_LIST_FILE}"
            RESULT_VARIABLE _mutation_result
            OUTPUT_QUIET ERROR_QUIET)
        if(_mutation_result EQUAL 0)
            message(FATAL_ERROR
                "Upstream reconciliation contract accepted mutation: "
                "${_mutation}")
        endif()
    endforeach()
endif()

message(STATUS "Curated upstream reconciliation source contract passed")
