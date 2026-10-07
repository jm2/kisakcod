# Script VM and script-string tests.
# Included from tests/CMakeLists.txt.

add_executable(kisakcod-script-string-atomic-tests
    script_string_atomic_tests.cpp
)
target_include_directories(kisakcod-script-string-atomic-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-script-string-atomic-tests PRIVATE cxx_std_20)
target_link_libraries(kisakcod-script-string-atomic-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-script-string-atomic-tests)
set_target_properties(kisakcod-script-string-atomic-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME script-string-packed-atomic-contracts
    COMMAND kisakcod-script-string-atomic-tests
)
set_tests_properties(script-string-packed-atomic-contracts PROPERTIES TIMEOUT 20)

add_executable(kisakcod-script-memorytree-try-tests
    script_memorytree_try_tests.cpp
)
target_include_directories(
    kisakcod-script-memorytree-try-tests SYSTEM PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-script-memorytree-try-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-script-memorytree-try-tests PRIVATE
    KISAK_MP
    KISAK_MEMORY_TREE_VALIDATION_TESTING=1
    KISAK_SCRIPT_STRING_PERF_TESTING=1)
target_link_libraries(kisakcod-script-memorytree-try-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-script-memorytree-try-tests)
set_target_properties(kisakcod-script-memorytree-try-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME script-memorytree-try-contracts
    COMMAND kisakcod-script-memorytree-try-tests
)
set_tests_properties(script-memorytree-try-contracts PROPERTIES TIMEOUT 30)

# Include the production string-list translation unit directly so this
# white-box fixture can inspect its file-local persistent state without
# copying any ownership algorithm. The allocator, coordinator, transaction,
# and fast-lock implementations remain separately linked production
# translation units, matching the engine boundaries.
add_executable(kisakcod-script-string-ownership-tests
    script_string_ownership_tests.cpp
    ${SRC_DIR}/database/db_registry_ownership_coordinator.cpp
    ${SRC_DIR}/database/db_script_string_transaction.cpp
    ${SRC_DIR}/qcommon/sys_sync.cpp
    ${SRC_DIR}/script/scr_memorytree.cpp
)
target_include_directories(
    kisakcod-script-string-ownership-tests SYSTEM PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-script-string-ownership-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-script-string-ownership-tests PRIVATE
    KISAK_MP
    KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING=1
    KISAK_MEMORY_TREE_VALIDATION_TESTING=1
    KISAK_SCRIPT_STRING_PERF_TESTING=1)
target_link_libraries(
    kisakcod-script-string-ownership-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-script-string-ownership-tests)
set_target_properties(kisakcod-script-string-ownership-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME script-string-report-free-ownership-contracts
    COMMAND kisakcod-script-string-ownership-tests
)
set_tests_properties(
    script-string-report-free-ownership-contracts PROPERTIES TIMEOUT 30)

# script_value_split_test: contract tests for the script VM value-cell
# save-image mirror vs native runtime split (M4). The retail 32-bit value
# cell is frozen at 4-byte payload / 8-byte cell via ONDISK_SIZE in
# scr_native.hpp; the native runtime views widen to 8/16 bytes on 64-bit via
# RUNTIME_SIZE. The tests exercise the Disk <-> Native conversions, the
# value-bearing vs runtime-reconstruction vartype classification, the
# retail vartype encoding, float bit-preservation, and the no-fabricated-
# pointer rule. CTest entries match the 'script-value-split' regex so the
# bead's build/test command resolves to this target.
add_executable(kisakcod-script-value-split-tests
    script_value_split_test.cpp)
target_include_directories(
    kisakcod-script-value-split-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-script-value-split-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-script-value-split-tests)
if(MSVC)
    # scrStringGlob_t's KISAK_ALIGNAS(128) padding is deliberate cache-line
    # alignment; C4324 fires on the /W4 + /WX test targets only.
    target_compile_options(kisakcod-script-value-split-tests PRIVATE /wd4324)
endif()
set_target_properties(
    kisakcod-script-value-split-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(
    NAME script-value-split-contracts
    COMMAND kisakcod-script-value-split-tests)

# script_native64_layout_test: contract tests for the M4 (ki-n1et)
# script VM header widening. The migrated headers assert ILP32 sizes on
# 32-bit targets and natural widened sizes on 64-bit via RUNTIME_SIZE.
# Headers with a portable include chain are included for real; the
# Windows/production-bound ones are mirrored member-for-member and
# pinned to the same constants.
add_executable(kisakcod-script-native64-layout-tests
    script_native64_layout_test.cpp)
target_include_directories(
    kisakcod-script-native64-layout-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-script-native64-layout-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-script-native64-layout-tests)
if(MSVC)
    # See the script-value-split target: deliberate alignas(128) padding.
    target_compile_options(kisakcod-script-native64-layout-tests PRIVATE /wd4324)
endif()
set_target_properties(
    kisakcod-script-native64-layout-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(
    NAME script-native64-layout-contracts
    COMMAND kisakcod-script-native64-layout-tests)

# script-readstack-nested-contracts: production-path behavioral regression
# for the M4 (ki-n1et) nested VAR_STACK reader. The verbatim
# Scr_ReadStack slice is extracted from src/script/scr_readwrite.cpp at
# configure time via its "ki-n1et behavioral-test slice" anchors and
# compiled into the test TU against the real MemoryFile reader subject.
# Drives scalar-ended/empty children, siblings, multi-level nesting and
# the SCR_READSTACK_MAX_NESTING bound through actual save-image bytes.
set(_rw_slice_source "${SRC_DIR}/script/scr_readwrite.cpp")
file(READ "${_rw_slice_source}" _rw_slice_text)
set(_rw_slice_begin "//SCRIPT_READSTACK_SLICE_BEGIN")
set(_rw_slice_end "//SCRIPT_READSTACK_SLICE_END")
string(FIND "${_rw_slice_text}" "${_rw_slice_begin}" _rw_slice_b)
string(FIND "${_rw_slice_text}" "${_rw_slice_end}" _rw_slice_e)
if(_rw_slice_b EQUAL -1 OR _rw_slice_e EQUAL -1 OR _rw_slice_e LESS_EQUAL _rw_slice_b)
    message(FATAL_ERROR
        "Scr_ReadStack behavioral-test slice anchors missing or misordered "
        "in src/script/scr_readwrite.cpp")
endif()
string(LENGTH "${_rw_slice_begin}" _rw_slice_begin_len)
math(EXPR _rw_slice_b "${_rw_slice_b} + ${_rw_slice_begin_len} + 1")
math(EXPR _rw_slice_len "${_rw_slice_e} - ${_rw_slice_b}")
string(SUBSTRING "${_rw_slice_text}" "${_rw_slice_b}" "${_rw_slice_len}" _rw_slice_text)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(WRITE
    "${CMAKE_CURRENT_BINARY_DIR}/generated/script_readstack_slice.inc"
    "${_rw_slice_text}\n")

# Keep the reader and writer fixtures tied to their actual production bodies.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_rw_slice_source}")
file(READ "${_rw_slice_source}" _writer_source)
string(FIND "${_writer_source}" "//SCRIPT_WRITESTACK_SLICE_BEGIN" _writer_begin)
string(FIND "${_writer_source}" "//SCRIPT_WRITESTACK_SLICE_END" _writer_end)
if(_writer_begin EQUAL -1 OR _writer_end LESS_EQUAL _writer_begin)
    message(FATAL_ERROR "WriteStack behavioral-test slice is missing")
endif()
math(EXPR _writer_length "${_writer_end} - ${_writer_begin}")
string(SUBSTRING "${_writer_source}" ${_writer_begin} ${_writer_length} _writer_body)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/script_writestack_slice.inc" "${_writer_body}")

add_executable(kisakcod-script-readstack-nested-tests
    script_readstack_nested_test.cpp)
target_include_directories(kisakcod-script-readstack-nested-tests PRIVATE
    ${SRC_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_definitions(kisakcod-script-readstack-nested-tests PRIVATE KISAK_MP)
target_compile_features(kisakcod-script-readstack-nested-tests PRIVATE cxx_std_20)
# The verbatim decompiled slice keeps the legacy diagnostics isolation the
# memfile subject uses (see its note above); the harness shares the TU.
if(MSVC)
    target_compile_options(kisakcod-script-readstack-nested-tests PRIVATE /W0)
else()
    target_compile_options(kisakcod-script-readstack-nested-tests PRIVATE -w)
endif()
target_link_libraries(
    kisakcod-script-readstack-nested-tests PRIVATE
        kisakcod-memfile-test-subject
        Threads::Threads)
set_target_properties(kisakcod-script-readstack-nested-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(
    NAME script-readstack-nested-contracts
    COMMAND kisakcod-script-readstack-nested-tests)
set_tests_properties(script-readstack-nested-contracts PROPERTIES TIMEOUT 60)

# script-widening-value-contracts: the widened sval_u parse cell and the
# widened runtime records must move full-width host pointers through copy
# assignment and keep their RUNTIME_SIZE contract sizes on every target.
add_executable(kisakcod-script-widening-value-tests
    script_widening_value_test.cpp)
target_include_directories(
    kisakcod-script-widening-value-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-script-widening-value-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-script-widening-value-tests PRIVATE KISAK_DEDI_HEADLESS=1)
if(MSVC)
    # scrStringGlob_t intentionally aligns its table to 128 bytes.
    target_compile_options(kisakcod-script-widening-value-tests PRIVATE /wd4324)
endif()
kisakcod_test_warnings(kisakcod-script-widening-value-tests)
set_target_properties(
    kisakcod-script-widening-value-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(
    NAME script-widening-value-contracts
    COMMAND kisakcod-script-widening-value-tests)

# GSC parser and VM regressions (NOW row 20; #225, #199): real GSC source
# through every script TU the servers build, loaded, compiled and run as the
# game does. The boundary those TUs call is script_engine_harness.cpp.
# Linux and clang only: the script TUs follow the engine's clang +
# -fms-extensions toolchain policy (PLATFORM_POSIX.md), and the engine
# compiles off Windows only on Linux so far. --gc-sections drops the engine
# code no case reaches, so only that boundary needs definitions.
if (KISAK_PLATFORM STREQUAL "linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8
    AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_library(kisakcod-script-engine-objects OBJECT
        ${SRC_DIR}/script/scr_animtree.cpp
        ${SRC_DIR}/script/scr_compiler2.cpp
        ${SRC_DIR}/script/scr_const.cpp
        ${SRC_DIR}/script/scr_debugger.cpp
        ${SRC_DIR}/script/scr_evaluate.cpp
        ${SRC_DIR}/script/scr_main.cpp
        ${SRC_DIR}/script/scr_memorytree.cpp
        ${SRC_DIR}/script/scr_parser.cpp
        ${SRC_DIR}/script/scr_parsetree.cpp
        ${SRC_DIR}/script/scr_stringlist.cpp
        ${SRC_DIR}/script/scr_variable.cpp
        ${SRC_DIR}/script/scr_vm.cpp
        ${SRC_DIR}/script/scr_yacc2.cpp
    )
    # The Linux headless server's defines; SYSTEM keeps the decompiled
    # headers' warnings off the tests' strict warning set.
    target_include_directories(kisakcod-script-engine-objects SYSTEM PUBLIC ${SRC_DIR} ${DEPS_DIR})
    target_compile_features(kisakcod-script-engine-objects PUBLIC cxx_std_20)
    target_compile_definitions(kisakcod-script-engine-objects PUBLIC
        KISAK_MP KISAK_DEDICATED DEDICATED KISAK_DEDI_HEADLESS)
    target_compile_options(kisakcod-script-engine-objects PUBLIC
        -fms-extensions -ffunction-sections -fdata-sections)
    target_link_options(kisakcod-script-engine-objects PUBLIC -Wl,--gc-sections)

    add_executable(kisakcod-script-parser-stack-growth-tests
        script_parser_stack_growth_test.cpp script_engine_harness.cpp)
    add_executable(kisakcod-script-vm-arithmetic-tests
        script_vm_arithmetic_test.cpp script_engine_harness.cpp)
    foreach(_target kisakcod-script-parser-stack-growth-tests kisakcod-script-vm-arithmetic-tests)
        target_link_libraries(${_target} PRIVATE kisakcod-script-engine-objects)
        kisakcod_test_warnings(${_target})
        set_target_properties(${_target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    endforeach()
    # One process per case: each leaves the script system as a drop does.
    foreach(_case stack-growth long-lexeme error-recovery)
        add_test(NAME script-parser-${_case}-contracts
            COMMAND kisakcod-script-parser-stack-growth-tests ${_case})
        set_tests_properties(script-parser-${_case}-contracts PROPERTIES TIMEOUT 120)
    endforeach()
    foreach(_case arithmetic animtree-limit vectors)
        add_test(NAME script-vm-${_case}-contracts
            COMMAND kisakcod-script-vm-arithmetic-tests ${_case})
        set_tests_properties(script-vm-${_case}-contracts PROPERTIES TIMEOUT 120)
    endforeach()
endif()

# Compile selected production bodies against real runtime record headers and
# service doubles. These regressions exercise allocations and pointer consumers,
# including the object-save handoff, rather than a copy of their algorithms.
set(_runtime_slice "")
function(kisakcod_append_runtime_slice FILE TAG)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SRC_DIR}/script/${FILE}")
    file(READ "${SRC_DIR}/script/${FILE}" _source)
    string(FIND "${_source}" "//SCRIPT_RUNTIME_${TAG}_BEGIN" _begin)
    string(FIND "${_source}" "//SCRIPT_RUNTIME_${TAG}_END" _end)
    if(_begin EQUAL -1 OR _end LESS_EQUAL _begin)
        message(FATAL_ERROR "Production runtime slice ${TAG} is missing")
    endif()
    math(EXPR _length "${_end} - ${_begin}")
    string(SUBSTRING "${_source}" ${_begin} ${_length} _body)
    set(_runtime_slice "${_runtime_slice}${_body}\n" PARENT_SCOPE)
endfunction()
# The production range initializer is namespaced only to let the fixture
# observe Scr_InitVariables calls without replacing its implementation.
kisakcod_append_runtime_slice(scr_variable.cpp INIT_VARIABLE_RANGE)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/script_variable_range.inc" "${_runtime_slice}")
set(_runtime_slice "")
kisakcod_append_runtime_slice(scr_variable.cpp ALLOC_VALUE)
kisakcod_append_runtime_slice(scr_variable.cpp SET_NEW_VARIABLE_VALUE)
kisakcod_append_runtime_slice(scr_variable.cpp EVAL_VARIABLE)
kisakcod_append_runtime_slice(scr_variable.cpp DEBUG_POSITION_COMPARE)
kisakcod_append_runtime_slice(scr_variable.cpp THREAD_INFO_COMPARE)
kisakcod_append_runtime_slice(scr_compiler2.cpp SPECIFY_THREAD_POSITION)
kisakcod_append_runtime_slice(scr_debugger.cpp WATCH)
kisakcod_append_runtime_slice(scr_debugger.cpp OPCODE)
kisakcod_append_runtime_slice(scr_evaluate.cpp CANON_COMPARE)
kisakcod_append_runtime_slice(scr_evaluate.cpp CANON_ARCHIVE)
kisakcod_append_runtime_slice(scr_vm.cpp TERMINATE)
kisakcod_append_runtime_slice(scr_readwrite.cpp REFERENCES)
kisakcod_append_runtime_slice(scr_readwrite.cpp SAVE_OBJECT)
kisakcod_append_runtime_slice(scr_readwrite.cpp CLASS_ARRAYS)
kisakcod_append_runtime_slice(scr_readwrite.cpp SAVE_SHUTDOWN)
kisakcod_append_runtime_slice(scr_readwrite.cpp DEBUG_EXPR_REFS)
kisakcod_append_runtime_slice(scr_vm.h OPCODES)
kisakcod_append_runtime_slice(scr_compiler2.cpp CALL_TYPES)
kisakcod_append_runtime_slice(scr_compiler2.cpp ADD_FUNCTION)
kisakcod_append_runtime_slice(scr_compiler2.cpp EMIT_CODEPOS)
kisakcod_append_runtime_slice(scr_compiler2.cpp EMIT_SHORT)
kisakcod_append_runtime_slice(scr_compiler2.cpp EMIT_UNSIGNED_SHORT)
kisakcod_append_runtime_slice(scr_compiler2.cpp EMIT_IF_ELSE)
kisakcod_append_runtime_slice(scr_compiler2.cpp CASE_COMPARE)
kisakcod_append_runtime_slice(scr_compiler2.cpp EMIT_SWITCH)
kisakcod_append_runtime_slice(scr_vm.cpp READ_CODEPOS)
kisakcod_append_runtime_slice(scr_vm.cpp READ_UNSIGNED)
kisakcod_append_runtime_slice(scr_vm.cpp READ_SHORT)
kisakcod_append_runtime_slice(scr_variable.cpp INIT_VARIABLES)
kisakcod_append_runtime_slice(scr_variable.cpp FIND_THREADS)
kisakcod_append_runtime_slice(scr_variable.cpp EQUALITY)
kisakcod_append_runtime_slice(scr_variable.cpp STRING_PAIR)
kisakcod_append_runtime_slice(scr_evaluate.cpp VALUE_STRING)
kisakcod_append_runtime_slice(scr_vm.cpp READ_NATIVE_INT)
kisakcod_append_runtime_slice(scr_vm.cpp NEXT_CODEPOS)
kisakcod_append_runtime_slice(scr_compiler2.cpp GET_INTEGER)
# The integer branch is extracted separately: other entity field kinds
# remain outside this focused union-width regression.
set(_runtime_slice "${_runtime_slice}\nstatic void Fixture_SetGenericIntField(uint8_t *b, int32_t ofs) {\n")
kisakcod_append_runtime_slice(../game_mp/g_spawn_mp.cpp GENERIC_INT_FIELD)
set(_runtime_slice "${_runtime_slice}\n}\n")
kisakcod_append_runtime_slice(../game/g_hudelem.cpp HUD_BOOLEAN)
file(READ "${SRC_DIR}/game/game_public.h" _game_public_source)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SRC_DIR}/game/game_public.h")
string(FIND "${_game_public_source}" "struct game_hudelem_s // sizeof=0xAC" _hud_begin)
string(FIND "${_game_public_source}" "enum hudelem_update_t" _hud_end)
if(_hud_begin EQUAL -1 OR _hud_end LESS_EQUAL _hud_begin)
    message(FATAL_ERROR "Production MP HUD record declaration is missing")
endif()
math(EXPR _hud_length "${_hud_end} - ${_hud_begin}")
string(SUBSTRING "${_game_public_source}" ${_hud_begin} ${_hud_length} _hud_record)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/script_game_hud_types.inc" "${_hud_record}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/script_runtime_slice.inc" "${_runtime_slice}")
add_executable(kisakcod-script-runtime-pointer-tests script_runtime_pointer_test.cpp)
target_include_directories(kisakcod-script-runtime-pointer-tests PRIVATE
    ${SRC_DIR} ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_definitions(kisakcod-script-runtime-pointer-tests PRIVATE KISAK_MP KISAK_DEDI_HEADLESS=1)
target_compile_features(kisakcod-script-runtime-pointer-tests PRIVATE cxx_std_20)
# The slices retain decompiler diagnostics, as the existing readstack subject does.
if(MSVC)
    target_compile_options(kisakcod-script-runtime-pointer-tests PRIVATE /W0)
else()
    target_compile_options(kisakcod-script-runtime-pointer-tests PRIVATE -w)
endif()
set_target_properties(kisakcod-script-runtime-pointer-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME script-runtime-pointer-contracts COMMAND kisakcod-script-runtime-pointer-tests)

# Save preparation walks actual object and nested-stack registration paths.
set(_runtime_slice "")
kisakcod_append_runtime_slice(scr_readwrite.cpp SAVE_REGISTRATION)
kisakcod_append_runtime_slice(scr_readwrite.cpp SAVE_OBJECT_REGISTRATION)
kisakcod_append_runtime_slice(scr_readwrite.cpp SAVE_PRE)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/script_save_registration.inc" "${_runtime_slice}")
add_executable(kisakcod-script-save-registration-tests script_save_registration_test.cpp)
target_include_directories(kisakcod-script-save-registration-tests PRIVATE ${SRC_DIR} ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_definitions(kisakcod-script-save-registration-tests PRIVATE KISAK_MP KISAK_DEDI_HEADLESS=1)
target_compile_features(kisakcod-script-save-registration-tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(kisakcod-script-save-registration-tests PRIVATE /W0)
else()
    target_compile_options(kisakcod-script-save-registration-tests PRIVATE -w)
endif()
set_target_properties(kisakcod-script-save-registration-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME script-save-registration-contracts COMMAND kisakcod-script-save-registration-tests)

# Animation fixups compile production records, allocation/link consumers and cleanup.
set(_runtime_slice "")
kisakcod_append_runtime_slice(../bgame/bg_local.h ANIM_HANDLE_TYPES)
kisakcod_append_runtime_slice(../bgame/bg_local.h ANIM_TREE_TYPE)
kisakcod_append_runtime_slice(scr_animtree.h ANIM_PUBLIC_TYPE)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/script_animation_types.inc" "${_runtime_slice}")
set(_runtime_slice "")
kisakcod_append_runtime_slice(scr_animtree.cpp ANIM_FIXUPS)
kisakcod_append_runtime_slice(scr_animtree.cpp ANIM_CONNECT)
kisakcod_append_runtime_slice(scr_main.cpp ANIM_CODE_RANGE)
kisakcod_append_runtime_slice(scr_animtree.cpp ANIM_CHECK)
kisakcod_append_runtime_slice(scr_main.cpp ANIM_END_LOAD)
kisakcod_append_runtime_slice(scr_vm.cpp ANIM_ADD)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/script_animation_slice.inc" "${_runtime_slice}")
add_executable(kisakcod-script-animation-fixup-tests script_animation_fixup_test.cpp)
target_include_directories(kisakcod-script-animation-fixup-tests PRIVATE ${SRC_DIR} ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_definitions(kisakcod-script-animation-fixup-tests PRIVATE KISAK_MP KISAK_DEDI_HEADLESS=1)
target_compile_features(kisakcod-script-animation-fixup-tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(kisakcod-script-animation-fixup-tests PRIVATE /W0)
else()
    target_compile_options(kisakcod-script-animation-fixup-tests PRIVATE -w)
endif()
set_target_properties(kisakcod-script-animation-fixup-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME script-animation-fixup-contracts COMMAND kisakcod-script-animation-fixup-tests)

# Compile debugger compiler/evaluator bodies with actual native script records.
set(_debugger_slice "")
function(kisakcod_append_debugger_slice FILE TAG)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SRC_DIR}/script/${FILE}")
    file(READ "${SRC_DIR}/script/${FILE}" _source)
    string(FIND "${_source}" "//SCRIPT_DEBUGGER_${TAG}_BEGIN" _begin)
    string(FIND "${_source}" "//SCRIPT_DEBUGGER_${TAG}_END" _end)
    if(_begin EQUAL -1 OR _end LESS_EQUAL _begin)
        message(FATAL_ERROR "Production debugger slice ${TAG} is missing")
    endif()
    math(EXPR _length "${_end} - ${_begin}")
    string(SUBSTRING "${_source}" ${_begin} ${_length} _body)
    set(_debugger_slice "${_debugger_slice}${_body}\n" PARENT_SCOPE)
endfunction()
kisakcod_append_debugger_slice(scr_vm.h VM_TYPES)
kisakcod_append_debugger_slice(../ui/ui_shared.h SOURCE_POS_TYPE)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/script_debugger_types.inc" "${_debugger_slice}")
set(_debugger_slice "")
kisakcod_append_debugger_slice(scr_parsetree.cpp ORDINARY_NODES)
kisakcod_append_debugger_slice(scr_parsetree.cpp SCR_ALLOCDEBUGEXPR)
kisakcod_append_debugger_slice(scr_parsetree.cpp DEBUGGER_NODE0)
kisakcod_append_debugger_slice(scr_parsetree.cpp DEBUGGER_NODE1)
kisakcod_append_debugger_slice(scr_parsetree.cpp DEBUGGER_NODE2)
kisakcod_append_debugger_slice(scr_parsetree.cpp DEBUGGER_NODE3)
kisakcod_append_debugger_slice(scr_parsetree.cpp DEBUGGER_NODE4)
kisakcod_append_debugger_slice(scr_parsetree.cpp DEBUGGER_PREPEND_NODE)
kisakcod_append_debugger_slice(scr_parsetree.cpp DEBUGGER_BUFFER)
kisakcod_append_debugger_slice(scr_parsetree.cpp DEBUGGER_STRING)
kisakcod_append_debugger_slice(scr_parsetree.cpp LINKED_LIST_END)
kisakcod_append_debugger_slice(scr_parsetree.cpp PREPEND_NODE)
kisakcod_append_debugger_slice(scr_parsetree.cpp APPEND_NODE)
kisakcod_append_debugger_slice(scr_compiler2.cpp COUNT)
kisakcod_append_debugger_slice(scr_evaluate.cpp SCR_COMPILEEXPRESSION)
kisakcod_append_debugger_slice(scr_evaluate.cpp SCR_COMPILEPRIMITIVEEXPRESSION)
kisakcod_append_debugger_slice(scr_evaluate.cpp SCR_COMPILEVARIABLEEXPRESSION)
kisakcod_append_debugger_slice(scr_evaluate.cpp SCR_COMPILEPRIMITIVEEXPRESSIONFIELDOBJECT)
kisakcod_append_debugger_slice(scr_evaluate.cpp SCR_COMPILEPRIMITIVEEXPRESSIONLIST)
kisakcod_append_debugger_slice(scr_evaluate.cpp SCR_COMPILECALLEXPRESSION)
kisakcod_append_debugger_slice(scr_evaluate.cpp SCR_GETBUILTIN)
kisakcod_append_debugger_slice(scr_evaluate.cpp SCR_COMPILEFUNCTION)
kisakcod_append_debugger_slice(scr_evaluate.cpp SCR_COMPILECALLEXPRESSIONLIST)
kisakcod_append_debugger_slice(scr_evaluate.cpp SCR_COMPILEMETHOD)
kisakcod_append_debugger_slice(scr_evaluate.cpp SCR_GETVALUE)
kisakcod_append_debugger_slice(scr_evaluate.cpp SCR_PREEVALBUILTIN)
kisakcod_append_debugger_slice(scr_evaluate.cpp SCR_POSTEVALBUILTIN)
kisakcod_append_debugger_slice(scr_evaluate.cpp SCR_EVALFUNCTION)
kisakcod_append_debugger_slice(scr_evaluate.cpp SCR_EVALMETHOD)
kisakcod_append_debugger_slice(scr_variable.cpp ENTITY_FIELD)
kisakcod_append_debugger_slice(scr_variable.cpp EQUALITY)
kisakcod_append_debugger_slice(scr_debugger.cpp WATCH_EQUAL)
kisakcod_append_debugger_slice(scr_debugger.cpp SORT_CHILDREN)
kisakcod_append_debugger_slice(../ui/ui_component.cpp WATCH_POST)
kisakcod_append_debugger_slice(scr_compiler2.cpp BUILTIN_TYPES)
kisakcod_append_debugger_slice(scr_compiler2.cpp CACHE_TYPE)
kisakcod_append_debugger_slice(scr_compiler2.cpp UNCACHE_TYPE)
kisakcod_append_debugger_slice(scr_compiler2.cpp METHOD_CACHE)
kisakcod_append_debugger_slice(../ui/ui_component.cpp WATCH_CHILD_ARRAYS)
kisakcod_append_debugger_slice(../ui/ui_component.cpp CALLSTACK_UPDATE)
kisakcod_append_debugger_slice(../ui/ui_component.cpp SCRIPT_LIST_ADD)
kisakcod_append_debugger_slice(../ui/ui_component.cpp SCRIPT_LIST_DELETE)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/script_debugger_slice.inc" "${_debugger_slice}")
add_executable(kisakcod-script-debugger-pointer-tests script_debugger_pointer_test.cpp)
target_include_directories(kisakcod-script-debugger-pointer-tests PRIVATE ${SRC_DIR} ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_definitions(kisakcod-script-debugger-pointer-tests PRIVATE KISAK_MP KISAK_DEDI_HEADLESS=1)
target_compile_features(kisakcod-script-debugger-pointer-tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(kisakcod-script-debugger-pointer-tests PRIVATE /W0)
else()
    target_compile_options(kisakcod-script-debugger-pointer-tests PRIVATE -w)
endif()
set_target_properties(kisakcod-script-debugger-pointer-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME script-debugger-pointer-contracts COMMAND kisakcod-script-debugger-pointer-tests)

kisakcod_ilp32(kisakcod-script-memorytree-try-tests
    script-memorytree-try-contracts)

kisakcod_ilp32(kisakcod-script-string-atomic-tests
    script-string-packed-atomic-contracts)

kisakcod_ilp32(kisakcod-script-string-ownership-tests
    script-string-report-free-ownership-contracts)
