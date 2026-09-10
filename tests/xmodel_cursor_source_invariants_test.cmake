cmake_minimum_required(VERSION 3.16)

# xmodel_cursor_source_invariants_test.cmake
#
# Source-contract gate for the ki-okmr (#124) loader corrections. The
# xmodel loader TUs cannot link in a portable test binary (DirectX /
# Miles / ODE header web; engine targets are win32-only in this
# checkout), so the loader-side contract is pinned here the same way
# security_regression_test.cmake pins its invariants: the material
# second pass in XModelLoadFile must position through the checked
# cursor seek, never through a raw pointer rewind, and the checkpoint
# must come from the cursor (Tell), not from a raw pos alias.

function(read_repo_file RELATIVE_PATH OUT_VAR)
    file(READ "${SOURCE_ROOT}/${RELATIVE_PATH}" _text)
    set(${OUT_VAR} "${_text}" PARENT_SCOPE)
endfunction()

function(require_contains TEXT_VAR NEEDLE DESCRIPTION)
    string(FIND "${${TEXT_VAR}}" "${NEEDLE}" _position)
    if (_position EQUAL -1)
        message(FATAL_ERROR "Missing xmodel cursor invariant: ${DESCRIPTION}")
    endif()
endfunction()

function(forbid_contains TEXT_VAR NEEDLE DESCRIPTION)
    string(FIND "${${TEXT_VAR}}" "${NEEDLE}" _position)
    if (NOT _position EQUAL -1)
        message(FATAL_ERROR "Forbidden xmodel cursor regression: ${DESCRIPTION}")
    endif()
endfunction()

read_repo_file("src/xanim/xmodel_load_obj.cpp" _xmodel_source)

# The material second pass must rewind through the checked cursor seek.
require_contains(
    _xmodel_source
    "buf_cursor::SeekTo(v36)"
    "XModelLoadFile material second pass positions through buf_cursor::SeekTo")

# The second-pass checkpoint must be cursor-owned (Tell), not a raw pos
# alias taken before the first pass.
require_contains(
    _xmodel_source
    "v36 = buf_cursor::Tell()"
    "XModelLoadFile second-pass checkpoint is captured through buf_cursor::Tell")

# The raw rewind that desynchronized the cursor from *pos (and parsed
# the material pass at the first-pass end offset) must not return.
forbid_contains(
    _xmodel_source
    "pos = v36;"
    "raw pointer rewind of the second pass (pos = v36) is gone for good")

# The checked seek must fail closed into the ordinary malformed-input
# cleanup path.
require_contains(
    _xmodel_source
    "if (!buf_cursor::SeekTo(v36))
                goto LABEL_28;"
    "a failed second-pass seek rejects the model through LABEL_28 cleanup")

# Scoped nested ownership: every Activate in the loader must be paired
# with Deactivate on all exits (audited ki-okmr); pin the paired counts
# so a new early exit cannot silently skip the teardown. Occurrences are
# counted with REGEX MATCHALL (non-overlapping) — a hand-rolled
# find/slice loop must keep its offset and its search string consistent
# (string(FIND) returns _rest-relative offsets), otherwise the scan
# oscillates between earlier occurrences and never terminates.
string(REGEX MATCHALL "buf_cursor::Activate\\(" _activate_matches "${_xmodel_source}")
list(LENGTH _activate_matches _activate_count)
string(REGEX MATCHALL "buf_cursor::Deactivate\\(" _deactivate_matches "${_xmodel_source}")
list(LENGTH _deactivate_matches _deactivate_count)
if (NOT _activate_count GREATER 0)
    message(FATAL_ERROR "xmodel loader has no cursor activations; harness drift")
endif()
if (NOT _deactivate_count GREATER _activate_count)
    message(FATAL_ERROR
        "xmodel loader Deactivate count (${_deactivate_count}) must exceed "
        "Activate count (${_activate_count}): every activation needs at "
        "least its success exit plus error exits covered")
endif()
