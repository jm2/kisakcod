cmake_minimum_required(VERSION 3.16)

set(SRC_DIR "${SOURCE_ROOT}/src")
set(DEPS_DIR "${SOURCE_ROOT}/deps")
set(SCRIPTS_DIR "${SOURCE_ROOT}/scripts")
set(KISAK_DEDI_HEADLESS ON)

include("${SCRIPTS_DIR}/common_files.cmake")
include("${SCRIPTS_DIR}/mp/mp_files.cmake")
include("${SCRIPTS_DIR}/dedi/dedi_files.cmake")
include("${SCRIPTS_DIR}/dedi/dedi_sources.cmake")

kisakcod_get_dedi_sources(_dedi_sources)
kisakcod_assert_headless_dedi_sources(${_dedi_sources})

list(LENGTH _dedi_sources _source_count)
if (_source_count LESS 1)
    message(FATAL_ERROR "Headless dedicated source profile is empty")
endif()

foreach(_required
    "${SRC_DIR}/server_mp/sv_main_mp.cpp"
    "${SRC_DIR}/server_mp/sv_client_mp.cpp"
    "${SRC_DIR}/qcommon/msg_mp.cpp"
    "${SRC_DIR}/game_mp/g_main_mp.cpp"
    "${SRC_DIR}/cgame_mp/dedicated_cgame.cpp"
    "${SRC_DIR}/script/scr_debugger.cpp"
)
    if (NOT _required IN_LIST _dedi_sources)
        message(FATAL_ERROR "Headless dedicated source profile is missing ${_required}")
    endif()
endforeach()

file(READ "${SRC_DIR}/script/scr_debugger.cpp" _debugger_source)
string(FIND "${_debugger_source}" "#ifdef KISAK_DEDI_HEADLESS" _headless_branch)
string(FIND "${_debugger_source}" "#else" _legacy_branch)
string(FIND "${_debugger_source}" "scrDebuggerGlob_t scrDebuggerGlob;" _legacy_global)
string(FIND "${_debugger_source}" "Script debugger is unavailable in KISAK_DEDI_HEADLESS builds." _disabled_message)
string(FIND "${_debugger_source}" "NET_RestartDebug();" _remote_shutdown)
string(FIND "${_debugger_source}" "return OP_NOP;" _breakpoint_noop)

if (_headless_branch EQUAL -1
    OR _legacy_branch EQUAL -1
    OR _legacy_global EQUAL -1
    OR NOT _headless_branch LESS _legacy_branch
    OR NOT _legacy_branch LESS _legacy_global)
    message(FATAL_ERROR
        "Headless debugger backend must be selected before the legacy UI-backed debugger global")
endif()

if (_disabled_message EQUAL -1 OR _remote_shutdown EQUAL -1 OR _breakpoint_noop EQUAL -1)
    message(FATAL_ERROR
        "Headless debugger backend must explicitly reject UI/remote debugging and neutralize debugger opcodes")
endif()

foreach(_excluded
    "${SRC_DIR}/win32/win_input.cpp"
    "${SRC_DIR}/win32/win_input.h"
    "${SRC_DIR}/win32/win_wndproc.cpp"
    "${SRC_DIR}/win32/win_voice.cpp"
    "${SRC_DIR}/win32/win_storage.cpp"
)
    if (_excluded IN_LIST _dedi_sources)
        message(FATAL_ERROR "Headless dedicated source profile still contains client-only Win32 source ${_excluded}")
    endif()
endforeach()
