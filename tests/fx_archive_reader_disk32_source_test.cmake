cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_header_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_reader_disk32.h")
set(_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_reader_disk32.cpp")
set(_archive_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive.cpp")
set(_manifest_path "${SOURCE_ROOT}/scripts/common_files.cmake")
set(_tests_path "${SOURCE_ROOT}/tests/CMakeLists.txt")
set(_fixture_path
    "${SOURCE_ROOT}/tests/fx_archive_reader_disk32_tests.cpp")
set(_ci_path "${SOURCE_ROOT}/.github/workflows/ci.yml")

foreach(_path IN ITEMS
    "${_header_path}"
    "${_source_path}"
    "${_archive_path}"
    "${_manifest_path}"
    "${_tests_path}"
    "${_fixture_path}"
    "${_ci_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing FX Disk32 reader source: ${_path}")
    endif()
endforeach()

file(READ "${_header_path}" _header)
file(READ "${_source_path}" _source)
file(READ "${_archive_path}" _archive)
file(READ "${_manifest_path}" _manifest)
file(READ "${_tests_path}" _tests)
file(READ "${_fixture_path}" _fixture)
file(READ "${_ci_path}" _ci)

foreach(_var IN ITEMS
    _header _source _archive _manifest _tests _fixture _ci)
    string(REGEX REPLACE "[ \t\r\n]+" " " _normalized "${${_var}}")
    set(${_var} "${_normalized}")
endforeach()

function(require_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing FX Disk32 reader invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_not_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden FX Disk32 reader regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_ordered SOURCE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${FIRST}" _first)
    if(_first EQUAL -1)
        message(FATAL_ERROR
            "Missing first FX Disk32 reader invariant (${DESCRIPTION}): '${FIRST}'")
    endif()
    string(SUBSTRING "${${SOURCE_VAR}}" ${_first} -1 _tail)
    string(FIND "${_tail}" "${SECOND}" _second)
    if(_second LESS_EQUAL 0)
        message(FATAL_ERROR
            "Missing or unordered FX Disk32 reader invariant (${DESCRIPTION}): '${SECOND}'")
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
            "Wrong FX Disk32 reader invariant count (${DESCRIPTION}): "
            "expected ${EXPECTED}, found ${_count} for '${NEEDLE}'")
    endif()
endfunction()

function(require_not_matches SOURCE_VAR PATTERN DESCRIPTION)
    string(REGEX MATCH "${PATTERN}" _match "${${SOURCE_VAR}}")
    if(NOT _match STREQUAL "")
        message(FATAL_ERROR
            "Forbidden FX Disk32 reader regression (${DESCRIPTION}): '${_match}'")
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

# The public boundary owns all large fixed/native images and decoded physics
# records inside one noncopyable heap workspace.  Only the final Ready scalar
# publishes a view; callers retain and release the exact external table lease.
foreach(_marker IN ITEMS
    "enum class FxArchiveDisk32ReaderPhase"
    "Empty"
    "Ready"
    "enum class FxArchiveDisk32ReaderStatus"
    "Success"
    "Busy"
    "InvalidLease"
    "InvalidMemoryFile"
    "TruncatedInput"
    "InvalidStructuralImage"
    "InvalidSemanticImage"
    "InvalidRelocation"
    "InvalidBodyState"
    "struct FxArchiveDisk32ReaderPhysicsBody"
    "FxArchiveDisk32ReadyPhysicsDescriptor descriptor{};"
    "BodyState state{};"
    "TryReadFxArchiveDisk32NoReport("
    "TryGetFxArchiveDisk32ReaderReadyView("
    "class alignas(8) FxArchiveDisk32ReaderWorkspace final"
    "FxArchiveDisk32ReaderWorkspace( const FxArchiveDisk32ReaderWorkspace &) = delete;"
    "FxSystemDisk32 sourceSystem_{};"
    "FxSystemBuffersDisk32 sourceBuffers_{};"
    "FxArchiveDisk32NativeWorkspace nativeWorkspace_{};"
    "struct FxArchiveDisk32ReaderLeaseIdentity"
    "const void *identity = nullptr;"
    "const void *ownerCookie = nullptr;"
    "std::uint32_t lifecycleGeneration = 0;"
    "std::uint32_t serialLow = 0;"
    "std::uint32_t serialHigh = 0;"
    "RUNTIME_SIZE(FxArchiveDisk32ReaderLeaseIdentity, 0x14, 0x20);"
    "FxArchiveDisk32ReaderLeaseIdentity lease_{};"
    "ArchiveAddress32 archivedSystemAddress_{};"
    "BodyStateDisk32 bodyScratch_{};"
    "physicsBodies_[FX_ARCHIVE_PHYSICS_BODY_LIMIT]{};"
    "std::uint32_t physicsBodyCount_ = 0;"
    "FxArchiveDisk32ReaderPhase phase_ = FxArchiveDisk32ReaderPhase::Empty;"
    "mutable bool operating_ = false;"
    "struct FxArchiveDisk32ReaderReadyView"
    "FxArchiveDisk32ReadyView graph{};"
    "ArchiveAddress32 archivedSystemAddress{};"
    "const FxArchiveDisk32ReaderPhysicsBody *physicsBodies = nullptr;"
    "std::uint32_t physicsBodyCount = 0;"
    "static_assert(alignof(FxArchiveDisk32ReaderWorkspace) == 8);"
    "RUNTIME_SIZE(FxArchiveDisk32ReaderPhysicsBody, 0x80, 0x90);"
    "RUNTIME_SIZE(FxArchiveDisk32ReaderWorkspace, 0xA3D00, 0xA9D58);"
    "roughly 650--700 KiB object on the Windows x86"
    "thread stack; production integration must use the checked archive allocator")
    require_contains(_header "${_marker}" "bounded heap-owned public API")
endforeach()

# The stored Ready identity is a fixed-layout, bit-exact snapshot of the
# public lease.  Split serial halves avoid i386 ABI alignment differences
# without weakening any identity or ownership comparison.
foreach(_marker IN ITEMS
    "FxArchiveDisk32ReaderLeaseIdentity SnapshotLease("
    "static_cast<std::uint32_t>(lease.serial)"
    "static_cast<std::uint32_t>(lease.serial >> 32u)"
    "const FxArchiveDisk32ReaderLeaseIdentity candidate = SnapshotLease(lease);"
    "stored.identity == candidate.identity"
    "stored.ownerCookie == candidate.ownerCookie"
    "stored.lifecycleGeneration == candidate.lifecycleGeneration"
    "stored.serialLow == candidate.serialLow"
    "stored.serialHigh == candidate.serialHigh")
    require_contains(_source "${_marker}" "fixed-layout exact lease snapshot")
endforeach()
foreach(_forbidden IN ITEMS
    "#include <universal/memfile.h>"
    "FX_Restore"
    "FX_Save"
    "FX_GetSystem"
    "FX_GetSystemBuffers"
    "Phys_"
    "d3d9.h"
    "gfx_d3d")
    require_not_contains(
        _header "${_forbidden}" "public header remains portable and nonpublishing")
endforeach()

# ReadExact is the sole archive-I/O boundary and maps all MemoryFile outcomes
# without reporting.  Every complete object remains hidden in workspace-owned
# storage even though a failed legacy decoder may have written a prefix.
extract_slice(
    _source
    "FxArchiveDisk32ReaderStatus ReadExact("
    "struct DefinitionResolverContext"
    _read_exact
    "bounded no-report read adapter")
foreach(_marker IN ITEMS
    "MemFile_TryReadDataNoReport("
    "MemFileReadStatus::Success"
    "MemFileReadStatus::InvalidArgument"
    "MemFileReadStatus::InvalidState"
    "MemFileReadStatus::Overflow"
    "MemFileReadStatus::OutputTooSmall"
    "FxArchiveDisk32ReaderStatus::TruncatedInput")
    require_contains(_read_exact "${_marker}" "complete read-status mapping")
endforeach()
require_occurrence_count(
    _source
    "MemFile_TryReadDataNoReport("
    1
    "one fixed no-report MemoryFile boundary")

# Resolver callbacks borrow only the exact retained lease.  A missing
# definition is distinguished from a lifecycle/ownership loss before the
# structural error can be exposed to the outer transaction.
extract_slice(
    _source
    "struct DefinitionResolverContext"
    "struct PhysicsSinkContext"
    _resolver
    "definition resolver lease adapter")
foreach(_marker IN ITEMS
    "EffectTableRestoreFind(*context->lease, key)"
    "ValidateEffectTableRestoreLease(*context->lease)"
    "context->leaseRejected = true;"
    "static_cast<const FxEffectDef *>(definition)")
    require_contains(_resolver "${_marker}" "exact definition-lease resolution")
endforeach()

# Physics enumeration fills hidden descriptors in exact contiguous index
# order.  Every descriptor field validates before a body slot is touched.
extract_slice(
    _source
    "struct PhysicsSinkContext"
    "} // namespace"
    _physics_sink
    "bounded hidden physics descriptor sink")
foreach(_marker IN ITEMS
    "physicsIndex != context->acceptedCount"
    "physicsIndex >= FX_ARCHIVE_PHYSICS_BODY_LIMIT"
    "!descriptor.elem || !descriptor.model"
    "descriptor.ownerIndex >= MAX_ELEMS"
    "descriptor.token == FX_ARCHIVE_INVALID_PHYSICS_TOKEN"
    "body.descriptor = descriptor;"
    "body.state = BodyState{};"
    "++context->acceptedCount;")
    require_contains(_physics_sink "${_marker}" "bounded descriptor staging")
endforeach()
require_ordered(
    _physics_sink
    "descriptor.token == FX_ARCHIVE_INVALID_PHYSICS_TOKEN"
    "body.descriptor = descriptor;"
    "all descriptor validation precedes hidden output mutation")

# The complete reader transaction must follow the legacy logical wire order:
# system, buffers, semantic/native validation, address, then exactly the
# semantics-derived body count.  The lease is checked at both ends, retry
# invalidates old views first, and Ready is the final mutation.
extract_slice(
    _source
    "FxArchiveDisk32ReaderStatus TryReadFxArchiveDisk32NoReport("
    "bool TryGetFxArchiveDisk32ReaderReadyView("
    _reader
    "complete fixed-tail reader transaction")
require_ordered(
    _reader
    "if (workspace->operating_)"
    "workspace->phase_ = FxArchiveDisk32ReaderPhase::Empty;"
    "same-workspace reentry cannot invalidate the outer transaction")
require_ordered(
    _reader
    "workspace->phase_ = FxArchiveDisk32ReaderPhase::Empty;"
    "workspace->operating_ = true;"
    "old views invalidate before a lifecycle callback or staged read")
require_ordered(
    _reader
    "ValidateEffectTableRestoreLease(leaseSnapshot)"
    "memFile, sizeof(workspace->sourceSystem_)"
    "the exact lease validates before the first archive byte")
require_ordered(
    _reader
    "memFile, sizeof(workspace->sourceSystem_)"
    "memFile, sizeof(workspace->sourceBuffers_)"
    "fixed system precedes fixed buffers")
require_ordered(
    _reader
    "memFile, sizeof(workspace->sourceBuffers_)"
    "TryBuildFxArchiveDisk32StructuralImage("
    "both complete disk blocks precede structural conversion")
require_ordered(
    _reader
    "TryBuildFxArchiveDisk32StructuralImage("
    "TryFinalizeFxArchiveDisk32NativeImage("
    "structure precedes definition-aware Ready finalization")
require_ordered(
    _reader
    "TryFinalizeFxArchiveDisk32NativeImage("
    "TryEnumerateFxArchiveDisk32ReadyPhysics("
    "Ready physics count precedes descriptor enumeration")

# If ownership or lifecycle is lost while native semantic work is failing,
# the lease failure is authoritative.  Never misclassify it as hostile image
# damage, which would let callers take the wrong recovery path.
extract_slice(
    _reader
    "if (TryFinalizeFxArchiveDisk32NativeImage("
    "FxArchiveDisk32ReadyView graphView{};"
    _finalize_failure
    "native finalization failure classification")
require_ordered(
    _finalize_failure
    "ValidateEffectTableRestoreLease(leaseSnapshot)"
    "return finish(FxArchiveDisk32ReaderStatus::InvalidLease);"
    "finalization failure revalidates exact lease")
require_ordered(
    _finalize_failure
    "return finish(FxArchiveDisk32ReaderStatus::InvalidLease);"
    "FxArchiveDisk32ReaderStatus::InvalidSemanticImage"
    "lease loss takes precedence over semantic-image failure")

extract_slice(
    _reader
    "if (!TryEnumerateFxArchiveDisk32ReadyPhysics("
    "readStatus = ReadExact("
    _enumeration_failure
    "Ready physics enumeration failure classification")
require_ordered(
    _enumeration_failure
    "ValidateEffectTableRestoreLease(leaseSnapshot)"
    "return finish(FxArchiveDisk32ReaderStatus::InvalidLease);"
    "enumeration failure revalidates exact lease")
require_ordered(
    _enumeration_failure
    "return finish(FxArchiveDisk32ReaderStatus::InvalidLease);"
    "FxArchiveDisk32ReaderStatus::InvalidSemanticImage"
    "lease loss takes precedence over enumeration failure")
require_ordered(
    _reader
    "TryEnumerateFxArchiveDisk32ReadyPhysics("
    "memFile, sizeof(workspace->archivedSystemAddress_)"
    "all semantics derive the exact body count before remaining wire reads")
require_ordered(
    _reader
    "memFile, sizeof(workspace->archivedSystemAddress_)"
    "workspace->archivedSystemAddress_.value == 0"
    "the complete relocation word validates before body input")
require_ordered(
    _reader
    "workspace->archivedSystemAddress_.value == 0"
    "index < sink.acceptedCount"
    "only a nonzero relocation word admits body records")
require_ordered(
    _reader
    "memFile, sizeof(workspace->bodyScratch_)"
    "TryUnpackBodyStateDisk32("
    "each complete fixed body record precedes native decoding")
require_contains(
    _reader
    "graphView.system->msecNow, &workspace->physicsBodies_[index].state"
    "body sleep timestamps rebase to the validated staged FX clock")
require_ordered(
    _reader
    "TryUnpackBodyStateDisk32("
    "ValidateEffectTableRestoreLease(leaseSnapshot)"
    "the retained lease is revalidated after every body read")
require_ordered(
    _reader
    "ValidateEffectTableRestoreLease(leaseSnapshot)"
    "FxArchiveDisk32ReadyView finalGraphView{};"
    "the final lease handshake precedes graph stability validation")
require_ordered(
    _reader
    "workspace->lease_ = SnapshotLease(leaseSnapshot);"
    "workspace->physicsBodyCount_ ="
    "the exact lease publishes before its bounded body count")
require_ordered(
    _reader
    "workspace->physicsBodyCount_ ="
    "workspace->operating_ = false;"
    "all visible metadata completes before the operation gate opens")
require_ordered(
    _reader
    "workspace->operating_ = false;"
    "workspace->phase_ = FxArchiveDisk32ReaderPhase::Ready;"
    "Ready is the final successful workspace mutation")
extract_slice(
    _reader
    "workspace->phase_ = FxArchiveDisk32ReaderPhase::Ready;"
    "return FxArchiveDisk32ReaderStatus::Success;"
    _ready_tail
    "outer Ready publication tail")
string(REPLACE
    "workspace->phase_ = FxArchiveDisk32ReaderPhase::Ready;"
    ""
    _after_ready
    "${_ready_tail}")
require_not_contains(
    _after_ready "workspace->" "no workspace access follows Ready publication")
require_occurrence_count(
    _reader
    "workspace->phase_ = FxArchiveDisk32ReaderPhase::Ready;"
    1
    "one outer Ready publication point")
foreach(_marker IN ITEMS
    "const auto finish = [workspace]"
    "workspace->operating_ = false;"
    "return finish(FxArchiveDisk32ReaderStatus::InvalidLease);"
    "return finish(readStatus);"
    "FxArchiveDisk32ReaderStatus::InvalidStructuralImage"
    "FxArchiveDisk32ReaderStatus::InvalidSemanticImage"
    "FxArchiveDisk32ReaderStatus::InvalidRelocation"
    "FxArchiveDisk32ReaderStatus::InvalidBodyState")
    require_contains(_reader "${_marker}" "failure cleanup and status partition")
endforeach()
foreach(_forbidden IN ITEMS
    "ReleaseEffectTableRestore("
    "AbandonCurrentThreadEffectTableRestoreForError("
    "MemFile_MoveToSegment("
    "MemFile_Shutdown(")
    require_not_contains(
        _reader "${_forbidden}" "reader borrows the lease and streaming cursor")
endforeach()

# Ready views require the exact stored lease and a fresh lifecycle handshake.
# Every rejection preserves outView; the single commit occurs only after a
# complete local view and after reopening the operation gate.
extract_slice(
    _source
    "bool TryGetFxArchiveDisk32ReaderReadyView("
    "} // namespace fx::archive"
    _view_getter
    "lease-gated Ready view getter")
require_ordered(
    _view_getter
    "workspace->phase_ != FxArchiveDisk32ReaderPhase::Ready"
    "!LeaseMatches(workspace->lease_, lease)"
    "phase and exact stored lease both gate a view")
require_ordered(
    _view_getter
    "workspace->operating_ = true;"
    "ValidateEffectTableRestoreLease(lease)"
    "the operation gate covers the lifecycle callback")
require_ordered(
    _view_getter
    "ValidateEffectTableRestoreLease(lease)"
    "TryGetFxArchiveDisk32ReadyView("
    "the lease validates before native graph access")
require_ordered(
    _view_getter
    "const FxArchiveDisk32ReaderReadyView view{"
    "workspace->operating_ = false;"
    "a complete local view forms before reopening the operation gate")
require_ordered(
    _view_getter
    "workspace->operating_ = false;"
    "*outView = view;"
    "the output commits only after callback-capable work has ended")
extract_slice(
    _view_getter
    "bool TryGetFxArchiveDisk32ReaderReadyView("
    "*outView = view;"
    _view_precommit
    "Ready view rejection paths")
require_not_contains(
    _view_precommit "*outView =" "Ready view failures preserve caller output")
require_occurrence_count(
    _view_getter "*outView = view;" 1 "one Ready view commit")
require_not_contains(
    _view_getter
    "workspace->phase_ ="
    "view validation never invalidates a successfully staged reader")

# This helper is attacker-reachable staging code.  Allocation, reports, locks,
# native physics, live graph access, and implicit archive ownership stay out of
# the source; callers provide already heap-constructed storage.
foreach(_forbidden IN ITEMS
    "MemFile_ReadData("
    "MemFile_ArchiveData("
    "MemFile_ReadCString("
    "Z_Malloc"
    "Z_Free"
    "malloc("
    "calloc("
    "realloc("
    "operator new"
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
    "FX_Restore"
    "FX_Save"
    "FX_GetSystem"
    "FX_GetSystemBuffers"
    "FX_LinkSystemBuffers"
    "FX_BeginArchive"
    "Phys_")
    require_not_contains(
        _source "${_forbidden}" "report-free nonpublishing fixed staging")
endforeach()
require_not_matches(
    _source
    "(^|[^A-Za-z0-9_])dBody([^A-Za-z0-9_]|$)"
    "reader cannot invoke live ODE bodies")
foreach(_large_type IN ITEMS
    "FxSystemDisk32"
    "FxSystemBuffersDisk32"
    "FxArchiveDisk32NativeWorkspace"
    "FxArchiveDisk32ReaderWorkspace")
    require_not_matches(
        _source
        "(^|[^A-Za-z0-9_])${_large_type} +[A-Za-z_][A-Za-z0-9_]*"
        "large disk/native/workspace images cannot be local values")
endforeach()

# This checkpoint intentionally stages but does not replace production.  Both
# legacy x86 entry points and native64 guards remain, and production cannot
# include or invoke the new reader before the next equivalence/rollback PR.
foreach(_marker IN ITEMS
    "void __cdecl FX_Restore(int32_t clientIndex, MemoryFile *memFile)"
    "void __cdecl FX_Save(int32_t clientIndex, MemoryFile *memFile)"
    "FX archive restore requires Disk32 conversion on 64-bit targets"
    "FX archive save requires Disk32 conversion on 64-bit targets")
    require_contains(_archive "${_marker}" "production guard remains unchanged")
endforeach()
require_occurrence_count(
    _archive "if (sizeof(void *) != 4)" 2 "restore and save native64 guards")
foreach(_forbidden IN ITEMS
    "fx_archive_reader_disk32"
    "TryReadFxArchiveDisk32NoReport"
    "TryGetFxArchiveDisk32ReaderReadyView")
    require_not_contains(
        _archive "${_forbidden}" "portable reader remains nonproduction")
endforeach()

# Keep engine compilation, all portable runners, measured Windows x86, the
# executable raw/zlib fixture, and this source contract wired together.
foreach(_marker IN ITEMS
    "fx_archive_reader_disk32.cpp"
    "fx_archive_reader_disk32.h")
    require_contains(_manifest "${_marker}" "engine source manifest")
endforeach()
foreach(_marker IN ITEMS
    "add_library(kisakcod-fx-archive-reader-disk32-subject OBJECT"
    "EffectsCore/fx_archive_reader_disk32.cpp"
    "kisakcod_fx_helper_stack_budget( kisakcod-fx-archive-reader-disk32-subject)"
    "add_executable(kisakcod-fx-archive-reader-disk32-tests"
    "fx_archive_reader_disk32_tests.cpp"
    "kisakcod-fx-archive-system-disk32-subject"
    "kisakcod-fx-archive-buffers-disk32-subject"
    "kisakcod-fx-archive-native-disk32-subject"
    "kisakcod-fx-archive-body-state-disk32-subject"
    "kisakcod-fx-effect-table-restore-subject"
    "kisakcod-memfile-test-subject"
    "Threads::Threads"
    "effectscore-archive-reader-disk32"
    "effectscore-archive-reader-disk32-source-invariants"
    "fx_archive_reader_disk32_source_test.cmake")
    require_contains(_tests "${_marker}" "portable reader fixture and contract")
endforeach()
foreach(_marker IN ITEMS
    "#include <algorithm>"
    "extern const float fx_randomTable[507]{};"
    "AllocateArchiveRestoreWorkspace< archive::FxArchiveDisk32ReaderWorkspace>"
    "for (const bool compress : {false, true})"
    "std::size_t{0}, std::size_t{2}"
    "UINT32_C(0xFFFFFFF0)"
    "FX_ARCHIVE_PHYSICS_BODY_LIMIT"
    "TruncatedInput"
    "InvalidMemoryFile"
    "FxArchiveDisk32ReaderStatus::InvalidStructuralImage"
    "FxArchiveDisk32ReaderStatus::InvalidSemanticImage"
    "InvalidRelocation"
    "InvalidBodyState"
    "FxArchiveDisk32ReaderStatus::InvalidLease"
    "FxArchiveDisk32ReaderStatus::Busy"
    "EffectTableRestoreStatus::LifecycleChanged"
    "for (std::size_t addressBytes = 0; addressBytes < 4; ++addressBytes)"
    "bodyBytes < sizeof(archive::BodyStateDisk32)"
    "firstState.reenterReadyView = true;"
    "CHECK(!firstState.nestedViewStatus);"
    "forged[4].serial ^= UINT64_C(1) << 32u;"
    "invalidateAfterValidationCall = 3;"
    "TestLateLifecycleInvalidationClassification();"
    "TestRawAndCompressedEmptyAndPhysics();"
    "TestMaximumPhysicsCapacity();"
    "TestMalformedTailAndFreshReaderRetry();"
    "TestLeaseGatesAndCallbackReentry();")
    require_contains(_fixture "${_marker}" "executable reader coverage")
endforeach()
require_not_matches(
    _fixture
    "FxArchiveDisk32ReaderWorkspace +[A-Za-z_][A-Za-z0-9_]*"
    "the roughly 700 KiB reader workspace cannot enter the test stack")
foreach(_marker IN ITEMS
    "Linux amd64"
    "Linux arm64"
    "Windows amd64"
    "Windows arm64"
    "macOS arm64"
    "kisakcod-fx-archive-reader-disk32-tests"
    "effectscore-archive-reader-disk32")
    require_contains(_ci "${_marker}" "portable and Windows x86 CI coverage")
endforeach()

message(STATUS "FX Disk32 standalone reader source invariants verified")
