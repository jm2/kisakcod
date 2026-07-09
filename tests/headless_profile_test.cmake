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
)
    if (NOT _required IN_LIST _dedi_sources)
        message(FATAL_ERROR "Headless dedicated source profile is missing ${_required}")
    endif()
endforeach()

foreach(_excluded
    "${SRC_DIR}/win32/win_input.cpp"
    "${SRC_DIR}/win32/win_input.h"
    "${SRC_DIR}/win32/win_wndproc.cpp"
    "${SRC_DIR}/win32/win_voice.cpp"
)
    if (_excluded IN_LIST _dedi_sources)
        message(FATAL_ERROR "Headless dedicated source profile still contains client-only Win32 source ${_excluded}")
    endif()
endforeach()
