set(SRC_DIR "${SOURCE_ROOT}/src")
set(DEPS_DIR "${SOURCE_ROOT}/deps")
set(SCRIPTS_DIR "${SOURCE_ROOT}/scripts")
set(KISAK_DEDI_HEADLESS ON)

include("${SCRIPTS_DIR}/common_files.cmake")
include("${SCRIPTS_DIR}/mp/mp_files.cmake")
include("${SCRIPTS_DIR}/dedi/dedi_files.cmake")
include("${SCRIPTS_DIR}/dedi/dedi_sources.cmake")

kisakcod_get_dedi_sources(_dedi_sources)

set(_debt_include_regex
    "^[ \t]*#[ \t]*include[ \t]*[<\\\"](\\.\\./)*(client|client_mp|cgame|cgame_mp|gfx_d3d|sound|ui|ui_mp|EffectsCore|DynEntity|devgui|groupvoice|bink|mss)(/|[>\\\"])"
)

set(_found)
foreach(_source ${_dedi_sources})
    if (IS_DIRECTORY "${_source}" OR NOT EXISTS "${_source}")
        continue()
    endif()

    file(STRINGS "${_source}" _lines)
    set(_preproc_depth 0)
    set(_skip_until_depth -1)
    foreach(_line ${_lines})
        string(STRIP "${_line}" _stripped)

        if (_stripped MATCHES "^#[ \t]*ifndef[ \t]+KISAK_DEDI_HEADLESS([ \t]*($|//|/\\*)|$)")
            math(EXPR _preproc_depth "${_preproc_depth} + 1")
            set(_skip_until_depth ${_preproc_depth})
            continue()
        endif()

        if (_stripped MATCHES "^#[ \t]*(if|ifdef|ifndef)([ \t(]|$)")
            math(EXPR _preproc_depth "${_preproc_depth} + 1")
            continue()
        endif()

        if (_skip_until_depth GREATER -1
            AND _preproc_depth EQUAL _skip_until_depth
            AND _stripped MATCHES "^#[ \t]*(else|elif)([ \t(]|$)")
            set(_skip_until_depth -1)
            continue()
        endif()

        if (_stripped MATCHES "^#[ \t]*endif([ \t]*($|//|/\\*)|$)")
            if (_skip_until_depth GREATER -1
                AND _preproc_depth EQUAL _skip_until_depth)
                set(_skip_until_depth -1)
            endif()
            math(EXPR _preproc_depth "${_preproc_depth} - 1")
            continue()
        endif()

        if (_skip_until_depth GREATER -1)
            continue()
        endif()

        if (NOT _line MATCHES "${_debt_include_regex}")
            continue()
        endif()

        file(RELATIVE_PATH _rel "${SRC_DIR}" "${_source}")
        string(STRIP "${_line}" _include)
        list(APPEND _found "${_rel}|${_include}")
    endforeach()
endforeach()
list(SORT _found)

set(_allowed)
file(STRINGS "${ALLOWLIST}" _allowlist_lines)
foreach(_line ${_allowlist_lines})
    string(STRIP "${_line}" _line)
    if (_line STREQUAL "" OR _line MATCHES "^#")
        continue()
    endif()
    list(APPEND _allowed "${_line}")
endforeach()
list(SORT _allowed)

set(_new_debt)
foreach(_entry ${_found})
    if (NOT _entry IN_LIST _allowed)
        list(APPEND _new_debt "${_entry}")
    endif()
endforeach()

set(_stale_debt)
foreach(_entry ${_allowed})
    if (NOT _entry IN_LIST _found)
        list(APPEND _stale_debt "${_entry}")
    endif()
endforeach()

if (_new_debt OR _stale_debt)
    if (_new_debt)
        message("New headless client/media include debt:")
        foreach(_entry ${_new_debt})
            message("  ${_entry}")
        endforeach()
    endif()
    if (_stale_debt)
        message("Stale headless client/media include debt allowlist entries:")
        foreach(_entry ${_stale_debt})
            message("  ${_entry}")
        endforeach()
    endif()
    message(FATAL_ERROR "Update or burn down tests/headless_include_debt.allow")
endif()

list(LENGTH _found _debt_count)
message(STATUS "Tracked ${_debt_count} direct client/media includes in the headless dedicated source profile")
