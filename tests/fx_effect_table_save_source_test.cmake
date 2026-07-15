cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

set(_save_header_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_effect_table_save.h")
set(_save_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_effect_table_save.cpp")
set(_archive_source_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive.cpp")
set(_system_header_path
    "${SOURCE_ROOT}/src/EffectsCore/fx_system.h")
set(_common_files_path
    "${SOURCE_ROOT}/scripts/common_files.cmake")

foreach(_required_path IN ITEMS
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
    "AppendEffectTableSaveEntryNoReport("
    "ValidateEffectTableSaveSnapshotNoReport("
    "WriteEffectTableSaveSnapshotNoReport("
    "EffectTableSaveEntryCount(")
    require_contains(
        "${_save_header}"
        "${_api_marker}"
        "save helper must retain its opaque bounded API")
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
    "std::uint32_t key;"
    "static_assert(EFFECT_TABLE_RESTORE_CAPACITY == 1024)"
    "static_assert(EFFECT_TABLE_RESTORE_NAME_CAPACITY == 64)"
    "RUNTIME_SIZE(EffectTableSaveSnapshot::Record, 0x44, 0x44)"
    "RUNTIME_SIZE(EffectTableSaveSnapshot, 0x11008, 0x11010)")
    require_contains(
        "${_save_source}"
        "${_workspace_marker}"
        "save snapshot must retain its fixed Disk32 contract")
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
    "EffectTableSaveStatus AppendEffectTableSaveEntryNoReport("
    "EffectTableSaveStatus ValidateEffectTableSaveSnapshotNoReport("
    _append_source
    "effect-table capture phase")
foreach(_append_marker IN ITEMS
    "snapshot->phase != EffectTableSaveSnapshot::Phase::Capturing"
    "key == 0"
    "key > static_cast<std::uintptr_t>("
    "snapshot->entryCount >= EFFECT_TABLE_RESTORE_CAPACITY"
    "BoundedNameLength(name, &nameLength)"
    "std::memcpy(record.name, name, nameLength + 1u)"
    "record.key = static_cast<std::uint32_t>(key)")
    require_contains(
        "${_append_source}"
        "${_append_marker}"
        "capture callback must only perform bounded copy and narrowing")
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
    "other.key == record.key"
    "duplicate policy must be key-based")
require_contains(
    "${_validate_source}"
    "!RecordNamesEqual(other, record)"
    "only conflicting same-key names must fail")

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
    "AppendEffectTableSaveEntryNoReport("
    "database callbacks must delegate to the bounded capture helper")
require_contains(
    "${_production_callbacks}"
    "reinterpret_cast<std::uintptr_t>(effectDef)"
    "production capture must preserve all pointer bits for checked narrowing")
require_contains(
    "${_production_callbacks}"
    "capture->snapshot,
        name,
        key"
    "production capture must pass the full-width key to the helper")
foreach(_forbidden_production_narrowing IN ITEMS
    "static_cast<std::uint32_t>"
    "reinterpret_cast<std::uint32_t>"
    "std::uint32_t key")
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
    "FxEffectTableSaveOutcome FX_SaveEffectTableNoDrop("
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
    "FxEffectTableSaveOutcome FX_SaveEffectTableNoDrop("
    "bool FX_ValidateEffectTableRestoreLifecycle("
    _production_save
    "production save-table lifetime")
require_ordered(
    "${_production_save}"
    "Z_Malloc("
    "ConstructEffectTableSaveSnapshot("
    "save workspace must be heap-backed and explicitly constructed")
require_ordered(
    "${_production_save}"
    "ConstructEffectTableSaveSnapshot("
    "FX_CaptureEffectTableNoReport(snapshot)"
    "capture must begin only after construction")
require_ordered(
    "${_production_save}"
    "FX_CaptureEffectTableNoReport(snapshot)"
    "WriteEffectTableSaveSnapshotNoReport("
    "the complete table must capture and validate before the first write")
require_ordered(
    "${_production_save}"
    "WriteEffectTableSaveSnapshotNoReport("
    "DestroyEffectTableSaveSnapshot(snapshot)"
    "the write must finish before snapshot destruction")
extract_slice(
    "${_production_save}"
    "const bool destroyed ="
    "if (!destroyed)"
    _production_cleanup
    "production save-table cleanup")
require_ordered(
    "${_production_cleanup}"
    "DestroyEffectTableSaveSnapshot(snapshot)"
    "Z_Free(storage, 10);"
    "destruction must precede storage release")
require_absent(
    "${_production_save}"
    "Com_Error("
    "ERR_DROP reporting must remain outside the owned snapshot lifetime")

require_ordered(
    "${_archive_source}"
    "FX archive save requires Disk32 conversion on this target"
    "FX_SaveEffectTableNoDrop(memFile);"
    "unsupported native targets must fail before effect-table capture/write")
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
