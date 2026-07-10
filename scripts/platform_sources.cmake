include_guard(GLOBAL)

# Publish the source sets owned by one platform backend under target-agnostic
# names.  Backends pass variable names (rather than expanded lists) so an empty
# source set is an explicit, testable state instead of an accidental fallback.
function(kisakcod_select_platform_source_sets)
    cmake_parse_arguments(
        _KISAK_PLATFORM
        ""
        "PLATFORM;ENGINE_VAR;DEDI_HEADLESS_VAR;SERVICES_VAR;COMPLETE"
        ""
        ${ARGN}
    )

    foreach(_argument PLATFORM ENGINE_VAR DEDI_HEADLESS_VAR SERVICES_VAR COMPLETE)
        if (NOT DEFINED _KISAK_PLATFORM_${_argument}
            OR "${_KISAK_PLATFORM_${_argument}}" STREQUAL "")
            message(FATAL_ERROR
                "kisakcod_select_platform_source_sets requires ${_argument}")
        endif()
    endforeach()

    if (NOT DEFINED KISAK_PLATFORM)
        message(FATAL_ERROR "KISAK_PLATFORM must be selected before its source sets")
    endif()
    if (NOT "${KISAK_PLATFORM}" STREQUAL "${_KISAK_PLATFORM_PLATFORM}")
        message(FATAL_ERROR
            "Platform source file '${_KISAK_PLATFORM_PLATFORM}' was included while "
            "KISAK_PLATFORM='${KISAK_PLATFORM}'")
    endif()

    foreach(_source_variable
        "${_KISAK_PLATFORM_ENGINE_VAR}"
        "${_KISAK_PLATFORM_DEDI_HEADLESS_VAR}"
        "${_KISAK_PLATFORM_SERVICES_VAR}"
    )
        if (NOT DEFINED ${_source_variable})
            message(FATAL_ERROR
                "Platform '${KISAK_PLATFORM}' did not define source set ${_source_variable}")
        endif()
    endforeach()

    set(_engine_sources ${${_KISAK_PLATFORM_ENGINE_VAR}})
    set(_headless_sources ${${_KISAK_PLATFORM_DEDI_HEADLESS_VAR}})
    set(_service_sources ${${_KISAK_PLATFORM_SERVICES_VAR}})

    # An incomplete POSIX backend must never acquire Win32 implementations just
    # because an override file has not landed yet.
    if (NOT KISAK_PLATFORM STREQUAL "win32")
        foreach(_source ${_engine_sources} ${_headless_sources} ${_service_sources})
            if (_source MATCHES "[/\\\\]win32[/\\\\]")
                message(FATAL_ERROR
                    "Platform '${KISAK_PLATFORM}' source set contains Win32 source: ${_source}")
            endif()
        endforeach()
    endif()

    set(KISAK_SELECTED_PLATFORM "${KISAK_PLATFORM}" PARENT_SCOPE)
    set(KISAK_PLATFORM_SOURCES "${_engine_sources}" PARENT_SCOPE)
    set(KISAK_PLATFORM_DEDI_HEADLESS_SOURCES "${_headless_sources}" PARENT_SCOPE)
    set(KISAK_PLATFORM_SERVICE_SOURCES "${_service_sources}" PARENT_SCOPE)
    set(KISAK_PLATFORM_SOURCE_SETS_COMPLETE
        "${_KISAK_PLATFORM_COMPLETE}" PARENT_SCOPE)
endfunction()

function(kisakcod_require_platform_source_sets)
    foreach(_variable
        KISAK_SELECTED_PLATFORM
        KISAK_PLATFORM_SOURCES
        KISAK_PLATFORM_DEDI_HEADLESS_SOURCES
        KISAK_PLATFORM_SERVICE_SOURCES
        KISAK_PLATFORM_SOURCE_SETS_COMPLETE
    )
        if (NOT DEFINED ${_variable})
            message(FATAL_ERROR
                "Platform source sets are not selected; include the active platform.cmake first")
        endif()
    endforeach()

    if (NOT "${KISAK_SELECTED_PLATFORM}" STREQUAL "${KISAK_PLATFORM}")
        message(FATAL_ERROR
            "Selected source sets belong to '${KISAK_SELECTED_PLATFORM}', not '${KISAK_PLATFORM}'")
    endif()
endfunction()
