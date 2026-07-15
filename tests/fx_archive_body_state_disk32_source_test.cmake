cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_native_header_path
    "${SOURCE_ROOT}/src/physics/phys_body_state.h")
set(_physics_header_path
    "${SOURCE_ROOT}/src/physics/phys_local.h")
set(_physics_source_path
    "${SOURCE_ROOT}/src/physics/phys_ode.cpp")
set(_codec_header_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_body_state_disk32.h")
set(_codec_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_body_state_disk32.cpp")
set(_codec_tests_path
    "${SOURCE_ROOT}/tests/fx_archive_body_state_disk32_tests.cpp")

foreach(_path IN ITEMS
    "${_native_header_path}"
    "${_physics_header_path}"
    "${_physics_source_path}"
    "${_codec_header_path}"
    "${_codec_source_path}"
    "${_codec_tests_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing BodyState Disk32 source: ${_path}")
    endif()
endforeach()

file(READ "${_native_header_path}" _native_header)
file(READ "${_physics_header_path}" _physics_header)
file(READ "${_physics_source_path}" _physics_source)
file(READ "${_codec_header_path}" _codec_header)
file(READ "${_codec_source_path}" _codec_source)
file(READ "${_codec_tests_path}" _codec_tests)

foreach(_var IN ITEMS
    _native_header
    _physics_header
    _physics_source
    _codec_header
    _codec_source
    _codec_tests)
    string(REGEX REPLACE "[ \t\r\n]+" " " _normalized "${${_var}}")
    set(${_var} "${_normalized}")
endforeach()

function(require_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing BodyState Disk32 invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_not_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden BodyState Disk32 regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_ordered SOURCE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${FIRST}" _first)
    string(FIND "${${SOURCE_VAR}}" "${SECOND}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered BodyState Disk32 invariant (${DESCRIPTION})")
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

# BodyState must remain one lightweight, pointer-free native definition. The
# heavyweight physics header consumes it but cannot retain a second schema.
foreach(_marker IN ITEMS
    "#include <universal/kisak_abi.h>"
    "struct BodyState"
    "float position[3];"
    "float rotation[3][3];"
    "float velocity[3];"
    "float angVelocity[3];"
    "float centerOfMassOffset[3];"
    "int state;"
    "int timeLastAsleep;"
    "int type;"
    "int underwater;"
    "RUNTIME_SIZE(BodyState, 0x70, 0x70);"
    "RUNTIME_OFFSET(BodyState, underwater, 0x6C, 0x6C);"
    "alignof(BodyState) == 4"
    "std::is_standard_layout_v<BodyState>"
    "std::is_trivially_copyable_v<BodyState>")
    require_contains(_native_header "${_marker}" "native leaf layout")
endforeach()
foreach(_forbidden IN ITEMS
    "xanim/"
    "ode/"
    "d3d"
    "MemoryFile"
    "phys_local.h"
    "void *"
    "void*")
    require_not_contains(
        _native_header "${_forbidden}" "native BodyState leaf dependency")
endforeach()
require_contains(
    _physics_header
    "#include <physics/phys_body_state.h>"
    "heavy physics header consumes the leaf")
require_not_contains(
    _physics_header
    "struct BodyState "
    "BodyState must have one definition")

# The persisted schema is fixed-width and naturally aligned on every target.
foreach(_marker IN ITEMS
    "struct BodyStateDisk32 final"
    "std::int32_t state;"
    "std::int32_t timeLastAsleep;"
    "std::int32_t type;"
    "std::uint32_t underwater;"
    "std::endian::native == std::endian::little"
    "std::numeric_limits<float>::is_iec559"
    "ONDISK_SIZE(BodyStateDisk32, 0x70);"
    "ONDISK_OFFSET(BodyStateDisk32, position, 0x00);"
    "ONDISK_OFFSET(BodyStateDisk32, rotation, 0x0C);"
    "ONDISK_OFFSET(BodyStateDisk32, timeLastAsleep, 0x64);"
    "ONDISK_OFFSET(BodyStateDisk32, underwater, 0x6C);"
    "alignof(BodyStateDisk32) == 4"
    "std::is_standard_layout_v<BodyStateDisk32>"
    "std::is_trivially_copyable_v<BodyStateDisk32>"
    "TryUnpackBodyStateDisk32( const BodyStateDisk32 &source, std::int32_t archiveTime, BodyState *outState) noexcept;")
    require_contains(_codec_header "${_marker}" "fixed Disk32 schema")
endforeach()
foreach(_forbidden IN ITEMS
    "std::size_t state;"
    "std::size_t timeLastAsleep;"
    "std::size_t type;"
    "bool underwater;"
    "BodyState state;"
    "#pragma pack")
    require_not_contains(
        _codec_header "${_forbidden}" "disk schema cannot inherit native ABI")
endforeach()

# Preserve the exact existing FX acceptance policy, including FLT_MAX
# friction and dirty legacy upper bytes in the underwater word.
foreach(_marker IN ITEMS
    "SPATIAL_COMPONENT_MAX = 1048576.0f"
    "LINEAR_VELOCITY_MAX = 1048576.0f"
    "ANGULAR_VELOCITY_MAX = 65536.0f"
    "PHYSICS_MASS_MIN = 0.0001f"
    "PHYSICS_MASS_MAX = 1000000.0f"
    "PHYSICS_FRICTION_MAX = 10000.0f"
    "PHYSICS_BOUNCE_MAX = 1.0f"
    "ROTATION_COMPONENT_MAX = 1.001f"
    "UNIT_LENGTH_TOLERANCE = 0.025"
    "ORTHOGONAL_TOLERANCE = 0.025"
    "source.friction == (std::numeric_limits<float>::max)()"
    "source.state >= PHYSICS_STATE_MIN"
    "source.state <= PHYSICS_STATE_MAX"
    "source.type >= SOUND_CLASS_MIN"
    "source.type <= SOUND_CLASS_MAX"
    "(source.underwater & UNDERWATER_VALUE_MASK) <= 1u")
    require_contains(_codec_source "${_marker}" "validation parity")
endforeach()

require_ordered(
    _codec_source
    "if (!outState || archiveTime < 0 || !BodyStateIsValid(source))"
    "BodyState decoded{};"
    "all validation precedes native conversion")
require_ordered(
    _codec_source
    "decoded.timeLastAsleep = static_cast<int>(archiveTime);"
    "*outState = decoded;"
    "archive time must canonicalize before atomic output commit")
require_ordered(
    _codec_source
    "decoded.underwater = static_cast<int>( source.underwater & UNDERWATER_VALUE_MASK);"
    "*outState = decoded;"
    "legacy low byte must canonicalize before atomic output commit")
require_not_contains(
    _codec_source
    "source.timeLastAsleep"
    "untrusted serialized sleep time must never enter native state")
require_not_contains(
    _codec_source
    "outState->"
    "decoder may publish only the completed local state")

extract_slice(
    _codec_source
    "*outState = decoded;"
    "} // namespace fx::archive"
    _publication_tail
    "failure-free output publication tail")
require_not_contains(
    _publication_tail
    "return false;"
    "no failure may follow output publication")
foreach(_forbidden IN ITEMS
    "reinterpret_cast"
    "MemoryFile"
    "phys_local.h"
    "ODE_"
    "dBody"
    "Z_Malloc"
    "malloc("
    "calloc("
    "realloc("
    "new "
    "std::vector"
    "std::map"
    "std::unordered_"
    "Com_Error"
    "Com_Print"
    "Sys_Error"
    "Sys_EnterCriticalSection"
    "assert("
    "memcpy(")
    require_not_contains(
        _codec_source "${_forbidden}" "codec must stay portable and report-free")
endforeach()

# The generic writer must initialize all 112 bytes and assign the complete
# boolean word. This keeps the low byte wire-compatible while preventing the
# old three-byte stack disclosure.
extract_slice(
    _physics_source
    "void __cdecl Phys_ObjSave("
    "void __cdecl Phys_GetStateFromBody("
    _save
    "generic physics body writer")
foreach(_marker IN ITEMS
    "BodyState state{};"
    "Phys_GetStateFromBody(id, &state);"
    "MemFile_WriteData(memFile, 112, &state);")
    require_contains(_save "${_marker}" "deterministic body writer")
endforeach()
require_ordered(
    _save
    "BodyState state{};"
    "MemFile_WriteData(memFile, 112, &state);"
    "zero initialization must precede raw serialization")

extract_slice(
    _physics_source
    "void __cdecl Phys_GetStateFromBody("
    "dxBody *__cdecl Phys_ObjLoad("
    _capture
    "generic physics body capture")
require_contains(
    _capture
    "state->underwater = dBodyIsEnabled(body) ? 1 : 0;"
    "capture assigns the complete integer word")
require_not_contains(
    _capture
    "LOBYTE(state->underwater)"
    "capture cannot leave three indeterminate bytes")

# Behavioral coverage pins layout, conversion, every validation family,
# legacy compatibility, timestamp replacement, and failure atomicity.
foreach(_marker IN ITEMS
    "TestLayoutAndGoldenBytes()"
    "TestCompleteMappingAndCanonicalization()"
    "TestArgumentsAndFailureAtomicity()"
    "TestVectorBounds()"
    "TestRotationPolicy()"
    "TestScalarPolicies()"
    "TestIntegerPolicies()"
    "UINT32_C(0xFFFFFF00)"
    "UINT32_C(0x80000001)"
    "UINT32_C(0xFFFFFF02)"
    "UINT32_C(0x123456FF)"
    "dirtyZero.timeLastAsleep = INT32_MAX;"
    "dirtyOne.timeLastAsleep = INT32_MIN;"
    "output.timeLastAsleep == INT32_MAX"
    "ObjectBytes(output) == before"
    "std::numeric_limits<float>::quiet_NaN"
    "std::numeric_limits<float>::infinity"
    "std::nextafter"
    "state.rotation[0][0] = -1.0f"
    "state.state = -1"
    "state.state = 3"
    "state.type = -1"
    "state.type = 50")
    require_contains(_codec_tests "${_marker}" "focused behavioral coverage")
endforeach()
