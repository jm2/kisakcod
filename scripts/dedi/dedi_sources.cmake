function(kisakcod_get_dedi_sources OUT_VAR)
    kisakcod_require_platform_source_sets()

    if (KISAK_DEDI_HEADLESS)
        set(_platform_sources ${KISAK_PLATFORM_DEDI_HEADLESS_SOURCES})
    else()
        set(_platform_sources ${KISAK_PLATFORM_SOURCES})
    endif()

    set(_sources
        ${SRCROOT}
        ${BGAME}
        ${COMMON}
        ${DATABASE}
        ${GAME}
        ${GAME_MP}
        ${ODE}
        ${PHYSICS}
        ${PHYSICS_ODE}
        ${QCOMMON}
        ${SCRIPT}
        ${SERVER}
        ${SERVER_MP}
        ${SPEEX}
        ${STRINGED}
        ${UNIVERSAL}
        ${_platform_sources}
        ${KISAK_PLATFORM_SERVICE_SOURCES}
        ${XANIM}
        ${ZLIB}
        ${STEAM_HEADERS}
    )

    if (KISAK_DEDI_HEADLESS)
        list(APPEND _sources
            "${SRC_DIR}/cgame_mp/dedicated_cgame.cpp"
            # The exact-key table authenticates this component even before
            # production enrollment.  Headless excludes EffectsCore, so link
            # its database-neutral implementation to the fail-closed bridge.
            "${SRC_DIR}/database/db_zone_runtime_storage.cpp"
            "${SRC_DIR}/database/db_zone_runtime_storage_fx_bridge_headless.cpp"
        )
    else()
        list(APPEND _sources
            ${DYNENTITY}
            ${EFFECTSCORE}
            ${AIM_ASSIST}
            ${BINKLIB}
            ${CGAME}
            ${CGAME_MP}
            ${CLIENT}
            ${CLIENT_MP}
            ${DEVGUI}
            ${GFX_D3D}
            ${GROUPVOICE}
            ${GROUPVOICE_SPEEX}
            ${MSSLIB}
            ${RAGDOLL}
            ${SOUND}
            ${UI}
            ${UI_MP}
        )
    endif()

    set(${OUT_VAR} ${_sources} PARENT_SCOPE)
endfunction()

function(kisakcod_assert_headless_dedi_sources)
    if (NOT KISAK_DEDI_HEADLESS)
        return()
    endif()

    foreach(_source ${ARGN})
        file(RELATIVE_PATH _rel "${SRC_DIR}" "${_source}")
        if (_rel MATCHES "^(client|client_mp|cgame|gfx_d3d|sound|ui|ui_mp|EffectsCore|aim_assist|groupvoice|devgui|DynEntity)/")
            message(FATAL_ERROR "KISAK_DEDI_HEADLESS source list contains client/media source: ${_rel}")
        endif()
        if (_rel MATCHES "^\\.\\./deps/(binklib|msslib)/")
            message(FATAL_ERROR "KISAK_DEDI_HEADLESS source list contains proprietary media dependency: ${_rel}")
        endif()
    endforeach()
endfunction()
