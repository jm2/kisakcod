cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_header_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_native_disk32.h")
set(_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_native_disk32.cpp")
set(_archive_path "${SOURCE_ROOT}/src/EffectsCore/fx_archive.cpp")
set(_system_path "${SOURCE_ROOT}/src/EffectsCore/fx_system.cpp")
set(_manifest_path "${SOURCE_ROOT}/scripts/common_files.cmake")
set(_tests_path "${SOURCE_ROOT}/tests/CMakeLists.txt")
set(_ci_path "${SOURCE_ROOT}/.github/workflows/ci.yml")

foreach(_path IN ITEMS
    "${_header_path}"
    "${_source_path}"
    "${_archive_path}"
    "${_system_path}"
    "${_manifest_path}"
    "${_tests_path}"
    "${_ci_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing native FX Disk32 source: ${_path}")
    endif()
endforeach()

file(READ "${_header_path}" _header)
file(READ "${_source_path}" _source)
file(READ "${_archive_path}" _archive)
file(READ "${_system_path}" _system)
file(READ "${_manifest_path}" _manifest)
file(READ "${_tests_path}" _tests)
file(READ "${_ci_path}" _ci)

foreach(_var IN ITEMS
    _header _source _archive _system _manifest _tests _ci)
    string(REGEX REPLACE "[ \t\r\n]+" " " _normalized "${${_var}}")
    set(${_var} "${_normalized}")
endforeach()

function(require_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing native FX Disk32 invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_not_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden native FX Disk32 regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_not_matches SOURCE_VAR PATTERN DESCRIPTION)
    string(REGEX MATCH "${PATTERN}" _match "${${SOURCE_VAR}}")
    if(NOT _match STREQUAL "")
        message(FATAL_ERROR
            "Forbidden native FX Disk32 regression (${DESCRIPTION}): '${_match}'")
    endif()
endfunction()

function(require_ordered SOURCE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${FIRST}" _first)
    string(FIND "${${SOURCE_VAR}}" "${SECOND}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered native FX Disk32 invariant (${DESCRIPTION})")
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

# This checkpoint owns a heap-allocatable native candidate without claiming it
# is ready for publication.  Definition pointers are identities only until a
# later semantic pass has selected every definition-dependent union member.
foreach(_marker IN ITEMS
    "enum class FxArchiveDisk32WorkspacePhase"
    "Empty"
    "StructurallyValid"
    "Ready"
    "enum class FxArchiveDisk32StructuralStatus"
    "DefinitionNotFound"
    "InvalidPoolRecord"
    "InvalidGraph"
    "FxArchiveDisk32ResolveDefinitionCallback"
    "struct FxArchiveDisk32Resolver"
    "class FxArchiveDisk32NativeWorkspace final"
    "FxArchiveDisk32WorkspacePhase phase() const noexcept"
    "struct FxArchiveDisk32StructuralView"
    "const FxSystem *system"
    "const FxSystemBuffers *buffers"
    "const FxSystemBuffersDisk32PoolStates *poolStates"
    "const FxSystemDisk32Metadata *metadata"
    "bool building_ = false;"
    "TryBuildFxArchiveDisk32StructuralImage("
    "const FxSystemDisk32 &"
    "const FxSystemBuffersDisk32 &"
    "FxArchiveDisk32NativeWorkspace *"
    "TryGetFxArchiveDisk32StructuralView(")
    require_contains(_header "${_marker}" "bounded structural-only API")
endforeach()
foreach(_forbidden IN ITEMS
    "MemoryFile"
    "MemFile_"
    "FX_Restore"
    "FX_Save")
    require_not_contains(
        _header "${_forbidden}" "public structural API stays independent of production archive I/O")
endforeach()

# Raw free-slot tail preservation is valid only while the native union and
# fixed Disk32 slot have identical complete object representations.
foreach(_marker IN ITEMS
    "template <typename ITEM_TYPE, typename DISK_SLOT_TYPE> inline constexpr bool PoolSlotRepresentationIsCompatible ="
    "sizeof(FxPool<ITEM_TYPE>) == sizeof(DISK_SLOT_TYPE)"
    "alignof(FxPool<ITEM_TYPE>) == alignof(DISK_SLOT_TYPE)"
    "std::is_trivially_copyable_v<FxPool<ITEM_TYPE>>"
    "std::is_trivially_destructible_v<FxPool<ITEM_TYPE>>"
    "std::is_trivially_copyable_v<DISK_SLOT_TYPE>"
    "static_assert(PoolSlotRepresentationIsCompatible< FxElem, FxElemPoolSlotDisk32>);"
    "static_assert(PoolSlotRepresentationIsCompatible< FxTrail, FxTrailPoolSlotDisk32>);"
    "static_assert(PoolSlotRepresentationIsCompatible< FxTrailElem, FxTrailElemPoolSlotDisk32>);")
    require_contains(_source "${_marker}" "safe complete free-slot representation copy")
endforeach()

# The builder writes directly into caller-owned workspace, reconstructs every
# pool member with the proper lifetime, validates the linked native graph with
# caller-owned scratch, and publishes StructurallyValid only as its last step.
foreach(_marker IN ITEMS
    "TryUnpackFxSystemDisk32("
    "TryRebuildFxSystemBuffersDisk32PoolStates("
    "TryUnpackFxEffectDisk32("
    "workspace->metadata_.activeEffectSlots[index] != 0"
    "resolvedDefinition = resolver.resolve(resolver.context, key);"
    "TryDecodeFxPoolSlotFreeLinkDisk32("
    "std::construct_at("
    "FX_TryDeriveVisibilitySelectors("
    "FxValidatePoolAllocationGraphWithScratch("
    "FxArchiveDisk32WorkspacePhase::Empty"
    "FxArchiveDisk32WorkspacePhase::StructurallyValid"
    "TryGetFxArchiveDisk32StructuralView(")
    require_contains(_source "${_marker}" "transactional structural reconstruction")
endforeach()
foreach(_marker IN ITEMS
    "workspace->building_ || !resolver.resolve"
    "workspace->building_ = true;"
    "const auto finish = [workspace]"
    "workspace->building_ = false;"
    "return FxArchiveDisk32StructuralStatus::Success;")
    require_contains(_source "${_marker}" "same-workspace resolver reentrancy gate and cleanup")
endforeach()
extract_slice(
    _source
    "workspace->building_ = true;"
    "bool TryGetFxArchiveDisk32StructuralView("
    _guarded_build
    "guarded structural build body")
string(REPLACE
    "return FxArchiveDisk32StructuralStatus::Success;"
    ""
    _guarded_failure_paths
    "${_guarded_build}")
require_not_contains(
    _guarded_failure_paths
    "return FxArchiveDisk32StructuralStatus::"
    "every guarded exit must clear the reentrancy flag through finish")
require_ordered(
    _source
    "FxArchiveDisk32WorkspacePhase::Empty"
    "FxValidatePoolAllocationGraphWithScratch("
    "workspace begins unpublished before graph validation")
require_ordered(
    _source
    "FxValidatePoolAllocationGraphWithScratch("
    "FxArchiveDisk32WorkspacePhase::StructurallyValid"
    "structural publication follows graph validation")
extract_slice(
    _source
    "FxValidatePoolAllocationGraphWithScratch("
    "return FxArchiveDisk32StructuralStatus::Success;"
    _success_path
    "validated structural success tail")
require_ordered(
    _success_path
    "return finish(FxArchiveDisk32StructuralStatus::InvalidGraph);"
    "workspace->building_ = false;"
    "successful validation clears the reentrancy guard")
require_ordered(
    _success_path
    "workspace->building_ = false;"
    "workspace->phase_ = FxArchiveDisk32WorkspacePhase::StructurallyValid;"
    "StructurallyValid is the final workspace mutation")
extract_slice(
    _source
    "workspace->phase_ = FxArchiveDisk32WorkspacePhase::StructurallyValid;"
    "bool TryGetFxArchiveDisk32StructuralView("
    _published_tail
    "post-publication builder tail")
string(REPLACE
    "workspace->phase_ = FxArchiveDisk32WorkspacePhase::StructurallyValid;"
    ""
    _after_publication
    "${_published_tail}")
require_not_contains(
    _after_publication
    "workspace->"
    "no workspace mutation or access may follow structural publication")

foreach(_forbidden IN ITEMS
    "FxValidatePoolAllocationGraph("
    "workspace->phase_ = FxArchiveDisk32WorkspacePhase::Ready"
    "resolvedDefinition->"
    "MemoryFile"
    "MemFile_"
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
    "FxSystemBuffers buffers{};"
    "FxSystemBuffersDisk32 buffers{};"
    "FxArchiveDisk32NativeWorkspace workspace{};"
    "FxPoolAllocationGraphScratch scratch{};"
    "= FxSystemBuffers{};"
    "= FxSystemBuffersDisk32{};"
    "= FxArchiveDisk32NativeWorkspace{};")
    require_not_contains(
        _source "${_forbidden}" "builder stays report-free, allocation-free, and avoids large stack values")
endforeach()

foreach(_large_type IN ITEMS
    "FxSystem"
    "FxSystemBuffers"
    "FxSystemBuffersDisk32"
    "FxArchiveDisk32NativeWorkspace"
    "FxPoolAllocationGraphScratch")
    require_not_matches(
        _source
        "(^|[^A-Za-z0-9_])${_large_type} +[A-Za-z_][A-Za-z0-9_]*"
        "large native/disk images and validation scratch cannot pass or stage by value")
endforeach()

# This structural helper is compiled into the engine for later use, but this
# checkpoint must not alter the legacy MemoryFile reader or publish to the live
# FX system.  Those integrations require the later semantic Ready phase.
foreach(_var IN ITEMS _archive _system)
    foreach(_forbidden IN ITEMS
        "fx_archive_native_disk32"
        "TryBuildFxArchiveDisk32StructuralImage")
        require_not_contains(
            ${_var} "${_forbidden}" "production archive/publication remains unchanged")
    endforeach()
endforeach()

# Keep production compilation, all five portable runners, measured Windows
# x86 Debug/Release, the executable fixture, and this contract wired together.
foreach(_marker IN ITEMS
    "fx_archive_native_disk32.cpp"
    "fx_archive_native_disk32.h")
    require_contains(_manifest "${_marker}" "engine source manifest")
endforeach()
foreach(_marker IN ITEMS
    "fx_archive_native_disk32_tests.cpp"
    "kisakcod-fx-archive-native-disk32-subject"
    "kisakcod_fx_helper_stack_budget( kisakcod-fx-archive-native-disk32-subject)"
    "kisakcod-fx-archive-native-disk32-tests"
    "effectscore-archive-native-disk32-codec"
    "effectscore-archive-native-disk32-source-invariants"
    "fx_archive_native_disk32_source_test.cmake")
    require_contains(_tests "${_marker}" "portable fixture and source-contract target")
endforeach()
foreach(_marker IN ITEMS
    "Linux amd64"
    "Linux arm64"
    "Windows amd64"
    "Windows arm64"
    "macOS arm64"
    "ctest --test-dir build-tests -C Release --output-on-failure"
    "kisakcod-fx-archive-native-disk32-tests"
    "effectscore-archive-(disk32|system-disk32|buffers-disk32|native-disk32)-codec")
    require_contains(_ci "${_marker}" "portable and measured Windows x86 execution")
endforeach()
