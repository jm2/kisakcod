cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_header_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_system_disk32.h")
set(_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_system_disk32.cpp")
set(_manifest_path "${SOURCE_ROOT}/scripts/common_files.cmake")
set(_tests_path "${SOURCE_ROOT}/tests/CMakeLists.txt")
set(_ci_path "${SOURCE_ROOT}/.github/workflows/ci.yml")

foreach(_path IN ITEMS
    "${_header_path}"
    "${_source_path}"
    "${_manifest_path}"
    "${_tests_path}"
    "${_ci_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing FX system Disk32 source: ${_path}")
    endif()
endforeach()

file(READ "${_header_path}" _header)
file(READ "${_source_path}" _source)
file(READ "${_manifest_path}" _manifest)
file(READ "${_tests_path}" _tests)
file(READ "${_ci_path}" _ci)

foreach(_var IN ITEMS _header _source _manifest _tests _ci)
    string(REGEX REPLACE "[ \t\r\n]+" " " _normalized "${${_var}}")
    set(${_var} "${_normalized}")
endforeach()

function(require_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing FX system Disk32 invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_not_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden FX system Disk32 regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_ordered SOURCE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${FIRST}" _first)
    string(FIND "${${SOURCE_VAR}}" "${SECOND}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered FX system Disk32 invariant (${DESCRIPTION})")
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

# The fixed record schema must remain independent of native pointer width and
# every ABI-sensitive boundary must stay executable at compile time.
extract_slice(
    _header
    "struct ArchiveAddress32 final"
    "// Address-independent facts"
    _schema
    "fixed FxSystemDisk32 schema")
foreach(_marker IN ITEMS
    "struct ArchiveAddress32 final"
    "struct FxCameraDisk32 final"
    "struct FxSpriteInfoDisk32 final"
    "struct FxSystemDisk32 final"
    "std::uint8_t isInitialized;"
    "std::uint8_t needsGarbageCollection;"
    "std::uint8_t isArchiving;")
    require_contains(_schema "${_marker}" "fixed-width schema and layout")
endforeach()
foreach(_marker IN ITEMS
    "ONDISK_SIZE(ArchiveAddress32, 0x04);"
    "ONDISK_SIZE(FxCameraDisk32, 0xB0);"
    "ONDISK_SIZE(FxSpriteInfoDisk32, 0x10);"
    "ONDISK_SIZE(FxSystemDisk32, 0xA60);"
    "ONDISK_OFFSET(FxSystemDisk32, restartList, 0x9E0);")
    require_contains(_header "${_marker}" "fixed-width schema layout")
endforeach()
foreach(_forbidden IN ITEMS
    "PointerToken"
    "EffectDefinitionKey32"
    "reinterpret_cast"
    "std::size_t"
    " volatile "
    " bool ")
    require_not_contains(
        _schema "${_forbidden}" "archive records cannot inherit native ABI")
endforeach()
foreach(_marker IN ITEMS
    "std::endian::native == std::endian::little"
    "ONDISK_SIZE(float, 0x04);"
    "std::numeric_limits<float>::is_iec559"
    "RUNTIME_SIZE( FxSystemDisk32Metadata, MAX_EFFECTS + 2, MAX_EFFECTS + 2);"
    "alignof(FxSystemDisk32Metadata) == 1")
    require_contains(_header "${_marker}" "host representation precondition")
endforeach()

# The pure decoder treats archived addresses as numeric topology only, proves
# the entire image span, validates all ring handles before publishing, and
# leaves every native pointer detached for the later full-image transaction.
foreach(_marker IN ITEMS
    "FX_BUFFER_DISK32_SIZE = 0x47480u"
    "FX_BUFFER_ELEMS_OFFSET = 0x20000u"
    "FX_BUFFER_TRAILS_OFFSET = 0x34000u"
    "FX_BUFFER_TRAIL_ELEMS_OFFSET = 0x34400u"
    "FX_BUFFER_VIS_STATE_OFFSET = 0x44400u"
    "FX_BUFFER_DEFERRED_ELEMS_OFFSET = 0x46420u"
    "source.effects.value > (std::numeric_limits<std::uint32_t>::max)() - (FX_BUFFER_DISK32_SIZE - 1u)"
    "readSelector == writeSelector"
    "TryDecodeFxEffectHandleDisk32("
    "metadata.activeEffectSlots[effectIndex]"
    "source.activeSpotLightEffectCount == 0"
    "Disk32ElemHandleIsValid("
    "outSystem->effects = nullptr;"
    "outSystem->elems = nullptr;"
    "outSystem->trails = nullptr;"
    "outSystem->trailElems = nullptr;"
    "outSystem->deferredElems = nullptr;"
    "outSystem->visState = nullptr;"
    "outSystem->visStateBufferRead = nullptr;"
    "outSystem->visStateBufferWrite = nullptr;"
    "*outMetadata = metadata;"
    "return true;")
    require_contains(_source "${_marker}" "transactional numeric decoder")
endforeach()
require_ordered(
    _source
    "if (source.activeSpotLightElemCount == 1)"
    "UnpackCamera(source.camera, &outSystem->camera);"
    "all fallible spotlight validation precedes output publication")
require_ordered(
    _source
    "UnpackCamera(source.camera, &outSystem->camera);"
    "*outMetadata = metadata;"
    "metadata publishes after the native system")
extract_slice(
    _source
    "UnpackCamera(source.camera, &outSystem->camera);"
    "} // namespace fx::archive"
    _publication
    "infallible FxSystemDisk32 publication tail")
require_not_contains(
    _publication
    "return false;"
    "no failed return may follow the first output mutation")
require_not_contains(
    _publication
    "throw"
    "publication cannot unwind after the first output mutation")
foreach(_forbidden IN ITEMS
    "reinterpret_cast"
    "PointerToken"
    "EffectDefinitionKey32"
    "MemoryFile"
    "Z_Malloc"
    "malloc("
    "malloc ("
    "calloc("
    "calloc ("
    "realloc("
    "realloc ("
    "new "
    "new("
    "new ("
    "std::make_unique"
    "std::make_shared"
    "std::vector"
    "std::deque"
    "std::list"
    "std::map"
    "std::unordered_"
    "throw"
    "Com_Error"
    "Com_Print"
    "Sys_Error"
    "assert("
    "assert ("
    "memcpy(")
    require_not_contains(
        _source "${_forbidden}" "decoder must stay report-free and allocation-free")
endforeach()

# Keep production, portable tests, measured Windows x86, and the source
# contract wired together so this seam cannot silently disappear.
foreach(_marker IN ITEMS
    "fx_archive_system_disk32.cpp"
    "fx_archive_system_disk32.h")
    require_contains(_manifest "${_marker}" "engine source manifest")
endforeach()
foreach(_marker IN ITEMS
    "kisakcod-fx-archive-system-disk32-subject"
    "kisakcod_fx_helper_stack_budget( kisakcod-fx-archive-system-disk32-subject)"
    "effectscore-archive-system-disk32-codec")
    require_contains(_tests "${_marker}" "portable codec target")
endforeach()
require_contains(
    _ci
    "kisakcod-fx-archive-system-disk32-tests"
    "measured Windows x86 build")
require_contains(
    _ci
    "effectscore-archive-(disk32|system-disk32)-codec"
    "measured Windows x86 execution")
