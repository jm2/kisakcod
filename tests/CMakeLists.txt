function(kisakcod_test_warnings target)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

# Utility-only configurations do not enter an engine subdirectory, so select
# the host platform source sets here before compiling the service backends.
include("${SCRIPTS_DIR}/platform_override.cmake")
include("${SCRIPTS_DIR}/common_files.cmake")
include("${PLATFORM_DIR}/${KISAK_PLATFORM}/platform.cmake")
kisakcod_require_platform_source_sets()
find_package(Threads REQUIRED)

# Tests labelled `ilp32` also run on the Windows x86 legs (ctest -L ilp32).
# Label a test with kisakcod_ilp32(<executable target> <test>...) next to its
# registration; the target then hangs off kisakcod-ilp32-tests, which is what
# those legs build. Absent targets and tests (other hosts) are skipped.
add_custom_target(kisakcod-ilp32-tests)
function(kisakcod_ilp32 TARGET_NAME)
    if (NOT TARGET ${TARGET_NAME})
        return()
    endif()
    add_dependencies(kisakcod-ilp32-tests ${TARGET_NAME})
    foreach(_test IN LISTS ARGN)
        if (TEST ${_test})
            set_property(TEST ${_test} APPEND PROPERTY LABELS ilp32)
        endif()
    endforeach()
endfunction()

# Test registrations, one file per area. Add a test to its area file and,
# if it must also run on the Windows x86 legs, label it with kisakcod_ilp32().
include(cmake/common.cmake)
include(cmake/physics_memory.cmake)
include(cmake/platform.cmake)
include(cmake/fx.cmake)
include(cmake/db.cmake)
include(cmake/script.cmake)
include(cmake/xmodel_xanim.cmake)
include(cmake/net.cmake)
include(cmake/voice_audio.cmake)
include(cmake/renderer.cmake)
include(cmake/upstream.cmake)
include(cmake/misc.cmake)
