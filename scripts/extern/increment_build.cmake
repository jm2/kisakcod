# The build number and source identity are stamped when the update_build_number
# target RUNS, not when CMake configures. Resolving them only at configure time
# stamps a reused build directory with a stale revision after any commit or
# checkout that changes no CMake input: CMake never re-runs, so the previously
# configured revision reached freshly built binaries. add_custom_target is
# always out-of-date, so the build-time stamp script below re-resolves the
# commit count and the source identity on every build.
#
# Resolution semantics are unchanged and live in
# scripts/extern/resolve_source_identity.cmake: an explicit KISAK_SOURCE_COMMIT
# override wins, then the working checkout's HEAD, then the substituted
# src/source_identity.txt carrier of a git-free source archive.
#
# The stamp alone is not enough to keep compiled artifacts current. The build
# number header is generated DURING the build, and Ninja validates the
# dependency graph up front: a header rewritten mid-build by an order
# dependency does not re-dirty translation units the graph already judged
# fresh, so after a commit that changed only a source file one ordinary build
# recompiled that file and relinked it with a buildnumber.cpp object that was
# still compiled against the previous revision. The stamp therefore writes into
# a staging directory, and a custom command whose OUTPUT is the real
# src/buildnumber.h publishes it with copy_if_different. As a declared build
# edge output, the published header is a first-class dependency of every
# consumer, and Ninja's restat re-evaluates those consumers in the SAME
# ordinary build when - and only when - the content actually changed; an
# unchanged stamp leaves the header untouched and every consumer stays clean.
#
# The staging round-trip alone still leaves one clock-shaped hole, closed by
# the stamp script: build tools disagree on timestamp resolution, and Apple's
# GNU Make 3.81 (the default generator on macOS CI) compares whole seconds.
# A stamp that rewrites the staged header within the same wall-clock second in
# which the previous build published the old revision ties the publish edge's
# mtime comparison, and make skips the copy - the stale revision survives one
# ordinary build. The stamp therefore also receives the published header's
# path and removes it whenever the stamped content differs: a missing output
# forces the publish edge to run on every generator, and the recreated file
# re-dirties compiled consumers in the same ordinary build regardless of clock
# resolution. An unchanged stamp compares equal and removes nothing.

# The staging directory must exist before the platform stamp script writes
# into it.
set(KISAK_STAMP_STAGE_DIR "${CMAKE_BINARY_DIR}/buildnumber-stamp")
file(MAKE_DIRECTORY "${KISAK_STAMP_STAGE_DIR}")

# An explicit KISAK_SOURCE_COMMIT override (cache variable or environment) is
# part of the resolution contract, so a configure-time cache override is
# forwarded into the stamp process: cmake -P runs in its own process and would
# never see this build's cache. Forwarding an undefined value is harmless: it
# arrives empty and the resolver skips an empty override in favor of the
# checkout or archive carrier. The staged header is declared as a byproduct so
# the generator knows this always-run edge produces it and re-evaluates it
# when the stamp runs.
add_custom_target(
  update_build_number_stamp
  COMMAND "${CMAKE_COMMAND}"
    "-DKISAK_STAMP_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    "-DKISAK_STAMP_SRC_DIR=${KISAK_STAMP_STAGE_DIR}"
    "-DKISAK_STAMP_SCRIPTS_DIR=${SCRIPTS_DIR}"
    "-DKISAK_SOURCE_COMMIT=${KISAK_SOURCE_COMMIT}"
    "-DKISAK_STAMP_PUBLISH_HEADER=${SRC_DIR}/buildnumber.h"
    -P "${SCRIPTS_DIR}/extern/stamp_build_number.cmake"
  BYPRODUCTS "${KISAK_STAMP_STAGE_DIR}/buildnumber.h"
  COMMENT "Running build number script..."
  VERBATIM
)

# Publish the staged stamp into SRC_DIR. The file dependency on the staged
# header is what reschedules this edge - and through the published header,
# every compiled consumer - in the same ordinary build when the stamp produced
# new content: the target dependency alone would be order-only, merely running
# the stamp before this edge without ever re-dirtying it. The content-comparing
# copy keeps the published header's mtime (and therefore every consumer) clean
# when nothing changed. When the stamp superseded the published content within
# one timestamp-resolution step, the stamp has already removed the published
# header (see the coarse-clock guard in stamp_build_number.cmake): a missing
# output forces this edge to run and restores the header with fresh content.
add_custom_command(
  OUTPUT "${SRC_DIR}/buildnumber.h"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${KISAK_STAMP_STAGE_DIR}/buildnumber.h"
    "${SRC_DIR}/buildnumber.h"
  DEPENDS "${KISAK_STAMP_STAGE_DIR}/buildnumber.h"
    update_build_number_stamp
  COMMENT "Publishing stamped buildnumber.h"
  VERBATIM
)

# Public entry point, named for scripts/pre_build.cmake's
# add_dependencies(${PROJECT_NAME} update_build_number) and for the tooling
# that invokes the target directly. Depending on the published header pulls
# both the stamp and the publish edge into every ordinary build.
add_custom_target(
  update_build_number
  DEPENDS "${SRC_DIR}/buildnumber.h"
)
