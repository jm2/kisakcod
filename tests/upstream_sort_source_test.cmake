cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

function(read_normalized RELATIVE_PATH OUT_VARIABLE)
    set(_path "${SOURCE_ROOT}/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing curated-sort source: ${_path}")
    endif()
    file(READ "${_path}" _source)
    string(REGEX REPLACE "[ \t\r\n]+" " " _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(require_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing curated-sort invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden curated-sort regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_ordered SOURCE_VARIABLE FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${FIRST}" _first_position)
    string(FIND "${${SOURCE_VARIABLE}}" "${SECOND}" _second_position)
    if(_first_position EQUAL -1
        OR _second_position EQUAL -1
        OR _first_position GREATER_EQUAL _second_position)
        message(FATAL_ERROR
            "Missing or unordered curated-sort invariant (${DESCRIPTION})")
    endif()
endfunction()

function(require_count SOURCE_VARIABLE NEEDLE EXPECTED_COUNT DESCRIPTION)
    set(_remaining "${${SOURCE_VARIABLE}}")
    string(LENGTH "${NEEDLE}" _needle_length)
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
    if(NOT _count EQUAL EXPECTED_COUNT)
        message(FATAL_ERROR
            "Unexpected curated-sort invariant count (${DESCRIPTION}): "
            "expected ${EXPECTED_COUNT}, found ${_count}")
    endif()
endfunction()

# Upstream 6f0284ad attempted to repair these sorts. The assertions below pin
# the curated native-width implementation and the additional ordering fixes;
# they intentionally do not enroll that commit's unrelated matrix changes.
read_normalized("src/cgame/cg_modelpreviewer.cpp" _model_previewer)
require_contains(
    _model_previewer
    "return kisak::sort::CStringLess(string1, string2);"
    "model-preview names use a lexical strict predicate")
foreach(_field IN ITEMS model anim)
    require_contains(
        _model_previewer
        "sizeof(*g_mdlprv.system.${_field}Names)"
        "${_field} name allocation follows native pointer width")
    require_contains(
        _model_previewer
        "alignof(const char *)"
        "${_field} name allocation follows native pointer alignment")
    require_contains(
        _model_previewer
        "g_mdlprv.system.${_field}Names + g_mdlprv.system.${_field}Count, CG_ModPrvCompareString);"
        "${_field} names use a typed full-range sort")
    require_ordered(
        _model_previewer
        "g_mdlprv.system.${_field}Names[context.count] = nullptr;"
        "g_mdlprv.system.${_field}Names + g_mdlprv.system.${_field}Count, CG_ModPrvCompareString);"
        "${_field} terminator is installed before the active range is sorted")
endforeach()
require_count(
    _model_previewer "alignof(const char *)" 2
    "both native-pointer name tables retain explicit alignment")
foreach(_legacy IN ITEMS
    "(4 * g_mdlprv.system.modelCount)"
    "(4 * g_mdlprv.system.animCount)"
    "(Material **)g_mdlprv.system.modelNames"
    "(Material **)g_mdlprv.system.animNames"
    "KISAKTODO: fix sorting of model names"
    "v3 != 0")
    forbid_contains(
        _model_previewer "${_legacy}" "no fixed-width or asymmetric model sort")
endforeach()

read_normalized("src/gfx_d3d/r_material_load_obj.cpp" _materials)
require_contains(
    _materials
    "int __cdecl Material_Compare(const Material *mtl0, const Material *mtl1)"
    "material comparison takes typed pointer values")
require_contains(
    _materials
    "if (mtl0 == mtl1) return 0;"
    "material comparison remains irreflexive for duplicate pointers")
require_contains(
    _materials
    "return Material_Compare(left, right) < 0;"
    "std::sort adapts the three-way material ordering explicitly")
require_contains(
    _materials
    "kisak::sort::CompareFloatAscending( pixelLiteralConsts[0].value[constIndex][j], pixelLiteralConsts[1].value[constIndex][j]);"
    "material constants give NaNs a deterministic equivalence class")
foreach(_legacy IN ITEMS
    "qsort(sortedMaterials"
    "Material_Compare(const void *"
    "Material_Compare(const void*"
    "return Material_Compare(left, right) != 0;")
    forbid_contains(
        _materials "${_legacy}" "no untyped material sort may return")
endforeach()

read_normalized("src/gfx_d3d/r_shadowcookie.cpp" _shadows)
require_contains(
    _shadows
    "return kisak::sort::FloatLess(a.weight, b.weight);"
    "shadow weights use the strict floating-point predicate")
require_contains(
    _shadows
    "std::sort(candidates, candidates + ARRAY_COUNT(candidates), R_ShadowCandidatePred);"
    "all 24 shadow candidates are sorted")
forbid_contains(
    _shadows "&candidates[23]" "the final shadow candidate is not excluded")

read_normalized("src/EffectsCore/fx_marks.cpp" _fx_marks)
read_normalized("src/EffectsCore/fx_system.h" _fx_system)
foreach(_source IN ITEMS _fx_marks _fx_system)
    require_contains(
        ${_source}
        "bool __cdecl FX_CompareMarkTris(const FxMarkTri &tri0, const FxMarkTri &tri1)"
        "FX mark-triangle predicate has its actual bool type")
endforeach()
require_contains(
    _fx_marks
    "if (contextCompareResult) return contextCompareResult > 0; else return tri0.indices[0] < tri1.indices[0];"
    "FX comparison retains context then triangle-index ordering")
forbid_contains(
    _fx_system
    "int32_t __cdecl FX_CompareMarkTris"
    "the stale non-predicate FX declaration cannot return")

read_normalized("src/gfx_d3d/r_draw_method.cpp" _draw_method)
require_contains(
    _draw_method
    "gfxDrawMethod.litTechType[0][2] = TECHNIQUE_LIT_SPOT; gfxDrawMethod.litTechType[0][3] = TECHNIQUE_LIT_OMNI;"
    "spot and omni techniques occupy distinct slots")
forbid_contains(
    _draw_method
    "gfxDrawMethod.litTechType[0][2] = TECHNIQUE_LIT_OMNI;"
    "omni does not overwrite the spot technique")
foreach(_column RANGE 0 6)
    require_count(
        _draw_method "gfxDrawMethod.litTechType[0][${_column}] =" 1
        "draw-method row zero assigns column ${_column} exactly once")
endforeach()

# Master d5a6e799 already superseded upstream's fixed-four-byte FS rewrite.
read_normalized("src/universal/com_files.cpp" _com_files)
require_contains(
    _com_files
    "Sys_FileSystemSortPathPointers( filelist, static_cast<std::size_t>(numfiles));"
    "filesystem sorting delegates to the tested native-pointer service")
foreach(_legacy IN ITEMS
    "Z_Malloc(4 * numfiles)"
    "_DWORD"
    "memcpy(buf, filelist, 4 * numfiles)"
    "memcpy(filelist, buf, 4 * numfiles)")
    forbid_contains(
        _com_files "${_legacy}" "filesystem sorting remains native-width")
endforeach()

# The broad portable matrix exercises this runtime test on 64-bit Linux,
# Windows, and macOS hosts; the explicit Win32 engine job supplies ILP32.
read_normalized(".github/workflows/ci.yml" _ci)
require_contains(
    _ci
    "kisakcod-upstream-sort-tests"
    "Windows x86 explicitly builds the runtime sort contract")
require_contains(
    _ci
    "sort-(contracts|source-invariants)"
    "Windows x86 explicitly runs both curated sort contracts")

read_normalized("scripts/common_files.cmake" _common_manifest)
require_contains(
    _common_manifest
    [["${SRC_DIR}/universal/sort_utils.h"]]
    "the shared sort helper remains in the engine source manifest")
