cmake_minimum_required(VERSION 3.16)

# Pointer-truncation tripwire (milestone M2). Fails when a NEW pointer->narrow-int
# truncation appears, or when an allowlist entry goes stale (forcing burndown as the
# 64-bit ABI work removes sites). Runs in CMake script mode with zero compiler
# dependency, so it rides the existing portable-tests ctest step on every runner.
#
# Patterns (a truncation is a truncation in every build profile, so no #if scoping):
#   P1  address cast to a narrow integer:  (int)&  (unsigned int)&  (uint32_t)&  (DWORD)&
#   P2  truncating page/align mask on an address:  0xFFFFF000 / 0xFFFFFFF0
# Pure-comment lines (// ...) are skipped so decompiler jump-table remnants are ignored.
#
# Tokens are "<rel>|<index>" (index 1..N per file). Integer tokens are list-safe in
# CMake, unlike raw C++ source lines (which carry ';' and '"'). Fixing a site lowers a
# file's count and makes its highest-index token stale -> forces an allowlist burndown.
#
# NOTE: the bare-hex-sizeof-assert funnel (P3 in the design) lands in M4 alongside the
# ABI layout-header split; it is intentionally not enforced yet.

set(SRC_DIR "${SOURCE_ROOT}/src")

# Third-party sources vendored under src/ are out of scope for the sweep.
set(_excluded_dir_regex "/physics/ode/|/groupvoice/speex/")

# CMake's regex engine has no {n} repetition, so hex runs are spelled out; [Ff] covers case.
set(_trunc_regex "\\((int|unsigned int|uint32_t|DWORD)\\)[ \t]*&|0[xX][Ff][Ff][Ff][Ff][Ff]000|0[xX][Ff][Ff][Ff][Ff][Ff][Ff][Ff]0")

file(GLOB_RECURSE _sources
    "${SRC_DIR}/*.cpp"
    "${SRC_DIR}/*.h"
)

set(_found)
foreach(_source ${_sources})
    if (_source MATCHES "${_excluded_dir_regex}")
        continue()
    endif()

    file(STRINGS "${_source}" _matches REGEX "${_trunc_regex}")
    file(RELATIVE_PATH _rel "${SRC_DIR}" "${_source}")

    set(_index 0)
    foreach(_line IN LISTS _matches)
        string(STRIP "${_line}" _stripped)
        if (_stripped MATCHES "^//")
            continue()
        endif()
        math(EXPR _index "${_index} + 1")
        list(APPEND _found "${_rel}|${_index}")
    endforeach()
endforeach()
list(SORT _found)

set(_allowed)
if (EXISTS "${ALLOWLIST}")
    file(STRINGS "${ALLOWLIST}" _allowlist_lines)
    foreach(_line ${_allowlist_lines})
        string(STRIP "${_line}" _line)
        if (_line STREQUAL "" OR _line MATCHES "^#")
            continue()
        endif()
        list(APPEND _allowed "${_line}")
    endforeach()
endif()
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
        message("New pointer-truncation debt (file|index):")
        foreach(_entry ${_new_debt})
            message("  ${_entry}")
        endforeach()
    endif()
    if (_stale_debt)
        message("Stale pointer-truncation allowlist entries (site fixed -> remove the entry):")
        foreach(_entry ${_stale_debt})
            message("  ${_entry}")
        endforeach()
    endif()
    message(FATAL_ERROR "Update or burn down tests/pointer_truncation.allow")
endif()

list(LENGTH _found _debt_count)
message(STATUS "Tracked ${_debt_count} pointer-truncation debt sites")
