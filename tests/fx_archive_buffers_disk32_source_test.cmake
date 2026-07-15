cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_header_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_buffers_disk32.h")
set(_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_buffers_disk32.cpp")
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
        message(FATAL_ERROR "Missing FX buffer Disk32 source: ${_path}")
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
            "Missing FX buffer Disk32 invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_not_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden FX buffer Disk32 regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_ordered SOURCE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${FIRST}" _first)
    string(FIND "${${SOURCE_VAR}}" "${SECOND}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered FX buffer Disk32 invariant (${DESCRIPTION})")
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

# The fixed buffer image is an exact, pointer-free 32-bit archive schema.
# Active/free union storage remains raw aligned bytes until a later transaction
# has resolved object lifetime and definition-dependent element payloads.
extract_slice(
    _header
    "struct FxElemDisk32 final"
    "// Address-independent ownership metadata"
    _schema
    "fixed FxSystemBuffersDisk32 schema")
foreach(_marker IN ITEMS
    "struct FxElemDisk32 final"
    "std::uint8_t payload[0x0C];"
    "std::uint8_t value[0x04];"
    "struct FxTrailDisk32 final"
    "struct FxTrailElemDisk32 final"
    "struct FxVisBlockerDisk32 final"
    "struct FxVisStateDisk32 final"
    "struct alignas(4) FxElemPoolSlotDisk32 final"
    "struct alignas(4) FxTrailPoolSlotDisk32 final"
    "struct alignas(4) FxTrailElemPoolSlotDisk32 final"
    "struct FxSystemBuffersDisk32 final")
    require_contains(_schema "${_marker}" "fixed-width raw buffer schema")
endforeach()
foreach(_forbidden IN ITEMS
    "union"
    "PointerToken"
    "ArchiveAddress32"
    "reinterpret_cast"
    "std::size_t"
    " volatile "
    " bool "
    "*")
    require_not_contains(
        _schema "${_forbidden}" "archive schema cannot inherit native ABI or object lifetime")
endforeach()

foreach(_marker IN ITEMS
    "ONDISK_SIZE(FxElemDisk32, 0x28);"
    "ONDISK_OFFSET(FxElemDisk32, payload, 0x18);"
    "ONDISK_OFFSET(FxElemDisk32, value, 0x24);"
    "ONDISK_SIZE(FxTrailDisk32, 0x08);"
    "alignof(FxTrailDisk32) == 2"
    "ONDISK_SIZE(FxTrailElemDisk32, 0x20);"
    "ONDISK_SIZE(FxVisBlockerDisk32, 0x10);"
    "ONDISK_SIZE(FxVisStateDisk32, 0x1010);"
    "ONDISK_OFFSET(FxVisStateDisk32, blockerCount, 0x1000);"
    "ONDISK_SIZE(FxElemPoolSlotDisk32, 0x28);"
    "ONDISK_SIZE(FxTrailPoolSlotDisk32, 0x08);"
    "ONDISK_SIZE(FxTrailElemPoolSlotDisk32, 0x20);"
    "alignof(FxElemPoolSlotDisk32) == 4"
    "alignof(FxTrailPoolSlotDisk32) == 4"
    "alignof(FxTrailElemPoolSlotDisk32) == 4"
    "ONDISK_SIZE(FxSystemBuffersDisk32, 0x47480);"
    "ONDISK_OFFSET(FxSystemBuffersDisk32, effects, 0x00000);"
    "ONDISK_OFFSET(FxSystemBuffersDisk32, elems, 0x20000);"
    "ONDISK_OFFSET(FxSystemBuffersDisk32, trails, 0x34000);"
    "ONDISK_OFFSET(FxSystemBuffersDisk32, trailElems, 0x34400);"
    "ONDISK_OFFSET(FxSystemBuffersDisk32, visState, 0x44400);"
    "ONDISK_OFFSET(FxSystemBuffersDisk32, deferredElems, 0x46420);"
    "ONDISK_OFFSET(FxSystemBuffersDisk32, padBuffer, 0x47420);"
    "std::endian::native == std::endian::little"
    "std::numeric_limits<float>::is_iec559")
    require_contains(_header "${_marker}" "executable fixed-layout boundary")
endforeach()

# Free-list reconstruction reads an explicit little-endian signed word only
# from visited raw slots, rejects every malformed topology, and stages all
# three bitmaps before publishing a single output byte.
foreach(_marker IN ITEMS
    "if (!outStates)"
    "ValidatePoolHeader<LIMIT>(firstFree, activeCount)"
    "activeCount < 0"
    "firstFree < -1"
    "poolIsFull == (firstFree == -1)"
    "std::uint32_t{slot.bytes[0]}"
    "std::uint32_t{slot.bytes[1]} << 8u"
    "std::uint32_t{slot.bytes[2]} << 16u"
    "std::uint32_t{slot.bytes[3]} << 24u"
    "raw == (std::numeric_limits<std::uint32_t>::max)()"
    "raw > static_cast<std::uint32_t>( (std::numeric_limits<std::int32_t>::max)())"
    "rebuilt.allocatedWords.fill( (std::numeric_limits<std::uint64_t>::max)())"
    "rebuilt.allocatedWords.data()[index / wordBits]"
    "freeCount >= LIMIT"
    "nextFree == freeIndex"
    "rebuilt.allocatedCount != static_cast<std::size_t>(activeCount)"
    "FxSystemBuffersDisk32PoolStates rebuilt{};"
    "buffers.elems, system.firstFreeElem, system.activeElemCount, &rebuilt.elems"
    "buffers.trails, system.firstFreeTrail, system.activeTrailCount, &rebuilt.trails"
    "buffers.trailElems, system.firstFreeTrailElem, system.activeTrailElemCount, &rebuilt.trailElems"
    "*outStates = rebuilt;"
    "return true;")
    require_contains(_source "${_marker}" "bounded transactional free-list reconstruction")
endforeach()
require_ordered(
    _source
    "FxSystemBuffersDisk32PoolStates rebuilt{};"
    "*outStates = rebuilt;"
    "all pool validation precedes output publication")
extract_slice(
    _source
    "*outStates = rebuilt;"
    "} // namespace fx::archive"
    _publication
    "infallible buffer-state publication tail")
require_not_contains(
    _publication
    "return false;"
    "no failed return may follow output publication")
require_not_contains(
    _publication
    "throw"
    "publication cannot unwind after output mutation")
foreach(_forbidden IN ITEMS
    "sizeof("
    "sizeof ("
    "reinterpret_cast"
    "FxPool<"
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
    " assert("
    " assert ("
    "memcpy("
    "memcpy (")
    require_not_contains(
        _source "${_forbidden}" "decoder must stay ABI-neutral, report-free, and allocation-free")
endforeach()

# Keep production, portable tests, measured Windows x86, and this source
# contract wired together so the seam cannot silently disappear.
foreach(_marker IN ITEMS
    "fx_archive_buffers_disk32.cpp"
    "fx_archive_buffers_disk32.h")
    require_contains(_manifest "${_marker}" "engine source manifest")
endforeach()
foreach(_marker IN ITEMS
    "kisakcod-fx-archive-buffers-disk32-subject"
    "kisakcod_fx_helper_stack_budget( kisakcod-fx-archive-buffers-disk32-subject)"
    "effectscore-archive-buffers-disk32-codec"
    "effectscore-archive-buffers-disk32-source-invariants"
    "fx_archive_buffers_disk32_source_test.cmake")
    require_contains(_tests "${_marker}" "portable codec and source-contract target")
endforeach()
require_contains(
    _ci
    "kisakcod-fx-archive-buffers-disk32-tests"
    "measured Windows x86 build")
require_contains(
    _ci
    "effectscore-archive-(disk32|system-disk32|buffers-disk32)-codec"
    "measured Windows x86 execution")
