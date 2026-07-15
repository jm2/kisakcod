function(kisakcod_enable_fx_archive_stack_measurement target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR
            "FX archive stack measurement target does not exist: ${target}")
    endif()
    if(NOT MSVC)
        message(FATAL_ERROR
            "KISAK_MEASURE_FX_ARCHIVE_STACK requires the MSVC compiler")
    endif()

    set(_kisak_fx_archive_source
        "${SRC_DIR}/EffectsCore/fx_archive.cpp")
    set(_kisak_fx_archive_ruleset
        "${CMAKE_SOURCE_DIR}/tests/fx_archive_stack.ruleset")
    set(_kisak_fx_archive_log
        "${CMAKE_CURRENT_BINARY_DIR}/fx_archive_stack_$<CONFIG>.xml")

    if(NOT EXISTS "${_kisak_fx_archive_ruleset}")
        message(FATAL_ERROR
            "FX archive stack ruleset is missing: ${_kisak_fx_archive_ruleset}")
    endif()

    # A one-byte reporting threshold makes every function frame visible, so a
    # missing FX_Save/FX_Restore measurement cannot be mistaken for a small
    # frame. This source property is directory-scoped: scripts/mp owns only the
    # one multiplayer target and therefore produces one log per configuration.
    # Analysis warnings remain warnings here so the parser can enforce distinct
    # Save, Restore, other-function, and absolute ceilings from the XML report.
    set_property(
        SOURCE "${_kisak_fx_archive_source}"
        APPEND PROPERTY COMPILE_OPTIONS
            /analyze
            /analyze:WX-
            /analyze:log:format:xml
            /analyze:stacksize
            1
            "/analyze:ruleset${_kisak_fx_archive_ruleset}"
            /analyze:log
            "${_kisak_fx_archive_log}")
endfunction()
