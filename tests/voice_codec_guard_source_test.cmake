cmake_minimum_required(VERSION 3.16)

# Voice codec substitution guard (VOX-7) and conditional codec identity
# (DEP-3 companion), docs/AUDIO_VOICE_CINEMATIC_PARITY_GATES.md.
#
# VOX-7 requires that the commercial voice path is never re-coded onto a
# different codec or framing: no Opus or new voice protocol, wire bytes for
# valid voice unchanged. The byte-exact wire evidence lives in the runtime
# codec goldens (tests/voice_gate_tests.cpp: VOX-1a bitstreams) and the
# statement-level framing pins (tests/voice_framing_source_test.cmake); this
# contract is the missing GUARD half: a substitution tripwire over the
# production voice wrappers, the codec-gate build surface and the vendored
# Speex snapshot itself.
#
# DEP-3 preserves the in-tree Speex 1.1.9 build conditional on recorded
# original-commercial 1.7 / Steam 1.8 reference evidence (#122): the version
# identity, the deterministic synthesis LCG (ki-dkeb CWE-327 repair) and the
# snapshot file set are pinned here so any codec change is detected and must
# go through the recorded reference-evidence process. This guard does NOT
# certify interoperation with the original commercial binaries (see §6 of the
# gates document); it only makes silent substitution detectable.
#
# Review-hardened build-input identity (2026-09 rework): name-set and count
# checks alone accept same-directory substitution and content-only edits, so
# this guard additionally pins
#   - the ORDERED entries of the test codec-gate build list
#     (tests/CMakeLists.txt set(SPEEX_CODEC_SOURCES ...), 35 entries),
#   - the ORDERED entries of the production build lists
#     (scripts/mp/mp_files.cmake GROUPVOICE and GROUPVOICE_SPEEX) together
#     with their consumption in scripts/mp/CMakeLists.txt, and
#   - the per-file SHA-256 of every vendored snapshot file and of the whole
#     deps/speex public surface (content-only drift trips the guard).
# Nine CONTRACT_MUTATION negative controls cover wrapper substitution, list
# removal, same-directory entry replacement in both the test and production
# lists, LCG/version drift, snapshot content mutation and a deps/speex
# surface file-set addition.

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

function(read_normalized RELATIVE_PATH OUT_VARIABLE)
    set(_path "${SOURCE_ROOT}/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing voice codec guard source: ${_path}")
    endif()
    file(READ "${_path}" _source)
    string(REGEX REPLACE "[ \t\r\n]+" " " _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(require_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing voice codec guard invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden voice codec substitution (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_count SOURCE_VARIABLE NEEDLE EXPECTED_COUNT DESCRIPTION)
    set(_remaining "${${SOURCE_VARIABLE}}")
    string(LENGTH "${NEEDLE}" _needle_length)
    if(_needle_length EQUAL 0)
        message(FATAL_ERROR "Empty voice codec guard count needle (${DESCRIPTION})")
    endif()
    set(_count 0)
    while(TRUE)
        string(FIND "${_remaining}" "${NEEDLE}" _position)
        if(_position EQUAL -1)
            break()
        endif()
        math(EXPR _count "${_count} + 1")
        math(EXPR _next "${_position} + ${_needle_length}")
        string(SUBSTRING "${_remaining}" ${_next} -1 _remaining)
    endwhile()
    if(NOT _count EQUAL EXPECTED_COUNT)
        message(FATAL_ERROR
            "Unexpected voice codec guard invariant count (${DESCRIPTION}): "
            "expected ${EXPECTED_COUNT}, found ${_count}")
    endif()
endfunction()

# Ordered-manifest helpers. Regex and LIST membership cannot see entry
# position or content, so the build lists are extracted with plain
# string(FIND)/string(SUBSTRING) scanning and compared entry by entry.
function(extract_set_block SOURCE_VARIABLE SET_NAME OUT_VARIABLE)
    set(_needle "set(${SET_NAME} ")
    string(FIND "${${SOURCE_VARIABLE}}" "${_needle}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR
            "Missing voice codec guard build input (${SET_NAME} not declared)")
    endif()
    string(SUBSTRING "${${SOURCE_VARIABLE}}" ${_start} -1 _tail)
    string(FIND "${_tail}" ")" _close)
    if(_close EQUAL -1)
        message(FATAL_ERROR
            "Unterminated voice codec guard build input (${SET_NAME})")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_close} _block)
    set(${OUT_VARIABLE} "${_block}" PARENT_SCOPE)
endfunction()

function(extract_quoted_entries BLOCK_TEXT ENTRY_PREFIX OUT_VARIABLE)
    set(_entries "")
    set(_remaining "${BLOCK_TEXT}")
    set(_needle "\"${ENTRY_PREFIX}")
    while(TRUE)
        string(FIND "${_remaining}" "${_needle}" _start)
        if(_start EQUAL -1)
            break()
        endif()
        math(EXPR _body_start "${_start} + 1")
        string(SUBSTRING "${_remaining}" ${_body_start} -1 _tail)
        string(FIND "${_tail}" "\"" _end)
        if(_end EQUAL -1)
            message(FATAL_ERROR
                "Unterminated voice codec guard build input entry: "
                "'${ENTRY_PREFIX}...'")
        endif()
        string(SUBSTRING "${_tail}" 0 ${_end} _entry)
        list(APPEND _entries "\"${_entry}\"")
        math(EXPR _next "${_end} + 1")
        string(SUBSTRING "${_tail}" ${_next} -1 _remaining)
    endwhile()
    set(${OUT_VARIABLE} "${_entries}" PARENT_SCOPE)
endfunction()

function(require_ordered_manifest ACTUAL_VARIABLE EXPECTED_VARIABLE DESCRIPTION)
    list(LENGTH ${ACTUAL_VARIABLE} _actual_count)
    list(LENGTH ${EXPECTED_VARIABLE} _expected_count)
    if(NOT _actual_count EQUAL _expected_count)
        message(FATAL_ERROR
            "Voice codec guard build-input identity drifted (${DESCRIPTION}): "
            "expected ${_expected_count} ordered entries, found "
            "${_actual_count}")
    endif()
    set(_index 0)
    foreach(_expected_entry IN LISTS ${EXPECTED_VARIABLE})
        list(GET ${ACTUAL_VARIABLE} ${_index} _actual_entry)
        if(NOT _actual_entry STREQUAL _expected_entry)
            message(FATAL_ERROR
                "Voice codec guard build-input identity drifted "
                "(${DESCRIPTION}): entry ${_index} is ${_actual_entry}, "
                "expected ${_expected_entry}")
        endif()
        math(EXPR _index "${_index} + 1")
    endforeach()
endfunction()

function(require_pinned_sha256 RELATIVE_DIRECTORY FILE_LIST_VARIABLE
         HASH_VARIABLE_PREFIX DESCRIPTION)
    foreach(_name IN LISTS ${FILE_LIST_VARIABLE})
        string(REPLACE "." "_" _hash_variable "${HASH_VARIABLE_PREFIX}${_name}")
        if(NOT DEFINED ${_hash_variable})
            message(FATAL_ERROR
                "Voice codec guard content pin missing (${DESCRIPTION}): "
                "${_hash_variable}")
        endif()
        set(_pinned_path "${SOURCE_ROOT}/${RELATIVE_DIRECTORY}/${_name}")
        if(NOT EXISTS "${_pinned_path}")
            message(FATAL_ERROR
                "Pinned voice codec content missing (${DESCRIPTION}): "
                "${_pinned_path}")
        endif()
        file(SHA256 "${_pinned_path}" _actual_hash)
        if(NOT "${_actual_hash}" STREQUAL "${${_hash_variable}}")
            message(FATAL_ERROR
                "Pinned voice codec content drifted (${DESCRIPTION}): "
                "${RELATIVE_DIRECTORY}/${_name}")
        endif()
    endforeach()
endfunction()

# Substitution needles for the production voice wrapper layer (not the
# vendored snapshot: its own heritage comments legitimately name other
# codecs, e.g. smallft.c's Vorbis lineage).
set(_WRAPPER_SUBSTITUTION_NEEDLES
    "Opus" "opus" "OPUS"
    "ilbc" "iLBC"
    "g711"
    "vorbis" "Vorbis"
    "silk_"
    "gsm_"
    "celt_")

read_normalized("src/groupvoice/encode.cpp" _encode)
read_normalized("src/groupvoice/decode.cpp" _decode)
read_normalized("src/groupvoice/directsound.h" _directsound)
read_normalized("src/win32/win_voice.cpp" _winvoice)
read_normalized("src/groupvoice/play_dsound.cpp" _playdsound)
read_normalized("src/groupvoice/record_dsound.cpp" _recorddsound)
read_normalized("tests/CMakeLists.txt" _testscmake)
read_normalized("CMakeLists.txt" _rootcmake)
read_normalized("src/groupvoice/speex/misc.h" _misch)
read_normalized("src/groupvoice/speex/misc.c" _miscc)
read_normalized("deps/speex/readme.txt" _speexreadme)
read_normalized("scripts/mp/mp_files.cmake" _mpfiles)
read_normalized("scripts/mp/CMakeLists.txt" _mpcmake)

# --- VOX-7: wrapper-layer codec exclusivity ----------------------------------
foreach(_wrapper IN ITEMS _encode _decode _directsound _winvoice _playdsound _recorddsound)
    foreach(_needle IN LISTS _WRAPPER_SUBSTITUTION_NEEDLES)
        forbid_contains(${_wrapper} "${_needle}"
            "production voice wrapper carries a foreign codec reference")
    endforeach()
endforeach()
require_count(_encode "#include <speex/speex.h>" 1
    "Encode_Sample speaks to the vendored Speex and nothing else")
require_count(_decode "#include <speex/speex.h>" 1
    "Decode_Sample speaks to the vendored Speex and nothing else")

# --- VOX-7: build-surface guard ----------------------------------------------
require_count(_testscmake "set(SPEEX_CODEC_SOURCES" 1
    "the codec gate's vendored source list is declared exactly once")
require_count(_testscmake "\"\${SRC_DIR}/groupvoice/speex/"
    35
    "the codec gate links exactly the pinned 35-file Speex 1.1.9 build")
foreach(_cmake IN ITEMS _testscmake _rootcmake)
    foreach(_needle IN LISTS _WRAPPER_SUBSTITUTION_NEEDLES)
        forbid_contains(${_cmake} "${_needle}"
            "build surface carries a foreign codec reference")
    endforeach()
endforeach()

# --- DEP-3: vendored snapshot identity ---------------------------------------
require_count(_misch "#define SPEEX_VERSION \"speex-1.1.9\"" 1
    "vendored snapshot version identity is pinned (DEP-3 conditional preservation)")
require_count(_miscc "static spx_uint32_t speex_rand_state = 1u;" 1
    "deterministic synthesis LCG state (ki-dkeb CWE-327 repair) is pinned")
require_count(_miscc "*state = *state * 1103515245u + 12345u;" 1
    "fixed-point LCG body is pinned (no libc randomness regression)")
forbid_contains(_miscc "srand("
    "libc seeding must not re-enter decoder synthesis")
forbid_contains(_miscc " = rand("
    "libc rand() must not re-enter decoder synthesis")
require_contains(_speexreadme
    "This is an include path for the groupvoice/ encoders"
    "deps/speex provenance marker is pinned")

# Snapshot file-set identity: an ADDED or REMOVED file in the vendored
# snapshot is a codec change and must trip this guard. The pinned set below
# is the complete src/groupvoice/speex/ listing at the guarded SHA.
set(_SPEEX_SNAPSHOT_FILES
    Makefile.am Makefile.in arch.h bits.c cb_search.c cb_search.h
    cb_search_arm4.h cb_search_sse.h exc_10_16_table.c exc_10_32_table.c
    exc_20_32_table.c exc_5_256_table.c exc_5_64_table.c exc_8_128_table.c
    filters.c filters.h filters_arm4.h filters_sse.h fixed_arm4.h
    fixed_arm5e.h fixed_debug.h fixed_generic.h gain_table.c
    gain_table_lbr.c hexc_10_32_table.c hexc_table.c high_lsp_tables.c
    jitter.c lbr_48k_tables.c lpc.c lpc.h lsp.c lsp.h lsp_tables_nb.c
    ltp.c ltp.h ltp_arm4.h ltp_sse.h math_approx.c math_approx.h mdf.c
    misc.c misc.h modes.c modes.h nb_celp.c nb_celp.h preprocess.c
    quant_lsp.c quant_lsp.h sb_celp.c sb_celp.h smallft.c smallft.h
    speex.c speex_callbacks.c speex_header.c stack_alloc.h stereo.c vbr.c
    vbr.h vq.c vq.h vq_arm4.h vq_sse.h)
file(GLOB _snapshot_entries LIST_DIRECTORIES FALSE
    "${SOURCE_ROOT}/src/groupvoice/speex/*")
list(LENGTH _snapshot_entries _snapshot_count)
list(LENGTH _SPEEX_SNAPSHOT_FILES _pinned_count)
if(NOT _snapshot_count EQUAL _pinned_count)
    message(FATAL_ERROR
        "Vendored Speex snapshot file count drifted (DEP-3): "
        "expected ${_pinned_count}, found ${_snapshot_count}")
endif()
foreach(_entry IN LISTS _snapshot_entries)
    get_filename_component(_name "${_entry}" NAME)
    if(NOT _name IN_LIST _SPEEX_SNAPSHOT_FILES)
        message(FATAL_ERROR
            "Unrecognized file in the vendored Speex snapshot (DEP-3): ${_name}")
    endif()
endforeach()

# Expected ordered manifest of the test codec-gate build list
# (tests/CMakeLists.txt set(SPEEX_CODEC_SOURCES ...), 35 entries).
set(_EXPECTED_SPEEX_CODEC_SOURCES
    "\"\${SRC_DIR}/groupvoice/speex/bits.c\""
    "\"\${SRC_DIR}/groupvoice/speex/cb_search.c\""
    "\"\${SRC_DIR}/groupvoice/speex/exc_10_16_table.c\""
    "\"\${SRC_DIR}/groupvoice/speex/exc_10_32_table.c\""
    "\"\${SRC_DIR}/groupvoice/speex/exc_20_32_table.c\""
    "\"\${SRC_DIR}/groupvoice/speex/exc_5_256_table.c\""
    "\"\${SRC_DIR}/groupvoice/speex/exc_5_64_table.c\""
    "\"\${SRC_DIR}/groupvoice/speex/exc_8_128_table.c\""
    "\"\${SRC_DIR}/groupvoice/speex/filters.c\""
    "\"\${SRC_DIR}/groupvoice/speex/gain_table.c\""
    "\"\${SRC_DIR}/groupvoice/speex/gain_table_lbr.c\""
    "\"\${SRC_DIR}/groupvoice/speex/hexc_10_32_table.c\""
    "\"\${SRC_DIR}/groupvoice/speex/hexc_table.c\""
    "\"\${SRC_DIR}/groupvoice/speex/high_lsp_tables.c\""
    "\"\${SRC_DIR}/groupvoice/speex/jitter.c\""
    "\"\${SRC_DIR}/groupvoice/speex/lbr_48k_tables.c\""
    "\"\${SRC_DIR}/groupvoice/speex/lpc.c\""
    "\"\${SRC_DIR}/groupvoice/speex/lsp.c\""
    "\"\${SRC_DIR}/groupvoice/speex/lsp_tables_nb.c\""
    "\"\${SRC_DIR}/groupvoice/speex/ltp.c\""
    "\"\${SRC_DIR}/groupvoice/speex/math_approx.c\""
    "\"\${SRC_DIR}/groupvoice/speex/mdf.c\""
    "\"\${SRC_DIR}/groupvoice/speex/misc.c\""
    "\"\${SRC_DIR}/groupvoice/speex/modes.c\""
    "\"\${SRC_DIR}/groupvoice/speex/nb_celp.c\""
    "\"\${SRC_DIR}/groupvoice/speex/preprocess.c\""
    "\"\${SRC_DIR}/groupvoice/speex/quant_lsp.c\""
    "\"\${SRC_DIR}/groupvoice/speex/sb_celp.c\""
    "\"\${SRC_DIR}/groupvoice/speex/smallft.c\""
    "\"\${SRC_DIR}/groupvoice/speex/speex.c\""
    "\"\${SRC_DIR}/groupvoice/speex/speex_callbacks.c\""
    "\"\${SRC_DIR}/groupvoice/speex/speex_header.c\""
    "\"\${SRC_DIR}/groupvoice/speex/stereo.c\""
    "\"\${SRC_DIR}/groupvoice/speex/vbr.c\""
    "\"\${SRC_DIR}/groupvoice/speex/vq.c\""
)
set(_EXPECTED_GROUPVOICE
    "\"\${SRC_DIR}/groupvoice/decode.cpp\""
    "\"\${SRC_DIR}/groupvoice/directsound.h\""
    "\"\${SRC_DIR}/groupvoice/encode.cpp\""
    "\"\${SRC_DIR}/groupvoice/play_dsound.cpp\""
    "\"\${SRC_DIR}/groupvoice/record_dsound.cpp\""
)
set(_EXPECTED_GROUPVOICE_SPEEX
    "\"\${SRC_DIR}/groupvoice/speex/arch.h\""
    "\"\${SRC_DIR}/groupvoice/speex/bits.c\""
    "\"\${SRC_DIR}/groupvoice/speex/cb_search.c\""
    "\"\${SRC_DIR}/groupvoice/speex/cb_search.h\""
    "\"\${SRC_DIR}/groupvoice/speex/cb_search_arm4.h\""
    "\"\${SRC_DIR}/groupvoice/speex/cb_search_sse.h\""
    "\"\${SRC_DIR}/groupvoice/speex/exc_10_16_table.c\""
    "\"\${SRC_DIR}/groupvoice/speex/exc_10_32_table.c\""
    "\"\${SRC_DIR}/groupvoice/speex/exc_20_32_table.c\""
    "\"\${SRC_DIR}/groupvoice/speex/exc_5_256_table.c\""
    "\"\${SRC_DIR}/groupvoice/speex/exc_5_64_table.c\""
    "\"\${SRC_DIR}/groupvoice/speex/exc_8_128_table.c\""
    "\"\${SRC_DIR}/groupvoice/speex/filters.c\""
    "\"\${SRC_DIR}/groupvoice/speex/filters.h\""
    "\"\${SRC_DIR}/groupvoice/speex/filters_arm4.h\""
    "\"\${SRC_DIR}/groupvoice/speex/filters_sse.h\""
    "\"\${SRC_DIR}/groupvoice/speex/fixed_arm4.h\""
    "\"\${SRC_DIR}/groupvoice/speex/fixed_arm5e.h\""
    "\"\${SRC_DIR}/groupvoice/speex/fixed_debug.h\""
    "\"\${SRC_DIR}/groupvoice/speex/fixed_generic.h\""
    "\"\${SRC_DIR}/groupvoice/speex/gain_table.c\""
    "\"\${SRC_DIR}/groupvoice/speex/gain_table_lbr.c\""
    "\"\${SRC_DIR}/groupvoice/speex/hexc_10_32_table.c\""
    "\"\${SRC_DIR}/groupvoice/speex/hexc_table.c\""
    "\"\${SRC_DIR}/groupvoice/speex/high_lsp_tables.c\""
    "\"\${SRC_DIR}/groupvoice/speex/jitter.c\""
    "\"\${SRC_DIR}/groupvoice/speex/lbr_48k_tables.c\""
    "\"\${SRC_DIR}/groupvoice/speex/lpc.c\""
    "\"\${SRC_DIR}/groupvoice/speex/lpc.h\""
    "\"\${SRC_DIR}/groupvoice/speex/lsp.c\""
    "\"\${SRC_DIR}/groupvoice/speex/lsp.h\""
    "\"\${SRC_DIR}/groupvoice/speex/lsp_tables_nb.c\""
    "\"\${SRC_DIR}/groupvoice/speex/ltp.c\""
    "\"\${SRC_DIR}/groupvoice/speex/ltp.h\""
    "\"\${SRC_DIR}/groupvoice/speex/ltp_arm4.h\""
    "\"\${SRC_DIR}/groupvoice/speex/ltp_sse.h\""
    "\"\${SRC_DIR}/groupvoice/speex/math_approx.c\""
    "\"\${SRC_DIR}/groupvoice/speex/math_approx.h\""
    "\"\${SRC_DIR}/groupvoice/speex/mdf.c\""
    "\"\${SRC_DIR}/groupvoice/speex/misc.c\""
    "\"\${SRC_DIR}/groupvoice/speex/misc.h\""
    "\"\${SRC_DIR}/groupvoice/speex/modes.c\""
    "\"\${SRC_DIR}/groupvoice/speex/modes.h\""
    "\"\${SRC_DIR}/groupvoice/speex/nb_celp.c\""
    "\"\${SRC_DIR}/groupvoice/speex/nb_celp.h\""
    "\"\${SRC_DIR}/groupvoice/speex/preprocess.c\""
    "\"\${SRC_DIR}/groupvoice/speex/quant_lsp.c\""
    "\"\${SRC_DIR}/groupvoice/speex/quant_lsp.h\""
    "\"\${SRC_DIR}/groupvoice/speex/sb_celp.c\""
    "\"\${SRC_DIR}/groupvoice/speex/sb_celp.h\""
    "\"\${SRC_DIR}/groupvoice/speex/smallft.c\""
    "\"\${SRC_DIR}/groupvoice/speex/smallft.h\""
    "\"\${SRC_DIR}/groupvoice/speex/speex.c\""
    "\"\${SRC_DIR}/groupvoice/speex/speex_callbacks.c\""
    "\"\${SRC_DIR}/groupvoice/speex/speex_header.c\""
    "\"\${SRC_DIR}/groupvoice/speex/stack_alloc.h\""
    "\"\${SRC_DIR}/groupvoice/speex/stereo.c\""
    "\"\${SRC_DIR}/groupvoice/speex/vbr.c\""
    "\"\${SRC_DIR}/groupvoice/speex/vbr.h\""
    "\"\${SRC_DIR}/groupvoice/speex/vq.c\""
    "\"\${SRC_DIR}/groupvoice/speex/vq.h\""
    "\"\${SRC_DIR}/groupvoice/speex/vq_arm4.h\""
    "\"\${SRC_DIR}/groupvoice/speex/vq_sse.h\""
)
set(_SPEEX_DEPS_SURFACE_FILES
    readme.txt
    speex.h
    speex_bits.h
    speex_callbacks.h
    speex_config_types.h.in
    speex_echo.h
    speex_header.h
    speex_jitter.h
    speex_preprocess.h
    speex_stereo.h
    speex_types.h
)
# Pinned per-file SHA-256 of the vendored Speex snapshot
# (src/groupvoice/speex/, DEP-3 conditional-preservation checksum).
set(_SNAPSHOT_SHA256_Makefile_am "29d8e643fac2d0fecace998637b7416bec6d743168be2fb527fddd824e43c30f")
set(_SNAPSHOT_SHA256_Makefile_in "c88bf438c68a952f19c887f2e4765a83c36ed8c97a06c12985200a43dea866fa")
set(_SNAPSHOT_SHA256_arch_h "4cc91e0f4dfd200aa0941bdffacd55d9f77dcacfa209b99df806e935f849d279")
set(_SNAPSHOT_SHA256_bits_c "65d2da8efe6e964f9d4a3681e3ff76b3918888af6e750376efdda996f0cfb38f")
set(_SNAPSHOT_SHA256_cb_search_c "a6ed6f23dd2fc41af9e45286509529858f50056c95d528be6d26e894bb397c59")
set(_SNAPSHOT_SHA256_cb_search_h "33e78e805e3dc8a778b341ce55209a318006ef78c0d7d08866aac26161ef333d")
set(_SNAPSHOT_SHA256_cb_search_arm4_h "4f8f6df738243a9765dc3a0519d642c02dcde954baa1afc59ed5e9643fa45267")
set(_SNAPSHOT_SHA256_cb_search_sse_h "fb5ad5e0df6f3cb286fbdb744e998705da532a825aa502b45b78ff989280fc48")
set(_SNAPSHOT_SHA256_exc_10_16_table_c "46d434b36a0d87b41cfbfef3344ba598cffe0d8519bcd124b287a1c1a2c12b33")
set(_SNAPSHOT_SHA256_exc_10_32_table_c "64de2fd2c5140677d5b752e3b62b8e1e853469d7ee9fcf752c8bf5f25bb72fea")
set(_SNAPSHOT_SHA256_exc_20_32_table_c "92d5ffa23a7e1e4d0f4a93dbf4779bab85d68069e8e803c52338da7ac2a1f020")
set(_SNAPSHOT_SHA256_exc_5_256_table_c "e11023d85ea3988dc0232d60d187b9cdfbcc43f0a57bc60444bc063ab1dd385c")
set(_SNAPSHOT_SHA256_exc_5_64_table_c "9549d931ad6ee106946db8a6c73c3109bf97f0e7ec17e2de0f1164eb6b85f238")
set(_SNAPSHOT_SHA256_exc_8_128_table_c "ab08b7193b7d57bf0103b74a223d23ff1a211cc401743f9a95c92b59744f4f74")
set(_SNAPSHOT_SHA256_filters_c "c010905e59db62db573e3cc0599640ae5acaf0291e31c575dec24139f1a22601")
set(_SNAPSHOT_SHA256_filters_h "86270654c8708a023cc1e23221dba63ebc43526dd786a8b98c257fbfae516c7c")
set(_SNAPSHOT_SHA256_filters_arm4_h "731e66f0f71bd1a2fbce49c9edd5606d661d73993663f8798a9f472bce21e91c")
set(_SNAPSHOT_SHA256_filters_sse_h "49ba336c779291e2384511fb710c0e9f7756e73d0066fb46f4665e8b78c6df3e")
set(_SNAPSHOT_SHA256_fixed_arm4_h "bada5586607d5ad07dfe7f14993d9a54f636666c43f601370ef554b5cb793c0b")
set(_SNAPSHOT_SHA256_fixed_arm5e_h "50a6c62ca186dc06ae0d0a70426b7f5458e1d71c16f7ac887ab8311a227ebdf4")
set(_SNAPSHOT_SHA256_fixed_debug_h "98e5ee771e4cc665dada7dda803d690b552c9ec5daf0345d4a4a29faf9d76892")
set(_SNAPSHOT_SHA256_fixed_generic_h "1909a07d1d89b12fdaaeec2392b6016071ca0e9b86ae7171f329eac1b3fe6b31")
set(_SNAPSHOT_SHA256_gain_table_c "fbd4d29e5f6800ebb07965ca086d8dce07eaf73b79b3425a0688a872bf25be84")
set(_SNAPSHOT_SHA256_gain_table_lbr_c "004703d17b61526b342078ec2ec0bf556fb58e3ed64c2ae39982ffc453d7e2fe")
set(_SNAPSHOT_SHA256_hexc_10_32_table_c "b3387ee60a1891589526968ebf49a0cc5d18f2ffa4145756b415411395935e08")
set(_SNAPSHOT_SHA256_hexc_table_c "63a8e47a5ebd9063252ff21f91b39280a34c2edf8caaf34105592bb125159b8d")
set(_SNAPSHOT_SHA256_high_lsp_tables_c "6b42d936b9701eda568313a54e794c5a7a6959631e49ac0d7a0e790b202ed55d")
set(_SNAPSHOT_SHA256_jitter_c "aaf220b861a77fe7dbbba9eefb09a4a4febc8da2c7c16a9c8baa0714df54ef6b")
set(_SNAPSHOT_SHA256_lbr_48k_tables_c "8d3b2226328db63ad46bae9ab4deee769292fe438482a1af05a8deef1217cbca")
set(_SNAPSHOT_SHA256_lpc_c "fe06bd1e851acf7ccbf9fa6691905b286ded6eaad0b0fb0b1728f4b00240270f")
set(_SNAPSHOT_SHA256_lpc_h "e886049a301ed11bf1923708e893c4cae126c12ca4d650ec4e992086ec7f0ef4")
set(_SNAPSHOT_SHA256_lsp_c "113aaa779d86979b92e1aa5c5873abf8981f400725c0070effb9178663bc0be8")
set(_SNAPSHOT_SHA256_lsp_h "ccdfccbad648486b25be597c8d843ba2cdf16bf37a916f4a17c80b264bf7b533")
set(_SNAPSHOT_SHA256_lsp_tables_nb_c "719037e6af0229611fd3ee782732030a2922055618a06d506a8476a75d98aa5c")
set(_SNAPSHOT_SHA256_ltp_c "f86d0cb04c5dc84189ff1cd9506bc484104df83f9edf96df4dbcd084077116d5")
set(_SNAPSHOT_SHA256_ltp_h "7868b6d8cdf09e82bf38a6bb94a09deb0477a6b2ffbdbd0918fbc6aafe3c4718")
set(_SNAPSHOT_SHA256_ltp_arm4_h "5cab09f3a963ad48472c6717f3c0af316d408693565d0eaa844f5e1ea4d0bd68")
set(_SNAPSHOT_SHA256_ltp_sse_h "21d8216983e93e3d173ae8a415efe79577ce1daba2c2bd34bf14391170853191")
set(_SNAPSHOT_SHA256_math_approx_c "fdae06c06df5e3708e2e7a82d7b85d9576889656819b89fa60296cd5c59f3670")
set(_SNAPSHOT_SHA256_math_approx_h "75bfdfe01ae1af3323933975baf570e849af8ad44d577c267447340430e57cbd")
set(_SNAPSHOT_SHA256_mdf_c "e0590d71a6abdf37d7118bcb66856b59b455d372285a6dc2d8de500fe790d40a")
set(_SNAPSHOT_SHA256_misc_c "3a35e54001a759f52616b121e130e53d9b4446527f65edfc83535436d18e32e1")
set(_SNAPSHOT_SHA256_misc_h "c5421177cda71cac59cbf8fb0147e87da2160438234db998cff3623767c374ff")
set(_SNAPSHOT_SHA256_modes_c "557ba27154e431131123dbb02a56fcef7f86ed1d5ef13fd97b8cfefc09ff2239")
set(_SNAPSHOT_SHA256_modes_h "251cddacfdbdbbf976e2c92ce86b7d52caf5643d1077b9451be268b734a6e9da")
set(_SNAPSHOT_SHA256_nb_celp_c "53451314c9a4ad1f032c3e52323f2e2e5a0b725349d395f5a774ba0d22d97d62")
set(_SNAPSHOT_SHA256_nb_celp_h "356e80c5d33f1e6e6942e5ad08bf9d1d35b7e545d8041e6a439b17b23203d154")
set(_SNAPSHOT_SHA256_preprocess_c "ce72a1f45b2f5799c81151e8863354c9b167e87dabeefbbbf3c5b3496db10308")
set(_SNAPSHOT_SHA256_quant_lsp_c "cb40eaa3fc839df7b54c126f1b8e1a9a7c1d997da8ead6b51141d92b6847335a")
set(_SNAPSHOT_SHA256_quant_lsp_h "00f948b22eb20bc5f6b5711e55dba45ebcc70c25ebd27354c9adda83c2d13347")
set(_SNAPSHOT_SHA256_sb_celp_c "11d3ca900ea6398972fff073dc6d6dc997e5a1675f62e3dce69a4a912ce7fe4d")
set(_SNAPSHOT_SHA256_sb_celp_h "8f82c4b0628c81e0849c33462029199b3d4cf2e356da687fb3378c33e746fe76")
set(_SNAPSHOT_SHA256_smallft_c "ff97bde913b54fab3e58ba2cd4427be86ddf01d68a397adf12d2edb54bac070c")
set(_SNAPSHOT_SHA256_smallft_h "4399fe4e3fd7359814c713f42d89f883147d5c96cdb23914f48610cdc16efbec")
set(_SNAPSHOT_SHA256_speex_c "562d952961ca413541a095aaf500c138e13b78a34800d9169079feba4479d8ae")
set(_SNAPSHOT_SHA256_speex_callbacks_c "53f4e0c937c6c1467599444deb8047bbec83152e282c4e0b1721f5e6c564737d")
set(_SNAPSHOT_SHA256_speex_header_c "7b72ee4437e707aa1ce05599ab31ec3f9cd639d4cc2c7f55e623e374b52eb70d")
set(_SNAPSHOT_SHA256_stack_alloc_h "595d52cf77bf1b2e3c0e83e052878b1b8fc3bf831d8f0caf6e665b49d77d43e4")
set(_SNAPSHOT_SHA256_stereo_c "1ed6d92af9c68d93a624a8648cdefd9ac6c0c95fd16d2182bd0c669534784bac")
set(_SNAPSHOT_SHA256_vbr_c "61a8fc81d698c6a5444cfbbfb6e23a36c5a03dfe2a1fe4a70fd73eb8bbc194bd")
set(_SNAPSHOT_SHA256_vbr_h "ba79f6f2e5f856fd51f2bc734bd4af5006a1e060d897fe8a6986d743c288be45")
set(_SNAPSHOT_SHA256_vq_c "d60c5e9ab2e3bbf4f7678011246d54a03095a6d0e741bffe791f0eca4896ddaa")
set(_SNAPSHOT_SHA256_vq_h "3cba2fe27cf2b060d1e0a30463b41c7c51a45ae1a111a62c8076167359231987")
set(_SNAPSHOT_SHA256_vq_arm4_h "8214c9f92ca68a2f910f34f229f7cbd876e1e6974f82998f115ac5ba0d6b5bb5")
set(_SNAPSHOT_SHA256_vq_sse_h "9fa50758afc05dccaadb7eb5fd907dc2d98c5263749d462e8f24c0ed73a21f0b")
# Pinned per-file SHA-256 of the deps/speex public surface.
set(_SPEEX_DEPS_SHA256_readme_txt "bc8deb738a5a67be3aec4277ad3ec050a0626b33a1eab9ab805746a190b50b75")
set(_SPEEX_DEPS_SHA256_speex_h "a496154dc1725aef48408043034298237bf99c7e1c1dcabe6b604d421440b62d")
set(_SPEEX_DEPS_SHA256_speex_bits_h "8bf5db886e1b4386ba95ae95c297c13a457192400baa0d15d0ffc736db258a01")
set(_SPEEX_DEPS_SHA256_speex_callbacks_h "c3bd2260f88080238b3d01e9197446587cdb7eafe8cb8bdc913463ed665e9dd8")
set(_SPEEX_DEPS_SHA256_speex_config_types_h_in "44406b258c280b913a50914949621bec09cd33f93f8f450591553df6441f8570")
set(_SPEEX_DEPS_SHA256_speex_echo_h "41e4fe644e2eea04411b697e7b754e79782eb2db32648778ea60af043f3b9a75")
set(_SPEEX_DEPS_SHA256_speex_header_h "8d83cb0321f2e69833238f00983380a99aa8315d25ed223e4d4ae74a118e2c5b")
set(_SPEEX_DEPS_SHA256_speex_jitter_h "826091e9fd8144d4a4ba5cfe89d3f921ba38500e8f116bc7dd079a103e0976a4")
set(_SPEEX_DEPS_SHA256_speex_preprocess_h "1715a704df8515909d56adf4e2c798175f6439cd24aaa1a9bafd785808c8dd55")
set(_SPEEX_DEPS_SHA256_speex_stereo_h "97c790090b43b6531b7a14004de43ebcf4094ecc07866deaede280fa656e703d")
set(_SPEEX_DEPS_SHA256_speex_types_h "9659e472069af443d4f6bea49408a34d869ac42d21faacf98eeb734a881a57e8")

# Contract mutation self-verification: each mutation below is a plausible
# regression and must be rejected by the checks above.
if(DEFINED CONTRACT_MUTATION AND NOT CONTRACT_MUTATION STREQUAL "")
    if(CONTRACT_MUTATION STREQUAL "opus_wrapper_include")
        string(REPLACE
            "#include <speex/speex.h>"
            "#include <speex/speex.h> #include <opus/opus.h>"
            _encode "${_encode}")
    elseif(CONTRACT_MUTATION STREQUAL "speex_include_dropped")
        string(REPLACE
            "#include <speex/speex.h>"
            "" _decode "${_decode}")
    elseif(CONTRACT_MUTATION STREQUAL "codec_source_unlisted")
        string(REPLACE
            "\"\${SRC_DIR}/groupvoice/speex/bits.c\""
            "" _testscmake "${_testscmake}")
    elseif(CONTRACT_MUTATION STREQUAL "lcg_body_libc_rand")
        string(REPLACE
            "*state = *state * 1103515245u + 12345u;"
            "*state = (spx_uint32_t) rand();"
            _miscc "${_miscc}")
    elseif(CONTRACT_MUTATION STREQUAL "version_drift")
        string(REPLACE
            "#define SPEEX_VERSION \"speex-1.1.9\""
            "#define SPEEX_VERSION \"speex-1.2.1\""
            _misch "${_misch}")
    elseif(CONTRACT_MUTATION STREQUAL "codec_source_entry_replaced")
        # Same-directory substitution in the test codec-gate list: the entry
        # count stays 35 and every name is still known, so only the ordered
        # manifest check can reject it (review r4053096015 / r4053096442).
        string(REPLACE
            "\"\${SRC_DIR}/groupvoice/speex/bits.c\""
            "\"\${SRC_DIR}/groupvoice/speex/sb_celp.c\""
            _testscmake "${_testscmake}")
    elseif(CONTRACT_MUTATION STREQUAL "production_source_entry_replaced")
        # Same substitution inside the production GROUPVOICE_SPEEX build list,
        # which the production build surface never inspected before this
        # rework.
        string(REPLACE
            "\"\${SRC_DIR}/groupvoice/speex/nb_celp.c\""
            "\"\${SRC_DIR}/groupvoice/speex/sb_celp.c\""
            _mpfiles "${_mpfiles}")
    elseif(CONTRACT_MUTATION STREQUAL "snapshot_content_mutated")
        # Content-only drift of one vendored snapshot file: name sets and
        # counts are untouched, the pinned digest no longer matches the
        # on-disk bytes (review r4053096015).
        set(_SNAPSHOT_SHA256_bits_c
            "0000000000000000000000000000000000000000000000000000000000000000")
    elseif(CONTRACT_MUTATION STREQUAL "deps_surface_file_added")
        # An ADDED file in the deps/speex public surface: the per-file
        # digests only iterate the pinned names, so they cannot see extra
        # bytes. The regression is modeled in-memory by dropping one real
        # surface file from the pinned list — the on-disk file then becomes
        # an unrecognized extra that the file-set check must reject
        # (review r4062880244). This stays a pure in-memory mutation like
        # every other control: the mutation subprocess shares the real
        # SOURCE_ROOT and must never write into it.
        list(REMOVE_ITEM _SPEEX_DEPS_SURFACE_FILES "speex_types.h")
    else()
        message(FATAL_ERROR
            "Unknown voice codec guard contract mutation: '${CONTRACT_MUTATION}'")
    endif()
endif()

# Mutation-survival re-checks: every registered mutation must trip one of
# these invariants.
forbid_contains(_encode "opus"
    "foreign codec include survives mutations")
require_count(_decode "#include <speex/speex.h>" 1
    "Speex exclusivity survives mutations")
require_count(_testscmake "\"\${SRC_DIR}/groupvoice/speex/"
    35
    "pinned codec build list survives mutations")
require_count(_miscc "*state = *state * 1103515245u + 12345u;" 1
    "LCG body survives mutations")
require_count(_misch "#define SPEEX_VERSION \"speex-1.1.9\"" 1
    "snapshot version identity survives mutations")
# --- Review-hardened build-input identity (ordered manifests, consumption,
# --- pinned content digests) --------------------------------------------------
# These checks run after CONTRACT_MUTATION application so the
# entry-replacement and content-drift mutations trip here; on an unmutated
# run they are the primary guard. Name-set and count checks alone accept
# same-directory substitution and content-only edits, so:
#   - the test codec-gate list must equal the ordered 35-entry manifest,
#   - the production GROUPVOICE / GROUPVOICE_SPEEX lists must equal their
#     ordered manifests AND be consumed by scripts/mp/CMakeLists.txt, and
#   - every vendored snapshot file and the whole deps/speex surface must
#     match its pinned per-file SHA-256.
set(_SPEEX_TEST_PREFIX "\${SRC_DIR}/groupvoice/speex/")
set(_SPEEX_DEPS_PREFIX "\${SRC_DIR}/groupvoice/speex/")
extract_set_block(_testscmake "SPEEX_CODEC_SOURCES" _test_codec_block)
extract_quoted_entries("${_test_codec_block}" "${_SPEEX_TEST_PREFIX}"
    _actual_test_codec_entries)
require_ordered_manifest(_actual_test_codec_entries _EXPECTED_SPEEX_CODEC_SOURCES
    "test codec-gate build list must equal the pinned ordered manifest")

extract_set_block(_mpfiles "GROUPVOICE" _production_wrapper_block)
extract_quoted_entries("${_production_wrapper_block}" "\${SRC_DIR}/groupvoice/"
    _actual_production_wrapper_entries)
require_ordered_manifest(_actual_production_wrapper_entries _EXPECTED_GROUPVOICE
    "production GROUPVOICE wrapper list must equal the pinned ordered manifest")

extract_set_block(_mpfiles "GROUPVOICE_SPEEX" _production_codec_block)
extract_quoted_entries("${_production_codec_block}" "${_SPEEX_TEST_PREFIX}"
    _actual_production_codec_entries)
require_ordered_manifest(_actual_production_codec_entries
    _EXPECTED_GROUPVOICE_SPEEX
    "production GROUPVOICE_SPEEX codec list must equal the pinned ordered manifest")

# The production build must actually consume both lists (an unused declared
# list would not guard anything).
require_count(_mpfiles "set(GROUPVOICE " 1
    "the production wrapper list is declared exactly once")
require_count(_mpfiles "set(GROUPVOICE_SPEEX " 1
    "the production codec list is declared exactly once")
require_count(_mpcmake "\${GROUPVOICE}" 1
    "the production mp build consumes the GROUPVOICE wrapper sources")
require_count(_mpcmake "\${GROUPVOICE_SPEEX}" 1
    "the production mp build consumes the GROUPVOICE_SPEEX codec sources")

# Content pins: the DEP-3 conditional-preservation checksum is per file, so a
# content-only edit of any snapshot file or any deps/speex surface header
# trips the guard even though the name set is unchanged.
require_pinned_sha256("src/groupvoice/speex" _SPEEX_SNAPSHOT_FILES
    "_SNAPSHOT_SHA256_" "vendored Speex snapshot (DEP-3)")
require_pinned_sha256("deps/speex" _SPEEX_DEPS_SURFACE_FILES
    "_SPEEX_DEPS_SHA256_" "deps/speex public surface (DEP-3)")

# File-set identity for the deps/speex public surface: the per-file digests
# above iterate the pinned names only, so they cannot detect an ADDED file —
# and DEP-3 pins the whole surface, not just the known names. Mirror the
# vendored snapshot file-set check: the directory must contain exactly the
# pinned set, no more and no less (review r4062880244).
file(GLOB _deps_surface_entries LIST_DIRECTORIES FALSE
    "${SOURCE_ROOT}/deps/speex/*")
list(LENGTH _deps_surface_entries _deps_surface_count)
list(LENGTH _SPEEX_DEPS_SURFACE_FILES _deps_pinned_count)
if(NOT _deps_surface_count EQUAL _deps_pinned_count)
    message(FATAL_ERROR
        "deps/speex public surface file count drifted (DEP-3): "
        "expected ${_deps_pinned_count}, found ${_deps_surface_count}")
endif()
foreach(_deps_entry IN LISTS _deps_surface_entries)
    get_filename_component(_deps_name "${_deps_entry}" NAME)
    if(NOT _deps_name IN_LIST _SPEEX_DEPS_SURFACE_FILES)
        message(FATAL_ERROR
            "Unrecognized file in the deps/speex public surface (DEP-3): "
            "${_deps_name}")
    endif()
endforeach()

if(NOT DEFINED CONTRACT_MUTATION OR CONTRACT_MUTATION STREQUAL "")
    foreach(_mutation IN ITEMS
        opus_wrapper_include
        speex_include_dropped
        codec_source_unlisted
        codec_source_entry_replaced
        production_source_entry_replaced
        snapshot_content_mutated
        lcg_body_libc_rand
        version_drift
        deps_surface_file_added)
        execute_process(
            COMMAND "${CMAKE_COMMAND}"
                "-DSOURCE_ROOT=${SOURCE_ROOT}"
                "-DCONTRACT_MUTATION=${_mutation}"
                -P "${CMAKE_CURRENT_LIST_FILE}"
            RESULT_VARIABLE _mutation_result
            OUTPUT_VARIABLE _mutation_stdout
            ERROR_VARIABLE _mutation_stderr)
        if(_mutation_result EQUAL 0)
            message(STATUS "Mutation stdout: ${_mutation_stdout}")
            message(STATUS "Mutation stderr: ${_mutation_stderr}")
            message(FATAL_ERROR
                "Voice codec guard contract accepted mutation: ${_mutation}")
        endif()
    endforeach()
endif()

message(STATUS "Voice codec guard source contract passed")
