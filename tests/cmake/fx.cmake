# EffectsCore archive, fast-file and physics-sidecar tests.
# Included from tests/CMakeLists.txt.

function(kisakcod_fx_helper_stack_budget target)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _stack_build_type_upper)
    set(_stack_active_cxx_flags
        "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${_stack_build_type_upper}}")
    if(NOT MSVC
        AND _stack_active_cxx_flags MATCHES "(^|[ ;])-fsanitize=")
        # Sanitizer redzones intentionally inflate otherwise bounded helper
        # frames. Runtime sanitizer fixtures still execute these paths; the
        # 4 KiB static budget is enforced on every production-style build.
        return()
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /analyze
            /analyze:autolog-
            "SHELL:/analyze:stacksize 4096"
            "/analyze:ruleset${CMAKE_CURRENT_SOURCE_DIR}/fx_archive_stack.ruleset")
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE
            -Wstack-usage=4096
            -fstack-usage)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "^(Clang|AppleClang)$")
        target_compile_options(${target} PRIVATE
            -Wframe-larger-than=4096
            -Walloca
            -fstack-usage)
    endif()
endfunction()

add_library(kisakcod-fx-effect-table-restore-subject OBJECT
    ${SRC_DIR}/EffectsCore/fx_effect_table_restore.cpp
)
target_include_directories(
    kisakcod-fx-effect-table-restore-subject PRIVATE
        ${SRC_DIR}/EffectsCore
)
target_include_directories(
    kisakcod-fx-effect-table-restore-subject SYSTEM PRIVATE
        ${SRC_DIR}
)
target_compile_features(
    kisakcod-fx-effect-table-restore-subject PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-effect-table-restore-subject PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-effect-table-restore-subject)
kisakcod_fx_helper_stack_budget(
    kisakcod-fx-effect-table-restore-subject)

add_library(kisakcod-fx-effect-table-save-subject OBJECT
    ${SRC_DIR}/EffectsCore/fx_effect_table_save.cpp
)
target_include_directories(
    kisakcod-fx-effect-table-save-subject PRIVATE
        ${SRC_DIR}/EffectsCore
)
target_include_directories(
    kisakcod-fx-effect-table-save-subject SYSTEM PRIVATE
        ${SRC_DIR}
)
target_compile_features(
    kisakcod-fx-effect-table-save-subject PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-effect-table-save-subject PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-effect-table-save-subject)
kisakcod_fx_helper_stack_budget(
    kisakcod-fx-effect-table-save-subject)

add_executable(kisakcod-fx-effect-table-restore-tests
    fx_effect_table_restore_tests.cpp
    $<TARGET_OBJECTS:kisakcod-fx-effect-table-restore-subject>
)
target_include_directories(
    kisakcod-fx-effect-table-restore-tests PRIVATE
        ${SRC_DIR}/EffectsCore
)
target_include_directories(
    kisakcod-fx-effect-table-restore-tests SYSTEM PRIVATE
        ${SRC_DIR}
)
target_compile_features(
    kisakcod-fx-effect-table-restore-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-effect-table-restore-tests PRIVATE KISAK_MP)
target_link_libraries(
    kisakcod-fx-effect-table-restore-tests PRIVATE
        kisakcod-memfile-test-subject
        Threads::Threads)
kisakcod_test_warnings(kisakcod-fx-effect-table-restore-tests)
set_target_properties(kisakcod-fx-effect-table-restore-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-effect-table-transactional-restore
    COMMAND kisakcod-fx-effect-table-restore-tests
)
set_tests_properties(
    effectscore-effect-table-transactional-restore PROPERTIES TIMEOUT 30)

add_executable(kisakcod-fx-effect-table-save-tests
    fx_effect_table_save_tests.cpp
    $<TARGET_OBJECTS:kisakcod-fx-effect-table-save-subject>
    $<TARGET_OBJECTS:kisakcod-fx-effect-table-restore-subject>
)
target_include_directories(
    kisakcod-fx-effect-table-save-tests PRIVATE
        ${SRC_DIR}/EffectsCore
)
target_include_directories(
    kisakcod-fx-effect-table-save-tests SYSTEM PRIVATE
        ${SRC_DIR}
)
target_compile_features(
    kisakcod-fx-effect-table-save-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-effect-table-save-tests PRIVATE KISAK_MP)
target_link_libraries(
    kisakcod-fx-effect-table-save-tests PRIVATE
        kisakcod-memfile-test-subject
        Threads::Threads)
kisakcod_test_warnings(kisakcod-fx-effect-table-save-tests)
set_target_properties(kisakcod-fx-effect-table-save-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-effect-table-bounded-save
    COMMAND kisakcod-fx-effect-table-save-tests
)
set_tests_properties(
    effectscore-effect-table-bounded-save PROPERTIES TIMEOUT 60)

string(TOUPPER "${CMAKE_BUILD_TYPE}" _kisakcod_build_type_upper)
set(_kisakcod_active_cxx_flags
    "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${_kisakcod_build_type_upper}}")
if(NOT MSVC AND NOT _kisakcod_active_cxx_flags MATCHES "(^|[ ;])-fsanitize=")
    # Sanitizer instrumentation intentionally introduces dynamic helper frames.
    # Its runtime remains covered above with a bounded 1 MiB worker stack; the
    # static .su contract measures the production, non-instrumented objects.
    set(_kisakcod_fx_save_stack_report_dir
        "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/kisakcod-fx-effect-table-save-subject.dir")
    set(_kisakcod_fx_restore_stack_report_dir
        "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/kisakcod-fx-effect-table-restore-subject.dir")
    add_test(
        NAME effectscore-effect-table-stack-usage
        COMMAND ${CMAKE_COMMAND}
            "-DSTACK_USAGE_FILES=${_kisakcod_fx_save_stack_report_dir};${_kisakcod_fx_restore_stack_report_dir}"
            -DSTACK_USAGE_LIMIT_BYTES=4096
            -P ${CMAKE_CURRENT_SOURCE_DIR}/fx_stack_usage_report_test.cmake
    )
endif()

add_executable(kisakcod-fx-archive-disk32-tests
    fx_archive_disk32_tests.cpp
    ${SRC_DIR}/EffectsCore/fx_archive_disk32.cpp
)
target_include_directories(kisakcod-fx-archive-disk32-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-fx-archive-disk32-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-fx-archive-disk32-tests)
set_target_properties(kisakcod-fx-archive-disk32-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-archive-disk32-codec
    COMMAND kisakcod-fx-archive-disk32-tests
)
set_tests_properties(effectscore-archive-disk32-codec PROPERTIES TIMEOUT 10)

add_library(kisakcod-fx-archive-body-state-disk32-subject OBJECT
    ${SRC_DIR}/EffectsCore/fx_archive_body_state_disk32.cpp
)
target_include_directories(
    kisakcod-fx-archive-body-state-disk32-subject PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-archive-body-state-disk32-subject PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-archive-body-state-disk32-subject PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-archive-body-state-disk32-subject)
kisakcod_fx_helper_stack_budget(
    kisakcod-fx-archive-body-state-disk32-subject)

add_executable(kisakcod-fx-archive-body-state-disk32-tests
    fx_archive_body_state_disk32_tests.cpp
    $<TARGET_OBJECTS:kisakcod-fx-archive-body-state-disk32-subject>
)
target_include_directories(
    kisakcod-fx-archive-body-state-disk32-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-archive-body-state-disk32-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-archive-body-state-disk32-tests PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-archive-body-state-disk32-tests)
set_target_properties(
    kisakcod-fx-archive-body-state-disk32-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-archive-body-state-disk32-codec
    COMMAND kisakcod-fx-archive-body-state-disk32-tests
)
set_tests_properties(
    effectscore-archive-body-state-disk32-codec PROPERTIES TIMEOUT 20)

add_library(kisakcod-fx-archive-system-disk32-subject OBJECT
    ${SRC_DIR}/EffectsCore/fx_archive_system_disk32.cpp
)
target_include_directories(
    kisakcod-fx-archive-system-disk32-subject PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-archive-system-disk32-subject PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-archive-system-disk32-subject PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-archive-system-disk32-subject)
kisakcod_fx_helper_stack_budget(
    kisakcod-fx-archive-system-disk32-subject)

add_executable(kisakcod-fx-archive-system-disk32-tests
    fx_archive_system_disk32_tests.cpp
    ${SRC_DIR}/EffectsCore/fx_archive_disk32.cpp
    $<TARGET_OBJECTS:kisakcod-fx-archive-system-disk32-subject>
)
target_include_directories(
    kisakcod-fx-archive-system-disk32-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-archive-system-disk32-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-archive-system-disk32-tests PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-archive-system-disk32-tests)
set_target_properties(kisakcod-fx-archive-system-disk32-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-archive-system-disk32-codec
    COMMAND kisakcod-fx-archive-system-disk32-tests
)
set_tests_properties(
    effectscore-archive-system-disk32-codec PROPERTIES TIMEOUT 20)

add_library(kisakcod-fx-archive-buffers-disk32-subject OBJECT
    ${SRC_DIR}/EffectsCore/fx_archive_buffers_disk32.cpp
)
target_include_directories(
    kisakcod-fx-archive-buffers-disk32-subject PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-archive-buffers-disk32-subject PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-archive-buffers-disk32-subject PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-archive-buffers-disk32-subject)
kisakcod_fx_helper_stack_budget(
    kisakcod-fx-archive-buffers-disk32-subject)

add_executable(kisakcod-fx-archive-buffers-disk32-tests
    fx_archive_buffers_disk32_tests.cpp
    $<TARGET_OBJECTS:kisakcod-fx-archive-buffers-disk32-subject>
)
target_include_directories(
    kisakcod-fx-archive-buffers-disk32-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-archive-buffers-disk32-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-archive-buffers-disk32-tests PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-archive-buffers-disk32-tests)
set_target_properties(kisakcod-fx-archive-buffers-disk32-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-archive-buffers-disk32-codec
    COMMAND kisakcod-fx-archive-buffers-disk32-tests
)
set_tests_properties(
    effectscore-archive-buffers-disk32-codec PROPERTIES TIMEOUT 20)

add_library(kisakcod-fx-archive-native-disk32-subject OBJECT
    ${SRC_DIR}/EffectsCore/fx_archive_native_disk32.cpp
    ${SRC_DIR}/EffectsCore/fx_archive_semantics.cpp
)
target_include_directories(
    kisakcod-fx-archive-native-disk32-subject PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-archive-native-disk32-subject PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-archive-native-disk32-subject PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-archive-native-disk32-subject)
kisakcod_fx_helper_stack_budget(
    kisakcod-fx-archive-native-disk32-subject)

add_executable(kisakcod-fx-archive-native-disk32-tests
    fx_archive_native_disk32_tests.cpp
    ${SRC_DIR}/EffectsCore/fx_archive_disk32.cpp
    $<TARGET_OBJECTS:kisakcod-fx-archive-system-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-archive-buffers-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-archive-native-disk32-subject>
)
target_include_directories(
    kisakcod-fx-archive-native-disk32-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-archive-native-disk32-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-archive-native-disk32-tests PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-archive-native-disk32-tests)
set_target_properties(kisakcod-fx-archive-native-disk32-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-archive-native-disk32-codec
    COMMAND kisakcod-fx-archive-native-disk32-tests
)
set_tests_properties(
    effectscore-archive-native-disk32-codec PROPERTIES TIMEOUT 30)

add_library(kisakcod-fx-archive-reader-disk32-subject OBJECT
    ${SRC_DIR}/EffectsCore/fx_archive_reader_disk32.cpp
)
target_include_directories(
    kisakcod-fx-archive-reader-disk32-subject PRIVATE
        ${SRC_DIR}/EffectsCore
)
target_include_directories(
    kisakcod-fx-archive-reader-disk32-subject SYSTEM PRIVATE
        ${SRC_DIR}
)
target_compile_features(
    kisakcod-fx-archive-reader-disk32-subject PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-archive-reader-disk32-subject PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-archive-reader-disk32-subject)
kisakcod_fx_helper_stack_budget(
    kisakcod-fx-archive-reader-disk32-subject)

add_executable(kisakcod-fx-archive-reader-disk32-tests
    fx_archive_reader_disk32_tests.cpp
    ${SRC_DIR}/EffectsCore/fx_archive_disk32.cpp
    $<TARGET_OBJECTS:kisakcod-fx-archive-system-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-archive-buffers-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-archive-native-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-archive-body-state-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-archive-reader-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-effect-table-restore-subject>
)
target_include_directories(
    kisakcod-fx-archive-reader-disk32-tests PRIVATE
        ${SRC_DIR}/EffectsCore
)
target_include_directories(
    kisakcod-fx-archive-reader-disk32-tests SYSTEM PRIVATE
        ${SRC_DIR}
)
target_compile_features(
    kisakcod-fx-archive-reader-disk32-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-archive-reader-disk32-tests PRIVATE KISAK_MP)
target_link_libraries(
    kisakcod-fx-archive-reader-disk32-tests PRIVATE
        kisakcod-memfile-test-subject
        Threads::Threads)
kisakcod_test_warnings(kisakcod-fx-archive-reader-disk32-tests)
set_target_properties(
    kisakcod-fx-archive-reader-disk32-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-archive-reader-disk32
    COMMAND kisakcod-fx-archive-reader-disk32-tests
)
set_tests_properties(
    effectscore-archive-reader-disk32 PROPERTIES TIMEOUT 60)

add_library(kisakcod-fx-archive-restore-candidate-disk32-subject OBJECT
    ${SRC_DIR}/EffectsCore/fx_archive_restore_candidate_disk32.cpp
)
target_include_directories(
    kisakcod-fx-archive-restore-candidate-disk32-subject PRIVATE
        ${SRC_DIR}/EffectsCore
)
target_include_directories(
    kisakcod-fx-archive-restore-candidate-disk32-subject SYSTEM PRIVATE
        ${SRC_DIR}
)
target_compile_features(
    kisakcod-fx-archive-restore-candidate-disk32-subject PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-archive-restore-candidate-disk32-subject PRIVATE KISAK_MP)
kisakcod_test_warnings(
    kisakcod-fx-archive-restore-candidate-disk32-subject)
kisakcod_fx_helper_stack_budget(
    kisakcod-fx-archive-restore-candidate-disk32-subject)

add_executable(kisakcod-fx-archive-restore-candidate-disk32-tests
    fx_archive_restore_candidate_disk32_tests.cpp
    ${SRC_DIR}/EffectsCore/fx_archive_disk32.cpp
    $<TARGET_OBJECTS:kisakcod-fx-archive-system-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-archive-buffers-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-archive-native-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-archive-body-state-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-archive-reader-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-archive-restore-candidate-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-effect-table-restore-subject>
)
target_include_directories(
    kisakcod-fx-archive-restore-candidate-disk32-tests PRIVATE
        ${SRC_DIR}/EffectsCore
)
target_include_directories(
    kisakcod-fx-archive-restore-candidate-disk32-tests SYSTEM PRIVATE
        ${SRC_DIR}
)
target_compile_features(
    kisakcod-fx-archive-restore-candidate-disk32-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-archive-restore-candidate-disk32-tests PRIVATE KISAK_MP)
target_link_libraries(
    kisakcod-fx-archive-restore-candidate-disk32-tests PRIVATE
        kisakcod-memfile-test-subject
        Threads::Threads)
kisakcod_test_warnings(
    kisakcod-fx-archive-restore-candidate-disk32-tests)
set_target_properties(
    kisakcod-fx-archive-restore-candidate-disk32-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-archive-restore-candidate-disk32
    COMMAND kisakcod-fx-archive-restore-candidate-disk32-tests
)
set_tests_properties(
    effectscore-archive-restore-candidate-disk32 PROPERTIES TIMEOUT 60)

add_executable(kisakcod-fx-fastfile-disk32-tests
    fx_fastfile_disk32_tests.cpp
)
target_include_directories(
    kisakcod-fx-fastfile-disk32-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-fastfile-disk32-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-fastfile-disk32-tests PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-fastfile-disk32-tests)
set_target_properties(kisakcod-fx-fastfile-disk32-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-fastfile-disk32-schema
    COMMAND kisakcod-fx-fastfile-disk32-tests
)
set_tests_properties(
    effectscore-fastfile-disk32-schema PROPERTIES TIMEOUT 20)

add_library(kisakcod-fx-fastfile-native-disk32-subject OBJECT
    ${SRC_DIR}/EffectsCore/fx_fastfile_native_disk32.cpp
)
target_include_directories(
    kisakcod-fx-fastfile-native-disk32-subject PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-fastfile-native-disk32-subject PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-fastfile-native-disk32-subject PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-fastfile-native-disk32-subject)
kisakcod_fx_helper_stack_budget(
    kisakcod-fx-fastfile-native-disk32-subject)

add_executable(kisakcod-fx-fastfile-native-disk32-tests
    fx_fastfile_native_disk32_tests.cpp
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-native-disk32-subject>
)
target_include_directories(
    kisakcod-fx-fastfile-native-disk32-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-fastfile-native-disk32-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-fastfile-native-disk32-tests PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-fastfile-native-disk32-tests)
set_target_properties(kisakcod-fx-fastfile-native-disk32-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-fastfile-native-disk32-conversion
    COMMAND kisakcod-fx-fastfile-native-disk32-tests
)
set_tests_properties(
    effectscore-fastfile-native-disk32-conversion PROPERTIES TIMEOUT 60)

add_library(kisakcod-fx-fastfile-impact-native-disk32-subject OBJECT
    ${SRC_DIR}/EffectsCore/fx_fastfile_impact_native_disk32.cpp
)
target_include_directories(
    kisakcod-fx-fastfile-impact-native-disk32-subject PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-fastfile-impact-native-disk32-subject PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-fastfile-impact-native-disk32-subject PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-fastfile-impact-native-disk32-subject)
kisakcod_fx_helper_stack_budget(
    kisakcod-fx-fastfile-impact-native-disk32-subject)

add_executable(kisakcod-fx-fastfile-impact-native-disk32-tests
    fx_fastfile_impact_native_disk32_tests.cpp
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-impact-native-disk32-subject>
)
target_include_directories(
    kisakcod-fx-fastfile-impact-native-disk32-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-fastfile-impact-native-disk32-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-fastfile-impact-native-disk32-tests PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-fastfile-impact-native-disk32-tests)
set_target_properties(
    kisakcod-fx-fastfile-impact-native-disk32-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-fastfile-impact-native-disk32-conversion
    COMMAND kisakcod-fx-fastfile-impact-native-disk32-tests
)
set_tests_properties(
    effectscore-fastfile-impact-native-disk32-conversion PROPERTIES TIMEOUT 60)

add_library(kisakcod-fx-fastfile-native-arena-subject OBJECT
    ${SRC_DIR}/EffectsCore/fx_fastfile_native_arena.cpp
)
target_include_directories(
    kisakcod-fx-fastfile-native-arena-subject PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-fastfile-native-arena-subject PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-fastfile-native-arena-subject PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-fastfile-native-arena-subject)
kisakcod_fx_helper_stack_budget(kisakcod-fx-fastfile-native-arena-subject)

add_executable(kisakcod-fx-fastfile-native-arena-tests
    fx_fastfile_native_arena_tests.cpp
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-native-arena-subject>
)
target_include_directories(
    kisakcod-fx-fastfile-native-arena-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-fastfile-native-arena-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-fastfile-native-arena-tests PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-fastfile-native-arena-tests)
set_target_properties(kisakcod-fx-fastfile-native-arena-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-fastfile-native-arena
    COMMAND kisakcod-fx-fastfile-native-arena-tests
)
set_tests_properties(
    effectscore-fastfile-native-arena PROPERTIES TIMEOUT 20)

add_library(kisakcod-fx-fastfile-zone-adapter-disk32-subject OBJECT
    ${SRC_DIR}/EffectsCore/fx_fastfile_zone_adapter_disk32.cpp
)
target_include_directories(
    kisakcod-fx-fastfile-zone-adapter-disk32-subject PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-fastfile-zone-adapter-disk32-subject PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-fastfile-zone-adapter-disk32-subject PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-fastfile-zone-adapter-disk32-subject)
kisakcod_fx_helper_stack_budget(
    kisakcod-fx-fastfile-zone-adapter-disk32-subject)

add_executable(kisakcod-fx-fastfile-zone-adapter-disk32-tests
    fx_fastfile_zone_adapter_disk32_tests.cpp
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-zone-adapter-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-native-arena-subject>
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-native-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-impact-native-disk32-subject>
)
target_include_directories(
    kisakcod-fx-fastfile-zone-adapter-disk32-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-fastfile-zone-adapter-disk32-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-fastfile-zone-adapter-disk32-tests PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-fastfile-zone-adapter-disk32-tests)
set_target_properties(
    kisakcod-fx-fastfile-zone-adapter-disk32-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-fastfile-zone-adapter-disk32
    COMMAND kisakcod-fx-fastfile-zone-adapter-disk32-tests
)
set_tests_properties(
    effectscore-fastfile-zone-adapter-disk32 PROPERTIES TIMEOUT 60)

add_executable(kisakcod-db-fx-zone-adapter-wiring-tests
    db_fx_zone_adapter_wiring_tests.cpp
    ${SRC_DIR}/database/db_fx_zone_adapter_wiring.cpp
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-zone-adapter-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-native-arena-subject>
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-native-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-impact-native-disk32-subject>
)
target_include_directories(
    kisakcod-db-fx-zone-adapter-wiring-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-db-fx-zone-adapter-wiring-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-db-fx-zone-adapter-wiring-tests PRIVATE
        KISAK_MP
        KISAK_DB_FX_ZONE_ADAPTER_WIRING_TESTING=1)
kisakcod_test_warnings(kisakcod-db-fx-zone-adapter-wiring-tests)
set_target_properties(
    kisakcod-db-fx-zone-adapter-wiring-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME database-fx-zone-adapter-wiring
    COMMAND kisakcod-db-fx-zone-adapter-wiring-tests
)
set_tests_properties(
    database-fx-zone-adapter-wiring PROPERTIES TIMEOUT 30)

add_executable(kisakcod-db-fx-zone-adapter-wiring-production-call-site-tests
    db_fx_zone_adapter_wiring_production_call_site_tests.cpp
    ${SRC_DIR}/database/db_fx_zone_adapter_wiring.cpp
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-zone-adapter-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-native-arena-subject>
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-native-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-impact-native-disk32-subject>
)
target_include_directories(
    kisakcod-db-fx-zone-adapter-wiring-production-call-site-tests
    PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-db-fx-zone-adapter-wiring-production-call-site-tests
    PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-db-fx-zone-adapter-wiring-production-call-site-tests PRIVATE
        KISAK_MP
        KISAK_DB_FX_ZONE_ADAPTER_WIRING_TESTING=1)
kisakcod_test_warnings(
    kisakcod-db-fx-zone-adapter-wiring-production-call-site-tests)
set_target_properties(
    kisakcod-db-fx-zone-adapter-wiring-production-call-site-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME database-fx-zone-adapter-wiring-production-call-site
    COMMAND kisakcod-db-fx-zone-adapter-wiring-production-call-site-tests
)
set_tests_properties(
    database-fx-zone-adapter-wiring-production-call-site PROPERTIES TIMEOUT 30)

add_executable(kisakcod-db-fx-zone-adapter-wiring-headless-tests
    db_fx_zone_adapter_wiring_headless_tests.cpp
    ${SRC_DIR}/database/db_fx_zone_adapter_wiring_headless.cpp
)
target_include_directories(
    kisakcod-db-fx-zone-adapter-wiring-headless-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-db-fx-zone-adapter-wiring-headless-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-db-fx-zone-adapter-wiring-headless-tests PRIVATE
        KISAK_MP
        KISAK_DEDI_HEADLESS)
kisakcod_test_warnings(
    kisakcod-db-fx-zone-adapter-wiring-headless-tests)
set_target_properties(
    kisakcod-db-fx-zone-adapter-wiring-headless-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME database-fx-zone-adapter-wiring-headless
    COMMAND kisakcod-db-fx-zone-adapter-wiring-headless-tests
)
set_tests_properties(
    database-fx-zone-adapter-wiring-headless PROPERTIES TIMEOUT 30)

add_executable(kisakcod-fx-atomic-layout-tests
    fx_atomic_layout_tests.cpp
)
target_include_directories(kisakcod-fx-atomic-layout-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-fx-atomic-layout-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-fx-atomic-layout-tests)
set_target_properties(kisakcod-fx-atomic-layout-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-fixed-width-atomic-layouts
    COMMAND kisakcod-fx-atomic-layout-tests
)

add_executable(kisakcod-fx-runtime-blob-tests
    fx_runtime_blob_tests.cpp
)
target_include_directories(kisakcod-fx-runtime-blob-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-fx-runtime-blob-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-fx-runtime-blob-tests)
set_target_properties(kisakcod-fx-runtime-blob-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-runtime-blob-layout
    COMMAND kisakcod-fx-runtime-blob-tests
)

add_executable(kisakcod-fx-missing-effect-alias-tests
    fx_missing_effect_alias_tests.cpp
)
target_include_directories(kisakcod-fx-missing-effect-alias-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-fx-missing-effect-alias-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-fx-missing-effect-alias-tests)
set_target_properties(kisakcod-fx-missing-effect-alias-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-missing-effect-alias
    COMMAND kisakcod-fx-missing-effect-alias-tests
)

add_executable(kisakcod-fx-physics-sidecar-tests
    fx_physics_sidecar_tests.cpp
)
target_include_directories(kisakcod-fx-physics-sidecar-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-fx-physics-sidecar-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-fx-physics-sidecar-tests)
set_target_properties(kisakcod-fx-physics-sidecar-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-physics-body-sidecar
    COMMAND kisakcod-fx-physics-sidecar-tests
)

add_executable(kisakcod-fx-archive-capacity-tests
    fx_archive_capacity_tests.cpp
)
target_include_directories(kisakcod-fx-archive-capacity-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-fx-archive-capacity-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-fx-archive-capacity-tests)
set_target_properties(kisakcod-fx-archive-capacity-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-archive-capacity-planning
    COMMAND kisakcod-fx-archive-capacity-tests
)
set_tests_properties(effectscore-archive-capacity-planning PROPERTIES TIMEOUT 10)

add_executable(kisakcod-fx-archive-restore-control-tests
    fx_archive_restore_control_tests.cpp
    ${SRC_DIR}/EffectsCore/fx_archive_restore_control.cpp
)
target_include_directories(kisakcod-fx-archive-restore-control-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-fx-archive-restore-control-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-fx-archive-restore-control-tests)
set_target_properties(kisakcod-fx-archive-restore-control-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-archive-restore-control
    COMMAND kisakcod-fx-archive-restore-control-tests
)
set_tests_properties(effectscore-archive-restore-control PROPERTIES TIMEOUT 10)

add_executable(kisakcod-fx-archive-physics-batch-control-tests
    fx_archive_physics_batch_control_tests.cpp
    ${SRC_DIR}/EffectsCore/fx_archive_physics_batch_control.cpp
)
target_include_directories(
    kisakcod-fx-archive-physics-batch-control-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-archive-physics-batch-control-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-fx-archive-physics-batch-control-tests)
set_target_properties(kisakcod-fx-archive-physics-batch-control-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-archive-physics-batch-control
    COMMAND kisakcod-fx-archive-physics-batch-control-tests
)
set_tests_properties(effectscore-archive-physics-batch-control PROPERTIES TIMEOUT 10)

add_executable(kisakcod-fx-archive-gate-control-tests
    fx_archive_gate_control_tests.cpp
    ${SRC_DIR}/EffectsCore/fx_archive_gate_control.cpp
)
target_include_directories(
    kisakcod-fx-archive-gate-control-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-archive-gate-control-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-fx-archive-gate-control-tests)
set_target_properties(kisakcod-fx-archive-gate-control-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-archive-gate-control
    COMMAND kisakcod-fx-archive-gate-control-tests
)
set_tests_properties(effectscore-archive-gate-control PROPERTIES TIMEOUT 10)

add_executable(kisakcod-fx-archive-restore-workspace-tests
    fx_archive_restore_workspace_tests.cpp
)
target_include_directories(
    kisakcod-fx-archive-restore-workspace-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-archive-restore-workspace-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-fx-archive-restore-workspace-tests)
set_target_properties(kisakcod-fx-archive-restore-workspace-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-archive-restore-workspace
    COMMAND kisakcod-fx-archive-restore-workspace-tests
)

add_executable(kisakcod-fx-visibility-atomic-tests
    fx_visibility_atomic_tests.cpp
)
target_include_directories(kisakcod-fx-visibility-atomic-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-fx-visibility-atomic-tests PRIVATE cxx_std_20)
target_link_libraries(kisakcod-fx-visibility-atomic-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-fx-visibility-atomic-tests)
set_target_properties(kisakcod-fx-visibility-atomic-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-visibility-publication
    COMMAND kisakcod-fx-visibility-atomic-tests
)
set_tests_properties(effectscore-visibility-publication PROPERTIES TIMEOUT 20)

add_executable(kisakcod-fx-iterator-atomic-tests
    fx_iterator_atomic_tests.cpp
)
target_include_directories(kisakcod-fx-iterator-atomic-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-fx-iterator-atomic-tests PRIVATE cxx_std_20)
target_link_libraries(kisakcod-fx-iterator-atomic-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-fx-iterator-atomic-tests)
set_target_properties(kisakcod-fx-iterator-atomic-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-iterator-atomic-protocol
    COMMAND kisakcod-fx-iterator-atomic-tests
)
set_tests_properties(effectscore-iterator-atomic-protocol PROPERTIES TIMEOUT 20)

add_executable(kisakcod-fx-snapshot-publication-tests
    fx_snapshot_publication_tests.cpp
)
target_include_directories(
    kisakcod-fx-snapshot-publication-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-snapshot-publication-tests PRIVATE cxx_std_20)
target_link_libraries(
    kisakcod-fx-snapshot-publication-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-fx-snapshot-publication-tests)
set_target_properties(kisakcod-fx-snapshot-publication-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-snapshot-publication-coherence
    COMMAND kisakcod-fx-snapshot-publication-tests
)
set_tests_properties(
    effectscore-snapshot-publication-coherence PROPERTIES TIMEOUT 30)

add_executable(kisakcod-fx-pool-tests
    fx_pool_tests.cpp
)
target_include_directories(kisakcod-fx-pool-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-fx-pool-tests PRIVATE cxx_std_20)
target_link_libraries(kisakcod-fx-pool-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-fx-pool-tests)
set_target_properties(kisakcod-fx-pool-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-pool-and-handle-contracts
    COMMAND kisakcod-fx-pool-tests
)
set_tests_properties(effectscore-pool-and-handle-contracts PROPERTIES TIMEOUT 30)

kisakcod_ilp32(kisakcod-fx-archive-body-state-disk32-tests
    effectscore-archive-body-state-disk32-codec)

kisakcod_ilp32(kisakcod-fx-archive-buffers-disk32-tests
    effectscore-archive-buffers-disk32-codec)

kisakcod_ilp32(kisakcod-fx-archive-disk32-tests
    effectscore-archive-disk32-codec)

kisakcod_ilp32(kisakcod-fx-archive-native-disk32-tests
    effectscore-archive-native-disk32-codec)

kisakcod_ilp32(kisakcod-fx-archive-reader-disk32-tests
    effectscore-archive-reader-disk32)

kisakcod_ilp32(kisakcod-fx-archive-restore-candidate-disk32-tests
    effectscore-archive-restore-candidate-disk32)

kisakcod_ilp32(kisakcod-fx-archive-system-disk32-tests
    effectscore-archive-system-disk32-codec)

kisakcod_ilp32(kisakcod-fx-effect-table-save-tests
    effectscore-effect-table-bounded-save)

kisakcod_ilp32(kisakcod-fx-effect-table-restore-tests
    effectscore-effect-table-transactional-restore)

kisakcod_ilp32(kisakcod-fx-fastfile-disk32-tests
    effectscore-fastfile-disk32-schema)

kisakcod_ilp32(kisakcod-fx-fastfile-impact-native-disk32-tests
    effectscore-fastfile-impact-native-disk32-conversion)

kisakcod_ilp32(kisakcod-fx-fastfile-native-arena-tests
    effectscore-fastfile-native-arena)

kisakcod_ilp32(kisakcod-fx-fastfile-native-disk32-tests
    effectscore-fastfile-native-disk32-conversion)

kisakcod_ilp32(kisakcod-fx-fastfile-zone-adapter-disk32-tests
    effectscore-fastfile-zone-adapter-disk32)
