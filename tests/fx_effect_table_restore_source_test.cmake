cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

set(_restore_header_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_effect_table_restore.h")
set(_key_header_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_key.h")
set(_restore_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_effect_table_restore.cpp")
set(_archive_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive.cpp")
set(_system_header_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_system.h")
set(_system_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_system.cpp")
set(_draw_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_draw.cpp")
set(_atomic_header_path
    "${SOURCE_ROOT}/src/universal/sys_atomic.h")
set(_common_files_path
    "${SOURCE_ROOT}/scripts/common_files.cmake")

foreach(_required_source IN ITEMS
    "${_key_header_path}"
    "${_restore_header_path}"
    "${_restore_source_path}"
    "${_archive_source_path}"
    "${_system_header_path}"
    "${_system_source_path}"
    "${_draw_source_path}"
    "${_atomic_header_path}"
    "${_common_files_path}")
    if(NOT EXISTS "${_required_source}")
        message(FATAL_ERROR "FX effect-table source not found: ${_required_source}")
    endif()
endforeach()

file(READ "${_key_header_path}" _key_header)
file(READ "${_restore_header_path}" _restore_header)
file(READ "${_restore_source_path}" _restore_source)
file(READ "${_archive_source_path}" _archive_source)
file(READ "${_system_header_path}" _system_header)
file(READ "${_system_source_path}" _system_source)
file(READ "${_draw_source_path}" _draw_source)
file(READ "${_atomic_header_path}" _atomic_header)
file(READ "${_common_files_path}" _common_files)

function(require_contains source needle description)
    string(FIND "${source}" "${needle}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "${description}: missing '${needle}'")
    endif()
endfunction()

function(require_absent source needle description)
    string(FIND "${source}" "${needle}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR "${description}: found forbidden '${needle}'")
    endif()
endfunction()

function(require_ordered source first second description)
    string(FIND "${source}" "${first}" _first_position)
    string(FIND "${source}" "${second}" _second_position)
    if(_first_position EQUAL -1
        OR _second_position EQUAL -1
        OR NOT _first_position LESS _second_position)
        message(FATAL_ERROR "${description}: missing or out of order")
    endif()
endfunction()

function(require_literal_count source needle expected_count description)
    set(_remaining "${source}")
    set(_count 0)
    string(LENGTH "${needle}" _needle_length)
    if(_needle_length EQUAL 0)
        message(FATAL_ERROR "${description}: needle cannot be empty")
    endif()
    while(TRUE)
        string(FIND "${_remaining}" "${needle}" _position)
        if(_position EQUAL -1)
            break()
        endif()
        math(EXPR _count "${_count} + 1")
        math(EXPR _next "${_position} + ${_needle_length}")
        string(SUBSTRING "${_remaining}" ${_next} -1 _remaining)
    endwhile()
    if(NOT _count EQUAL expected_count)
        message(FATAL_ERROR
            "${description}: expected ${expected_count}, found ${_count}")
    endif()
endfunction()

function(extract_slice source begin_marker end_marker out_slice description)
    string(FIND "${source}" "${begin_marker}" _begin)
    if(_begin EQUAL -1)
        message(FATAL_ERROR "${description}: missing '${begin_marker}'")
    endif()
    string(SUBSTRING "${source}" ${_begin} -1 _tail)
    string(FIND "${_tail}" "${end_marker}" _relative_end)
    if(_relative_end LESS_EQUAL 0)
        message(FATAL_ERROR "${description}: missing ordered '${end_marker}'")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_relative_end} _slice)
    set(${out_slice} "${_slice}" PARENT_SCOPE)
endfunction()

# The helper owns one fixed BSS image. It must not fall back to legacy
# reporting readers or introduce fallible heap ownership around FX_Register.
foreach(_public_contract IN ITEMS
    "EFFECT_TABLE_RESTORE_CAPACITY = 1024"
    "EFFECT_TABLE_RESTORE_NAME_CAPACITY = 64"
    "EffectDefinitionKey32 *key"
    "EffectDefinitionKey32 key) noexcept"
    "RestoreEffectTableNoReport("
    "ValidateEffectTableRestoreLease("
    "ReleaseEffectTableRestore("
    "AbandonCurrentThreadEffectTableRestoreForError() noexcept"
    "EffectTableRestoreLeaseIsActive() noexcept")
    require_contains(
        "${_restore_header}"
        "${_public_contract}"
        "effect-table restore API must retain its bounded lease contract")
endforeach()

foreach(_key_contract IN ITEMS
    "struct EffectDefinitionKey32 final"
    "std::uint32_t value = 0;"
    "ONDISK_SIZE(EffectDefinitionKey32, 4)")
    require_contains(
        "${_key_header}"
        "${_key_contract}"
        "restore must use the shared fixed-width effect key")
endforeach()

foreach(_workspace_member IN ITEMS
    "char g_restoreNames[EFFECT_TABLE_RESTORE_CAPACITY]"
    "std::uint8_t g_restoreNameLengths[EFFECT_TABLE_RESTORE_CAPACITY]"
    "EffectDefinitionKey32 g_restoreKeys[EFFECT_TABLE_RESTORE_CAPACITY]"
    "const void *g_restoreDefinitions[EFFECT_TABLE_RESTORE_CAPACITY]"
    "std::size_t g_restoreEntryCount;"
    "void *volatile g_restoreOwnerCookie;")
    require_contains(
        "${_restore_source}"
        "${_workspace_member}"
        "effect-table restore state must remain one bounded static image")
endforeach()

foreach(_forbidden_helper_call IN ITEMS
    "MemFile_ReadCString("
    "MemFile_ReadData("
    "FX_Register("
    "MyAssertHandler("
    "Com_Error("
    "Com_Printf("
    "Sys_Error("
    "Z_Malloc("
    "Z_Free("
    "malloc("
    "free(")
    require_absent(
        "${_restore_source}"
        "${_forbidden_helper_call}"
        "the portable restore helper must remain report- and heap-free")
endforeach()

extract_slice(
    "${_restore_source}"
    "EffectTableRestoreStatus ParseEffectTable("
    "} // namespace"
    _parse_source
    "effect-table parse phase")
require_contains(
    "${_parse_source}"
    "MemFile_TryReadCStringNoReport("
    "effect names must use the segment-bounded silent reader")
require_contains(
    "${_parse_source}"
    "MemFile_TryReadDataNoReport("
    "effect keys must use the segment-bounded silent reader")
foreach(_little_endian_shift IN ITEMS "<< 8u" "<< 16u" "<< 24u")
    require_contains(
        "${_parse_source}"
        "${_little_endian_shift}"
        "effect keys must be decoded explicitly little-endian")
endforeach()
require_contains(
    "${_parse_source}"
    "const EffectDefinitionKey32 key{"
    "parsed bytes must remain a strong archive key rather than a pointer")
require_contains(
    "${_parse_source}"
    "EffectDefinitionKeyIsValid(key)"
    "zero archive keys must fail before registration")
require_absent(
    "${_parse_source}"
    "registerEffect("
    "the complete table must parse before the first registration")

extract_slice(
    "${_restore_source}"
    "EffectTableRestoreResult RestoreEffectTableNoReport("
    "bool EffectTableRestoreGetEntry("
    _restore_operation
    "effect-table restore transaction")
require_ordered(
    "${_restore_operation}"
    "status = ParseEffectTable("
    "stableCallbacks.registerEffect("
    "registration must start only after complete parsing")
require_ordered(
    "${_restore_operation}"
    "stableCallbacks.registerEffect("
    "g_restoreEntryCount = entryCount;"
    "the table view must publish only after every registration")
require_ordered(
    "${_restore_operation}"
    "stableCallbacks.validateLifecycle("
    "Sys_AtomicCompareExchangePointer("
    "the lifecycle precheck must precede exact owner acquisition")
require_ordered(
    "${_restore_operation}"
    "Sys_AtomicCompareExchangePointer("
    "status = ValidateActiveLifecycle(lease);"
    "owner acquisition must be followed by a lifecycle handshake")

extract_slice(
    "${_restore_source}"
    "EffectTableRestoreStatus ValidateEffectTableRestoreLease("
    "bool EffectTableRestoreGetEntry("
    _lease_validation
    "effect-table exact lease validation")
require_contains(
    "${_lease_validation}"
    "return ValidateActiveLifecycle(lease);"
    "public lease validation must delegate to the exact active lifecycle guard")
require_absent(
    "${_lease_validation}"
    "CloseCurrentThreadLease("
    "lease validation must not close the caller-owned lease")
require_absent(
    "${_lease_validation}"
    "ClearRestoreWorkspace("
    "lease validation must not mutate the published table")

extract_slice(
    "${_restore_source}"
    "EffectTableRestoreStatus CloseCurrentThreadLease("
    "EffectTableRestoreResult FailureResult("
    _close_operation
    "effect-table lease close")
require_ordered(
    "${_close_operation}"
    "ClosingCookie(),"
    "ClearRestoreWorkspace();"
    "the exact active owner must transition to Closing before workspace clear")
require_ordered(
    "${_close_operation}"
    "ClearRestoreWorkspace();"
    "static_cast<void *>(nullptr),"
    "the workspace must clear before the global gate reopens")
require_ordered(
    "${_close_operation}"
    "static_cast<void *>(nullptr),"
    "ClearCurrentThreadOwner();"
    "the global gate must reopen before the retry-capable TLS token clears")

foreach(_normalization_guard IN ITEMS
    "name[index - 1] != '.'"
    "name[index - 1] != ' '"
    "name[index - 1] == '.'"
    "name[index - 1] == ' '"
    "ComponentHasReservedWindowsStem("
    "value == '\"'"
    "value == '<'"
    "value == '>'"
    "value == '|'"
    "value == '?'"
    "value == '*'")
    require_contains(
        "${_restore_source}"
        "${_normalization_guard}"
        "effect names must reject Windows-normalized trailing component bytes")
endforeach()

# The pointer CAS is the cross-toolchain ownership primitive. Both compiler
# paths must compare the exact pointer and return the previous value.
foreach(_atomic_contract IN ITEMS
    "Sys_AtomicCompareExchangePointer("
    "_InterlockedCompareExchangePointer("
    "__atomic_compare_exchange_n(")
    require_contains(
        "${_atomic_header}"
        "${_atomic_contract}"
        "pointer-width owner CAS must remain portable")
endforeach()

# Production integration retains the exact lease across the complete portable
# reader and mutable-candidate build. All fallible staging is prepared while
# definitions/models are protected, the reader is destroyed and the lease is
# revalidated before release, and archive admission follows without new work.
foreach(_listed_source IN ITEMS
    "EffectsCore/fx_effect_table_restore.cpp"
    "EffectsCore/fx_effect_table_restore.h")
    require_contains(
        "${_common_files}"
        "${_listed_source}"
        "the production source manifest must compile the restore helper")
endforeach()

foreach(_obsolete_contract IN ITEMS
    "FxEffectDefTable"
    "FX_RestoreEffectDefTable("
    "FX_AddEffectDefTableEntry("
    "FX_FindEffectDefInTable(")
    require_absent(
        "${_system_header}"
        "${_obsolete_contract}"
        "the stack-backed public effect-table API must remain removed")
    require_absent(
        "${_archive_source}"
        "${_obsolete_contract}"
        "the stack-backed effect-table implementation must remain removed")
endforeach()

extract_slice(
    "${_archive_source}"
    "void __cdecl FX_Restore(int32_t clientIndex, MemoryFile *memFile)"
    "FxEffect *__cdecl FX_EffectFromHandle("
    _archive_restore
    "production FX restore")
require_ordered(
    "${_archive_restore}"
    "RestoreEffectTableNoReport("
    "FX_AllocateArchiveDisk32ReaderWorkspace()"
    "effect registration must finish before fallible heap staging")
require_ordered(
    "${_archive_restore}"
    "TryReadFxArchiveDisk32NoReport("
    "TryBuildFxArchiveRestoreCandidateDisk32("
    "the lease-bound reader must validate the complete image before candidate copying")
extract_slice(
    "${_archive_restore}"
    "fx::archive::TryBuildFxArchiveRestoreCandidateDisk32("
    "if (candidateStatus"
    _candidate_build_call
    "exact reader/candidate build call")
require_ordered(
    "${_candidate_build_call}"
    "staging.reader,"
    "tableResult.lease,"
    "candidate construction must consume the exact reader before its lease")
require_ordered(
    "${_candidate_build_call}"
    "tableResult.lease,"
    "staging.candidate);"
    "candidate construction must bind the retained lease before mutable output")
require_ordered(
    "${_archive_restore}"
    "TryBuildFxArchiveRestoreCandidateDisk32("
    "TryGetFxArchiveRestoreCandidateDisk32ReadyView("
    "only a complete candidate may expose mutable production staging")
extract_slice(
    "${_archive_restore}"
    "if (!fx::archive::TryGetFxArchiveRestoreCandidateDisk32ReadyView("
    "FxSystem *const restoredSystem"
    _candidate_getter_call
    "exact-lease candidate Ready lookup")
require_ordered(
    "${_candidate_getter_call}"
    "staging.candidate,"
    "tableResult.lease,"
    "candidate Ready lookup must consume the built workspace before its lease")
require_ordered(
    "${_candidate_getter_call}"
    "tableResult.lease,"
    "&candidateView)"
    "candidate Ready lookup must bind the exact lease before output")
require_ordered(
    "${_archive_restore}"
    "FX_AllocateArchiveRestoreTransactionWorkspace()"
    "FX_DestroyArchiveDisk32ReaderWorkspace(staging.reader)"
    "all fallible transaction staging must precede reader destruction")
require_ordered(
    "${_archive_restore}"
    "FX_DestroyArchiveDisk32ReaderWorkspace(staging.reader)"
    "ReleaseEffectTableRestore(tableResult.lease)"
    "the reader must be destroyed while exact lease ownership remains active")
extract_slice(
    "${_archive_restore}"
    "staging.reader = nullptr;"
    "const fx::archive::EffectTableRestoreStatus tableReleaseStatus ="
    _post_reader_lease_handshake
    "post-reader exact lease handshake")
require_contains(
    "${_post_reader_lease_handshake}"
    "ValidateEffectTableRestoreLease(tableResult.lease)"
    "the exact lease must validate after reader destruction")
require_ordered(
    "${_archive_restore}"
    "staging.reader = nullptr;"
    "ReleaseEffectTableRestore(tableResult.lease)"
    "the lease must be revalidated after successful reader destruction")
require_ordered(
    "${_archive_restore}"
    "ReleaseEffectTableRestore(tableResult.lease)"
    "FX_BeginArchive(system, restoreGeneration)"
    "the effect-table lease must release immediately before archive admission")
foreach(_obsolete_raw_restore IN ITEMS
    "FX_ReadArchiveDataNoDrop("
    "FX_FixupEffectDefHandlesNoDrop("
    "FX_RebuildArchivePoolAllocationStates("
    "FX_CollectArchivePhysicsEntries(")
    require_absent(
        "${_archive_restore}"
        "${_obsolete_raw_restore}"
        "production restore must not bypass unified reader/candidate validation")
endforeach()

extract_slice(
    "${_archive_source}"
    "[[noreturn]] void FX_ReportArchiveDisk32RestoreFailure("
    "fx::archive::RestoreControlOperationStatus"
    _restore_failure
    "portable restore reporting boundary")
require_ordered(
    "${_restore_failure}"
    "FX_DestroyArchiveDisk32ReaderWorkspace(staging->reader)"
    "staging->reader = nullptr;"
    "the reader must be destroyed and disowned while its exact lease is active")
require_ordered(
    "${_restore_failure}"
    "staging->reader = nullptr;"
    "ReleaseEffectTableRestore(*lease)"
    "failure cleanup must release the exact lease only after reader destruction")
require_ordered(
    "${_restore_failure}"
    "ReleaseEffectTableRestore(*lease)"
    "FX_DestroyArchiveDisk32RestoreStaging(staging)"
    "remaining self-owned staging must be destroyed after lease release")
require_ordered(
    "${_restore_failure}"
    "FX_DestroyArchiveDisk32RestoreStaging(staging)"
    "Com_Error(ERR_DROP"
    "all staging and restore ownership must close before ERR_DROP can longjmp")

foreach(_admission_guard IN ITEMS
    "EffectTableRestoreLeaseIsActive()"
    "FX_EffectTableRestoreLifecycleIsCurrent("
    "FX_BeginArchive(\n    FxSystem *const system,\n    const std::uint32_t expectedGeneration)")
    require_contains(
        "${_system_source}"
        "${_admission_guard}"
        "archive and lifecycle admission must honor effect-table ownership")
endforeach()

extract_slice(
    "${_system_source}"
    "case Operation::ClaimPending:"
    "case Operation::TryAcquireIterator:"
    _archive_claim
    "archive/effect-table admission handshake")
require_ordered(
    "${_archive_claim}"
    "if (fx::archive::EffectTableRestoreLeaseIsActive())"
    "context.gate, pending, open) != open"
    "archive admission must precheck the effect-table owner before gate CAS")
require_ordered(
    "${_archive_claim}"
    "context.gate, pending, open) != open"
    "if (!fx::archive::EffectTableRestoreLeaseIsActive())"
    "archive admission must recheck the effect-table owner after gate CAS")
require_ordered(
    "${_archive_claim}"
    "if (!fx::archive::EffectTableRestoreLeaseIsActive())"
    "context.gate, open, pending) == pending"
    "a lost archive/effect-table race must reopen only the claimed gate")

extract_slice(
    "${_system_source}"
    "FxLifecycleClaimStatus FX_BeginLifecycleClaim("
    "bool FX_EndLifecycleClaim("
    _lifecycle_claim
    "lifecycle/effect-table admission handshake")
require_ordered(
    "${_lifecycle_claim}"
    "if (fx::archive::EffectTableRestoreLeaseIsActive())"
    "Sys_AtomicCompareExchange(archiveGate, 2, 0)"
    "lifecycle admission must precheck the effect-table owner before gate CAS")
require_ordered(
    "${_lifecycle_claim}"
    "Sys_AtomicCompareExchange(archiveGate, 2, 0)"
    "const bool reopened ="
    "lifecycle admission must recheck and roll back after gate CAS")
require_ordered(
    "${_draw_source}"
    "AbandonCurrentThreadEffectTableRestoreForError();"
    "FX_AbandonCurrentThreadEffectKillExclusiveForError();"
    "error unwind must abandon effect-table ownership before other FX gates")
