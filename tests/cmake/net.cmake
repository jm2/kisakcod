# Network wire-format, netchan and download tests.
# Included from tests/CMakeLists.txt.

add_executable(kisakcod-huffman-tests
    huffman_tests.cpp
    ${SRC_DIR}/qcommon/huffman.cpp
)
target_include_directories(kisakcod-huffman-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-huffman-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-huffman-tests)
set_target_properties(kisakcod-huffman-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(NAME huffman-bounds-and-pointer-width COMMAND kisakcod-huffman-tests)

add_executable(kisakcod-server-file-compare-tests
    server_file_compare_tests.cpp
)
target_include_directories(
    kisakcod-server-file-compare-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-server-file-compare-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-server-file-compare-tests)
set_target_properties(kisakcod-server-file-compare-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME qcommon-server-file-compare
    COMMAND kisakcod-server-file-compare-tests
)

add_executable(kisakcod-dl-http-tests
    dl_http_tests.cpp
    dl_http_head_parse_tests.cpp
    ${SRC_DIR}/qcommon/dl_http.cpp
    ${SRC_DIR}/qcommon/dl_http_parse.cpp
    ${SRC_DIR}/qcommon/dl_http_url.cpp
)
target_include_directories(kisakcod-dl-http-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-dl-http-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-dl-http-tests)
set_target_properties(kisakcod-dl-http-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME dl-http-protocol-contracts
    COMMAND kisakcod-dl-http-tests
)
set_tests_properties(dl-http-protocol-contracts PROPERTIES TIMEOUT 30)

add_executable(kisakcod-net-wire-contract-tests
    net_wire_contract_tests.cpp
    # Commercial-reference fixture subsystem (issue #127 / ki-dyqxl):
    # manifest + explicit variable capture fields + masked byte-compare, the
    # production-codec round-trip contracts, and the fail-closed
    # capture-certification gate.
    net_capture_fixtures.cpp
    net_capture_fixture_tests.cpp
    net_capture_certification.cpp
    # The PRODUCTION MP bit codec (issue #127: this target previously linked
    # only its own test TU, so no wire behavior was exercised at all). Same
    # production source set the msg-wire-contract target links; msg_mp.cpp
    # itself stays out (ILP32-pinned game/server header chain, see the
    # net-chan production objects below).
    ${SRC_DIR}/qcommon/msg_bits_mp.cpp
    ${SRC_DIR}/qcommon/msg_bits_write_mp.cpp
    ${SRC_DIR}/qcommon/msg_bits_read_mp.cpp
    ${SRC_DIR}/qcommon/msg_bits_usercmd_mp.cpp
    ${SRC_DIR}/qcommon/huffman.cpp
)
# The legacy engine headers (q_shared.h / qcommon.h) and the decompiled
# production TUs are not -Wall -Wextra clean. SYSTEM keeps the strict warning
# set (applied per-source below, mirroring the msg-wire-contract target) on
# this test's own code while not failing the build on warnings originating
# inside those headers or production sources.
target_include_directories(kisakcod-net-wire-contract-tests SYSTEM PRIVATE ${SRC_DIR} ${DEPS_DIR})
target_compile_features(kisakcod-net-wire-contract-tests PRIVATE cxx_std_20)
target_compile_definitions(kisakcod-net-wire-contract-tests PRIVATE
    KISAK_MP
    # In-tree reference-evidence root for the certification gate; the env
    # var KISAKCOD_NETCAPTURES_DIR overrides for operator captures.
    "KISAKCOD_NETCAPTURES_IN_TREE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/fixtures/netcaptures\""
)
if (MSVC)
    set_source_files_properties(net_wire_contract_tests.cpp
        net_capture_fixtures.cpp
        net_capture_fixture_tests.cpp
        net_capture_certification.cpp PROPERTIES
        COMPILE_OPTIONS "/W4;/WX")
else()
    set_source_files_properties(net_wire_contract_tests.cpp
        net_capture_fixtures.cpp
        net_capture_fixture_tests.cpp
        net_capture_certification.cpp PROPERTIES
        COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Werror")
endif()
set_target_properties(kisakcod-net-wire-contract-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME net-wire-format-contracts
    COMMAND kisakcod-net-wire-contract-tests
)
set_tests_properties(net-wire-format-contracts PROPERTIES TIMEOUT 60)
# Fail-closed commercial-reference certification gate (#122/#127). With no
# reference captures on disk (the default state of a fresh checkout) the
# binary reports its blockers on stderr and exits 77, which SKIP_RETURN_CODE
# renders as a named "Not Run" -- missing evidence must never look like a
# pass or a silent skip. When an operator records the commercial 1.7 / 1.8
# captures, the same entry byte-verifies them and turns drift into a hard
# failure. See tests/fixtures/netcaptures/README.md.
add_test(
    NAME net-capture-certification
    COMMAND kisakcod-net-wire-contract-tests capture-certification
)
set_tests_properties(net-capture-certification PROPERTIES
    TIMEOUT 30
    SKIP_RETURN_CODE 77
)

add_executable(kisakcod-net-chan-reassembly-tests
    net_chan_reassembly_tests.cpp
)
# Same treatment as the wire-contract tests: the decompiled engine headers are
# not -Wall -Wextra clean, so SYSTEM keeps the strict warning set on this
# test's own code without failing on warnings inside those headers. The test
# invokes the production Netchan_ReassembledSpanFits from net_chan_mp.h -- the
# exact bounds function Netchan_Process consults before rebuilding a complete
# message in the destination buffer.
target_include_directories(kisakcod-net-chan-reassembly-tests SYSTEM PRIVATE ${SRC_DIR} ${DEPS_DIR})
target_compile_features(kisakcod-net-chan-reassembly-tests PRIVATE cxx_std_20)
target_compile_definitions(kisakcod-net-chan-reassembly-tests PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-net-chan-reassembly-tests)
set_target_properties(kisakcod-net-chan-reassembly-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME net-chan-reassembly-span-contracts
    COMMAND kisakcod-net-chan-reassembly-tests
)

# Stage 2 (issue #123): on the engine's actual ILP32 Win32 target, link the
# production netchan layer into the test and drive Netchan_Process itself
# over wire-format packets (net_chan_process_tests.cpp), with inert stubs for
# the engine environment it does not need (net_chan_process_test_stubs.cpp).
# The engine's game/server header chain carries ILP32 layout assertions
# (EntHandleInfo, pml_t), so these production TUs can only compile where the
# engine itself compiles; portable legs keep the predicate-only shape above.
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    # The production TUs are decompiled engine code and are not -W4 clean, so
    # they compile in a dedicated object library at the engine target's own
    # warning level (/W3 on MSVC). A directory-scoped
    # set_source_files_properties /W3 is applied AFTER each consuming
    # target's own options and therefore leaks into every target compiling
    # these files -- it silently dropped the huffman-wire contract target's
    # /W4 coverage of huffman.cpp (refinery review, Codex finding).
    # Target-level options on the object library keep the downgrade scoped to
    # exactly these seven sources. Scoped to MSVC: msg_mp, huffman and the
    # msg_bits_* TUs also compile under GCC/Clang in the portable
    # huffman-wire/msg-wire-contract targets, where /W3 is not a valid flag.
    # The four msg_bits_*_mp.cpp TUs are build-list enrollments only: the MSG
    # bit primitives (MSG_Init/Write*/Read*/ReadAngle16,
    # GetMinBitCountForNum) live there since the codec was split out of
    # msg_mp.cpp, and net_chan_mp.cpp plus the Netchan_Process driver tests
    # reference them -- without them this Win32-ILP32 link fails with
    # unresolved externals. This mirrors the engine link set, where the split
    # left msg_mp.cpp and msg_bits_*_mp.cpp with disjoint symbol sets.
    add_library(kisakcod-net-chan-production-objects OBJECT
        ${SRC_DIR}/qcommon/net_chan_mp.cpp
        ${SRC_DIR}/qcommon/msg_mp.cpp
        ${SRC_DIR}/qcommon/msg_bits_mp.cpp
        ${SRC_DIR}/qcommon/msg_bits_write_mp.cpp
        ${SRC_DIR}/qcommon/msg_bits_read_mp.cpp
        ${SRC_DIR}/qcommon/msg_bits_usercmd_mp.cpp
        ${SRC_DIR}/qcommon/huffman.cpp
        # NET_StringToAdr resolves non-legacy platform-shaped addresses
        # through the portable socket service, so the selected platform
        # sys_socket.cpp must compile alongside net_chan_mp.obj; without it
        # the Win32 ILP32 leg fails to link (LNK2019 on Sys_SocketResolveHost).
        # The single-platform selection filter above already guarantees
        # exactly one backend per configure.
        ${_platform_socket_sources}
    )
    # Same treatment as the wire-contract tests: the decompiled engine headers
    # are not warning-clean, so SYSTEM keeps the strict warning set on the
    # consumers' own code. PUBLIC so the usage requirements reach the test
    # target through target_link_libraries, mirroring the compilation
    # environment these TUs had when they lived on the shared test target.
    target_include_directories(kisakcod-net-chan-production-objects SYSTEM PUBLIC ${SRC_DIR} ${DEPS_DIR})
    target_compile_features(kisakcod-net-chan-production-objects PUBLIC cxx_std_20)
    target_compile_definitions(kisakcod-net-chan-production-objects PUBLIC
        KISAK_MP
        KISAK_DEDI_HEADLESS
    )
    target_compile_options(kisakcod-net-chan-production-objects PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W3>
    )
    # The enrolled platform socket backend speaks Winsock on this platform;
    # the import library must reach the test executable that links this
    # object library (PUBLIC propagates link dependencies to consumers).
    target_link_libraries(kisakcod-net-chan-production-objects PUBLIC ws2_32)

    target_sources(kisakcod-net-chan-reassembly-tests PRIVATE
        net_chan_process_tests.cpp
        net_chan_process_span_tests.cpp
        net_chan_process_test_stubs.cpp
        # ki-zv15h: netchan-level capture validation (production
        # Netchan_Transmit into the engine loopback queues, captured frames
        # replayed through Netchan_Process) and the fixed-tick paired-channel
        # simulation harness. The harness lives in its own TU
        # (net_chan_fixed_tick_tests.cpp, shared machinery in
        # net_chan_capture_tick_support.h); the original single-TU layout
        # tripped Codacy's per-file length limit.
        net_chan_capture_tick_tests.cpp
        net_chan_fixed_tick_tests.cpp
    )
    target_link_libraries(kisakcod-net-chan-reassembly-tests PRIVATE
        kisakcod-net-chan-production-objects
    )
    target_compile_definitions(kisakcod-net-chan-reassembly-tests PRIVATE
        KISAKCOD_NET_CHAN_PROCESS_TESTS_AVAILABLE
        KISAK_DEDI_HEADLESS
    )
endif()

add_executable(kisakcod-huffman-wire-contract-tests
    huffman_wire_contract_tests.cpp
    ${SRC_DIR}/qcommon/huffman.cpp
)
# Links the production Huffman codec (huffman.cpp) and the shared retail
# msg_hData table, then pins the derived codebook and fixed-input byte output.
# huffman.cpp is decompiled engine code, so SYSTEM keeps the strict warning set
# on the test's own code without failing the build on decompiled headers.
target_include_directories(kisakcod-huffman-wire-contract-tests SYSTEM PRIVATE ${SRC_DIR} ${DEPS_DIR})
target_compile_features(kisakcod-huffman-wire-contract-tests PRIVATE cxx_std_20)
target_compile_definitions(kisakcod-huffman-wire-contract-tests PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-huffman-wire-contract-tests)
set_target_properties(kisakcod-huffman-wire-contract-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME huffman-wire-format-contracts
    COMMAND kisakcod-huffman-wire-contract-tests
)

add_executable(kisakcod-msg-wire-contract-tests
    msg_wire_contract_tests.cpp
    msg_wire_delta_framing_tests.cpp
    ${SRC_DIR}/qcommon/msg_bits_mp.cpp
    ${SRC_DIR}/qcommon/msg_bits_write_mp.cpp
    ${SRC_DIR}/qcommon/msg_bits_read_mp.cpp
    ${SRC_DIR}/qcommon/msg_bits_usercmd_mp.cpp
    ${SRC_DIR}/qcommon/huffman.cpp
)
# Links the production MP bit-codec (the msg_bits_*_mp.cpp TUs, the code
# motion of the MSG bit primitives out of msg_mp.cpp, split by function
# family for the static-analysis file-size budget) together with the Huffman
# codec, so
# every wire golden is checked against the real retail encoder/decoder rather
# than a reimplementation. The two test TUs share the harness and link stubs
# in msg_wire_test_harness.hpp. The engine headers AND the decompiled
# production TUs are not -Wall -Wextra clean (the engine build does not enable
# those flags), so the strict warning set applies to the test's own code only.
target_include_directories(kisakcod-msg-wire-contract-tests SYSTEM PRIVATE ${SRC_DIR} ${DEPS_DIR})
target_compile_features(kisakcod-msg-wire-contract-tests PRIVATE cxx_std_20)
target_compile_definitions(kisakcod-msg-wire-contract-tests PRIVATE KISAK_MP)
if (MSVC)
    set_source_files_properties(msg_wire_contract_tests.cpp
        msg_wire_delta_framing_tests.cpp PROPERTIES
        COMPILE_OPTIONS "/W4;/WX")
else()
    set_source_files_properties(msg_wire_contract_tests.cpp
        msg_wire_delta_framing_tests.cpp PROPERTIES
        COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Werror")
endif()
set_target_properties(kisakcod-msg-wire-contract-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME msg-wire-format-contracts
    COMMAND kisakcod-msg-wire-contract-tests
)

kisakcod_ilp32(kisakcod-huffman-wire-contract-tests
    huffman-wire-format-contracts)

kisakcod_ilp32(kisakcod-msg-wire-contract-tests
    msg-wire-format-contracts)

kisakcod_ilp32(kisakcod-net-chan-reassembly-tests
    net-chan-reassembly-span-contracts)

kisakcod_ilp32(kisakcod-server-file-compare-tests
    qcommon-server-file-compare)
