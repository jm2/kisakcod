# Publish-edge aging step for the update_build_number stamp.
#
# This script runs as the second command of the publish custom command
# defined by scripts/extern/increment_build.cmake, immediately after the
# content-comparing copy. The stamp script (stamp_build_number.cmake)
# removes the published header whenever its content has been superseded and
# records every such supersede in a build-directory marker file; an unchanged
# stamp writes no marker at all.
#
# Why a strictly greater mtime is required: generators do not agree on
# timestamp resolution. Apple's GNU Make 3.81 - the default generator on
# macOS CI - compares whole seconds, and the entire stamp/publish cycle can
# complete within the wall-clock second in which the PREVIOUS build compiled
# its consumers. A recreated header then ties those consumer objects' mtimes
# and make skips their recompilation, relinking binaries that carry the
# previous revision's identity even though the header content is already
# correct. Removing the header guarantees only that the publish edge runs;
# only a strictly greater mtime guarantees the consumers recompile. Aging the
# freshly published header one second into the future makes it strictly newer
# than any object file that can exist at whole-second resolution, on every
# generator, with no sleeps and no second build.
#
# The step is deliberately inert whenever the stamp published identical
# content: no marker, no touch, so unchanged rebuilds leave the published
# header - and every consumer - untouched. The offset is written once per
# actual content change; consumers converge as soon as wall time passes the
# aged stamp, and later changes re-age the header.
#
# The clock itself must be the REAL wall clock. CMake's string(TIMESTAMP)
# honors SOURCE_DATE_EPOCH ("its value will be used instead of the current
# time"), so a reproducible-build environment would age the freshly published
# header to the pinned epoch - typically years in the past. Existing consumer
# objects are then strictly newer at whole-second resolution, make skips their
# recompilation, and binaries relink with the previous revision's identity:
# exactly the defect this step exists to prevent, reintroduced through the
# calendar source. The clock is therefore read from the filesystem instead
# (see below); the header's CONTENT stays deterministic, and this step only
# runs when the content actually changed, so reproducible-build semantics are
# preserved end to end.

if(NOT DEFINED KISAK_PUBLISH_HEADER OR KISAK_PUBLISH_HEADER STREQUAL "")
    message(FATAL_ERROR
        "schedule_publish_header.cmake requires KISAK_PUBLISH_HEADER")
endif()
if(NOT DEFINED KISAK_PUBLISH_SUPERSEDED_MARKER
        OR KISAK_PUBLISH_SUPERSEDED_MARKER STREQUAL "")
    message(FATAL_ERROR
        "schedule_publish_header.cmake requires "
        "KISAK_PUBLISH_SUPERSEDED_MARKER")
endif()

# Unchanged stamp: the publication was content-identical and stays untouched.
if(NOT EXISTS "${KISAK_PUBLISH_SUPERSEDED_MARKER}")
    return()
endif()

# Consume the marker exactly once, whether or not aging proceeds below.
file(REMOVE "${KISAK_PUBLISH_SUPERSEDED_MARKER}")

if(NOT EXISTS "${KISAK_PUBLISH_HEADER}")
    message(FATAL_ERROR
        "The stamp superseded ${KISAK_PUBLISH_HEADER} but the publish edge "
        "did not recreate it; the dependency graph is inconsistent")
endif()

# Windows builders (MSBuild, and Ninja over NTFS) compare high-resolution
# timestamps; the whole-second tie is a POSIX make defect. There is nothing
# to age there and no behavior changes on those generators.
if(NOT UNIX OR CMAKE_HOST_WIN32)
    return()
endif()

find_program(KISAK_PUBLISH_TOUCH_PROGRAM touch)
if(NOT KISAK_PUBLISH_TOUCH_PROGRAM)
    message(FATAL_ERROR
        "The publish-edge aging step found no touch utility; refusing to "
        "publish a header that whole-second make implementations would tie "
        "against already-built consumers")
endif()

# One second into the future, derived from the true wall clock, so the value is
# always strictly greater than any pre-existing consumer's whole-second
# timestamp. The calendar fields come from pure integer civil-from-days
# arithmetic (Hinnant's algorithm) - no date utility is consulted - and
# `touch -t` receives UTC fields under TZ=UTC0, the one spelling both GNU
# and BSD touch interpret identically.
#
# string(TIMESTAMP) cannot supply that clock: it substitutes SOURCE_DATE_EPOCH
# when the environment defines one, backdating the header into the past (see
# the comment block above). The true current time is read from the filesystem
# instead: file(TOUCH) stamps a probe file with the real current mtime and
# file(TIMESTAMP) reads that mtime back, and neither command consults
# SOURCE_DATE_EPOCH. The probe lives beside the superseded marker, inside the
# build tree.
set(KISAK_PUBLISH_CLOCK_PROBE "${KISAK_PUBLISH_SUPERSEDED_MARKER}.clock")
file(TOUCH "${KISAK_PUBLISH_CLOCK_PROBE}")
file(TIMESTAMP "${KISAK_PUBLISH_CLOCK_PROBE}" KISAK_PUBLISH_NOW_EPOCH "%s")
file(REMOVE "${KISAK_PUBLISH_CLOCK_PROBE}")
if(KISAK_PUBLISH_NOW_EPOCH STREQUAL "" OR KISAK_PUBLISH_NOW_EPOCH MATCHES "[^0-9]")
    message(FATAL_ERROR
        "The publish-edge aging step read no usable wall-clock epoch from a "
        "touched probe file ('${KISAK_PUBLISH_NOW_EPOCH}'); refusing to "
        "publish a header that whole-second make implementations would tie "
        "against already-built consumers")
endif()
math(EXPR KISAK_PUBLISH_FUTURE_EPOCH "${KISAK_PUBLISH_NOW_EPOCH} + 1")
math(EXPR _aging_day_number "${KISAK_PUBLISH_FUTURE_EPOCH} / 86400")
math(EXPR _aging_seconds "${KISAK_PUBLISH_FUTURE_EPOCH} % 86400")
math(EXPR _aging_hour "${_aging_seconds} / 3600")
math(EXPR _aging_minute "(${_aging_seconds} % 3600) / 60")
math(EXPR _aging_second "${_aging_seconds} % 60")
math(EXPR _aging_z "${_aging_day_number} + 719468")
math(EXPR _aging_era "${_aging_z} / 146097")
math(EXPR _aging_doe "${_aging_z} - ${_aging_era} * 146097")
math(EXPR _aging_yoe
    "(${_aging_doe} - ${_aging_doe} / 1460 + ${_aging_doe} / 36524 - ${_aging_doe} / 146096) / 365")
math(EXPR _aging_year "${_aging_yoe} + ${_aging_era} * 400")
math(EXPR _aging_doy
    "${_aging_doe} - (365 * ${_aging_yoe} + ${_aging_yoe} / 4 - ${_aging_yoe} / 100)")
math(EXPR _aging_mp "(5 * ${_aging_doy} + 2) / 153")
math(EXPR _aging_day "${_aging_doy} - (153 * ${_aging_mp} + 2) / 5 + 1")
math(EXPR _aging_month "${_aging_mp} + 3")
if(_aging_month GREATER 12)
    math(EXPR _aging_month "${_aging_month} - 12")
    math(EXPR _aging_year "${_aging_year} + 1")
endif()
foreach(_aging_field IN ITEMS
        _aging_month _aging_day _aging_hour _aging_minute _aging_second)
    if(${${_aging_field}} LESS 10)
        set(${_aging_field} "0${${_aging_field}}")
    endif()
endforeach()
set(KISAK_PUBLISH_FUTURE_STAMP
    "${_aging_year}${_aging_month}${_aging_day}${_aging_hour}${_aging_minute}.${_aging_second}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "TZ=UTC0"
        "${KISAK_PUBLISH_TOUCH_PROGRAM}" -t "${KISAK_PUBLISH_FUTURE_STAMP}"
        "${KISAK_PUBLISH_HEADER}"
    RESULT_VARIABLE KISAK_PUBLISH_TOUCH_RESULT
)
if(NOT KISAK_PUBLISH_TOUCH_RESULT EQUAL 0)
    message(FATAL_ERROR
        "touch -t ${KISAK_PUBLISH_FUTURE_STAMP} failed with exit code "
        "${KISAK_PUBLISH_TOUCH_RESULT}; the freshly published header would "
        "tie whole-second make comparisons against already-built consumers")
endif()
