cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_disk32_path "${SOURCE_ROOT}/src/database/db_disk32.h")
set(_asset_mode_path "${SOURCE_ROOT}/src/database/db_asset_mode.h")
set(_header_path "${SOURCE_ROOT}/src/database/db_xasset_disk32.h")
set(_source_path "${SOURCE_ROOT}/src/database/db_xasset_disk32.cpp")
set(_fixture_path "${SOURCE_ROOT}/tests/db_xasset_disk32_tests.cpp")
set(_manifest_path "${SOURCE_ROOT}/scripts/common_files.cmake")
set(_tests_path "${SOURCE_ROOT}/tests/CMakeLists.txt")
set(_ci_path "${SOURCE_ROOT}/.github/workflows/ci.yml")

foreach(_path IN ITEMS
    "${_disk32_path}"
    "${_asset_mode_path}"
    "${_header_path}"
    "${_source_path}"
    "${_fixture_path}"
    "${_manifest_path}"
    "${_tests_path}"
    "${_ci_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing Disk32 XAsset envelope source: ${_path}")
    endif()
endforeach()

file(READ "${_disk32_path}" _disk32)
file(READ "${_asset_mode_path}" _asset_mode)
file(READ "${_header_path}" _header)
file(READ "${_source_path}" _source)
file(READ "${_fixture_path}" _fixture)
file(READ "${_manifest_path}" _manifest)
file(READ "${_tests_path}" _tests)
file(READ "${_ci_path}" _ci)

foreach(_var IN ITEMS
    _disk32
    _asset_mode
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
            "Missing Disk32 XAsset invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_not_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden Disk32 XAsset regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_ordered SOURCE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${FIRST}" _first)
    string(FIND "${${SOURCE_VAR}}" "${SECOND}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered Disk32 XAsset invariant (${DESCRIPTION})")
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

# The envelope must reuse the one canonical packed-token vocabulary and remain
# independent of the native xanim header whose pointer fields widen on 64-bit.
foreach(_marker IN ITEMS
    "struct PointerToken"
    "ONDISK_SIZE(PointerToken, 4);"
    "template <class T> struct Ptr32"
    "ONDISK_SIZE(Ptr32<void>, 4);")
    require_contains(_disk32 "${_marker}" "canonical Disk32 vocabulary")
endforeach()
require_contains(
    _header
    "#include <database/db_disk32.h>"
    "canonical token import")
foreach(_var IN ITEMS _header _source)
    foreach(_forbidden IN ITEMS
        "xanim/xanim.h"
        "database/database.h"
        "database/db_load.cpp"
        "sizeof(XAsset)"
        "sizeof(XAssetList)"
        "XAsset *"
        "XAssetList *"
        "XAssetHeader *"
        "varXAsset"
        "DB_PushStreamPos"
        "Load_Stream("
        "PMem_"
        "XZoneMemory")
        require_not_contains(
            ${_var} "${_forbidden}" "pure native-independent boundary")
    endforeach()
endforeach()
foreach(_marker IN ITEMS
    "Every root API requires list to point to a live, four-byte-aligned"
    "When sourced from wire bytes, populate it with an"
    "exact 0x10-byte copy"
    "Only the separately supplied assetRecords byte span is alignment-agnostic."
    "context may collect diagnostics"
    "State that affects admission must remain"
    "with stable admission results")
    require_contains(_header "${_marker}" "public borrowing/alignment contract")
endforeach()

# Freeze the exact retail x86 root and element layouts. Serialized pointers
# remain four-byte opaque tokens, including high-bit and sentinel values.
foreach(_marker IN ITEMS
    "struct XAssetHeaderDisk32 final { disk32::PointerToken token; };"
    "struct XAssetDisk32 final { std::int32_t type; XAssetHeaderDisk32 header; };"
    "struct ScriptStringListDisk32 final { std::int32_t count; disk32::Ptr32<disk32::Ptr32<const char>> strings; };"
    "struct XAssetListDisk32 final { ScriptStringListDisk32 stringList; std::int32_t assetCount; disk32::Ptr32<XAssetDisk32> assets; };"
    "std::endian::native == std::endian::little"
    "ONDISK_SIZE(XAssetHeaderDisk32, 0x04);"
    "ONDISK_OFFSET(XAssetHeaderDisk32, token, 0x00);"
    "ONDISK_SIZE(XAssetDisk32, 0x08);"
    "ONDISK_OFFSET(XAssetDisk32, type, 0x00);"
    "ONDISK_OFFSET(XAssetDisk32, header, 0x04);"
    "ONDISK_SIZE(ScriptStringListDisk32, 0x08);"
    "ONDISK_OFFSET(ScriptStringListDisk32, strings, 0x04);"
    "ONDISK_SIZE(XAssetListDisk32, 0x10);"
    "ONDISK_OFFSET(XAssetListDisk32, assetCount, 0x08);"
    "ONDISK_OFFSET(XAssetListDisk32, assets, 0x0C);"
    "std::is_trivially_copyable_v<XAssetHeaderDisk32>"
    "std::is_trivially_copyable_v<XAssetDisk32>"
    "std::is_trivially_copyable_v<ScriptStringListDisk32>"
    "std::is_trivially_copyable_v<XAssetListDisk32>")
    require_contains(_header "${_marker}" "fixed packed schema")
endforeach()
foreach(_forbidden IN ITEMS
    "#pragma pack"
    "std::uintptr_t"
    "reinterpret_cast"
    "XAssetType type;"
    "void *data;")
    require_not_contains(
        _header "${_forbidden}" "schema cannot inherit native ABI")
endforeach()

# The raw int32 discriminator is range-checked before the caller's portable
# build policy. A missing policy fails closed instead of accepting all types.
foreach(_marker IN ITEMS
    "using XAssetTypeDisk32AdmissionCallback = bool (*)(void *context, std::int32_t rawType) noexcept;"
    "struct XAssetTypeDisk32Policy final"
    "std::int32_t typeCount = 0;"
    "XAssetTypeDisk32AdmissionCallback admitType = nullptr;")
    require_contains(_header "${_marker}" "portable type-policy declaration")
endforeach()
foreach(_marker IN ITEMS
    "return policy.typeCount > 0 && policy.admitType != nullptr;"
    "if (asset.type < 0 || asset.type >= policy.typeCount)"
    "if (!policy.admitType(policy.context, asset.type))"
    "return XAssetListDisk32Status::UnsupportedAssetType;")
    require_contains(_source "${_marker}" "portable type-policy enforcement")
endforeach()
require_ordered(
    _source
    "if (asset.type < 0 || asset.type >= policy.typeCount)"
    "if (!policy.admitType(policy.context, asset.type))"
    "raw range validation precedes callback admission")
require_contains(
    _asset_mode
    "constexpr bool IsAssetTypeSupported("
    "portable MP/SP policy remains available without xanim")

# Count/token parity and checked 0x8 span arithmetic must happen before any
# record read. The root accepts at most 32768 assets and retains the legacy
# 65536 script-string limit.
foreach(_marker IN ITEMS
    "inline constexpr std::int32_t kMaxXAssetListAssets = 32768;"
    "inline constexpr std::int32_t kMaxScriptStringListStrings = 65536;")
    require_contains(_header "${_marker}" "fixed list limits")
endforeach()
foreach(_marker IN ITEMS
    "if (count < 0)"
    "constexpr std::uint32_t stride = sizeof(XAssetDisk32);"
    "(std::numeric_limits<std::uint32_t>::max)() / stride"
    "if (count > kMaxXAssetListAssets)"
    "*outBytes = unsignedCount * stride;"
    "hasAssets != (list->assetCount != 0)"
    "hasStrings != (list->stringList.count != 0)"
    "assetRecordBytes < layout.assetBytes"
    "std::memcpy(&asset, records + offset, sizeof(asset));")
    require_contains(_source "${_marker}" "bounded envelope validation")
endforeach()
require_ordered(
    _source
    "(std::numeric_limits<std::uint32_t>::max)() / stride"
    "if (count > kMaxXAssetListAssets)"
    "checked extent overflow is distinguished before the policy cap")
require_ordered(
    _source
    "status = ValidateAssetSpan(candidate, assetRecords, assetRecordBytes);"
    "const XAssetDisk32 asset = ReadAsset(records, index);"
    "span validation precedes every record read")

# Every public output is assembled in a local and published only after the
# complete validation path succeeds. Iterator advancement follows record
# publication and no failure branch follows either mutation.
foreach(_marker IN ITEMS
    "XAssetListDisk32Layout candidate{};"
    "*outLayout = candidate;"
    "XAssetListDisk32Iterator candidate{};"
    "*outIterator = candidate;"
    "const XAssetDisk32 asset = ReadAsset("
    "*outAsset = asset;"
    "++iterator->nextIndex_;")
    require_contains(_source "${_marker}" "failure-atomic output")
endforeach()
extract_slice(
    _source
    "*outAsset = asset;"
    "} // namespace db::xasset"
    _next_publication_tail
    "iterator publication tail")
require_not_contains(
    _next_publication_tail
    "return XAssetListDisk32Status::Invalid"
    "no iterator failure after output publication")
require_ordered(
    _source
    "*outAsset = asset;"
    "++iterator->nextIndex_;"
    "complete record publishes before cursor advance")

# The fixture pins golden bytes, unaligned exact-stride reads, guard bytes,
# high-bit tokens, all limit/parity failures, late rejection, post-begin
# mutation, and unchanged outputs on failure/End.
foreach(_marker IN ITEMS
    "void TestExactSchemaBytes()"
    "void TestHeaderValidationAndLimits()"
    "void TestSpanBoundsAndTypePolicy()"
    "void TestBuildAdmissionPolicy()"
    "void TestUnalignedIteratorAndGuardBytes()"
    "void TestIteratorFailureAtomicity()"
    "void TestLateRejectionIsAtomic()"
    "void TestMaximumAssetIteration()"
    "std::memcpy( &copiedRoot, copiedRootBytes.data(), sizeof(copiedRoot));"
    "UINT32_C(0xFEDCBA98)"
    "UINT32_C(262144)"
    "TruncatedAssetSpan"
    "InvalidAssetType"
    "UnsupportedAssetType"
    "TryValidateXAssetListDisk32Span( &empty, nullptr, 0, policy, &layout)"
    "TryBeginXAssetListDisk32( &empty, nullptr, 0, policy, &emptyIterator)"
    "== xasset::XAssetListDisk32Status::End"
    "probe.calls == 0"
    "iterator.nextIndex() == 0"
    "iterator.remaining() == 1"
    "bytes.front() == prefixGuard"
    "probe.calls == xasset::kMaxXAssetListAssets * INT32_C(2)"
    "db::asset_mode::IsAssetTypeSupported(mode, rawType)"
    "Disk32 XAsset envelope tests passed")
    require_contains(_fixture "${_marker}" "adversarial fixture coverage")
endforeach()

# Keep the implementation in production source manifests and execute it on
# every portable target plus the explicit Windows x86 engine matrix.
foreach(_marker IN ITEMS
    "\${SRC_DIR}/database/db_xasset_disk32.cpp"
    "\${SRC_DIR}/database/db_xasset_disk32.h")
    require_contains(_manifest "${_marker}" "production manifest coverage")
endforeach()
foreach(_marker IN ITEMS
    "add_executable(kisakcod-db-xasset-disk32-tests"
    "db_xasset_disk32_tests.cpp"
    "\${SRC_DIR}/database/db_xasset_disk32.cpp"
    "NAME database-xasset-disk32-envelope"
    "COMMAND kisakcod-db-xasset-disk32-tests"
    "NAME database-xasset-disk32-source-invariants"
    "db_xasset_disk32_source_test.cmake")
    require_contains(_tests "${_marker}" "CMake test integration")
endforeach()
foreach(_marker IN ITEMS
    "kisakcod-db-xasset-disk32-tests"
    "database-xasset-disk32-envelope")
    require_contains(_ci "${_marker}" "Windows x86 CI integration")
endforeach()

message(STATUS "Disk32 XAsset envelope source invariants passed")
