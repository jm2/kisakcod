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

# An explicit KISAK_SOURCE_COMMIT override (cache variable or environment) is
# part of the resolution contract, so a configure-time cache override is
# forwarded into the stamp process: cmake -P runs in its own process and would
# never see this build's cache. Forwarding an undefined value is harmless: it
# arrives empty and the resolver skips an empty override in favor of the
# checkout or archive carrier.
add_custom_target(
  update_build_number
  COMMAND "${CMAKE_COMMAND}"
    "-DKISAK_STAMP_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    "-DKISAK_STAMP_SRC_DIR=${SRC_DIR}"
    "-DKISAK_STAMP_SCRIPTS_DIR=${SCRIPTS_DIR}"
    "-DKISAK_SOURCE_COMMIT=${KISAK_SOURCE_COMMIT}"
    -P "${SCRIPTS_DIR}/extern/stamp_build_number.cmake"
  COMMENT "Running build number script..."
  VERBATIM
)
