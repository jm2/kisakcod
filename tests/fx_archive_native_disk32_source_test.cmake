cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_header_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_native_disk32.h")
set(_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_native_disk32.cpp")
set(_semantics_header_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_semantics.h")
set(_semantics_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_semantics.cpp")
set(_fxprimitives_path
    "${SOURCE_ROOT}/src/gfx_d3d/fxprimitives.h")
set(_archive_path "${SOURCE_ROOT}/src/EffectsCore/fx_archive.cpp")
set(_system_path "${SOURCE_ROOT}/src/EffectsCore/fx_system.cpp")
set(_manifest_path "${SOURCE_ROOT}/scripts/common_files.cmake")
set(_tests_path "${SOURCE_ROOT}/tests/CMakeLists.txt")
set(_ci_path "${SOURCE_ROOT}/.github/workflows/ci.yml")

foreach(_path IN ITEMS
    "${_header_path}"
    "${_source_path}"
    "${_semantics_header_path}"
    "${_semantics_source_path}"
    "${_fxprimitives_path}"
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
file(READ "${_semantics_header_path}" _semantics_header)
file(READ "${_semantics_source_path}" _semantics_source)
file(READ "${_fxprimitives_path}" _fxprimitives)
file(READ "${_archive_path}" _archive)
file(READ "${_system_path}" _system)
file(READ "${_manifest_path}" _manifest)
file(READ "${_tests_path}" _tests)
file(READ "${_ci_path}" _ci)

foreach(_var IN ITEMS
    _header
    _source
    _semantics_header
    _semantics_source
    _fxprimitives
    _archive
    _system
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

function(require_occurrence_count SOURCE_VAR NEEDLE EXPECTED DESCRIPTION)
    string(LENGTH "${NEEDLE}" _needle_length)
    if(_needle_length EQUAL 0)
        message(FATAL_ERROR "Cannot count an empty invariant: ${DESCRIPTION}")
    endif()
    string(LENGTH "${${SOURCE_VAR}}" _source_length)
    string(REPLACE "${NEEDLE}" "" _without "${${SOURCE_VAR}}")
    string(LENGTH "${_without}" _without_length)
    math(EXPR _removed_length "${_source_length} - ${_without_length}")
    math(EXPR _count "${_removed_length} / ${_needle_length}")
    if(NOT _count EQUAL EXPECTED)
        message(FATAL_ERROR
            "Wrong native FX Disk32 invariant count (${DESCRIPTION}): "
            "expected ${EXPECTED}, found ${_count} for '${NEEDLE}'")
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

# This checkpoint owns a heap-allocatable native candidate whose structural
# phase keeps definition pointers opaque and whose explicit semantic phase is
# the only transition to Ready. Neither phase publishes the image live.
foreach(_marker IN ITEMS
    "enum class FxArchiveDisk32WorkspacePhase"
    "Empty"
    "StructurallyValid"
    "Ready"
    "enum class FxArchiveDisk32StructuralStatus"
    "DefinitionNotFound"
    "InvalidPoolRecord"
    "InvalidGraph"
    "enum class FxArchiveDisk32ReadyStatus"
    "InvalidPhase"
    "InvalidSemantics"
    "FxArchiveDisk32ResolveDefinitionCallback"
    "struct FxArchiveDisk32Resolver"
    "class alignas(8) FxArchiveDisk32NativeWorkspace final"
    "FxArchiveDisk32WorkspacePhase phase() const noexcept"
    "RUNTIME_SIZE(FxArchiveDisk32NativeWorkspace, 0x4BD90, 0x4FDD8);"
    "struct FxArchiveDisk32StructuralView"
    "const FxSystem *system"
    "const FxSystemBuffers *buffers"
    "const FxSystemBuffersDisk32PoolStates *poolStates"
    "const FxSystemDisk32Metadata *metadata"
    "bool building_ = false;"
    "std::uint32_t physicsBodyCount_ = 0;"
    "TryBuildFxArchiveDisk32StructuralImage("
    "const FxSystemDisk32 &"
    "const FxSystemBuffersDisk32 &"
    "FxArchiveDisk32NativeWorkspace *"
    "TryGetFxArchiveDisk32StructuralView("
    "TryFinalizeFxArchiveDisk32NativeImage("
    "TryGetFxArchiveDisk32ReadyView("
    "struct FxArchiveDisk32ReadyView"
    "std::uint32_t physicsBodyCount = 0;"
    "callers must not cast away const or mutate any reachable staging")
    require_contains(
        _header "${_marker}" "bounded structural and Ready API")
endforeach()
foreach(_forbidden IN ITEMS
    "MemoryFile"
    "MemFile_"
    "FX_Restore"
    "FX_Save")
    require_not_contains(
        _header
        "${_forbidden}"
        "public native-image API stays independent of production archive I/O")
endforeach()

# The shared semantic boundary keeps renderer-owned FxElemDef opaque to
# portable builds. Every accessed field has one named native-width layout
# constant, and callbacks/results carry only bounded native identities.
foreach(_marker IN ITEMS
    "#include <universal/kisak_abi.h>"
    "namespace layout"
    "FX_ARCHIVE_PHYSICS_BODY_LIMIT = 512u"
    "FX_ARCHIVE_INVALID_PHYSICS_TOKEN = 0u"
    "KISAK_ARCH_64BIT ? 0x120u : 0xFCu"
    "KISAK_ARCH_64BIT ? 0xC8u : 0xBCu"
    "KISAK_ARCH_64BIT ? 0x110u : 0xF4u"
    "ELEM_DEF_STRIDE"
    "ELEM_DEF_FLAGS_OFFSET"
    "ELEM_DEF_SPAWN_OFFSET"
    "ELEM_DEF_SPAWN_DELAY_OFFSET"
    "ELEM_DEF_LIFE_SPAN_OFFSET"
    "ELEM_DEF_ELEM_TYPE_OFFSET"
    "ELEM_DEF_VISUAL_COUNT_OFFSET"
    "ELEM_DEF_VISUALS_OFFSET"
    "ELEM_DEF_TRAIL_DEF_OFFSET"
    "enum class FxArchiveElemPayloadKind"
    "OriginLighting"
    "PhysicsLighting"
    "OriginTrailTexCoord"
    "FxArchivePrepareElemPayloadCallback"
    "must preserve the complete FxElem object representation"
    "FxArchiveSemanticPhysicsDescriptor"
    "FxElem *elem = nullptr;"
    "const XModel *model = nullptr;"
    "std::size_t ownerIndex = 0;"
    "std::uint32_t token = 0;"
    "FxArchiveSemanticPhysicsSinkCallback"
    "struct FxArchiveSemanticCallbacks"
    "struct FxArchiveSemanticResult"
    "std::uint32_t physicsBodyCount = 0;"
    "std::int16_t spotLightBoltDobj = -1;"
    "TryValidateFxArchiveSemanticsNoReport(")
    require_contains(
        _semantics_header
        "${_marker}"
        "portable opaque semantic API and layout contract")
endforeach()
foreach(_forbidden IN ITEMS
    "fx_system.h"
    "fxprimitives.h"
    "r_gfx.h"
    "d3d9.h"
    "MemoryFile"
    "MemFile_"
    "FX_Restore"
    "FX_Save")
    require_not_contains(
        _semantics_header
        "${_forbidden}"
        "semantic header stays renderer-, platform-, and archive-I/O-neutral")
endforeach()

# A complete renderer-facing translation unit proves the opaque semantic
# layout against the runtime ABI constants and every field offset it reads.
require_contains(
    _archive
    "FX_ELEM_DEF_RUNTIME_SIZE == fx::archive::layout::ELEM_DEF_STRIDE"
    "opaque definition stride must use the architecture-aware runtime ABI")
require_not_contains(
    _archive
    "sizeof(FxElemDef)"
    "opaque definition stride must not reintroduce a direct native sizeof gate")
foreach(_layout_pin IN ITEMS
    "flags;ELEM_DEF_FLAGS_OFFSET"
    "spawn;ELEM_DEF_SPAWN_OFFSET"
    "spawnDelayMsec;ELEM_DEF_SPAWN_DELAY_OFFSET"
    "lifeSpanMsec;ELEM_DEF_LIFE_SPAN_OFFSET"
    "elemType;ELEM_DEF_ELEM_TYPE_OFFSET"
    "visualCount;ELEM_DEF_VISUAL_COUNT_OFFSET"
    "visuals;ELEM_DEF_VISUALS_OFFSET"
    "trailDef;ELEM_DEF_TRAIL_DEF_OFFSET")
    list(GET _layout_pin 0 _member)
    list(GET _layout_pin 1 _constant)
    require_contains(
        _archive
        "offsetof(FxElemDef, ${_member}) == fx::archive::layout::${_constant}"
        "renderer definition ${_member} offset must pin the opaque semantic view")
endforeach()

# The byte-view helpers also depend on nested range/union/trail layouts. Pin
# those assumptions where the complete renderer types are defined.
foreach(_nested_layout_pin IN ITEMS
    "RUNTIME_SIZE(FxIntRange, 0x8, 0x8);"
    "RUNTIME_OFFSET(FxIntRange, base, 0x0, 0x0);"
    "RUNTIME_OFFSET(FxIntRange, amplitude, 0x4, 0x4);"
    "RUNTIME_SIZE(FxSpawnDefLooping, 0x8, 0x8);"
    "RUNTIME_OFFSET(FxSpawnDefLooping, intervalMsec, 0x0, 0x0);"
    "RUNTIME_OFFSET(FxSpawnDefLooping, count, 0x4, 0x4);"
    "RUNTIME_SIZE(FxSpawnDefOneShot, 0x8, 0x8);"
    "RUNTIME_OFFSET(FxSpawnDefOneShot, count, 0x0, 0x0);"
    "RUNTIME_SIZE(FxSpawnDef, 0x8, 0x8);"
    "RUNTIME_SIZE(FxElemVisuals, 0x4, 0x8);"
    "RUNTIME_SIZE(FxTrailDef, 0x1C, 0x28);"
    "RUNTIME_OFFSET(FxTrailDef, scrollTimeMsec, 0x0, 0x0);"
    "RUNTIME_OFFSET(FxTrailDef, repeatDist, 0x4, 0x4);"
    "RUNTIME_OFFSET(FxTrailDef, splitDist, 0x8, 0x8);")
    require_contains(
        _fxprimitives
        "${_nested_layout_pin}"
        "renderer nested semantic ABI assumption")
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
require_contains(
    _source
    "static_assert( std::extent_v<decltype(destination->padBuffer)> == std::extent_v<decltype(source.padBuffer)>);"
    "fixed and native padding extents must match before copying")

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
foreach(_selector_guard IN ITEMS
    "workspace->metadata_.readVisibilitySelector > 1"
    "workspace->metadata_.writeVisibilitySelector > 1")
    require_contains(
        _guarded_build
        "${_selector_guard}"
        "visibility selectors are bounded before native pointer derivation")
endforeach()
require_ordered(
    _guarded_build
    "workspace->metadata_.writeVisibilitySelector > 1"
    "LinkWorkspaceBuffers("
    "visibility selectors are bounded before native pointer derivation")
foreach(_forbidden_structural_operation IN ITEMS
    "FxArchiveDisk32WorkspacePhase::Ready"
    "TryValidateFxArchiveSemanticsNoReport("
    "ActivateDefinitionSelectedElemPayload("
    "resolvedDefinition->")
    require_not_contains(
        _guarded_build
        "${_forbidden_structural_operation}"
        "structural reconstruction cannot inspect definition-selected semantics")
endforeach()
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

# Definition-selected union lifetimes change only through the Ready callback.
# It first snapshots the inactive representation, starts the selected member,
# then restores those same bytes; origin/lighting remain live by construction.
extract_slice(
    _source
    "bool ActivateDefinitionSelectedElemPayload("
    "FxArchiveDisk32StructuralStatus TryBuildFxArchiveDisk32StructuralImage("
    _payload_activation
    "definition-selected FxElem payload activation")
foreach(_payload_kind IN ITEMS
    "FxArchiveElemPayloadKind::PhysicsLighting"
    "FxArchiveElemPayloadKind::OriginTrailTexCoord"
    "FxArchiveElemPayloadKind::OriginLighting")
    require_contains(
        _payload_activation
        "${_payload_kind}"
        "every semantic payload kind has an explicit lifetime policy")
endforeach()
extract_slice(
    _payload_activation
    "if (payloadKind == FxArchiveElemPayloadKind::PhysicsLighting)"
    "else if (payloadKind == FxArchiveElemPayloadKind::OriginTrailTexCoord)"
    _physics_payload_activation
    "physics payload activation")
require_ordered(
    _physics_payload_activation
    "std::memcpy( physicsTokenBytes,"
    "std::construct_at(std::addressof(elem->physObjId));"
    "physics token bytes must be captured before changing union lifetime")
require_ordered(
    _physics_payload_activation
    "std::construct_at(std::addressof(elem->physObjId));"
    "std::memcpy( static_cast<void *>(std::addressof(elem->physObjId)),"
    "the activated physics member must receive the preserved token bytes")
extract_slice(
    _payload_activation
    "else if (payloadKind == FxArchiveElemPayloadKind::OriginTrailTexCoord)"
    "else if (payloadKind != FxArchiveElemPayloadKind::OriginLighting)"
    _trail_payload_activation
    "trail-coordinate payload activation")
require_ordered(
    _trail_payload_activation
    "std::memcpy( trailTexCoordBytes,"
    "std::construct_at(std::addressof(elem->u.trailTexCoord));"
    "trail-coordinate bytes must be captured before changing union lifetime")
require_ordered(
    _trail_payload_activation
    "std::construct_at(std::addressof(elem->u.trailTexCoord));"
    "std::memcpy( static_cast<void *>(std::addressof(elem->u)),"
    "the activated trail member must receive the preserved coordinate bytes")

# Finalization invalidates the structural phase before any union mutation,
# revalidates the complete graph, runs the shared two-pass oracle, then commits
# canonical counters and Ready as the last workspace mutation.
extract_slice(
    _source
    "FxArchiveDisk32ReadyStatus TryFinalizeFxArchiveDisk32NativeImage("
    "bool TryGetFxArchiveDisk32ReadyView("
    _ready_finalization
    "native Disk32 Ready transition")
require_ordered(
    _ready_finalization
    "workspace->phase_ = FxArchiveDisk32WorkspacePhase::Empty;"
    "workspace->building_ = true;"
    "Ready finalization must invalidate prior views before semantic mutation")
require_ordered(
    _ready_finalization
    "FxValidatePoolAllocationGraphWithScratch("
    "TryValidateFxArchiveSemanticsNoReport("
    "Ready finalization must revalidate structure before definition semantics")
require_contains(
    _ready_finalization
    "const FxArchiveSemanticCallbacks callbacks{ nullptr, ActivateDefinitionSelectedElemPayload, nullptr};"
    "Ready finalization must activate payloads through the shared callback")
require_ordered(
    _ready_finalization
    "TryValidateFxArchiveSemanticsNoReport("
    "Sys_AtomicStore(&effect->frameCount, 0);"
    "frame counters canonicalize only after complete semantic validation")
extract_slice(
    _ready_finalization
    "Sys_AtomicStore(&effect->frameCount, 0);"
    "return FxArchiveDisk32ReadyStatus::Success;"
    _ready_success_tail
    "validated Ready success tail")
require_ordered(
    _ready_success_tail
    "semanticResult.spotLightBoltDobj"
    "semanticResult.physicsBodyCount"
    "Ready metadata must commit the validated spotlight before body count")
require_ordered(
    _ready_success_tail
    "semanticResult.physicsBodyCount"
    "workspace->building_ = false;"
    "validated Ready metadata must finish before clearing the reentrancy gate")
require_ordered(
    _ready_success_tail
    "workspace->building_ = false;"
    "workspace->phase_ = FxArchiveDisk32WorkspacePhase::Ready;"
    "Ready publication must be the final workspace mutation")
require_occurrence_count(
    _source
    "workspace->phase_ = FxArchiveDisk32WorkspacePhase::Ready;"
    1
    "Ready has one publication point")
extract_slice(
    _ready_finalization
    "workspace->phase_ = FxArchiveDisk32WorkspacePhase::Ready;"
    "return FxArchiveDisk32ReadyStatus::Success;"
    _ready_publication_tail
    "Ready publication tail")
string(REPLACE
    "workspace->phase_ = FxArchiveDisk32WorkspacePhase::Ready;"
    ""
    _after_ready_publication
    "${_ready_publication_tail}")
require_not_contains(
    _after_ready_publication
    "workspace->"
    "no workspace access may follow Ready publication")

# Ready views are gated by phase and committed only after a complete local
# view has been formed; every rejection preserves the caller's output bytes.
extract_slice(
    _source
    "bool TryGetFxArchiveDisk32ReadyView("
    "} // namespace fx::archive"
    _ready_view_getter
    "Ready view getter")
require_ordered(
    _ready_view_getter
    "workspace->phase_ != FxArchiveDisk32WorkspacePhase::Ready"
    "const FxArchiveDisk32ReadyView view{"
    "Ready view storage must remain inaccessible before phase validation")
require_ordered(
    _ready_view_getter
    "const FxArchiveDisk32ReadyView view{"
    "*outView = view;"
    "Ready view output must commit from one complete local value")
require_ordered(
    _ready_view_getter
    "*outView = view;"
    "return true;"
    "Ready view success may return only after output commit")
require_occurrence_count(
    _ready_view_getter
    "*outView = view;"
    1
    "Ready view has one output commit")
extract_slice(
    _ready_view_getter
    "bool TryGetFxArchiveDisk32ReadyView("
    "const FxArchiveDisk32ReadyView view{"
    _ready_view_precommit
    "Ready view rejection paths")
require_not_contains(
    _ready_view_precommit
    "*outView ="
    "Ready view failures must preserve caller output")

foreach(_forbidden IN ITEMS
    "FxValidatePoolAllocationGraph("
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

# The semantic implementation reads renderer-owned definitions and inactive
# FxElem payloads only through named object-representation boundaries. It never
# depends on the renderer helper that would reintroduce platform headers.
foreach(_layout_marker IN ITEMS
    "layout::ELEM_DEF_STRIDE"
    "layout::ELEM_DEF_FLAGS_OFFSET"
    "layout::ELEM_DEF_SPAWN_OFFSET"
    "layout::ELEM_DEF_SPAWN_DELAY_OFFSET"
    "layout::ELEM_DEF_LIFE_SPAN_OFFSET"
    "layout::ELEM_DEF_ELEM_TYPE_OFFSET"
    "layout::ELEM_DEF_VISUAL_COUNT_OFFSET"
    "layout::ELEM_DEF_VISUALS_OFFSET"
    "layout::ELEM_DEF_TRAIL_DEF_OFFSET")
    require_contains(
        _semantics_source
        "${_layout_marker}"
        "semantic implementation uses every named opaque layout field")
endforeach()
foreach(_marker IN ITEMS
    "template <typename VALUE_TYPE> VALUE_TYPE ReadRepresentation("
    "static_assert(std::is_trivially_copyable_v<VALUE_TYPE>);"
    "std::memcpy(&value, bytes + offset, sizeof(value));"
    "const FxElemDef *ArchiveElemDefAt("
    "bytes + index * layout::ELEM_DEF_STRIDE"
    "void CopyArchiveElemOrigin("
    "std::memcpy(origin, bytes + offsetof(FxElem, physObjId), sizeof(origin));"
    "std::uint32_t CopyArchiveElemPhysicsToken("
    "ReadRepresentation<std::uint32_t>( &elem, offsetof(FxElem, physObjId))"
    "TrySelectPhysicsModelNoReport("
    "ReadRepresentation<const XModel *>"
    "FxPoolItemIndex<FxElem, MAX_ELEMS>("
    "const std::uint32_t token = CopyArchiveElemPhysicsToken(*elem);")
    require_contains(
        _semantics_source
        "${_marker}"
        "raw semantic field and physics descriptor validation")
endforeach()
foreach(_forbidden_typed_read IN ITEMS
    "elemDef->"
    "elemDef."
    "elem->origin"
    "elem->physObjId"
    "elem->u."
    "FX_GetElemVisuals(")
    require_not_contains(
        _semantics_source
        "${_forbidden_typed_read}"
        "opaque definition and inactive payload members cannot be read through the wrong type")
endforeach()

extract_slice(
    _semantics_source
    "bool PrepareElemPayload("
    "bool TrySelectPhysicsModelNoReport("
    _payload_prepare
    "representation-preserving payload preparation")
require_ordered(
    _payload_prepare
    "std::memcpy(before.data(), elem, before.size());"
    "callbacks.prepareElemPayload("
    "payload bytes must be captured before the lifetime callback")
require_ordered(
    _payload_prepare
    "callbacks.prepareElemPayload("
    "std::memcmp(before.data(), elem, before.size()) == 0"
    "the lifetime callback must preserve the complete element representation")

# One callback-free traversal proves the complete graph before a second,
# identical traversal can activate union members or emit physics descriptors.
extract_slice(
    _semantics_source
    "static bool TraverseFxArchiveSemanticsNoReport("
    "bool TryValidateFxArchiveSemanticsNoReport("
    _semantic_traversal
    "bounded semantic traversal")
foreach(_bound IN ITEMS
    "if (chainLength++ == MAX_ELEMS)"
    "if (trailCount++ == MAX_TRAILS)"
    "if (trailElemCount++ == MAX_TRAIL_ELEMS)"
    "*physicsBodyCount >= FX_ARCHIVE_PHYSICS_BODY_LIMIT")
    require_contains(
        _semantics_source
        "${_bound}"
        "semantic traversal and physics output remain pool-bounded")
endforeach()
require_ordered(
    _semantic_traversal
    "remoteElem->item.defIndex >= elemDefCount"
    "ArchiveElemDefAt(*effect->def, elem->defIndex)"
    "ordinary element definition index must validate before opaque lookup")
require_ordered(
    _semantic_traversal
    "PrepareElemPayload( callbacks, system, effect, elem, elemDef)"
    "ValidateArchiveElemRuntime( system, effect, elem, elemDef)"
    "payload activation must precede activation-sensitive validation in the applied pass")
require_ordered(
    _semantic_traversal
    "ValidateArchiveElemRuntime( system, effect, elem, elemDef)"
    "ArchiveElemTypeMatchesClass( ArchiveElemDefType(elemDef), elemClass)"
    "element runtime must validate before class admission")
require_ordered(
    _semantic_traversal
    "ArchiveElemTypeMatchesClass( ArchiveElemDefType(elemDef), elemClass)"
    "AcceptPhysicsElem( system, effect, elem, elemDef, callbacks, &physicsBodyCount)"
    "element class must validate before physics descriptor emission")
require_ordered(
    _semantic_traversal
    "remoteElem->item.defIndex >= spotLightEffectDefCount"
    "PrepareElemPayload( callbacks, system, spotLightEffect, elem, elemDef)"
    "spotlight definition index must validate before its payload callback")
require_ordered(
    _semantic_traversal
    "const FxArchiveSemanticResult result{"
    "*outResult = result;"
    "semantic traversal output must commit from one complete local result")
require_ordered(
    _semantic_traversal
    "*outResult = result;"
    "return true;"
    "semantic traversal can report success only after result commit")
require_occurrence_count(
    _semantic_traversal
    "*outResult = result;"
    1
    "one traversal has one result commit")

extract_slice(
    _semantics_source
    "bool AcceptPhysicsElem("
    "static bool TraverseFxArchiveSemanticsNoReport("
    _physics_acceptance
    "validated physics descriptor sink")
require_ordered(
    _physics_acceptance
    "TrySelectPhysicsModelNoReport("
    "FxPoolItemIndex<FxElem, MAX_ELEMS>("
    "physics model selection must finish before owner-index validation")
require_ordered(
    _physics_acceptance
    "token == FX_ARCHIVE_INVALID_PHYSICS_TOKEN"
    "const FxArchiveSemanticPhysicsDescriptor descriptor{"
    "physics identity must validate before descriptor construction")
require_ordered(
    _physics_acceptance
    "const FxArchiveSemanticPhysicsDescriptor descriptor{"
    "callbacks.acceptPhysics( callbacks.context, descriptor, *physicsBodyCount)"
    "only a complete descriptor may reach the caller sink")
require_ordered(
    _physics_acceptance
    "callbacks.acceptPhysics( callbacks.context, descriptor, *physicsBodyCount)"
    "++*physicsBodyCount;"
    "a rejected sink callback cannot advance the accepted body count")

extract_slice(
    _semantics_source
    "bool TryValidateFxArchiveSemanticsNoReport("
    "} // namespace fx::archive"
    _semantic_transaction
    "two-pass semantic transaction")
require_ordered(
    _semantic_transaction
    "const FxArchiveSemanticCallbacks noCallbacks{};"
    "system, noCallbacks, &preflight)"
    "the first complete traversal must be callback-free")
require_ordered(
    _semantic_transaction
    "system, noCallbacks, &preflight)"
    "system, callbacks, &applied)"
    "callbacks may run only after complete semantic preflight")
require_ordered(
    _semantic_transaction
    "system, callbacks, &applied)"
    "applied.physicsBodyCount != preflight.physicsBodyCount"
    "the applied traversal result must be cross-checked against preflight")
require_ordered(
    _semantic_transaction
    "applied.spotLightBoltDobj != preflight.spotLightBoltDobj"
    "*outResult = applied;"
    "public output commits only after both semantic results match")
extract_slice(
    _semantic_transaction
    "bool TryValidateFxArchiveSemanticsNoReport("
    "*outResult = applied;"
    _semantic_precommit
    "semantic rejection paths")
require_not_contains(
    _semantic_precommit
    "*outResult ="
    "every semantic failure must preserve caller output")
require_ordered(
    _semantic_transaction
    "*outResult = applied;"
    "return true;"
    "semantic success returns only after output commit")
require_occurrence_count(
    _semantic_transaction
    "TraverseFxArchiveSemanticsNoReport("
    2
    "public semantic transaction performs exactly two traversals")
require_occurrence_count(
    _semantic_transaction
    "*outResult = applied;"
    1
    "public semantic transaction has one output commit")
require_occurrence_count(
    _semantics_source
    "callbacks.prepareElemPayload("
    1
    "payload callback has one invocation boundary")
require_occurrence_count(
    _semantics_source
    "callbacks.acceptPhysics("
    1
    "physics sink has one invocation boundary")

# Both native conversion and the shared oracle are attacker-reachable helpers:
# they stay allocation-, report-, lock-, exception-, and archive-I/O-free.
foreach(_var IN ITEMS _source _semantics_source)
    foreach(_forbidden IN ITEMS
        "MemoryFile"
        "MemFile_"
        "Z_Malloc"
        "Z_Free"
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
        "MyAssertHandler"
        "iassert("
        "vassert("
        "Sys_EnterCriticalSection"
        "Sys_LeaveCriticalSection"
        "CRITSECT_"
        "std::mutex"
        "std::fstream"
        "std::ifstream"
        "std::ofstream"
        "fopen("
        "fread("
        "fwrite(")
        require_not_contains(
            ${_var}
            "${_forbidden}"
            "native conversion and semantics stay nonblocking and side-effect-free")
    endforeach()
endforeach()
require_not_contains(
    _semantics_source
    "fx_physics_sidecar.h"
    "portable semantics must not import unrelated sidecar stack-heavy helpers")
foreach(_large_type IN ITEMS
    "FxSystem"
    "FxSystemDisk32"
    "FxSystemBuffers"
    "FxSystemBuffersDisk32"
    "FxArchiveDisk32NativeWorkspace"
    "FxPoolAllocationGraphScratch"
    "BodySidecarValidationScratch")
    require_not_matches(
        _semantics_source
        "(^|[^A-Za-z0-9_])${_large_type} +[A-Za-z_][A-Za-z0-9_]*"
        "semantic traversal cannot stage large native images or scratch by value")
endforeach()

# Ready conversion is compiled into the engine and portable fixture, but this
# checkpoint still does not replace the legacy MemoryFile reader or publish a
# native candidate into the live FX system. Production integration follows.
foreach(_var IN ITEMS _archive _system)
    foreach(_forbidden IN ITEMS
        "fx_archive_native_disk32"
        "TryBuildFxArchiveDisk32StructuralImage"
        "TryFinalizeFxArchiveDisk32NativeImage"
        "TryGetFxArchiveDisk32ReadyView")
        require_not_contains(
            ${_var} "${_forbidden}" "production archive/publication remains unchanged")
    endforeach()
endforeach()

# Keep production compilation, all five portable runners, measured Windows
# x86 Debug/Release, the executable fixture, and this contract wired together.
foreach(_marker IN ITEMS
    "fx_archive_native_disk32.cpp"
    "fx_archive_native_disk32.h"
    "fx_archive_semantics.cpp"
    "fx_archive_semantics.h")
    require_contains(_manifest "${_marker}" "engine source manifest")
endforeach()
extract_slice(
    _tests
    "add_library(kisakcod-fx-archive-native-disk32-subject OBJECT"
    "add_executable(kisakcod-fx-archive-native-disk32-tests"
    _portable_native_subject
    "portable native Disk32 subject")
foreach(_marker IN ITEMS
    "EffectsCore/fx_archive_native_disk32.cpp"
    "EffectsCore/fx_archive_semantics.cpp"
    "kisakcod_test_warnings(kisakcod-fx-archive-native-disk32-subject)"
    "kisakcod_fx_helper_stack_budget( kisakcod-fx-archive-native-disk32-subject)")
    require_contains(
        _portable_native_subject
        "${_marker}"
        "portable native and semantic compilation gate")
endforeach()
foreach(_marker IN ITEMS
    "fx_archive_native_disk32_tests.cpp"
    "fx_archive_semantics.cpp"
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
