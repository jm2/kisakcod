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

if(NOT DEFINED CONTRACT_MUTATION OR CONTRACT_MUTATION STREQUAL "")
    foreach(_mutation IN ITEMS
        opus_wrapper_include
        speex_include_dropped
        codec_source_unlisted
        lcg_body_libc_rand
        version_drift)
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
