# Resolve the immutable source identity for this tree.
#
# A binary has to record which source revision produced it. In a git checkout
# that is the checked-out commit. A source archive produced by `git archive`
# carries no `.git`, so the identity travels inside the archive: the committed
# src/source_identity.txt holds `$Format:...$` placeholders that `git archive`
# expands (src/.gitattributes marks the file `export-subst`).
#
# Resolution order, first usable value wins:
#   1. KISAK_SOURCE_COMMIT (cache variable or environment) - an explicit
#      override for reproducible/CI builds that already know the revision.
#   2. `git rev-parse HEAD` in the source directory - a normal checkout.
#   3. The substituted src/source_identity.txt - an extracted archive.
#
# Unsubstituted `$Format:...$` placeholder text is never accepted, and a
# candidate must look like a git object name (7..40 hex digits). If nothing
# usable is found the result is empty rather than a fabricated identity; the
# caller decides how to record that.
#
# Defines:
#   kisak_resolve_source_identity(<source_dir> <out_commit>)
#
# The function is usable from configure-time includes and from `cmake -P`
# script tests.

function(_kisak_identity_is_placeholder VALUE OUT)
    set(_placeholder FALSE)
    if(VALUE STREQUAL "")
        set(_placeholder TRUE)
    else()
        string(FIND "${VALUE}" "$Format:" _format_position)
        if(NOT _format_position EQUAL -1)
            set(_placeholder TRUE)
        endif()
    endif()
    set(${OUT} ${_placeholder} PARENT_SCOPE)
endfunction()

function(_kisak_identity_looks_like_commit VALUE OUT)
    set(_looks_like FALSE)
    if(VALUE MATCHES "^[0-9a-fA-F]+$")
        string(LENGTH "${VALUE}" _length)
        if(_length GREATER_EQUAL 7 AND _length LESS_EQUAL 40)
            set(_looks_like TRUE)
        endif()
    endif()
    set(${OUT} ${_looks_like} PARENT_SCOPE)
endfunction()

function(kisak_resolve_source_identity SOURCE_DIR OUT_COMMIT)
    if(NOT DEFINED SOURCE_DIR OR SOURCE_DIR STREQUAL "")
        message(FATAL_ERROR
            "kisak_resolve_source_identity requires a non-empty source directory")
    endif()

    # 1. Explicit override (cache or environment).
    set(_override "")
    if(DEFINED KISAK_SOURCE_COMMIT AND NOT KISAK_SOURCE_COMMIT STREQUAL "")
        set(_override "${KISAK_SOURCE_COMMIT}")
    elseif(DEFINED ENV{KISAK_SOURCE_COMMIT})
        set(_override "$ENV{KISAK_SOURCE_COMMIT}")
    endif()
    if(NOT _override STREQUAL "")
        _kisak_identity_looks_like_commit("${_override}" _override_ok)
        if(NOT _override_ok)
            message(FATAL_ERROR
                "KISAK_SOURCE_COMMIT override is not a git commit hash: "
                "'${_override}'")
        endif()
        set(${OUT_COMMIT} "${_override}" PARENT_SCOPE)
        return()
    endif()

    # 2. The tree's own git checkout. GIT_CEILING_DIRECTORIES stops the upward
    # search at the tree's parent, so an extracted archive sitting inside an
    # unrelated repository cannot inherit that repository's HEAD; the tree
    # itself is still examined for `.git`.
    get_filename_component(_source_dir_absolute "${SOURCE_DIR}" ABSOLUTE)
    get_filename_component(_git_ceiling "${_source_dir_absolute}" DIRECTORY)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "GIT_CEILING_DIRECTORIES=${_git_ceiling}"
            git rev-parse HEAD
        WORKING_DIRECTORY "${_source_dir_absolute}"
        OUTPUT_VARIABLE _git_commit
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _git_result
    )
    if(_git_result EQUAL 0)
        _kisak_identity_looks_like_commit("${_git_commit}" _git_ok)
        if(_git_ok)
            set(${OUT_COMMIT} "${_git_commit}" PARENT_SCOPE)
            return()
        endif()
    endif()

    # 3. Substituted export-subst carrier shipped inside a source archive.
    set(_identity_file "${SOURCE_DIR}/src/source_identity.txt")
    if(EXISTS "${_identity_file}")
        file(STRINGS "${_identity_file}" _commit_lines REGEX "^commit=")
        if(_commit_lines)
            list(GET _commit_lines 0 _commit_line)
            string(SUBSTRING "${_commit_line}" 7 -1 _archive_commit)
            string(STRIP "${_archive_commit}" _archive_commit)
            _kisak_identity_is_placeholder("${_archive_commit}" _archive_placeholder)
            _kisak_identity_looks_like_commit("${_archive_commit}" _archive_ok)
            if(NOT _archive_placeholder AND _archive_ok)
                set(${OUT_COMMIT} "${_archive_commit}" PARENT_SCOPE)
                return()
            endif()
        endif()
    endif()

    set(${OUT_COMMIT} "" PARENT_SCOPE)
endfunction()
