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
