cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

set(_save_header_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_effect_table_save.h")
set(_key_header_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_key.h")
set(_save_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_effect_table_save.cpp")
set(_archive_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive.cpp")
set(_system_header_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_system.h")
set(_common_files_path
    "${SOURCE_ROOT}/scripts/common_files.cmake")

foreach(_required_path IN ITEMS
    "${_key_header_path}"
    "${_save_header_path}"
    "${_save_source_path}"
    "${_archive_source_path}"
    "${_system_header_path}"
    "${_common_files_path}")
    if(NOT EXISTS "${_required_path}")
        message(FATAL_ERROR
            "FX effect-table save source not found: ${_required_path}")
    endif()
endforeach()

file(READ "${_key_header_path}" _key_header)
file(READ "${_save_header_path}" _save_header)
file(READ "${_save_source_path}" _save_source)
file(READ "${_archive_source_path}" _archive_source)
file(READ "${_system_header_path}" _system_header)
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

function(extract_slice source begin_marker end_marker out_slice description)
    string(FIND "${source}" "${begin_marker}" _begin)
    if(_begin EQUAL -1)
        message(FATAL_ERROR "${description}: missing '${begin_marker}'")
    endif()
    string(SUBSTRING "${source}" ${_begin} -1 _tail)
    string(FIND "${_tail}" "${end_marker}" _relative_end)
    if(_relative_end LESS_EQUAL 0)
        message(FATAL_ERROR
            "${description}: missing ordered '${end_marker}'")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_relative_end} _slice)
    set(${out_slice} "${_slice}" PARENT_SCOPE)
endfunction()

foreach(_api_marker IN ITEMS
    "struct EffectTableSaveSnapshot;"
    "EffectTableSaveSnapshotSize() noexcept"
    "EffectTableSaveSnapshotAlignment() noexcept"
    "ConstructEffectTableSaveSnapshot("
    "DestroyEffectTableSaveSnapshot("
    "AppendEffectTableSaveDefinitionNoReport("
    "ValidateEffectTableSaveSnapshotNoReport("
    "FindEffectTableSaveDefinitionKey("
    "WriteEffectTableSaveSnapshotNoReport("
    "EffectTableSaveEntryCount(")
    require_contains(
        "${_save_header}"
        "${_api_marker}"
        "save helper must retain its opaque bounded API")
endforeach()

foreach(_key_contract IN ITEMS
    "struct EffectDefinitionKey32 final"
    "std::uint32_t value = 0;"
    "LegacyPointerBits"
    "OpaqueSequential"
    "ONDISK_SIZE(EffectDefinitionKey32, 4)")
    require_contains(
        "${_key_header}"
        "${_key_contract}"
        "save helper must use one strong fixed-width archive key contract")
endforeach()

foreach(_status_marker IN ITEMS
    "NameTooLong"
    "InvalidName"
    "CapacityExceeded"
    "InvalidKey"
    "ConflictingDuplicate"
    "WriterFailed")
    require_contains(
        "${_save_header}"
        "${_status_marker}"
        "save helper failures must remain explicit")
endforeach()

foreach(_workspace_marker IN ITEMS
    "Record records[EFFECT_TABLE_RESTORE_CAPACITY]"
    "char name[EFFECT_TABLE_RESTORE_NAME_CAPACITY]"
    "std::uintptr_t nativeIdentity;"
    "EffectDefinitionKey32 diskKey;"
    "std::uint32_t nextOpaqueKey;"
    "static_assert(EFFECT_TABLE_RESTORE_CAPACITY == 1024)"
    "static_assert(EFFECT_TABLE_RESTORE_NAME_CAPACITY == 64)"
    "RUNTIME_SIZE(EffectTableSaveSnapshot::Record, 0x48, 0x50)"
    "RUNTIME_SIZE(EffectTableSaveSnapshot, 0x1200C, 0x14010)")
    require_contains(
        "${_save_source}"
        "${_workspace_marker}"
        "save snapshot must retain bounded native identities and Disk32 keys")
endforeach()

foreach(_forbidden_helper_call IN ITEMS
    "MemFile_"
    "Com_Error("
    "Com_Printf("
    "MyAssertHandler("
    "Sys_Error("
    "Z_Malloc("
    "Z_Free("
    "malloc("
    "free("
    "operator new"
    "std::vector"
    "std::string"
    "std::atomic")
    require_absent(
        "${_save_source}"
        "${_forbidden_helper_call}"
        "portable save helper must remain allocation- and report-free")
endforeach()

extract_slice(
    "${_save_source}"
    "EffectTableSaveStatus AppendEffectTableSaveDefinitionNoReport("
    "EffectTableSaveStatus ValidateEffectTableSaveSnapshotNoReport("
    _append_source
    "effect-table capture phase")
foreach(_append_marker IN ITEMS
    "snapshot->phase != EffectTableSaveSnapshot::Phase::Capturing"
    "nativeIdentity == 0"
    "snapshot->entryCount >= EFFECT_TABLE_RESTORE_CAPACITY"
    "BoundedNameLength(name, &nameLength)"
    "EffectTableSaveKeyPolicy::LegacyPointerBits"
    "nativeIdentity > static_cast<std::uintptr_t>("
    "EffectTableSaveKeyPolicy::OpaqueSequential"
    "snapshot->records[index].nativeIdentity == nativeIdentity"
    "diskKey.value = snapshot->nextOpaqueKey"
    "std::memcpy(record.name, name, nameLength + 1u)"
    "record.nativeIdentity = nativeIdentity"
    "record.diskKey = diskKey")
    require_contains(
        "${_append_source}"
        "${_append_marker}"
        "capture must map full native identities to bounded archive keys")
endforeach()
foreach(_forbidden_capture_call IN ITEMS
    "EffectTableRestoreNameIsValid("
    "callbacks.write("
    "strlen("
    "strcmp(")
    require_absent(
        "${_append_source}"
        "${_forbidden_capture_call}"
        "capture must defer deep validation and output")
endforeach()

extract_slice(
    "${_save_source}"
    "EffectTableSaveStatus ValidateEffectTableSaveSnapshotNoReport("
    "EffectTableSaveStatus WriteEffectTableSaveSnapshotNoReport("
    _validate_source
    "effect-table validation phase")
require_contains(
    "${_validate_source}"
    "EffectTableRestoreNameIsValid(record.name)"
    "captured names must use the archive-safe validator after enumeration")
require_contains(
    "${_validate_source}"
    "for (std::size_t previous = 0; previous < index; ++previous)"
    "every earlier key must participate in duplicate validation")
require_contains(
    "${_validate_source}"
    "other.diskKey.value == record.diskKey.value"
    "duplicate policy must be archive-key-based")
require_contains(
    "${_validate_source}"
    "other.nativeIdentity != record.nativeIdentity"
    "one archive key must never identify two native definitions")
require_contains(
    "${_validate_source}"
    "!RecordNamesEqual(other, record)"
    "only exact repeated identity/name records may share a key")

extract_slice(
    "${_save_source}"
    "bool FindEffectTableSaveDefinitionKey("
    "EffectTableSaveStatus WriteEffectTableSaveSnapshotNoReport("
    _membership_source
    "validated effect-table membership")
foreach(_membership_marker IN ITEMS
    "snapshot->status != EffectTableSaveStatus::Success"
    "snapshot->phase != EffectTableSaveSnapshot::Phase::Validated"
    "nativeIdentity == 0"
    "!outKey"
    "snapshot->records[index].nativeIdentity == nativeIdentity"
    "*outKey = snapshot->records[index].diskKey")
    require_contains(
        "${_membership_source}"
        "${_membership_marker}"
        "definition lookup must fail closed and return only bounded keys")
endforeach()
foreach(_forbidden_membership_operation IN ITEMS
    "callbacks.write("
    "record.name"
    "std::memcpy("
    "static_cast<std::uint32_t>(nativeIdentity)"
    "strlen("
    "strcmp(")
    require_absent(
        "${_membership_source}"
        "${_forbidden_membership_operation}"
        "definition membership must not emit bytes or inspect names")
endforeach()

extract_slice(
    "${_save_source}"
    "EffectTableSaveStatus WriteEffectTableSaveSnapshotNoReport("
    "std::size_t EffectTableSaveEntryCount("
    _write_source
    "effect-table serialization phase")
require_ordered(
    "${_write_source}"
    "status = WriteBytes(
            snapshot,
            stableCallbacks,
            record.name,
            nameLength + 1u);"
    "const std::uint8_t keyBytes[4]"
    "each copied name must precede its explicit key bytes")
foreach(_key_shift IN ITEMS ">> 8u" ">> 16u" ">> 24u")
    require_contains(
        "${_write_source}"
        "${_key_shift}"
        "effect keys must serialize explicitly little-endian")
endforeach()
require_contains(
    "${_write_source}"
    "record.diskKey.value"
    "serialization must emit the mapped archive key")
foreach(_forbidden_identity_write IN ITEMS
    "&record.nativeIdentity"
    "sizeof(record)"
    "&record,")
    require_absent(
        "${_write_source}"
        "${_forbidden_identity_write}"
        "native identities and record padding must never reach the writer")
endforeach()
require_contains(
    "${_write_source}"
    "const std::uint8_t terminator = 0;"
    "the legacy table must end with one empty-name sentinel")
require_ordered(
    "${_write_source}"
    "keyBytes,
            sizeof(keyBytes));"
    "const std::uint8_t terminator = 0;"
    "the empty-name sentinel must follow every record key")
extract_slice(
    "${_write_source}"
    "const std::uint8_t terminator = 0;"
    "snapshot->phase = EffectTableSaveSnapshot::Phase::Written;"
    _terminator_write
    "effect-table terminator write")
require_contains(
    "${_terminator_write}"
    "&terminator,
        sizeof(terminator))"
    "the final sentinel must be emitted through the checked writer")
require_ordered(
    "${_write_source}"
    "const EffectTableSaveCallbacks stableCallbacks = callbacks;"
    "snapshot->phase = EffectTableSaveSnapshot::Phase::Writing;"
    "callbacks must be copied before the reentrancy-visible write phase")

extract_slice(
    "${_archive_source}"
    "void __cdecl FX_CaptureEffectTableEntry_LoadObj("
    "fx::archive::EffectTableSaveStatus FX_CaptureEffectTableNoReport("
    _production_callbacks
    "production database enumeration callbacks")
foreach(_forbidden_production_callback IN ITEMS
    "MemFile_"
    "Com_"
    "Z_Malloc("
    "Z_Free("
    "ValidateEffectTableSaveSnapshotNoReport("
    "WriteEffectTableSaveSnapshotNoReport("
    "strlen("
    "strcmp(")
    require_absent(
        "${_production_callbacks}"
        "${_forbidden_production_callback}"
        "database callbacks must only capture bounded records")
endforeach()
require_contains(
    "${_production_callbacks}"
    "AppendEffectTableSaveDefinitionNoReport("
    "database callbacks must delegate to the bounded capture helper")
require_contains(
    "${_production_callbacks}"
    "reinterpret_cast<std::uintptr_t>(effectDef)"
    "production capture must preserve all native identity bits")
require_contains(
    "${_production_callbacks}"
    "capture->snapshot,
            name,
            nativeIdentity"
    "production capture must pass the full-width identity to the helper")
foreach(_forbidden_production_narrowing IN ITEMS
    "static_cast<std::uint32_t>"
    "reinterpret_cast<std::uint32_t>"
    "std::uint32_t key"
    "EffectDefinitionKey32")
    require_absent(
        "${_production_callbacks}"
        "${_forbidden_production_narrowing}"
        "production callbacks must not narrow definition identity early")
endforeach()

extract_slice(
    "${_archive_source}"
    "fx::archive::EffectTableSaveStatus FX_CaptureEffectTableNoReport("
    "bool FX_WriteEffectTableSaveBytes("
    _production_capture
    "production capture transaction")
require_contains(
    "${_production_capture}"
    "DB_EnumXAssets(
            ASSET_TYPE_FX,
            FX_CaptureEffectTableEntry_FastFile,
            &capture,
            false);"
    "fast-file capture must use the bounded callback and exclude overrides")
require_contains(
    "${_production_capture}"
    "FX_ForEachEffectDef(
            FX_CaptureEffectTableEntry_LoadObj,
            &capture);"
    "loose-file capture must use the bounded callback and preserve slot order")
require_ordered(
    "${_production_capture}"
    "DB_EnumXAssets("
    "ValidateEffectTableSaveSnapshotNoReport("
    "fast-file validation must happen only after database enumeration returns")
require_ordered(
    "${_production_capture}"
    "FX_ForEachEffectDef("
    "ValidateEffectTableSaveSnapshotNoReport("
    "validation must happen only after either enumerator returns")

extract_slice(
    "${_archive_source}"
    "bool FX_WriteEffectTableSaveBytes("
    "FxEffectTableSaveOutcome FX_StageEffectTableNoDrop("
    _production_writer
    "production effect-table writer adapter")
require_contains(
    "${_production_writer}"
    "FX_WriteArchiveDataNoDrop("
    "effect-table serialization must suppress MemoryFile overflow drops")
foreach(_forbidden_writer_boundary IN ITEMS
    "Com_Error("
    "Com_Printf("
    "Z_Malloc("
    "Z_Free(")
    require_absent(
        "${_production_writer}"
        "${_forbidden_writer_boundary}"
        "writer adapter must not directly report or own fallible storage")
endforeach()

extract_slice(
    "${_archive_source}"
    "FxEffectTableSaveOutcome FX_StageEffectTableNoDrop("
    "FxEffectTableSaveOutcome FX_WriteStagedEffectTableNoDrop("
    _production_stage
    "production save-table staging lifetime")
require_ordered(
    "${_production_stage}"
    "Z_Malloc("
    "ConstructEffectTableSaveSnapshot("
    "save workspace must be heap-backed and explicitly constructed")
require_ordered(
    "${_production_stage}"
    "ConstructEffectTableSaveSnapshot("
    "FX_CaptureEffectTableNoReport(snapshot)"
    "capture must begin only after construction")
require_contains(
    "${_production_stage}"
    "EffectTableSaveKeyPolicy::LegacyPointerBits"
    "x86 staging must retain exact legacy pointer-bit keys")
require_contains(
    "${_production_stage}"
    "EffectTableSaveKeyPolicy::OpaqueSequential"
    "native64 staging must select address-independent keys")
require_ordered(
    "${_production_stage}"
    "FX_CaptureEffectTableNoReport(snapshot)"
    "return FxEffectTableSaveOutcome::Success;"
    "a fully validated table must remain staged for graph admission")
require_absent(
    "${_production_stage}"
    "WriteEffectTableSaveSnapshotNoReport("
    "table staging must not emit archive bytes")
require_absent(
    "${_production_stage}"
    "MemFile_"
    "table staging must not mutate a MemoryFile")
require_absent(
    "${_production_stage}"
    "Com_Error("
    "ERR_DROP reporting must remain outside the owned snapshot lifetime")

extract_slice(
    "${_archive_source}"
    "FxEffectTableSaveOutcome FX_WriteStagedEffectTableNoDrop("
    "bool FX_ValidateEffectTableRestoreLifecycle("
    _production_staged_write
    "production staged table write")
require_contains(
    "${_production_staged_write}"
    "WriteEffectTableSaveSnapshotNoReport("
    "only the explicit staged-write phase may emit the table")
foreach(_forbidden_staged_write_operation IN ITEMS
    "Z_Malloc("
    "Z_Free("
    "Com_Error("
    "FX_CaptureEffectTableNoReport(")
    require_absent(
        "${_production_staged_write}"
        "${_forbidden_staged_write_operation}"
        "staged table output must not recapture, allocate, free, or report")
endforeach()

extract_slice(
    "${_archive_source}"
    "void FX_DestroyEffectTableSaveStaging("
    "void __cdecl FX_CaptureEffectTableEntry_LoadObj("
    _production_cleanup
    "production save-table cleanup")
require_ordered(
    "${_production_cleanup}"
    "DestroyEffectTableSaveSnapshot("
    "std::abort();"
    "destruction failure must be handled before storage release")
require_ordered(
    "${_production_cleanup}"
    "std::abort();"
    "Z_Free(staging->storage, 10);"
    "storage release must be unreachable after destruction failure")

extract_slice(
    "${_archive_source}"
    "bool FX_ValidateArchiveEffectDefinitionReferences("
    "bool FX_ValidateArchiveCopiedSnapshot("
    _production_graph_membership
    "copied graph definition admission")
require_contains(
    "${_production_graph_membership}"
    "FindEffectTableSaveDefinitionKey("
    "copied definitions must resolve through the staged table")
require_contains(
    "${_production_graph_membership}"
    "EffectDefinitionKey32 diskKey{};"
    "definition admission must produce a bounded key for Disk32 encoding")
require_contains(
    "${_production_graph_membership}"
    "reinterpret_cast<std::uintptr_t>(effect->def)"
    "definition admission must compare pointer bits without dereference")
foreach(_forbidden_definition_dereference IN ITEMS
    "effect->def->"
    "effect->def."
    "FX_GetArchiveEffectDefCount("
    "FX_ValidateArchiveEffectDefTiming(")
    require_absent(
        "${_production_graph_membership}"
        "${_forbidden_definition_dereference}"
        "unadmitted definition pointers must never be dereferenced")
endforeach()

extract_slice(
    "${_archive_source}"
    "bool FX_ValidateArchiveCopiedSnapshot("
    "// The archive restore transaction owns"
    _production_copied_snapshot
    "copied snapshot validation")
require_ordered(
    "${_production_copied_snapshot}"
    "FX_ValidateArchiveEffectDefinitionReferences("
    "FX_RebuildArchivePoolAllocationStates("
    "definition admission must precede copied graph traversal")
require_ordered(
    "${_production_copied_snapshot}"
    "FX_ValidateArchiveEffectDefinitionReferences("
    "FX_CollectArchivePhysicsEntries("
    "definition admission must precede definition field traversal")

extract_slice(
    "${_archive_source}"
    "void __cdecl FX_Save("
    "void __cdecl FX_Archive("
    _production_save
    "production FX save transaction")
require_ordered(
    "${_production_save}"
    "FX_StageEffectTableNoDrop(&effectTableStaging)"
    "FX_BeginArchive(system)"
    "effect-table capture must complete before graph exclusion")
require_ordered(
    "${_production_save}"
    "FX_ValidateArchiveCopiedSnapshot("
    "FX_EndArchive(system)"
    "copied graph admission must finish before archive release")
require_ordered(
    "${_production_save}"
    "FX_EndArchive(system)"
    "FX_WriteStagedEffectTableNoDrop(&effectTableStaging, memFile)"
    "archive ownership must be released before any staged output")
require_ordered(
    "${_production_save}"
    "FX_WriteStagedEffectTableNoDrop(&effectTableStaging, memFile)"
    "FX_ARCHIVE_SYSTEM_SIZE, &systemSnapshot"
    "legacy table bytes must still precede the system snapshot")

require_ordered(
    "${_archive_source}"
    "FX archive save requires Disk32 conversion on this target"
    "FX_StageEffectTableNoDrop(&effectTableStaging)"
    "unsupported native targets must fail before effect-table capture")
require_absent(
    "${_system_header}"
    "FX_SaveEffectDefTable"
    "obsolete public save-table entry points must not return")
require_contains(
    "${_common_files}"
    "EffectsCore/fx_effect_table_save.cpp"
    "production builds must compile the bounded save helper")
require_contains(
    "${_common_files}"
    "EffectsCore/fx_effect_table_save.h"
    "production manifests must expose the bounded save contract")

message(STATUS "FX effect-table save source invariants passed")
