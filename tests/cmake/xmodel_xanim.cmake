# XModel and XAnim loader tests.
# Included from tests/CMakeLists.txt.

# xanim_load_test: BufCursor transactional current/end cursor contracts.
# Exercises the four documented safety fixes from the prior polecat
# attempt (ki-vxc / recovery branch): UBSan misaligned float load,
# cursor/raw-pointer desynchronization, unbounded strlen-before-check,
# and XModelParts classification / useBones advances.
add_executable(kisakcod-xanim-load-tests
    xanim_load_test.cpp
    ${SRC_DIR}/xanim/buf_cursor.cpp)
target_include_directories(
    kisakcod-xanim-load-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-xanim-load-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-xanim-load-tests)
set_target_properties(
    kisakcod-xanim-load-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(
    NAME xanim-load-bounded-cursor
    COMMAND kisakcod-xanim-load-tests)

# xmodel_load_test: same cursor, scoped to xmodel patterns.
add_executable(kisakcod-xmodel-load-tests
    xmodel_load_test.cpp
    ${SRC_DIR}/xanim/buf_cursor.cpp)
target_include_directories(
    kisakcod-xmodel-load-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-xmodel-load-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-xmodel-load-tests)
set_target_properties(
    kisakcod-xmodel-load-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(
    NAME xmodel-load-bounded-cursor
    COMMAND kisakcod-xmodel-load-tests)

# xmodel_nested_cursor_test: production-call-graph contracts for the
# scoped nested cursor ownership and the checked second-pass seek
# (ki-okmr / #124). Drives the real buf_cursor entry points in the
# exact sequence the production loaders issue (XModelLoadFile ->
# nested XModelPartsPrecache / XModelSurfsPrecache windows, material
# second-pass rewind) over controlled xmodel/xmodelparts fixtures:
# cold/warm nested loads, parent position/limit/failure restoration,
# truncated-input cleanup, and three-level LIFO nesting. The
# save-stack overflow fail-closed contracts run in their own TU,
# kisakcod-xmodel-cursor-overflow-tests below; both suites share the
# fixtures and CHECK harness via xmodel_cursor_test_support.hpp.
# The loader TUs cannot link in this portable binary (win32-only engine
# headers). CTest entries match the 'xmodel'
# regex so the bead's build/test command resolves to this target.
add_executable(kisakcod-xmodel-nested-cursor-tests
    xmodel_nested_cursor_test.cpp
    xmodel_nested_cursor_walks.hpp
    xmodel_cursor_activation_scope_tests.hpp
    xmodel_cursor_test_support.hpp
    ${SRC_DIR}/xanim/buf_cursor.cpp)
target_include_directories(
    kisakcod-xmodel-nested-cursor-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-xmodel-nested-cursor-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-xmodel-nested-cursor-tests)
set_target_properties(
    kisakcod-xmodel-nested-cursor-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(
    NAME xmodel-nested-cursor-ownership
    COMMAND kisakcod-xmodel-nested-cursor-tests)

# xmodel_cursor_overflow_test: fail-closed contracts for the buf_cursor
# save-stack bound (ki-okmr / #124). Activation past the 16-slot save
# stack installs a pre-failed scope whose reads fail closed; while an
# overflow scope is open the depth is pinned at the bound, an overflow
# scope's Deactivate pops nothing but leaves the overflowed caller
# active and pre-failed (no unbounded legacy fallback), a sibling
# re-activation in the overflowed frame overflows again, and the scopes
# below unwind strictly LIFO. Split from xmodel_nested_cursor_test.cpp
# so each contract suite stays within the file-size budget; same
# production buf_cursor entry points, no fixtures of its own (the two
# suites share fixtures/harness via xmodel_cursor_test_support.hpp).
add_executable(kisakcod-xmodel-cursor-overflow-tests
    xmodel_cursor_overflow_test.cpp
    xmodel_cursor_test_support.hpp
    ${SRC_DIR}/xanim/buf_cursor.cpp)
target_include_directories(
    kisakcod-xmodel-cursor-overflow-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-xmodel-cursor-overflow-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-xmodel-cursor-overflow-tests)
set_target_properties(
    kisakcod-xmodel-cursor-overflow-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(
    NAME xmodel-cursor-overflow-fail-closed
    COMMAND kisakcod-xmodel-cursor-overflow-tests)

# xmodel_loader_entry_test: production-loader entry-point contracts
# (ki-okmr / #124; XAnim entries enrolled in ki-458h stage 2). Links
# and executes the REAL production entry points from
# src/xanim/xmodel_load_obj.cpp + src/xanim/xanim_load_obj.cpp —
# XModelLoadFile (config/collision/LOD parsing, the real nested parts
# parse, real nested surfs windows, the checked material second pass)
# and XAnimLoadFile / XModelPiecesLoadFile / XModelPiecesPrecache —
# over controlled fixtures served by the harness file system
# (xmodel_loader_entry_harness.hpp: in-memory FS and hunk cache,
# capture stubs; none of the entry points needs a stand-in). The
# loader TUs' DirectX/Miles/ODE header web and MSVC decompiled dialect
# (raw __declspec(align(16)), `const struct` definitions) compile only
# under the win32-x86 platform, so this target is gated to the Windows
# x86 CI leg and never configured on the portable 64-bit legs; the
# portable suites above cover the cursor contracts everywhere else.
if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_executable(kisakcod-xmodel-loader-entry-tests
        xmodel_loader_entry_test.cpp
        xmodel_loader_entry_harness.hpp
        xmodel_cursor_test_support.hpp
        ${SRC_DIR}/xanim/xmodel_load_obj.cpp
        ${SRC_DIR}/xanim/xanim_load_obj.cpp
        ${SRC_DIR}/xanim/buf_cursor.cpp
        ${SRC_DIR}/universal/com_math.cpp
        ${SRC_DIR}/qcommon/com_pack.cpp)
    # The vendored SDK include root: the loader TU's transitive header
    # web pulls in <ode/ode.h> (via dobj.h) and <msslib/mss.h> (via
    # snd_public.h), exactly as the game target's DEPS_DIR include does.
    target_include_directories(
        kisakcod-xmodel-loader-entry-tests PRIVATE
            ${SRC_DIR}
            ${CMAKE_SOURCE_DIR}/deps)
    target_compile_features(
        kisakcod-xmodel-loader-entry-tests PRIVATE cxx_std_20)
    # The SP flavor: XModelAllowLoadMesh()'s body is #elif KISAK_SP (an
    # unconditional true) and both branches compile out without a KISAK_*
    # define, leaving the function without a return (C4716). KISAK_SP is
    # the SP game target's own definition for these TUs (scripts/sp), and
    # it keeps KISAK_DEDI_HEADLESS undefined so XModelLoadFile runs its
    # real parse instead of the headless abort.
    target_compile_definitions(
        kisakcod-xmodel-loader-entry-tests PRIVATE KISAK_SP)
    # Warning policy: the test TU compiles at exactly the game target's
    # own warning surface (/W3, platform.cmake — no /WX). The
    # surface-content assertions need the full production header web
    # (xmodel.h plus xanim.h for the real XSurface definition, through
    # r_material/r_gfx/r_bsp/bg_weapons/bg_local/actor) — exactly the
    # web game TUs compile at /W3 with warnings tolerated (that web
    # emits benign decompiled-dialect diagnostics across levels, e.g.
    # C4091 unnamed-enum typedefs and C4369 enumerators, hundreds of
    # times across game TUs; no game target builds /WX). /WX here would
    # mean an open-ended suppression list peeled one CI cycle at a
    # time, so the MSVC gates this target relies on are compile, link
    # and run on Debug/Release, plus the ctest contracts and the code
    # analyzers. _CRT_SECURE_NO_WARNINGS covers the deliberate
    # _vsnprintf truncation contract in the Com_* printf wrappers (the
    # native CRT spelling on the win32-x86 leg; POSIX hosts route
    # through universal/msvc_printf_shim.h).
    if (MSVC)
        set_source_files_properties(xmodel_loader_entry_test.cpp PROPERTIES
            COMPILE_OPTIONS "/W3"
            COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS")
    else()
        set_source_files_properties(xmodel_loader_entry_test.cpp PROPERTIES
            COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Werror")
    endif()
    set_target_properties(
        kisakcod-xmodel-loader-entry-tests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(
        NAME xmodel-loader-entry-native
        COMMAND kisakcod-xmodel-loader-entry-tests)
endif ()

# xanim_parts_split_test: contract tests for the XAnimParts disk mirror
# vs native runtime split (M4 + Priority 7). The retail x86 layout is
# frozen at exactly 88 bytes per XAnimParts instance via ONDISK_SIZE; the
# native runtime view widens to 0x88 on 64-bit via RUNTIME_SIZE. The
# tests exercise the Disk <-> Native conversion round-trip, the size
# contracts, and the property that XAnimClone's runtime allocation would
# never under-allocate the runtime view on 64-bit. CTest entries match
# the 'xanim' regex so the bead's build/test command resolves to this
# target.
add_executable(kisakcod-xanim-parts-split-tests
    xanim_parts_split_test.cpp
    xanim_parts_split_test_shim.h)
target_include_directories(
    kisakcod-xanim-parts-split-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-xanim-parts-split-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-xanim-parts-split-tests)
set_target_properties(
    kisakcod-xanim-parts-split-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(
    NAME xanim-parts-split-contracts
    COMMAND kisakcod-xanim-parts-split-tests)

kisakcod_ilp32(kisakcod-xmodel-loader-entry-tests
    xmodel-loader-entry-native)
