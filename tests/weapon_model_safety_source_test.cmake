cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

function(load_text RELATIVE_PATH OUT_VARIABLE)
    set(_path "${SOURCE_ROOT}/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing weapon-model safety input: ${_path}")
    endif()
    file(READ "${_path}" _source)
    string(REPLACE "\r\n" "\n" _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(extract_slice
    SOURCE_VARIABLE START_MARKER END_MARKER OUT_VARIABLE DESCRIPTION)
    set(_source "${${SOURCE_VARIABLE}}")
    string(FIND "${_source}" "${START_MARKER}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR
            "Missing weapon-model slice start (${DESCRIPTION}): "
            "'${START_MARKER}'")
    endif()

    string(SUBSTRING "${_source}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${END_MARKER}" _relative_end)
    if(_relative_end LESS_EQUAL 0)
        message(FATAL_ERROR
            "Missing ordered weapon-model slice end (${DESCRIPTION}): "
            "'${END_MARKER}'")
    endif()

    string(SUBSTRING "${_tail}" 0 ${_relative_end} _slice)
    set(${OUT_VARIABLE} "${_slice}" PARENT_SCOPE)
endfunction()

function(require_contains HAYSTACK_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${HAYSTACK_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing weapon-model invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains HAYSTACK_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${HAYSTACK_VARIABLE}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden weapon-model regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_ordered HAYSTACK_VARIABLE FIRST SECOND DESCRIPTION)
    string(FIND "${${HAYSTACK_VARIABLE}}" "${FIRST}" _first_position)
    string(FIND "${${HAYSTACK_VARIABLE}}" "${SECOND}" _second_position)
    if(_first_position EQUAL -1
        OR _second_position EQUAL -1
        OR _first_position GREATER_EQUAL _second_position)
        message(FATAL_ERROR
            "Missing or unordered weapon-model invariant (${DESCRIPTION})")
    endif()
endfunction()

function(require_count HAYSTACK_VARIABLE NEEDLE EXPECTED_COUNT DESCRIPTION)
    set(_remaining "${${HAYSTACK_VARIABLE}}")
    set(_count 0)
    string(LENGTH "${NEEDLE}" _needle_length)
    if(_needle_length EQUAL 0)
        message(FATAL_ERROR "Empty weapon-model count needle (${DESCRIPTION})")
    endif()

    while(TRUE)
        string(FIND "${_remaining}" "${NEEDLE}" _position)
        if(_position EQUAL -1)
            break()
        endif()
        math(EXPR _count "${_count} + 1")
        math(EXPR _next "${_position} + ${_needle_length}")
        string(SUBSTRING "${_remaining}" ${_next} -1 _remaining)
    endwhile()

    if(NOT _count EQUAL EXPECTED_COUNT)
        message(FATAL_ERROR
            "Unexpected weapon-model invariant count (${DESCRIPTION}): "
            "expected ${EXPECTED_COUNT}, found ${_count}")
    endif()
endfunction()

function(forbid_dynamic_gun_model_access SOURCE_VARIABLE DESCRIPTION)
    set(_source "${${SOURCE_VARIABLE}}")
    string(REGEX REPLACE
        "gunXModel[ \t\r\n]*\\[[ \t\r\n]*0[uUlL]*[ \t\r\n]*\\]"
        "gunXModel_constant_zero"
        _source
        "${_source}")
    string(REGEX MATCH
        "gunXModel[ \t\r\n]*\\["
        _dynamic_access
        "${_source}")
    if(NOT _dynamic_access STREQUAL "")
        message(FATAL_ERROR
            "Dynamic gunXModel access bypasses the checked helper "
            "(${DESCRIPTION})")
    endif()
endfunction()

load_text("src/bgame/bg_weapon_model_safety.h" _helper)
load_text("src/bgame/bg_weapons.cpp" _bg_weapons)
load_text("src/cgame/cg_weapons.cpp" _cg_weapons)
load_text("src/cgame/cg_draw.cpp" _cg_draw_sp)
load_text("src/cgame_mp/cg_draw_mp.cpp" _cg_draw_mp)
load_text("src/game/g_weapon.cpp" _g_weapon)
load_text("src/game/g_client_script_cmd.cpp" _g_client_sp)
load_text("src/game_mp/g_client_script_cmd_mp.cpp" _g_client_mp)
load_text("src/database/db_load.cpp" _db_load)
load_text("scripts/common_files.cmake" _common_files)
load_text("tests/CMakeLists.txt" _tests_cmake)
load_text(".github/workflows/ci.yml" _ci)

if(DEFINED CONTRACT_MUTATION
    AND CONTRACT_MUTATION STREQUAL "helper_missing_signed_guard")
    string(REPLACE
        "if (modelIndex < 0\n        || static_cast<std::size_t>(modelIndex) >= ModelCount)"
        "if (static_cast<std::size_t>(modelIndex) >= ModelCount)"
        _helper "${_helper}")
elseif(DEFINED CONTRACT_MUTATION
    AND CONTRACT_MUTATION STREQUAL "mp_unsigned_script_input")
    string(REPLACE
        "const int32_t requestedWeaponModel = Scr_GetInt(1);"
        "const uint32_t requestedWeaponModel = Scr_GetInt(1);"
        _g_client_mp "${_g_client_mp}")
elseif(DEFINED CONTRACT_MUTATION
    AND CONTRACT_MUTATION STREQUAL "sp_direct_script_index")
    string(REPLACE
        "weaponModel = bg::weapon_model::ResolveIndex(\n            weaponDef->gunXModel, requestedWeaponModel);"
        "weaponModel = weaponDef->gunXModel[requestedWeaponModel]\n            ? static_cast<uint8_t>(requestedWeaponModel) : 0;"
        _g_client_sp "${_g_client_sp}")
elseif(DEFINED CONTRACT_MUTATION
    AND CONTRACT_MUTATION STREQUAL "give_sink_direct_index")
    string(REPLACE
        "if (!bg::weapon_model::CheckedLookup(\n            weapDef->gunXModel, altModelIndex))"
        "if (!weapDef->gunXModel[altModelIndex])"
        _g_weapon "${_g_weapon}")
elseif(DEFINED CONTRACT_MUTATION
    AND CONTRACT_MUTATION STREQUAL "cgame_second_lookup")
    string(REPLACE
        "dobjModels[1].model = gunModel;"
        "dobjModels[1].model = weapDef->gunXModel[weaponModel];"
        _cg_weapons "${_cg_weapons}")
elseif(DEFINED CONTRACT_MUTATION
    AND CONTRACT_MUTATION STREQUAL "can_player_array_decay")
    string(REPLACE
        "gunXModel[0] != nullptr"
        "gunXModel != 0"
        _bg_weapons "${_bg_weapons}")
elseif(DEFINED CONTRACT_MUTATION
    AND CONTRACT_MUTATION STREQUAL "future_header_alias")
    set(_inject_future_header_alias TRUE)
elseif(DEFINED CONTRACT_MUTATION
    AND CONTRACT_MUTATION STREQUAL "commented_helper_alias")
    set(_inject_commented_helper_alias TRUE)
elseif(DEFINED CONTRACT_MUTATION
    AND CONTRACT_MUTATION STREQUAL "db_loader_relocated_alias")
    string(REPLACE
        "    varXModelPtr = varWeaponDef->gunXModel;\n    Load_XModelPtrArray(0, 16);"
        "    Load_XModelPtrArray(0, 16);"
        _db_load "${_db_load}")
    string(APPEND _db_load
        "\nvarXModelPtr = varWeaponDef->gunXModel;\n")
elseif(DEFINED CONTRACT_MUTATION
    AND CONTRACT_MUTATION STREQUAL "db_loader_wrong_count")
    string(REPLACE
        "Load_XModelPtrArray(0, 16);"
        "Load_XModelPtrArray(0, 17);"
        _db_load "${_db_load}")
elseif(DEFINED CONTRACT_MUTATION AND NOT CONTRACT_MUTATION STREQUAL "")
    message(FATAL_ERROR
        "Unknown weapon-model contract mutation: ${CONTRACT_MUTATION}")
endif()

extract_slice(
    _helper
    "[[nodiscard]] constexpr ModelPointer CheckedLookup("
    "template <typename ModelPointer, std::size_t ModelCount>"
    _checked_lookup
    "CheckedLookup")
string(REGEX REPLACE
    "[ \t\r\n]+" " " _checked_lookup_normalized "${_checked_lookup}")
require_contains(
    _checked_lookup_normalized
    "if (modelIndex < 0 || static_cast<std::size_t>(modelIndex) >= ModelCount)"
    "signed indices are rejected before conversion and indexing")
require_ordered(
    _checked_lookup
    "if (modelIndex < 0"
    "return models[static_cast<std::size_t>(modelIndex)];"
    "the signed and extent guards precede the only array access")

extract_slice(
    _helper
    "[[nodiscard]] constexpr std::uint8_t ResolveIndex("
    "} // namespace bg::weapon_model"
    _resolve_index
    "ResolveIndex")
require_ordered(
    _resolve_index
    "CheckedLookup(models, requestedIndex)"
    "static_cast<std::uint8_t>(requestedIndex)"
    "the checked lookup precedes narrowing")

extract_slice(
    _g_client_mp
    "void __cdecl PlayerCmd_giveWeapon("
    "void __cdecl G_InitializeAmmo("
    _give_weapon_mp
    "MP PlayerCmd_giveWeapon")
extract_slice(
    _g_client_sp
    "void __cdecl PlayerCmd_giveWeapon("
    "void __cdecl PlayerCmd_takeWeapon("
    _give_weapon_sp
    "SP PlayerCmd_giveWeapon")
foreach(_give_slice IN ITEMS _give_weapon_mp _give_weapon_sp)
    require_contains(
        ${_give_slice}
        "uint8_t weaponModel = 0;"
        "the final model byte is initialized")
    require_contains(
        ${_give_slice}
        "const int32_t requestedWeaponModel = Scr_GetInt(1);"
        "script input remains signed until validation")
    require_count(
        ${_give_slice}
        "bg::weapon_model::ResolveIndex(" 1
        "the script path resolves the model exactly once")
    require_ordered(
        ${_give_slice}
        "requestedWeaponModel = Scr_GetInt(1);"
        "bg::weapon_model::ResolveIndex("
        "script input is captured before checked resolution")
    require_ordered(
        ${_give_slice}
        "bg::weapon_model::ResolveIndex("
        "G_GivePlayerWeapon("
        "model validation precedes weapon publication")
    forbid_contains(
        ${_give_slice}
        "LOBYTE(weaponModel)"
        "partial-byte initialization cannot return")
    forbid_contains(
        ${_give_slice}
        "weaponModel = Scr_GetInt(1)"
        "script input cannot narrow directly into the model byte")
endforeach()
forbid_contains(
    _give_weapon_mp "goto LABEL_20"
    "the MP invalid-input path no longer bypasses common validation")

extract_slice(
    _g_weapon
    "int __cdecl G_GivePlayerWeapon("
    "void __cdecl G_SetupWeaponDef("
    _give_sink
    "G_GivePlayerWeapon")
require_count(
    _give_sink "bg::weapon_model::CheckedLookup(" 1
    "the common give sink validates the model exactly once")
require_ordered(
    _give_sink
    "bg::weapon_model::CheckedLookup("
    "Com_BitSetAssert("
    "the common give sink validates before changing player state")

extract_slice(
    _cg_weapons
    "void __cdecl ChangeViewmodelDobj("
    "void __cdecl CG_UpdateHandViewmodels("
    _viewmodel_change
    "ChangeViewmodelDobj")
require_count(
    _viewmodel_change "bg::weapon_model::CheckedLookup(" 1
    "viewmodel construction performs one checked lookup")
require_contains(
    _viewmodel_change
    "dobjModels[1].model = gunModel;"
    "viewmodel construction reuses the validated pointer")

require_count(
    _cg_draw_sp "bg::weapon_model::CheckedLookup(" 1
    "SP diagnostic drawing uses the checked lookup")
require_count(
    _cg_draw_mp "bg::weapon_model::CheckedLookup(" 1
    "MP diagnostic drawing uses the checked lookup")

extract_slice(
    _bg_weapons
    "bool __cdecl BG_CanPlayerHaveWeapon("
    "bool __cdecl BG_ValidateWeaponNumber("
    _can_player_have_weapon
    "BG_CanPlayerHaveWeapon")
require_contains(
    _can_player_have_weapon
    "gunXModel[0] != nullptr"
    "weapon availability tests the default model slot")
forbid_contains(
    _can_player_have_weapon
    "gunXModel != 0"
    "an inline array must not be compared with null")

foreach(_production_source IN ITEMS
    _bg_weapons
    _cg_weapons
    _cg_draw_sp
    _cg_draw_mp
    _g_weapon
    _g_client_sp
    _g_client_mp)
    forbid_dynamic_gun_model_access(
        ${_production_source} "${_production_source}")
endforeach()

extract_slice(
    _db_load
    "void __cdecl Load_WeaponDef(bool atStreamStart)"
    "void __cdecl Load_WeaponDefPtr(bool atStreamStart)"
    _load_weapon_def
    "Load_WeaponDef")
extract_slice(
    _db_load
    "void __cdecl Mark_WeaponDef()"
    "void __cdecl Mark_WeaponDefPtr()"
    _mark_weapon_def
    "Mark_WeaponDef")
string(REGEX REPLACE
    "[ \t\r\n]+" " " _load_weapon_def_normalized "${_load_weapon_def}")
string(REGEX REPLACE
    "[ \t\r\n]+" " " _mark_weapon_def_normalized "${_mark_weapon_def}")
require_contains(
    _load_weapon_def_normalized
    "varXModelPtr = varWeaponDef->gunXModel; Load_XModelPtrArray(0, 16);"
    "the load alias is consumed immediately by the fixed 16-model walk")
require_contains(
    _mark_weapon_def_normalized
    "varXModelPtr = varWeaponDef->gunXModel; Mark_XModelPtrArray(16);"
    "the mark alias is consumed immediately by the fixed 16-model walk")
require_count(
    _db_load "varXModelPtr = varWeaponDef->gunXModel;" 2
    "only the two exact fixed-count asset walks decay the model array")

# Seal future production call sites as well as the curated set above. Literal
# slot-zero reads, exact checked-helper arguments, the frozen declaration, and
# the two fixed-count database-loader walks are the only reviewed occurrences.
# Everything else remains as residual text and fails, so replacing an allowed
# occurrence with a bare pointer alias cannot evade this gate by preserving a
# global count. Header and inline sources are included.
file(GLOB_RECURSE _production_cxx_files
    "${SOURCE_ROOT}/src/*.c"
    "${SOURCE_ROOT}/src/*.cc"
    "${SOURCE_ROOT}/src/*.cpp"
    "${SOURCE_ROOT}/src/*.cxx"
    "${SOURCE_ROOT}/src/*.h"
    "${SOURCE_ROOT}/src/*.hh"
    "${SOURCE_ROOT}/src/*.hpp"
    "${SOURCE_ROOT}/src/*.hxx"
    "${SOURCE_ROOT}/src/*.inc"
    "${SOURCE_ROOT}/src/*.inl")
get_filename_component(_source_root_absolute "${SOURCE_ROOT}" ABSOLUTE)
set(_unreviewed_gun_model_references "")
foreach(_production_cxx IN LISTS _production_cxx_files)
    file(READ "${_production_cxx}" _production_cxx_source)
    file(RELATIVE_PATH _production_relative
        "${_source_root_absolute}/src" "${_production_cxx}")
    if(_inject_commented_helper_alias
        AND _production_relative STREQUAL
            "bgame/bg_weapon_input_safety.h")
        string(APPEND _production_cxx_source
            "\nauto *unchecked =\n"
            "    // bg::weapon_model::CheckedLookup(\n"
            "    weaponDef->gunXModel, *copy = unchecked;\n")
    endif()

    # Whitespace normalization must never join an allowlisted helper prefix
    # from a line comment to a real pointer-decay expression on the next line.
    foreach(_helper_name IN ITEMS CheckedLookup ResolveIndex)
        string(REGEX MATCH
            "//[^\r\n]*bg::weapon_model::${_helper_name}[ \t]*\\("
            _commented_helper_prefix "${_production_cxx_source}")
        if(NOT _commented_helper_prefix STREQUAL "")
            message(FATAL_ERROR
                "Commented weapon-model helper prefix in "
                "${_production_relative}")
        endif()
    endforeach()

    string(REGEX REPLACE
        "[ \t\r\n]+" " " _production_cxx_residual
        "${_production_cxx_source}")

    # A literal default-model read is safe in any production source.
    string(REGEX REPLACE
        "gunXModel[ ]*\\[[ ]*0[uUlL]*[ ]*\\]"
        "reviewed_default_model"
        _production_cxx_residual "${_production_cxx_residual}")

    # These exact forms keep the array bound visible to the checked helper.
    foreach(_checked_prefix IN ITEMS
        "bg::weapon_model::CheckedLookup( weapDef->gunXModel,"
        "bg::weapon_model::ResolveIndex( weapDef->gunXModel,"
        "bg::weapon_model::ResolveIndex( weaponDef->gunXModel,")
        string(REPLACE
            "${_checked_prefix}"
            "reviewed_weapon_model_helper("
            _production_cxx_residual "${_production_cxx_residual}")
    endforeach()

    if(_production_relative STREQUAL "xanim/xanim.h")
        string(REPLACE
            "XModel* gunXModel[16];"
            "reviewed_weapon_model_declaration;"
            _production_cxx_residual "${_production_cxx_residual}")
    elseif(_production_relative STREQUAL "database/db_load.cpp")
        string(REPLACE
            "varXModelPtr = varWeaponDef->gunXModel;"
            "reviewed_fixed_count_weapon_model_load;"
            _production_cxx_residual "${_production_cxx_residual}")
    endif()

    string(FIND "${_production_cxx_residual}" "gunXModel" _residual_position)
    if(NOT _residual_position EQUAL -1)
        string(APPEND _unreviewed_gun_model_references
            "\n${_production_relative}")
    endif()
endforeach()
if(_inject_future_header_alias)
    string(APPEND _unreviewed_gun_model_references
        "\nfuture/header_alias.h")
endif()
if(NOT _unreviewed_gun_model_references STREQUAL "")
    message(FATAL_ERROR
        "Unreviewed production gunXModel reference(s):"
        "${_unreviewed_gun_model_references}")
endif()
require_count(
    _db_load "varWeaponDef->gunXModel" 2
    "the only intentional pointer-decay uses are fixed-count asset loading")

require_count(
    _common_files
    "\"\${SRC_DIR}/bgame/bg_weapon_model_safety.h\"" 1
    "the production manifest owns the helper exactly once")
require_count(
    _tests_cmake
    "add_executable(kisakcod-weapon-model-safety-tests" 1
    "the runtime contract target is registered")
require_count(
    _tests_cmake
    "NAME weapon-model-safety-contracts" 1
    "the runtime contract is exposed through CTest")
require_count(
    _tests_cmake
    "NAME weapon-model-safety-source-invariants" 1
    "this source contract is exposed through CTest")
require_count(
    _ci
    "kisakcod-weapon-model-safety-tests" 1
    "measured Windows x86 builds the runtime contract")
require_count(
    _ci
    "weapon-model-safety-(contracts|source-invariants)" 1
    "measured Windows x86 runs both weapon-model contracts")

if(NOT DEFINED CONTRACT_MUTATION OR CONTRACT_MUTATION STREQUAL "")
    foreach(_mutation IN ITEMS
        helper_missing_signed_guard
        mp_unsigned_script_input
        sp_direct_script_index
        give_sink_direct_index
        cgame_second_lookup
        can_player_array_decay
        future_header_alias
        commented_helper_alias
        db_loader_relocated_alias
        db_loader_wrong_count)
        execute_process(
            COMMAND "${CMAKE_COMMAND}"
                "-DSOURCE_ROOT=${SOURCE_ROOT}"
                "-DCONTRACT_MUTATION=${_mutation}"
                -P "${CMAKE_CURRENT_LIST_FILE}"
            RESULT_VARIABLE _mutation_result
            OUTPUT_QUIET
            ERROR_QUIET)
        if(_mutation_result EQUAL 0)
            message(FATAL_ERROR
                "Weapon-model contract accepted mutation: ${_mutation}")
        endif()
    endforeach()
endif()

message(STATUS "Weapon-model safety source contract passed")
