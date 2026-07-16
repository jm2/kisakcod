cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_disk32_path "${SOURCE_ROOT}/src/database/db_disk32.h")
set(_header_path "${SOURCE_ROOT}/src/database/db_xasset_disk32.h")
set(_source_path "${SOURCE_ROOT}/src/database/db_xasset_disk32.cpp")
set(_fixture_path
    "${SOURCE_ROOT}/tests/db_script_string_disk32_tests.cpp")
set(_manifest_path "${SOURCE_ROOT}/scripts/common_files.cmake")
set(_tests_path "${SOURCE_ROOT}/tests/CMakeLists.txt")
set(_ci_path "${SOURCE_ROOT}/.github/workflows/ci.yml")

foreach(_path IN ITEMS
    "${_disk32_path}"
    "${_header_path}"
    "${_source_path}"
    "${_fixture_path}"
    "${_manifest_path}"
    "${_tests_path}"
    "${_ci_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR
            "Missing Disk32 script-string walk source: ${_path}")
    endif()
endforeach()

file(READ "${_disk32_path}" _disk32)
file(READ "${_header_path}" _header)
file(READ "${_source_path}" _source)
file(READ "${_fixture_path}" _fixture)
file(READ "${_manifest_path}" _manifest)
file(READ "${_tests_path}" _tests)
file(READ "${_ci_path}" _ci)

foreach(_var IN ITEMS
    _disk32
    _header
    _source
    _fixture
    _manifest
    _tests
    _ci)
    string(REGEX REPLACE "[ \t\r\n]+" " " _normalized "${${_var}}")
    set(${_var} "${_normalized}")
endforeach()

function(require_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing Disk32 script-string invariant (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(require_not_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden Disk32 script-string regression (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(require_ordered SOURCE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${FIRST}" _first)
    string(FIND "${${SOURCE_VAR}}" "${SECOND}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered Disk32 script-string invariant "
            "(${DESCRIPTION})")
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
        message(FATAL_ERROR
            "Missing ordered end of ${DESCRIPTION}: '${END}'")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_end} _slice)
    set(${OUT_VAR} "${_slice}" PARENT_SCOPE)
endfunction()

# The walker must use the canonical four-byte token vocabulary and must not
# import native loader, script registry, memory, reporting, or zone state.
foreach(_marker IN ITEMS
    "constexpr uint32_t kInline = UINT32_MAX;"
    "constexpr uint32_t kSharedInline = UINT32_MAX - 1;"
    "struct PointerToken"
    "ONDISK_SIZE(PointerToken, 4);")
    require_contains(_disk32 "${_marker}" "canonical token vocabulary")
endforeach()
require_contains(
    _header
    "#include <database/db_disk32.h>"
    "canonical token import")
foreach(_var IN ITEMS _header _source)
    foreach(_forbidden IN ITEMS
        "database/database.h"
        "xanim/xanim.h"
        "const char **"
        "const char**"
        "sizeof(void *)"
        "sizeof(void*)"
        "std::uintptr_t"
        "reinterpret_cast"
        "Load_Stream("
        "DB_PushStreamPos"
        "DB_IncStreamPos"
        "DB_Resolve"
        "SL_"
        "PMem_"
        "Com_Error"
        "XZone"
        "varXAsset"
        "g_var"
        "std::printf"
        "std::fprintf"
        "malloc("
        "new ")
        require_not_contains(
            ${_var} "${_forbidden}" "pure report-free boundary")
    endforeach()
endforeach()

# Freeze the exact token schema and the root/alignment borrowing contract.
foreach(_marker IN ITEMS
    "struct ScriptStringTokenDisk32 final { disk32::PointerToken token; };"
    "ONDISK_SIZE(ScriptStringTokenDisk32, 0x04);"
    "ONDISK_OFFSET(ScriptStringTokenDisk32, token, 0x00);"
    "alignof(ScriptStringTokenDisk32) == 4"
    "std::is_trivially_copyable_v<ScriptStringTokenDisk32>"
    "Every root API requires list to point to a live, four-byte-aligned"
    "ScriptStringListDisk32 object. When sourced from wire bytes, populate it"
    "with an exact 0x8-byte copy"
    "span is alignment-agnostic."
    "The root strings token is a presence marker here"
    "any nonzero value is"
    "accepted when count is nonzero.")
    require_contains(_header "${_marker}" "fixed token schema")
endforeach()
foreach(_forbidden IN ITEMS
    "#pragma pack"
    "char *string"
    "void *string")
    require_not_contains(
        _header "${_forbidden}" "schema cannot inherit native ABI")
endforeach()

# The public API exposes checked header extent calculation, full-span
# preflight, a borrowed iterator, and one exact-token publication operation.
foreach(_marker IN ITEMS
    "enum class ScriptStringListDisk32Status : std::uint8_t"
    "InvalidStringCount"
    "InvalidStringPointerCount"
    "SizeOverflow"
    "InvalidTokenSpan"
    "TruncatedTokenSpan"
    "UnsupportedSharedInline"
    "InvalidIterator"
    "struct ScriptStringListDisk32Layout final"
    "std::uint32_t tokenBytes = 0;"
    "std::int32_t stringCount = 0;"
    "class ScriptStringListDisk32Iterator final"
    "TryValidateScriptStringListDisk32Header("
    "TryValidateScriptStringListDisk32Span("
    "TryBeginScriptStringListDisk32("
    "TryNextScriptStringTokenDisk32("
    "std::is_trivially_copyable_v<ScriptStringListDisk32Iterator>")
    require_contains(_header "${_marker}" "bounded public API")
endforeach()

# Checked count * 4 arithmetic distinguishes true uint32 overflow before the
# lower 65536-entry policy cap. Root and bounded-span presence must agree with
# the computed count before any record read.
foreach(_marker IN ITEMS
    "if (count < 0)"
    "constexpr std::uint32_t stride = sizeof(ScriptStringTokenDisk32);"
    "(std::numeric_limits<std::uint32_t>::max)() / stride"
    "if (count > kMaxScriptStringListStrings)"
    "*outBytes = unsignedCount * stride;"
    "hasStrings != (list->count != 0)"
    "hasRecords != hasRequiredRecords"
    "!hasRequiredRecords && tokenRecordBytes != 0"
    "tokenRecordBytes < layout.tokenBytes"
    "std::memcpy(&token, records + offset, sizeof(token));")
    require_contains(_source "${_marker}" "checked four-byte extent")
endforeach()
require_ordered(
    _source
    "(std::numeric_limits<std::uint32_t>::max)() / stride"
    "if (count > kMaxScriptStringListStrings)"
    "overflow is distinguished before the policy cap")
require_ordered(
    _source
    "status = ValidateScriptStringTokenSpan("
    "const ScriptStringTokenDisk32 token = ReadScriptStringToken(records, index);"
    "bounded span validation precedes every token read")

# Full Begin preflight rejects the unsupported legacy shared-inline sentinel
# before publishing an iterator. Next re-reads and revalidates borrowed bytes
# so a sequential post-Begin mutation cannot bypass that rule.
foreach(_marker IN ITEMS
    "if (token.token.isSharedInline())"
    "return ScriptStringListDisk32Status::UnsupportedSharedInline;"
    "ScriptStringListDisk32Layout candidate{};"
    "*outLayout = candidate;"
    "TryValidateScriptStringListDisk32Span("
    "ScriptStringListDisk32Iterator candidate{};"
    "*outIterator = candidate;"
    "const ScriptStringTokenDisk32 token = ReadScriptStringToken("
    "status = ValidateScriptStringToken(token);"
    "*outToken = token;"
    "++iterator->nextIndex_;")
    require_contains(_source "${_marker}" "preflight and mutation closure")
endforeach()
require_ordered(
    _source
    "TryValidateScriptStringListDisk32Span( list, tokenRecords, tokenRecordBytes, &layout);"
    "ScriptStringListDisk32Iterator candidate{};"
    "complete preflight precedes iterator publication")
extract_slice(
    _source
    "ScriptStringListDisk32Status TryNextScriptStringTokenDisk32("
    "bool XAssetListDisk32Iterator::isValid()"
    _next_slice
    "script-string Next implementation")
require_ordered(
    _next_slice
    "ReadScriptStringToken("
    "ValidateScriptStringToken(token)"
    "mutation revalidation follows the exact read")
require_ordered(
    _next_slice
    "ValidateScriptStringToken(token)"
    "*outToken = token;"
    "validation precedes output publication")
require_ordered(
    _next_slice
    "*outToken = token;"
    "++iterator->nextIndex_;"
    "complete token publishes before cursor advance")
extract_slice(
    _next_slice
    "*outToken = token;"
    "return ScriptStringListDisk32Status::Success;"
    _publication_tail
    "script-string publication tail")
require_not_contains(
    _publication_tail
    "return ScriptStringListDisk32Status::Invalid"
    "no failure follows output publication")

# The runtime fixture must pin every malformed-input and lifetime boundary:
# exact bytes, overflow/cap distinction, empty/extra spans, unaligned guards,
# every supported raw class, late shared-inline rejection, sequential
# mutation, failure atomicity, and maximum-count iteration.
foreach(_marker IN ITEMS
    "void TestExactTokenSchema()"
    "void TestHeaderValidationAndCheckedExtents()"
    "void TestSpanBoundsAndEmptyIteration()"
    "void TestTokenClassesAndRawPreservation()"
    "void TestUnalignedIteratorAndGuardBytes()"
    "void TestSharedInlinePreflightIsAtomic()"
    "void TestMutationRevalidationAndFailureAtomicity()"
    "void TestMaximumTokenIteration()"
    "UINT32_C(0xFEDCBA98)"
    "UINT32_C(262144)"
    "largestNonoverflowingCount + 1"
    "TruncatedTokenSpan"
    "trailing{ MakeToken(UINT32_C(0xF1234567)), MakeToken(disk32::kSharedInline)}"
    "bytes.data() + 1u"
    "bytes.front() == prefixGuard"
    "UINT32_C(0xFFFFFFFD)"
    "MakeToken(disk32::kSharedInline)"
    "ObjectBytes(iterator) == iteratorBefore"
    "xasset::kMaxScriptStringListStrings"
    "Disk32 script-string token walk tests passed")
    require_contains(_fixture "${_marker}" "adversarial runtime coverage")
endforeach()

# Keep the shared implementation in the production manifest and run the
# dedicated fixture on all portable targets plus measured Windows x86.
foreach(_marker IN ITEMS
    "\${SRC_DIR}/database/db_xasset_disk32.cpp"
    "\${SRC_DIR}/database/db_xasset_disk32.h")
    require_contains(_manifest "${_marker}" "production manifest coverage")
endforeach()
foreach(_marker IN ITEMS
    "add_executable(kisakcod-db-script-string-disk32-tests"
    "db_script_string_disk32_tests.cpp"
    "\${SRC_DIR}/database/db_xasset_disk32.cpp"
    "NAME database-script-string-disk32-walk"
    "COMMAND kisakcod-db-script-string-disk32-tests"
    "NAME database-script-string-disk32-source-invariants"
    "db_script_string_disk32_source_test.cmake")
    require_contains(_tests "${_marker}" "CMake test integration")
endforeach()
foreach(_marker IN ITEMS
    "kisakcod-db-script-string-disk32-tests"
    "database-script-string-disk32-walk")
    require_contains(_ci "${_marker}" "Windows x86 CI integration")
endforeach()

message(STATUS "Disk32 script-string walk source invariants passed")
