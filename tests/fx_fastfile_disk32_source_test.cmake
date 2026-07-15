cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_disk32_path "${SOURCE_ROOT}/src/database/db_disk32.h")
set(_schema_path "${SOURCE_ROOT}/src/EffectsCore/fx_fastfile_disk32.h")
set(_native_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_fastfile_native_disk32.h")
set(_db_load_path "${SOURCE_ROOT}/src/database/db_load.cpp")
set(_archive_path "${SOURCE_ROOT}/src/EffectsCore/fx_archive.cpp")

foreach(_path IN ITEMS
    "${_disk32_path}"
    "${_schema_path}"
    "${_native_path}"
    "${_db_load_path}"
    "${_archive_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing FX fast-file Disk32 source: ${_path}")
    endif()
endforeach()

file(READ "${_disk32_path}" _disk32)
file(READ "${_schema_path}" _schema)
file(READ "${_native_path}" _native)
file(READ "${_db_load_path}" _db_load)
file(READ "${_archive_path}" _archive)

foreach(_var IN ITEMS _disk32 _schema _native _db_load _archive)
    string(REGEX REPLACE "[ \t\r\n]+" " " _normalized "${${_var}}")
    set(${_var} "${_normalized}")
endforeach()

function(require_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing FX fast-file Disk32 invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_not_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden FX fast-file Disk32 regression (${DESCRIPTION}): '${NEEDLE}'")
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
            "Wrong FX fast-file Disk32 invariant count (${DESCRIPTION}): "
            "expected ${EXPECTED}, found ${_count} for '${NEEDLE}'")
    endif()
endfunction()

function(require_ordered SOURCE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${FIRST}" _first)
    if(_first EQUAL -1)
        message(FATAL_ERROR
            "Missing first FX fast-file Disk32 invariant (${DESCRIPTION}): '${FIRST}'")
    endif()
    string(SUBSTRING "${${SOURCE_VAR}}" ${_first} -1 _tail)
    string(FIND "${_tail}" "${SECOND}" _second)
    if(_second LESS_EQUAL 0)
        message(FATAL_ERROR
            "Missing or unordered FX fast-file Disk32 invariant (${DESCRIPTION}): "
            "'${SECOND}'")
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

# All packed FX records reuse the tree's one canonical token/Ptr32 vocabulary.
# The schema must never grow a parallel pointer wrapper or a native pointer.
foreach(_marker IN ITEMS
    "struct PointerToken"
    "ONDISK_SIZE(PointerToken, 4);"
    "template <class T> struct Ptr32"
    "PointerToken token;"
    "ONDISK_SIZE(Ptr32<void>, 4);")
    require_contains(_disk32 "${_marker}" "canonical database Disk32 vocabulary")
endforeach()
require_contains(
    _schema
    "#include <database/db_disk32.h>"
    "schema imports the canonical Disk32 vocabulary")
foreach(_forbidden IN ITEMS
    "struct PointerToken"
    "struct Ptr32"
    "class Ptr32"
    "std::uintptr_t"
    "reinterpret_cast")
    require_not_contains(
        _schema "${_forbidden}" "schema cannot define or widen disk pointers")
endforeach()

# The element discriminator is serialized data, independent of the renderer's
# native enum, and every ABI value is explicit.
require_contains(
    _schema
    "enum class FxElemTypeDisk32 : std::uint8_t { SpriteBillboard = 0, SpriteOriented = 1, Tail = 2, Trail = 3, Cloud = 4, Model = 5, OmniLight = 6, SpotLight = 7, Sound = 8, Decal = 9, Runner = 10, Count = 11, };"
    "complete serialized element discriminator")
require_contains(
    _schema
    "static_assert(std::endian::native == std::endian::little"
    "little-endian fast-file contract")
require_contains(
    _schema
    "static_assert(std::numeric_limits<float>::is_iec559"
    "binary32 float contract")

# Freeze every packed record and field used by the FX definition, trail, and
# impact-table loaders. These assertions are source-level backstops for the
# compiler-enforced ONDISK assertions in the schema itself.
foreach(_marker IN ITEMS
    "ONDISK_SIZE(float, 0x04);"
    "ONDISK_SIZE(FxElemTypeDisk32, 0x01);"
    "ONDISK_SIZE(FxFloatRangeDisk32, 0x08);"
    "ONDISK_OFFSET(FxFloatRangeDisk32, base, 0x00);"
    "ONDISK_OFFSET(FxFloatRangeDisk32, amplitude, 0x04);"
    "ONDISK_SIZE(FxIntRangeDisk32, 0x08);"
    "ONDISK_OFFSET(FxIntRangeDisk32, base, 0x00);"
    "ONDISK_OFFSET(FxIntRangeDisk32, amplitude, 0x04);"
    "ONDISK_SIZE(FxSpawnDefDisk32, 0x08);"
    "ONDISK_OFFSET(FxSpawnDefDisk32, intervalMsecOrCountBase, 0x00);"
    "ONDISK_OFFSET(FxSpawnDefDisk32, loopCountOrCountAmplitude, 0x04);"
    "ONDISK_SIZE(FxElemAtlasDisk32, 0x08);"
    "ONDISK_OFFSET(FxElemAtlasDisk32, behavior, 0x00);"
    "ONDISK_OFFSET(FxElemAtlasDisk32, index, 0x01);"
    "ONDISK_OFFSET(FxElemAtlasDisk32, fps, 0x02);"
    "ONDISK_OFFSET(FxElemAtlasDisk32, loopCount, 0x03);"
    "ONDISK_OFFSET(FxElemAtlasDisk32, colIndexBits, 0x04);"
    "ONDISK_OFFSET(FxElemAtlasDisk32, rowIndexBits, 0x05);"
    "ONDISK_OFFSET(FxElemAtlasDisk32, entryCount, 0x06);"
    "ONDISK_SIZE(FxElemVec3RangeDisk32, 0x18);"
    "ONDISK_OFFSET(FxElemVec3RangeDisk32, base, 0x00);"
    "ONDISK_OFFSET(FxElemVec3RangeDisk32, amplitude, 0x0C);"
    "ONDISK_SIZE(FxElemVisualStateDisk32, 0x18);"
    "ONDISK_OFFSET(FxElemVisualStateDisk32, color, 0x00);"
    "ONDISK_OFFSET(FxElemVisualStateDisk32, rotationDelta, 0x04);"
    "ONDISK_OFFSET(FxElemVisualStateDisk32, rotationTotal, 0x08);"
    "ONDISK_OFFSET(FxElemVisualStateDisk32, size, 0x0C);"
    "ONDISK_OFFSET(FxElemVisualStateDisk32, scale, 0x14);"
    "ONDISK_SIZE(FxElemVisStateSampleDisk32, 0x30);"
    "ONDISK_OFFSET(FxElemVisStateSampleDisk32, base, 0x00);"
    "ONDISK_OFFSET(FxElemVisStateSampleDisk32, amplitude, 0x18);"
    "ONDISK_SIZE(FxElemVelStateInFrameDisk32, 0x30);"
    "ONDISK_OFFSET(FxElemVelStateInFrameDisk32, velocity, 0x00);"
    "ONDISK_OFFSET(FxElemVelStateInFrameDisk32, totalDelta, 0x18);"
    "ONDISK_SIZE(FxElemVelStateSampleDisk32, 0x60);"
    "ONDISK_OFFSET(FxElemVelStateSampleDisk32, local, 0x00);"
    "ONDISK_OFFSET(FxElemVelStateSampleDisk32, world, 0x30);"
    "ONDISK_SIZE(FxEffectDefRefDisk32, 0x04);"
    "ONDISK_OFFSET(FxEffectDefRefDisk32, token, 0x00);"
    "ONDISK_SIZE(FxElemVisualsDisk32, 0x04);"
    "ONDISK_OFFSET(FxElemVisualsDisk32, token, 0x00);"
    "ONDISK_SIZE(FxElemDefVisualsDisk32, 0x04);"
    "ONDISK_OFFSET(FxElemDefVisualsDisk32, token, 0x00);"
    "ONDISK_SIZE(FxEffectDefHandleDisk32, 0x04);"
    "ONDISK_OFFSET(FxEffectDefHandleDisk32, token, 0x00);"
    "ONDISK_SIZE(FxElemMarkVisualsDisk32, 0x08);"
    "ONDISK_OFFSET(FxElemMarkVisualsDisk32, materials, 0x00);"
    "ONDISK_SIZE(FxTrailVertexDisk32, 0x14);"
    "ONDISK_OFFSET(FxTrailVertexDisk32, pos, 0x00);"
    "ONDISK_OFFSET(FxTrailVertexDisk32, normal, 0x08);"
    "ONDISK_OFFSET(FxTrailVertexDisk32, texCoord, 0x10);"
    "ONDISK_SIZE(FxTrailDefDisk32, 0x1C);"
    "ONDISK_OFFSET(FxTrailDefDisk32, scrollTimeMsec, 0x00);"
    "ONDISK_OFFSET(FxTrailDefDisk32, repeatDist, 0x04);"
    "ONDISK_OFFSET(FxTrailDefDisk32, splitDist, 0x08);"
    "ONDISK_OFFSET(FxTrailDefDisk32, vertCount, 0x0C);"
    "ONDISK_OFFSET(FxTrailDefDisk32, verts, 0x10);"
    "ONDISK_OFFSET(FxTrailDefDisk32, indCount, 0x14);"
    "ONDISK_OFFSET(FxTrailDefDisk32, inds, 0x18);"
    "ONDISK_SIZE(FxElemDefDisk32, 0xFC);"
    "ONDISK_OFFSET(FxElemDefDisk32, flags, 0x00);"
    "ONDISK_OFFSET(FxElemDefDisk32, spawn, 0x04);"
    "ONDISK_OFFSET(FxElemDefDisk32, spawnRange, 0x0C);"
    "ONDISK_OFFSET(FxElemDefDisk32, fadeInRange, 0x14);"
    "ONDISK_OFFSET(FxElemDefDisk32, fadeOutRange, 0x1C);"
    "ONDISK_OFFSET(FxElemDefDisk32, spawnFrustumCullRadius, 0x24);"
    "ONDISK_OFFSET(FxElemDefDisk32, spawnDelayMsec, 0x28);"
    "ONDISK_OFFSET(FxElemDefDisk32, lifeSpanMsec, 0x30);"
    "ONDISK_OFFSET(FxElemDefDisk32, spawnOrigin, 0x38);"
    "ONDISK_OFFSET(FxElemDefDisk32, spawnOffsetRadius, 0x50);"
    "ONDISK_OFFSET(FxElemDefDisk32, spawnOffsetHeight, 0x58);"
    "ONDISK_OFFSET(FxElemDefDisk32, spawnAngles, 0x60);"
    "ONDISK_OFFSET(FxElemDefDisk32, angularVelocity, 0x78);"
    "ONDISK_OFFSET(FxElemDefDisk32, initialRotation, 0x90);"
    "ONDISK_OFFSET(FxElemDefDisk32, gravity, 0x98);"
    "ONDISK_OFFSET(FxElemDefDisk32, reflectionFactor, 0xA0);"
    "ONDISK_OFFSET(FxElemDefDisk32, atlas, 0xA8);"
    "ONDISK_OFFSET(FxElemDefDisk32, elemType, 0xB0);"
    "ONDISK_OFFSET(FxElemDefDisk32, visualCount, 0xB1);"
    "ONDISK_OFFSET(FxElemDefDisk32, velIntervalCount, 0xB2);"
    "ONDISK_OFFSET(FxElemDefDisk32, visStateIntervalCount, 0xB3);"
    "ONDISK_OFFSET(FxElemDefDisk32, velSamples, 0xB4);"
    "ONDISK_OFFSET(FxElemDefDisk32, visSamples, 0xB8);"
    "ONDISK_OFFSET(FxElemDefDisk32, visuals, 0xBC);"
    "ONDISK_OFFSET(FxElemDefDisk32, collMins, 0xC0);"
    "ONDISK_OFFSET(FxElemDefDisk32, collMaxs, 0xCC);"
    "ONDISK_OFFSET(FxElemDefDisk32, effectOnImpact, 0xD8);"
    "ONDISK_OFFSET(FxElemDefDisk32, effectOnDeath, 0xDC);"
    "ONDISK_OFFSET(FxElemDefDisk32, effectEmitted, 0xE0);"
    "ONDISK_OFFSET(FxElemDefDisk32, emitDist, 0xE4);"
    "ONDISK_OFFSET(FxElemDefDisk32, emitDistVariance, 0xEC);"
    "ONDISK_OFFSET(FxElemDefDisk32, trailDef, 0xF4);"
    "ONDISK_OFFSET(FxElemDefDisk32, sortOrder, 0xF8);"
    "ONDISK_OFFSET(FxElemDefDisk32, lightingFrac, 0xF9);"
    "ONDISK_OFFSET(FxElemDefDisk32, useItemClip, 0xFA);"
    "ONDISK_OFFSET(FxElemDefDisk32, unused, 0xFB);"
    "ONDISK_SIZE(FxEffectDefDisk32, 0x20);"
    "ONDISK_OFFSET(FxEffectDefDisk32, name, 0x00);"
    "ONDISK_OFFSET(FxEffectDefDisk32, flags, 0x04);"
    "ONDISK_OFFSET(FxEffectDefDisk32, totalSize, 0x08);"
    "ONDISK_OFFSET(FxEffectDefDisk32, msecLoopingLife, 0x0C);"
    "ONDISK_OFFSET(FxEffectDefDisk32, elemDefCountLooping, 0x10);"
    "ONDISK_OFFSET(FxEffectDefDisk32, elemDefCountOneShot, 0x14);"
    "ONDISK_OFFSET(FxEffectDefDisk32, elemDefCountEmission, 0x18);"
    "ONDISK_OFFSET(FxEffectDefDisk32, elemDefs, 0x1C);"
    "ONDISK_SIZE(FxImpactEntryDisk32, 0x84);"
    "ONDISK_OFFSET(FxImpactEntryDisk32, nonflesh, 0x00);"
    "ONDISK_OFFSET(FxImpactEntryDisk32, flesh, 0x74);"
    "ONDISK_SIZE(FxImpactTableDisk32, 0x08);"
    "ONDISK_OFFSET(FxImpactTableDisk32, name, 0x00);"
    "ONDISK_OFFSET(FxImpactTableDisk32, table, 0x04);")
    require_contains(_schema "${_marker}" "exact packed schema layout")
endforeach()

# Token-bearing fields must use Ptr32 for typed locations and PointerToken for
# union/name-reference grammar. Effect-name references and asset handles are
# intentionally distinct types even though both occupy one disk word.
foreach(_marker IN ITEMS
    "disk32::Ptr32<void> materials[2];"
    "disk32::Ptr32<FxTrailVertexDisk32> verts;"
    "disk32::Ptr32<std::uint16_t> inds;"
    "disk32::Ptr32<FxElemVelStateSampleDisk32> velSamples;"
    "disk32::Ptr32<FxElemVisStateSampleDisk32> visSamples;"
    "disk32::Ptr32<FxTrailDefDisk32> trailDef;"
    "disk32::Ptr32<const char> name;"
    "disk32::Ptr32<FxElemDefDisk32> elemDefs;"
    "disk32::Ptr32<FxImpactEntryDisk32> table;"
    "struct FxEffectDefRefDisk32 final { disk32::PointerToken token; };"
    "struct FxEffectDefHandleDisk32 final { disk32::PointerToken token; };"
    "FxEffectDefRefDisk32 effectOnImpact;"
    "FxEffectDefRefDisk32 effectOnDeath;"
    "FxEffectDefRefDisk32 effectEmitted;"
    "FxEffectDefHandleDisk32 nonflesh[kImpactNonFleshEffectCount];"
    "FxEffectDefHandleDisk32 flesh[kImpactFleshEffectCount];")
    require_contains(_schema "${_marker}" "typed token grammar")
endforeach()
foreach(_forbidden IN ITEMS
    "FxEffectDefHandleDisk32 effectOnImpact"
    "FxEffectDefHandleDisk32 effectOnDeath"
    "FxEffectDefHandleDisk32 effectEmitted"
    "FxEffectDefRefDisk32 nonflesh"
    "FxEffectDefRefDisk32 flesh")
    require_not_contains(
        _schema "${_forbidden}" "name references and asset handles cannot mix")
endforeach()

# The converter boundary is silent and failure-reporting: callers receive a
# status from both noexcept passes, with no production I/O or error reporter in
# the API. Its source/provenance callbacks carry native-width identities.
foreach(_marker IN ITEMS
    "enum class FxFastFileDisk32SourceSpanKind : std::uint8_t"
    "EffectHeader"
    "ElementDefinitions"
    "VelocitySamples"
    "VisibilitySamples"
    "Visuals"
    "MarkVisuals"
    "TrailDefinition"
    "TrailVertices"
    "TrailIndices"
    "String"
    "enum class FxFastFileDisk32ReferenceKind : std::uint8_t"
    "EffectName"
    "Material"
    "Model"
    "SoundName"
    "EffectNameReference"
    "enum class FxFastFileNativeDisk32Status : std::uint8_t"
    "Success"
    "Busy"
    "InvalidArgument"
    "InvalidPhase"
    "InvalidPlan"
    "InvalidCount"
    "InvalidSourceLayout"
    "InvalidPointerCount"
    "InvalidProvenance"
    "UnresolvedReference"
    "InvalidString"
    "InvalidVisual"
    "InvalidTrail"
    "SizeOverflow"
    "SourceChanged"
    "MisalignedStorage"
    "InsufficientCapacity"
    "OverlappingStorage"
    "enum class FxFastFileNativeDisk32Phase : std::uint8_t { Empty, Planned, };"
    "const disk32::PointerToken *sourceField"
    "disk32::PointerToken token"
    "const void *address"
    "std::uint64_t byteCount"
    "struct FxFastFileDisk32ResolvedReference { const void *pointer = nullptr;"
    "FxFastFileNativeDisk32Status TryPlanFxEffectDefDisk32("
    "FxFastFileNativeDisk32Status TryMaterializeFxEffectDefDisk32(")
    require_contains(_native "${_marker}" "report-free native conversion boundary")
endforeach()
foreach(_forbidden IN ITEMS
    "Com_Error"
    "Com_Printf"
    "MemoryFile"
    "MemFile_"
    "ERR_DROP"
    "throw "
    "setjmp"
    "longjmp"
    "std::uintptr_t"
    "std::uint32_t pointer")
    require_not_contains(
        _native "${_forbidden}" "converter API stays silent and full-width")
endforeach()

extract_slice(
    _native
    "// The source view and every reachable source byte"
    "// Materializes into aligned caller-owned storage"
    _plan_api
    "public planning API")
foreach(_marker IN ITEMS
    "[[nodiscard]] FxFastFileNativeDisk32Status TryPlanFxEffectDefDisk32("
    "FxFastFileNativeDisk32Workspace *workspace"
    "const FxFastFileEffectDefDisk32View &source"
    "const FxFastFileDisk32Resolvers &resolvers"
    "FxFastFileNativeDisk32Plan *outPlan) noexcept;"
    "Planning resolves each"
    "retained native identity exactly once under the operation gate and commits"
    "outPlan only after the full graph, journal, and native layout validate")
    require_contains(_plan_api "${_marker}" "transactional planning contract")
endforeach()

extract_slice(
    _native
    "// Materializes into aligned caller-owned storage"
    "static_assert("
    _materialize_api
    "public materialization API")
foreach(_marker IN ITEMS
    "[[nodiscard]] FxFastFileNativeDisk32Status TryMaterializeFxEffectDefDisk32("
    "FxFastFileNativeDisk32Workspace *workspace"
    "const FxFastFileNativeDisk32Plan &plan"
    "void *storage"
    "std::size_t capacity"
    "FxEffectDef **outEffect) noexcept;"
    "exact unconsumed"
    "plan/journal binding. It performs no resolver callbacks. Failure never"
    "changes outEffect; storage is changed only after plan, source fingerprint,"
    "capacity, alignment, and overlap checks complete")
    require_contains(
        _materialize_api "${_marker}" "transactional materialization contract")
endforeach()
foreach(_forbidden IN ITEMS "Resolvers" "resolve(" "Callback" "Com_")
    require_not_contains(
        _materialize_api "${_forbidden}" "materialization consumes only the journal")
endforeach()

# The fixed scratch object is explicitly documented heap-only, is noncopyable
# and nonmovable, and exposes no partial state. The one operation gate maps
# reentry to Busy through the public status enum.
foreach(_marker IN ITEMS
    "Heap-only scratch."
    "class alignas(8) FxFastFileNativeDisk32Workspace final"
    "FxFastFileNativeDisk32Workspace( const FxFastFileNativeDisk32Workspace &) = delete;"
    "FxFastFileNativeDisk32Workspace &operator=( const FxFastFileNativeDisk32Workspace &) = delete;"
    "FxFastFileNativeDisk32Workspace( FxFastFileNativeDisk32Workspace &&) = delete;"
    "FxFastFileNativeDisk32Workspace &operator=( FxFastFileNativeDisk32Workspace &&) = delete;"
    "FxFastFileDisk32ResolvedReference resolved_[kFxFastFileDisk32MaxResolvedReferences]{};"
    "FxFastFileNativeDisk32Phase phase_ = FxFastFileNativeDisk32Phase::Empty;"
    "bool operating_ = false;"
    "std::is_nothrow_default_constructible_v< FxFastFileNativeDisk32Workspace>"
    "std::is_nothrow_destructible_v<FxFastFileNativeDisk32Workspace>")
    require_contains(_native "${_marker}" "heap workspace and Busy gate")
endforeach()

# Plans expose only sizes/counts. Their unforgeable binding remains private and
# records the exact workspace generation plus the validated source fingerprint.
extract_slice(
    _native
    "class FxFastFileNativeDisk32Plan final"
    "// Heap-only scratch."
    _plan_type
    "opaque plan type")
foreach(_marker IN ITEMS
    "explicit operator bool() const noexcept"
    "return workspaceIdentity_ != nullptr && serial_ != 0;"
    "private:"
    "const FxFastFileNativeDisk32Workspace *workspaceIdentity_ = nullptr;"
    "std::uint64_t serial_ = 0;"
    "std::uint64_t sourceFingerprint_ = 0;"
    "std::uint32_t outputBytes_ = 0;"
    "std::uint32_t outputAlignment_ = 0;"
    "std::uint32_t elementCount_ = 0;"
    "std::uint32_t resolvedReferenceCount_ = 0;"
    "std::uint32_t effectNameBytes_ = 0;")
    require_contains(_plan_type "${_marker}" "opaque exact-plan binding")
endforeach()
require_ordered(
    _plan_type
    "private:"
    "const FxFastFileNativeDisk32Workspace *workspaceIdentity_ = nullptr;"
    "plan binding is private")

# This reader-first checkpoint deliberately leaves the stateful x86 FX loader
# and archive writer in place. Source markers pin that deferral without relying
# on repository history, so a production switch must update this contract in a
# separately reviewed integration batch.
foreach(_marker IN ITEMS
    "void __cdecl Load_FxEffectDefHandle(bool atStreamStart)"
    "Load_Stream(atStreamStart, (uint8_t *)varFxEffectDefHandle, 4);"
    "value = (uint32_t)*varFxEffectDefHandle;"
    "void __cdecl Load_FxEffectDefRef(bool atStreamStart)"
    "Load_FxEffectDefFromName((const char **)varFxEffectDefRef);"
    "Load_Stream(atStreamStart, (uint8_t *)varFxElemMarkVisuals, 8);"
    "Load_StreamArray(atStreamStart, (uint8_t *)varFxElemVisuals, count, 4);"
    "Load_StreamArray(atStreamStart, varFxElemVisStateSample->base.color, count, 48);"
    "Load_StreamArray(atStreamStart, (uint8_t *)varFxElemVelStateSample, count, 96);"
    "Load_Stream(atStreamStart, (uint8_t *)varFxTrailDef, 28);"
    "Load_Stream(atStreamStart, (uint8_t *)varFxElemDef, 252);"
    "Load_Stream(atStreamStart, (uint8_t *)varFxEffectDef, 32);"
    "Load_Stream(atStreamStart, (uint8_t *)varFxImpactEntry, 132);"
    "Load_FxEffectDefHandleArray(0, 29);"
    "Load_FxEffectDefHandleArray(0, 4);"
    "Load_Stream(atStreamStart, (uint8_t *)varFxImpactTable, 8);"
    "Load_FxImpactEntryArray(1, 12);")
    require_contains(_db_load "${_marker}" "legacy x86 FX loader remains deferred")
endforeach()
foreach(_forbidden IN ITEMS
    "fx_fastfile_disk32.h"
    "fx_fastfile_native_disk32.h"
    "TryPlanFxEffectDefDisk32"
    "TryMaterializeFxEffectDefDisk32")
    require_not_contains(
        _db_load "${_forbidden}" "portable converter is not production-integrated yet")
endforeach()

foreach(_marker IN ITEMS
    "bool FX_WriteArchiveDataNoDrop("
    "FxEffectTableSaveOutcome FX_WriteStagedEffectTableNoDrop("
    "void __cdecl FX_Save(int32_t clientIndex, MemoryFile *memFile)"
    "if (sizeof(void *) != 4)"
    "FX archive save requires Disk32 conversion on 64-bit targets"
    "FX archive save ABI does not match the legacy format"
    "FX_WriteStagedEffectTableNoDrop(&effectTableStaging, memFile)"
    "FX_WriteArchiveDataNoDrop( memFile, FX_ARCHIVE_SYSTEM_SIZE, &systemSnapshot)"
    "FX_WriteArchiveDataNoDrop( memFile, FX_ARCHIVE_BUFFER_SIZE, bufferSnapshot)")
    require_contains(_archive "${_marker}" "legacy writer and native64 guard remain deferred")
endforeach()
require_occurrence_count(
    _archive
    "if (sizeof(void *) != 4)"
    1
    "one remaining FX save native64 guard")
foreach(_forbidden IN ITEMS
    "fx_fastfile_disk32.h"
    "fx_fastfile_native_disk32.h"
    "TryPlanFxEffectDefDisk32"
    "TryMaterializeFxEffectDefDisk32")
    require_not_contains(
        _archive "${_forbidden}" "fast-file converter does not alter archive I/O")
endforeach()

message(STATUS "FX fast-file Disk32 source contract passed")
