cmake_minimum_required(VERSION 3.16)

function(require_source_contains RELATIVE_PATH NEEDLE DESCRIPTION)
    file(READ "${SOURCE_ROOT}/src/${RELATIVE_PATH}" _source)
    string(FIND "${_source}" "${NEEDLE}" _position)
    if (_position EQUAL -1)
        message(FATAL_ERROR "Missing security invariant (${DESCRIPTION}) in src/${RELATIVE_PATH}")
    endif()
endfunction()

require_source_contains(
    "qcommon/files.cpp"
    "ARRAY_COUNT(fs_serverReferencedFFCheckSums)"
    "fast-file reference capacity must be derived from the destination array")
require_source_contains(
    "qcommon/files.cpp"
    "strlen(pak) >= sizeof(szFile)"
    "server pak names must be bounded before copying")
require_source_contains(
    "qcommon/files.cpp"
    "strlen(iwd) >= sizeof(szFile)"
    "IWD names must be bounded before copying")
require_source_contains(
    "database/db_stream_load.cpp"
    "CheckedArrayBytes(count, stride, &byteCount)"
    "generated fast-file arrays must use checked count multiplication")
require_source_contains(
    "database/db_stream_load.cpp"
    "DB_ResolveInsertedPointer"
    "fast-file aliases must resolve through registered provenance")
require_source_contains(
    "database/db_stream_load.cpp"
    "DB_ResolveOffsetBytes"
    "migrated direct offsets must resolve through materialized-range provenance")
require_source_contains(
    "database/db_stream_load.cpp"
    "requiredBytes ? requiredBytes : 1"
    "non-null zero-count direct offsets must still reference materialized storage")
require_source_contains(
    "database/db_stream_load.cpp"
    "DB_ResolveOffsetCString"
    "direct fast-file strings must scan only bounded materialized storage")
require_source_contains(
    "database/db_stream_load.cpp"
    "DB_RegisterStreamCString"
    "inline fast-file strings must register exact start and extent provenance")
require_source_contains(
    "database/db_stream_load.cpp"
    "SL_GetStringOfSize"
    "direct temporary strings must be interned with their validated extent")
require_source_contains(
    "database/db_validation.h"
    "kMaxInternedStringBytes = 65531"
    "temporary strings must remain below the script-memory allocation ceiling")
require_source_contains(
    "database/db_stream_load.cpp"
    "db::validation::CanInternString(byteCount)"
    "inline and direct temporary strings must enforce the allocation ceiling")
require_source_contains(
    "database/db_load.cpp"
    "varMaterial->constantCount,
            \"material constants\")"
    "material constants must reject missing nonempty spans")
require_source_contains(
    "database/db_load.cpp"
    "varMaterial->stateBitsCount,
            \"material state bits\")"
    "material state bits must reject missing nonempty spans")
require_source_contains(
    "database/db_load.cpp"
    "varGfxAabbTree->smodelIndexCount,
            \"world AABB static-model indices\")"
    "world AABB indices must reject missing nonempty spans")
require_source_contains(
    "database/db_load.cpp"
    "varGfxWorld->planeCount,
            \"world planes\")"
    "world planes must reject missing nonempty spans")
require_source_contains(
    "database/db_load.cpp"
    "db::validation::AllU16Below"
    "world AABB static-model indices must be bounded before runtime use")
require_source_contains(
    "database/db_load.cpp"
    "db::validation::CountInRange(varFont->glyphCount, 96, 65536)"
    "font glyph tables must cover direct ASCII indexing without oversized counts")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varMaterialArgumentDef->literalConst,
                16,
                4,
                kDirectBlock4"
    "literal material constants must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varMaterial->constantTable,
                constantByteCount,
                16,
                kDirectBlock4"
    "material constant tables must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varMaterial->stateBitsTable,
                stateBitsByteCount,
                4,
                kDirectBlock4"
    "material state-bit tables must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varGfxAabbTree->smodelIndexes,
                smodelIndexByteCount,
                2,
                kDirectBlock4"
    "world AABB indices must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varGfxWorldDpvsPlanes->planes,
                planeByteCount,
                4,
                kDirectBlock4"
    "world planes must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varFont->glyphs,
                glyphByteCount,
                4,
                kDirectBlock4"
    "font glyphs must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "DBAliasKind::XStringPointerSlot"
    "direct string-holder references must use completed-object provenance")
require_source_contains(
    "database/db_stream.cpp"
    "DB_ValidateStreamCString"
    "completed string holders must validate registered pointee provenance")
require_source_contains(
    "database/db_relocation.cpp"
    "resolvedAddress != aliasBlock.base + record.offset"
    "completed string holders must publish their exact registered slot")
require_source_contains(
    "database/db_file_load.cpp"
    "DB_MarkStreamRangeMaterialized(pos, size)"
    "successful fast-file output must be recorded as materialized")
require_source_contains(
    "database/db_file_load.cpp"
    "InterlockedExchange(&g_fileReadBytes, static_cast<LONG>(dwNumberOfBytesTransfered))"
    "asynchronous fast-file reads must preserve the actual completion byte count")
require_source_contains(
    "database/db_file_load.cpp"
    "initialReadSize < 12"
    "fast-file magic and version reads must reject truncated headers")
require_source_contains(
    "database/db_file_load.cpp"
    "db::validation::CanAppendBytes"
    "fast-file ring-buffer accounting must reject invalid accumulated input")
require_source_contains(
    "database/db_stringtable_load.cpp"
    "*var >= static_cast<uint32_t>(varXAssetList->stringList.count)"
    "script-string tokens must be range-checked before indexing")
require_source_contains(
    "qcommon/com_bsp_load_obj.cpp"
    "comBspGlob.fileSize > INT32_MAX"
    "BSP allocation sizes must fit the signed zone allocator")
require_source_contains(
    "universal/physicalmemory.cpp"
    "PMem_TryAlloc"
    "zone allocations must report checked failure before generic fatal PMem OOM handling")

file(STRINGS
    "${SOURCE_ROOT}/src/database/db_load.cpp"
    _raw_array_loads
    REGEX "Load_Stream\\(atStreamStart,.*([0-9]+[ \t]*\\*[ \t]*count|count[ \t]*\\*[ \t]*[0-9]+)\\)")
if (_raw_array_loads)
    message(FATAL_ERROR
        "Unchecked generated fast-file array loads remain in db_load.cpp; use Load_StreamArray")
endif()

file(READ "${SOURCE_ROOT}/src/database/db_load.cpp" _db_load_source)
file(STRINGS
    "${SOURCE_ROOT}/src/database/db_load.cpp"
    _legacy_direct_offsets
    REGEX "DB_ConvertOffsetToPointerLegacy")
list(LENGTH _legacy_direct_offsets _legacy_direct_offset_count)
if (NOT _legacy_direct_offset_count EQUAL 32)
    message(FATAL_ERROR
        "Expected exactly 32 explicitly legacy direct fast-file offsets; found ${_legacy_direct_offset_count}. "
        "Migrations must update this debt gate.")
endif()
string(REGEX MATCH
    "(\\*inserted[ \\t]*=|inserted->)"
    _raw_alias_slot_write
    "${_db_load_source}")
if (_raw_alias_slot_write)
    message(FATAL_ERROR
        "Native pointer write to four-byte fast-file alias slot remains: ${_raw_alias_slot_write}")
endif()
string(REPLACE "->" "." _db_load_count_source "${_db_load_source}")
string(REGEX MATCH
    "Load_[A-Za-z0-9_]+Array[ \\t\\r\\n]*\\([ \\t\\r\\n]*1[ \\t\\r\\n]*,[^;]*(\\+|-|\\*|/|%|<<|>>|&|\\||\\^)[^;]*\\);"
    _raw_derived_count
    "${_db_load_count_source}")
if (_raw_derived_count)
    message(FATAL_ERROR
        "Unchecked derived fast-file array count remains in db_load.cpp: ${_raw_derived_count}")
endif()

set(_format_sensitive_sources
    "cgame/cg_hudelem.cpp"
    "cgame/cg_info.cpp"
    "client_mp/cl_cgame_mp.cpp"
    "client_mp/cl_main_mp.cpp"
    "client_mp/cl_parse_mp.cpp"
    "game/g_main.cpp"
    "game_mp/g_main_mp.cpp"
    "game_mp/g_scr_main_mp.cpp"
    "qcommon/com_bsp_load_obj.cpp"
    "server_mp/sv_client_mp.cpp"
    "win32/win_steam.cpp"
)

# These files contain network/content-facing diagnostics. Reject the dangerous
# two-pass pattern where preformatted or external text is supplied as the format
# argument with no variadic values. This source tripwire complements compiler
# format warnings, which cannot see through va() or decompiled temporary variables.
set(_bare_format_regex
    "Com_(Error|Printf|PrintError|PrintWarning|DPrintf)[ \t]*\\([^,\r\n]+,[ \t]*(va[ \t]*\\([^\r\n]*\\)|[A-Za-z_][A-Za-z0-9_]*)[ \t]*\\)[ \t]*;")

foreach(_relative_path IN LISTS _format_sensitive_sources)
    set(_path "${SOURCE_ROOT}/src/${_relative_path}")
    file(STRINGS "${_path}" _matches REGEX "${_bare_format_regex}")
    foreach(_line IN LISTS _matches)
        string(STRIP "${_line}" _stripped)
        if (NOT _stripped MATCHES "^//")
            message(FATAL_ERROR
                "Dynamic diagnostic format in src/${_relative_path}: ${_stripped}\n"
                "Pass a literal format and external text as a %s argument.")
        endif()
    endforeach()
endforeach()

message(STATUS "Security source invariants verified")
