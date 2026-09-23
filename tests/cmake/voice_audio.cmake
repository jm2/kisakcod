# Voice codec and audio tests.
# Included from tests/CMakeLists.txt.

# ---------------------------------------------------------------------------
# Voice codec parity gates (VOX-1a / VOX-1b; docs/design/CLIENT.md, Voice).
#
# Drives the in-tree Speex 1.1.9 build (src/groupvoice/speex/*.c) through the
# exact production ctl sequences from src/groupvoice/encode.cpp / decode.cpp
# and pins encoder bitstreams byte-exactly (VOX-1a) plus decoder PCM within a
# recorded tolerance (VOX-1b). The production wrappers are not compiled here:
# directsound.h statically asserts the 32-bit Win32 ABI.
set(SPEEX_CODEC_SOURCES
    "${SRC_DIR}/groupvoice/speex/bits.c"
    "${SRC_DIR}/groupvoice/speex/cb_search.c"
    "${SRC_DIR}/groupvoice/speex/exc_10_16_table.c"
    "${SRC_DIR}/groupvoice/speex/exc_10_32_table.c"
    "${SRC_DIR}/groupvoice/speex/exc_20_32_table.c"
    "${SRC_DIR}/groupvoice/speex/exc_5_256_table.c"
    "${SRC_DIR}/groupvoice/speex/exc_5_64_table.c"
    "${SRC_DIR}/groupvoice/speex/exc_8_128_table.c"
    "${SRC_DIR}/groupvoice/speex/filters.c"
    "${SRC_DIR}/groupvoice/speex/gain_table.c"
    "${SRC_DIR}/groupvoice/speex/gain_table_lbr.c"
    "${SRC_DIR}/groupvoice/speex/hexc_10_32_table.c"
    "${SRC_DIR}/groupvoice/speex/hexc_table.c"
    "${SRC_DIR}/groupvoice/speex/high_lsp_tables.c"
    "${SRC_DIR}/groupvoice/speex/jitter.c"
    "${SRC_DIR}/groupvoice/speex/lbr_48k_tables.c"
    "${SRC_DIR}/groupvoice/speex/lpc.c"
    "${SRC_DIR}/groupvoice/speex/lsp.c"
    "${SRC_DIR}/groupvoice/speex/lsp_tables_nb.c"
    "${SRC_DIR}/groupvoice/speex/ltp.c"
    "${SRC_DIR}/groupvoice/speex/math_approx.c"
    "${SRC_DIR}/groupvoice/speex/mdf.c"
    "${SRC_DIR}/groupvoice/speex/misc.c"
    "${SRC_DIR}/groupvoice/speex/modes.c"
    "${SRC_DIR}/groupvoice/speex/nb_celp.c"
    "${SRC_DIR}/groupvoice/speex/preprocess.c"
    "${SRC_DIR}/groupvoice/speex/quant_lsp.c"
    "${SRC_DIR}/groupvoice/speex/sb_celp.c"
    "${SRC_DIR}/groupvoice/speex/smallft.c"
    "${SRC_DIR}/groupvoice/speex/speex.c"
    "${SRC_DIR}/groupvoice/speex/speex_callbacks.c"
    "${SRC_DIR}/groupvoice/speex/speex_header.c"
    "${SRC_DIR}/groupvoice/speex/stereo.c"
    "${SRC_DIR}/groupvoice/speex/vbr.c"
    "${SRC_DIR}/groupvoice/speex/vq.c"
)

# Third-party Speex 1.1.9 is not warning-clean; silence warnings on the codec
# C sources while keeping the strict warning set on the gate's own code.
# speex_config_types.h is generated (autotools substitution) for non-Win32
# targets; the _WIN32 branch of speex_types.h carries its own typedefs.
set(SPEEX_CONFIG_TYPES_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/speex-config-types")
file(MAKE_DIRECTORY "${SPEEX_CONFIG_TYPES_DIR}/speex")
set(SIZE16 short)
set(SIZE32 int)
configure_file(
    "${DEPS_DIR}/speex/speex_config_types.h.in"
    "${SPEEX_CONFIG_TYPES_DIR}/speex/speex_config_types.h"
    @ONLY
)

add_executable(kisakcod-voice-gate-tests
    voice_gate_tests.cpp
    voice_gate_decode_test.cpp
    ${SPEEX_CODEC_SOURCES}
)
target_include_directories(kisakcod-voice-gate-tests PRIVATE
    "${SPEEX_CONFIG_TYPES_DIR}")
target_include_directories(kisakcod-voice-gate-tests SYSTEM PRIVATE
    ${SRC_DIR} ${DEPS_DIR})
target_compile_features(kisakcod-voice-gate-tests PRIVATE cxx_std_20)
if(MSVC)
    set_source_files_properties(${SPEEX_CODEC_SOURCES} PROPERTIES
        COMPILE_OPTIONS "/w")
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/voice_gate_tests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/voice_gate_decode_test.cpp" PROPERTIES
        COMPILE_OPTIONS "/W4;/WX")
else()
    set_source_files_properties(${SPEEX_CODEC_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-w")
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/voice_gate_tests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/voice_gate_decode_test.cpp" PROPERTIES
        COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Werror")
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(kisakcod-voice-gate-tests PRIVATE m)
endif()
set_target_properties(kisakcod-voice-gate-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(
    NAME voice-codec-gate-contracts
    COMMAND kisakcod-voice-gate-tests
)
set_tests_properties(voice-codec-gate-contracts PROPERTIES TIMEOUT 120)

kisakcod_ilp32(kisakcod-voice-gate-tests
    voice-codec-gate-contracts)
