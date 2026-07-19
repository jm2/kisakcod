cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_header_path
    "${SOURCE_ROOT}/src/database/db_zone_stream_ownership.h")
set(_source_path
    "${SOURCE_ROOT}/src/database/db_zone_stream_ownership.cpp")
set(_internal_path
    "${SOURCE_ROOT}/src/database/db_zone_stream_ownership_internal.h")
set(_relocation_header_path
    "${SOURCE_ROOT}/src/database/db_relocation.h")
set(_relocation_source_path
    "${SOURCE_ROOT}/src/database/db_relocation.cpp")
set(_stream_path "${SOURCE_ROOT}/src/database/db_stream.cpp")
set(_stream_header_path "${SOURCE_ROOT}/src/database/db_stream.h")
set(_state_path "${SOURCE_ROOT}/src/database/db_stream_state.h")
set(_memory_path "${SOURCE_ROOT}/src/database/db_zone_memory.h")
set(_runtime_table_path
    "${SOURCE_ROOT}/src/database/db_zone_runtime_table.h")
set(_file_load_path "${SOURCE_ROOT}/src/database/db_file_load.cpp")
set(_com_error_path "${SOURCE_ROOT}/src/qcommon/com_error.h")
set(_integer_suffix_token_paste_path
    "${SOURCE_ROOT}/src/universal/q_shared.h")
set(_server_token_literal_path
    "${SOURCE_ROOT}/src/server_mp/sv_client_mp.cpp")
set(_ui_component_token_literal_path
    "${SOURCE_ROOT}/src/ui/ui_component.cpp")
set(_ui_parser_token_literal_path
    "${SOURCE_ROOT}/src/ui/ui_shared_obj.cpp")
set(_fixture_path
    "${SOURCE_ROOT}/tests/db_zone_stream_ownership_tests.cpp")
set(_seal_path
    "${SOURCE_ROOT}/tests/db_zone_stream_ownership_production_seal_tests.cpp")
set(_manifest_path "${SOURCE_ROOT}/scripts/common_files.cmake")
set(_tests_path "${SOURCE_ROOT}/tests/CMakeLists.txt")
set(_ci_path "${SOURCE_ROOT}/.github/workflows/ci.yml")

foreach(_path IN ITEMS
    "${_header_path}" "${_source_path}" "${_internal_path}"
    "${_relocation_header_path}" "${_relocation_source_path}"
    "${_stream_path}" "${_stream_header_path}" "${_state_path}"
    "${_memory_path}" "${_runtime_table_path}" "${_file_load_path}"
    "${_com_error_path}"
    "${_integer_suffix_token_paste_path}"
    "${_server_token_literal_path}"
    "${_ui_component_token_literal_path}"
    "${_ui_parser_token_literal_path}"
    "${_fixture_path}" "${_seal_path}" "${_manifest_path}"
    "${_tests_path}" "${_ci_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing zone-stream ownership source: ${_path}")
    endif()
endforeach()

file(READ "${_header_path}" _header)
file(READ "${_source_path}" _source)
file(READ "${_internal_path}" _internal)
file(READ "${_relocation_header_path}" _relocation_header)
file(READ "${_relocation_source_path}" _relocation_source)
file(READ "${_stream_path}" _stream)
file(READ "${_stream_header_path}" _stream_header)
file(READ "${_state_path}" _state)
file(READ "${_memory_path}" _memory)
file(READ "${_runtime_table_path}" _runtime_table)
file(READ "${_file_load_path}" _file_load)
file(READ "${_com_error_path}" _com_error)
file(READ "${_fixture_path}" _fixture)
file(READ "${_seal_path}" _seal)
file(READ "${_manifest_path}" _manifest)
file(READ "${_tests_path}" _tests)
file(READ "${_ci_path}" _ci)

foreach(_var IN ITEMS
    _header _source _internal _relocation_header _relocation_source
    _stream _stream_header _state _memory _runtime_table _file_load
    _fixture _seal _manifest _tests _ci _com_error)
    string(REGEX REPLACE "[ \t\r\n]+" " " _normalized "${${_var}}")
    set(${_var} "${_normalized}")
endforeach()

function(require_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing zone-stream ownership invariant (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(require_not_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden zone-stream ownership regression (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

string(ASCII 92 _zone_stream_backslash)
string(ASCII 13 _zone_stream_carriage_return)
string(ASCII 10 _zone_stream_line_feed)
set(_zone_stream_block_comment "/\\*([^*]|\\*+[^*/])*\\*+/")
set(_zone_stream_comment_atom
    "([ \t\r\n]|${_zone_stream_block_comment}|//[^\r\n]*)")
set(_zone_stream_comment_gap "${_zone_stream_comment_atom}*")
set(_zone_stream_comment_separator "${_zone_stream_comment_atom}+")

function(normalize_zone_stream_phase2 SOURCE_VAR OUT_VAR)
    set(_spliced "${${SOURCE_VAR}}")
    string(REPLACE
        "${_zone_stream_backslash}${_zone_stream_carriage_return}${_zone_stream_line_feed}"
        "" _spliced "${_spliced}")
    string(REPLACE
        "${_zone_stream_backslash}${_zone_stream_line_feed}"
        "" _spliced "${_spliced}")
    string(REPLACE
        "${_zone_stream_backslash}${_zone_stream_carriage_return}"
        "" _spliced "${_spliced}")
    set(${OUT_VAR} "${_spliced}" PARENT_SCOPE)
endfunction()

function(source_has_identifier SOURCE_VAR IDENTIFIER OUT_VAR)
    string(REGEX MATCH
        "(^|[^A-Za-z0-9_])${IDENTIFIER}([^A-Za-z0-9_]|$)"
        _match "${${SOURCE_VAR}}")
    if(_match STREQUAL "")
        set(${OUT_VAR} FALSE PARENT_SCOPE)
    else()
        set(${OUT_VAR} TRUE PARENT_SCOPE)
    endif()
endfunction()

function(source_has_qualified_ownership SOURCE_VAR OUT_VAR)
    string(REGEX MATCH
        "(^|[^A-Za-z0-9_])((db${_zone_stream_comment_gap}::${_zone_stream_comment_gap})?zone_stream_ownership)${_zone_stream_comment_gap}::"
        _match "${${SOURCE_VAR}}")
    if(_match STREQUAL "")
        set(${OUT_VAR} FALSE PARENT_SCOPE)
    else()
        set(${OUT_VAR} TRUE PARENT_SCOPE)
    endif()
endfunction()

function(source_has_ownership_namespace_declaration SOURCE_VAR OUT_VAR)
    string(REGEX MATCH
        "(^|[^A-Za-z0-9_])namespace${_zone_stream_comment_separator}((db${_zone_stream_comment_gap}::${_zone_stream_comment_gap})?zone_stream_ownership)(${_zone_stream_comment_gap}::|${_zone_stream_comment_gap}\\{)"
        _match "${${SOURCE_VAR}}")
    if(_match STREQUAL "")
        set(${OUT_VAR} FALSE PARENT_SCOPE)
    else()
        set(${OUT_VAR} TRUE PARENT_SCOPE)
    endif()
endfunction()

function(source_has_zone_stream_preprocessor_token_paste SOURCE_VAR OUT_VAR)
    # After exact path-specific removal of reviewed fixed suffixes and
    # literals, reject every spelling that can become a paste operator. This
    # raw policy cannot be confused by comments, raw strings, or phase order.
    foreach(_operator IN ITEMS "##" "%:%:" "??/" "??=")
        string(FIND "${${SOURCE_VAR}}" "${_operator}" _operator_position)
        if(NOT _operator_position EQUAL -1)
            set(${OUT_VAR} TRUE PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${OUT_VAR} FALSE PARENT_SCOPE)
endfunction()

function(remove_reviewed_zone_stream_token_text PATH SOURCE_VAR OUT_VAR)
    set(_candidate "${${SOURCE_VAR}}")
    if(PATH STREQUAL _integer_suffix_token_paste_path)
        # q_shared's suffixes are fixed and no protected identifier ends in
        # LL or i64. Any altered or additional paste remains visible.
        string(REPLACE "num ## LL" "" _candidate "${_candidate}")
        string(REPLACE "num ## i64" "" _candidate "${_candidate}")
    elseif(PATH STREQUAL _server_token_literal_path)
        string(REPLACE
            "\"###!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!###\\n\""
            "" _candidate "${_candidate}")
        string(REPLACE
            "\"########################################\\n\""
            "" _candidate "${_candidate}")
    elseif(PATH STREQUAL _ui_component_token_literal_path)
        string(REPLACE "\"##\"" "" _candidate "${_candidate}")
    elseif(PATH STREQUAL _ui_parser_token_literal_path)
        string(REPLACE "\"##\"" "" _candidate "${_candidate}")
        string(REPLACE
            "\"define with misplaced ##\"" "" _candidate "${_candidate}")
    endif()
    set(${OUT_VAR} "${_candidate}" PARENT_SCOPE)
endfunction()

function(require_ordered SOURCE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${FIRST}" _first)
    string(FIND "${${SOURCE_VAR}}" "${SECOND}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered zone-stream ownership invariant "
            "(${DESCRIPTION})")
    endif()
endfunction()

function(extract_slice SOURCE_VAR START END OUT_VAR DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${START}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR "Missing start of ${DESCRIPTION}: '${START}'")
    endif()
    string(SUBSTRING "${${SOURCE_VAR}}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${END}" _end)
    if(_end LESS_EQUAL 0)
        message(FATAL_ERROR "Missing ordered end of ${DESCRIPTION}: '${END}'")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_end} _slice)
    set(${OUT_VAR} "${_slice}" PARENT_SCOPE)
endfunction()

# The public primitive is stable-address, report-free, exception-free, and has
# no public mutation/test escape hatch.
foreach(_marker IN ITEMS
    "class alignas(8) ZoneStreamGenerationReceipt final"
    "class alignas(8) ActiveZoneStreamBinding final"
    "ZoneStreamGenerationReceipt(const ZoneStreamGenerationReceipt &) = delete;"
    "ZoneStreamGenerationReceipt(ZoneStreamGenerationReceipt &&) = delete;"
    "ActiveZoneStreamBinding(const ActiveZoneStreamBinding &) = delete;"
    "ActiveZoneStreamBinding(ActiveZoneStreamBinding &&) = delete;"
    "Invalidated,"
    "RUNTIME_SIZE(ZoneStreamGenerationReceipt, 0x20, 0x28);"
    "RUNTIME_SIZE(ActiveZoneStreamBinding, 0x68, 0xC0);"
    "TryBeginZoneStreamGeneration("
    "TryBindZoneStreams("
    "TryInvalidateZoneStreams("
    "const zone_load::ZoneLoadContextSlot *lifecycle()"
    "const XZoneMemory *zoneIdentity() const noexcept;"
    "const XZoneMemory *zoneIdentity,"
    "bool canonical() const noexcept;")
    require_contains(_header "${_marker}" "sealed stable receipt/controller API")
endforeach()
require_not_contains(
    _header "const void *zoneIdentity"
    "zone identity must remain a typed XZoneMemory boundary")
foreach(_var IN ITEMS _header _source)
    foreach(_forbidden IN ITEMS
        "KISAK_DB_ZONE_STREAM_OWNERSHIP_TESTING"
        "TestAccess"
        "Com_Error("
        "iassert("
        "memset("
        "malloc("
        "calloc("
        "realloc("
        "operator new"
        "throw "
        "catch ("
        "std::function"
        "PMem_"
        "DB_CompleteLoadingAsset("
        "DB_InitStreams(")
        require_not_contains(
            ${_var} "${_forbidden}" "report-free production-neutral primitive")
    endforeach()
    source_has_identifier(${_var} "assert" _runtime_assert)
    if(_runtime_assert)
        message(FATAL_ERROR
            "Forbidden runtime assert in report-free zone-stream primitive")
    endif()
endforeach()
require_not_contains(
    _runtime_table "ZoneStreamGenerationReceipt"
    "runtime table embedding remains a later reviewed batch")
require_not_contains(
    _runtime_table "ActiveZoneStreamBinding"
    "singleton controller is not partially enrolled")

# Exact usable lifecycle keys and self-addresses authenticate every durable
# phase. Terminal retries precede all active inspection.
foreach(_marker IN ITEMS
    "zone_slots::IsUsableZoneSlot(key.slot)"
    "self_ == this"
    "receipt_->key() == key_"
    "receipt->lifecycle_ != lifecycle"
    "ValidateRequestedKey(receipt->key_, key)"
    "ZoneStreamGenerationPhase::NeverBound"
    "ZoneStreamGenerationPhase::Bound"
    "ZoneStreamGenerationPhase::Invalidated"
    "ActiveZoneStreamPhase::Idle"
    "ActiveZoneStreamPhase::Bound")
    require_contains(_source "${_marker}" "exact-key self-authentication")
endforeach()
extract_slice(
    _source
    "ZoneStreamOwnershipStatus TryInvalidateZoneStreams("
    "namespace detail"
    _invalidate
    "exact invalidation")
require_ordered(
    _invalidate
    "receipt->phase() == ZoneStreamGenerationPhase::Invalidated"
    "if (!active)"
    "terminal retry before active validation")
require_ordered(
    _invalidate
    "receipt->phase() == ZoneStreamGenerationPhase::NeverBound"
    "receipt->phase() != ZoneStreamGenerationPhase::Bound"
    "NeverBound no-op before bound singleton inspection")
require_ordered(
    _invalidate
    "g_aliasRegistry.Invalidate();"
    "g_directResolver.Invalidate();"
    "alias state invalidates before direct state")
require_ordered(
    _invalidate
    "g_directResolver.Invalidate();"
    "ScrubStreamScalars();"
    "relocation provenance invalidates before stream globals")
require_ordered(
    _invalidate
    "ScrubStreamScalars();"
    "ScrubStreamArrays();"
    "logical counts clear before pointer arrays")
require_ordered(
    _invalidate
    "g_activeOwner = nullptr;"
    "// Terminal publication is last"
    "terminal receipt publishes last")
require_contains(
    _invalidate
    "receipt->phaseWord_ = static_cast<std::uint32_t>( ZoneStreamGenerationPhase::Invalidated);"
    "explicit Invalidated receipt publication")

# Bind snapshots input and validates count, pointer/size parity, block zero,
# alignment, native end, pairwise overlap, control disjointness, exact Loading
# lifecycle/key, idle singleton, and epoch capacity before the first mutation.
extract_slice(
    _source
    "ZoneStreamOwnershipStatus TryBindZoneStreams("
    "ZoneStreamOwnershipStatus TryInvalidateZoneStreams("
    _bind
    "checked bind")
foreach(_marker IN ITEMS
    "hasPointer != hasSize"
    "alignof(std::uint32_t)"
    "std::numeric_limits<std::uintptr_t>::max"
    "SpansOverlap("
    "blocks[0].base != 0 && blocks[0].size != 0")
    require_contains(_source "${_marker}" "complete block-layout preflight")
endforeach()
foreach(_marker IN ITEMS
    "blockCount != relocation::kBlockCount"
    "std::copy_n(blockArgument, relocation::kBlockCount, blocks);"
    "ZoneMatchesLayout(zone, blocks)"
    "ObjectIsAligned(zoneIdentity, alignof(XZoneMemory))"
    "LifecycleMatchesLoading(receipt->lifecycle_, key)"
    "if (!SingletonIsIdle())"
    "if (!g_aliasRegistry.CanReset())"
    "g_aliasRegistry.Reset("
    "g_directResolver.Reset("
    "g_activeOwner = active;"
    "ZoneStreamGenerationPhase::Bound")
    require_contains(_bind "${_marker}" "complete bind preflight/publication")
endforeach()
require_ordered(
    _bind
    "if (!g_aliasRegistry.CanReset())"
    "g_aliasRegistry.Reset("
    "epoch exhaustion checked before mutation")
require_ordered(
    _bind
    "g_activeOwner = active;"
    "receipt->phaseWord_ = static_cast<std::uint32_t>( ZoneStreamGenerationPhase::Bound);"
    "bound receipt publishes last")

# Every pointer-bearing legacy singleton is defined in the ownership TU and
# deterministically scrubbed. The dead g_streamBlocks alias stays removed.
foreach(_marker IN ITEMS
    "std::uint8_t *g_streamPosArray[db::relocation::kBlockCount];"
    "StreamDelayInfo g_streamDelayArray[4096];"
    "StreamPosInfo g_streamPosStack[64];"
    "XZoneMemory *g_streamZoneMem;"
    "std::uint8_t *g_streamPos;"
    "for (StreamDelayInfo &delay : g_streamDelayArray)"
    "delay.ptr = nullptr;"
    "delay.size = 0;"
    "for (StreamPosInfo &saved : g_streamPosStack)"
    "saved.pos = nullptr;"
    "saved.index = 0;"
    "for (std::uint8_t *&position : g_streamPosArray)"
    "g_streamZoneMem = nullptr;"
    "g_streamPos = nullptr;"
    "g_streamDelayIndex = 0;"
    "g_streamPosIndex = 0;"
    "g_streamPosStackIndex = 0;")
    require_contains(_source "${_marker}" "complete singleton scrub inventory")
endforeach()
foreach(_var IN ITEMS _header _source _stream _stream_header _state)
    require_not_contains(${_var} "g_streamBlocks" "dead stream-block alias removed")
endforeach()
foreach(_marker IN ITEMS
    "for (StreamDelayInfo &delay : g_streamDelayArray)"
    "delay.ptr = nullptr;"
    "for (StreamPosInfo &saved : g_streamPosStack)"
    "saved.pos = nullptr;")
    require_contains(
        _stream "${_marker}" "legacy replacement also scrubs native pointers")
endforeach()

# Relocation invalidation overwrites native addresses, releases all vector
# capacities, and never advances the alias epoch. Reset fails closed at max.
foreach(_marker IN ITEMS
    "void DirectResolver::Invalidate() noexcept"
    "std::vector<Interval>{}.swap(block.materialized);"
    "std::vector<StringRecord>{}.swap(strings_);"
    "void AliasRegistry::Invalidate() noexcept"
    "*resolvedAddress = 0;"
    "std::vector<Record>{}.swap(records_);"
    "!= (std::numeric_limits<std::uint64_t>::max)()"
    "return Status::GenerationExhausted;"
    "case Status::GenerationExhausted: return \"generation exhausted\";")
    require_contains(
        _relocation_source "${_marker}" "no-throw scrub and wrap fail-close")
endforeach()
foreach(_marker IN ITEMS
    "volatile std::uintptr_t *const resolvedAddress"
    "volatile std::uint32_t *const metadata"
    "volatile bool *const published"
    "volatile std::uint32_t *const offset"
    "volatile AliasKind *const kind")
    require_contains(
        _relocation_source "${_marker}" "observable alias-record scrub")
endforeach()
extract_slice(
    _relocation_source
    "void AliasRegistry::Invalidate() noexcept"
    "bool AliasRegistry::CanReset() const noexcept"
    _alias_invalidate
    "alias invalidation")
require_not_contains(
    _alias_invalidate "++generation_" "invalidation cannot advance epoch")
require_ordered(
    _relocation_source
    "if (!CanReset()) return Status::GenerationExhausted;"
    "++generation_;"
    "generation check before one exact increment")
foreach(_marker IN ITEMS
    "void Invalidate() noexcept;"
    "bool CanReset() const noexcept;"
    "GenerationExhausted,")
    require_contains(
        _relocation_header "${_marker}" "explicit relocation invalidation API")
endforeach()

# Internal mutable relocation access is private to the legacy wrapper and the
# ownership implementation. Public lifecycle APIs remain unused in production.
# Scan identifiers rather than call spellings so whitespace, using declarations,
# namespace aliases, and function-pointer references cannot bypass the seal.
# Translation phase 2 joins escaped physical lines before identifiers and
# header names form. Phase-3 comments are accepted only as complete token gaps
# in the qualified/manual-namespace detectors, avoiding a lossy comment pass
# that could mistake comment-like bytes inside string and character literals.
file(GLOB_RECURSE _production_sources
    "${SOURCE_ROOT}/src/*.c"
    "${SOURCE_ROOT}/src/*.cc"
    "${SOURCE_ROOT}/src/*.cpp"
    "${SOURCE_ROOT}/src/*.h"
    "${SOURCE_ROOT}/src/*.hpp")
foreach(_path IN LISTS _production_sources)
    file(READ "${_path}" _candidate_raw)
    normalize_zone_stream_phase2(_candidate_raw _candidate)
    remove_reviewed_zone_stream_token_text(
        "${_path}" _candidate _candidate)
    source_has_zone_stream_preprocessor_token_paste(
        _candidate _token_paste_found)
    if(_token_paste_found)
        message(FATAL_ERROR
            "Unreviewed token-paste capability can bypass the zone-stream seal in ${_path}")
    endif()

    if(NOT _path STREQUAL _header_path AND NOT _path STREQUAL _source_path)
        string(FIND "${_candidate}" "db_zone_stream_ownership.h"
            _public_header_reference)
        if(NOT _public_header_reference EQUAL -1)
            message(FATAL_ERROR
                "Premature zone-stream public header enrollment in ${_path}")
        endif()
        foreach(_public_api IN ITEMS
            TryBeginZoneStreamGeneration
            TryBindZoneStreams
            TryInvalidateZoneStreams)
            source_has_identifier(_candidate "${_public_api}" _found)
            if(_found)
                message(FATAL_ERROR
                    "Premature zone-stream public API reference in ${_path}: "
                    "${_public_api}")
            endif()
        endforeach()
    endif()

    if(NOT _path STREQUAL _internal_path
        AND NOT _path STREQUAL _source_path
        AND NOT _path STREQUAL _stream_path)
        string(FIND "${_candidate}"
            "db_zone_stream_ownership_internal.h" _internal_header_reference)
        if(NOT _internal_header_reference EQUAL -1)
            message(FATAL_ERROR
                "Private stream header escaped to ${_path}")
        endif()
        foreach(_internal_api IN ITEMS
            AliasRegistryForLegacyStream
            DirectResolverForLegacyStream
            OwnershipBindingActive)
            source_has_identifier(_candidate "${_internal_api}" _found)
            if(_found)
                message(FATAL_ERROR
                    "Private stream capability escaped to ${_path}: "
                    "${_internal_api}")
            endif()
        endforeach()
    endif()

    if(NOT _path STREQUAL _header_path
        AND NOT _path STREQUAL _source_path
        AND NOT _path STREQUAL _internal_path)
        source_has_ownership_namespace_declaration(_candidate _found)
        if(_found)
            message(FATAL_ERROR
                "Premature zone-stream namespace declaration in ${_path}")
        endif()
    endif()

    if(NOT _path STREQUAL _header_path
        AND NOT _path STREQUAL _source_path
        AND NOT _path STREQUAL _internal_path
        AND NOT _path STREQUAL _stream_path)
        source_has_qualified_ownership(_candidate _found)
        if(_found)
            message(FATAL_ERROR
                "Premature qualified zone-stream reference in ${_path}")
        endif()
    endif()
endforeach()

# Keep compile-valid representative bypasses recognizable to the detectors.
# These cover phase-2-spliced headers/identifiers and phase-3 block/line comments
# around using declarations, qualified function pointers, and namespaces.
string(CONCAT _public_header_bypass
    "#include <database/db_zone_stream_owner${_zone_stream_backslash}"
    "${_zone_stream_line_feed}ship.h>")
normalize_zone_stream_phase2(
    _public_header_bypass _public_header_bypass_normalized)
string(FIND "${_public_header_bypass_normalized}"
    "db_zone_stream_ownership.h" _public_header_detected)
if(_public_header_detected EQUAL -1)
    message(FATAL_ERROR
        "Zone-stream seal no longer recognizes public-header bypass")
endif()

string(CONCAT _internal_header_bypass
    "#include <database/db_zone_stream_ownership_in${_zone_stream_backslash}"
    "${_zone_stream_carriage_return}${_zone_stream_line_feed}ternal.h>")
normalize_zone_stream_phase2(
    _internal_header_bypass _internal_header_bypass_normalized)
string(FIND "${_internal_header_bypass_normalized}"
    "db_zone_stream_ownership_internal.h" _internal_header_detected)
if(_internal_header_detected EQUAL -1)
    message(FATAL_ERROR
        "Zone-stream seal no longer recognizes internal-header bypass")
endif()

string(CONCAT _qualified_using_bypass
    "using db/**/::/**/zone_stream_ownership/**/::/**/TryBindZoneStreams;\n"
    "auto bind = &TryBindZoneStreams;")
source_has_qualified_ownership(_qualified_using_bypass _qualified_detected)
source_has_identifier(
    _qualified_using_bypass TryBindZoneStreams _qualified_symbol_detected)
if(NOT _qualified_detected OR NOT _qualified_symbol_detected)
    message(FATAL_ERROR
        "Zone-stream seal no longer recognizes qualified using bypass")
endif()

string(CONCAT _token_paste_bypass
    "#define KISAK_STREAM_CAT_I(left, right) left/**/##/**/right\n"
    "#define KISAK_STREAM_CAT(left, right) "
    "KISAK_STREAM_CAT_I(left, right)\n"
    "auto bind = &KISAK_STREAM_CAT(TryBindZone, Streams);")
normalize_zone_stream_phase2(_token_paste_bypass _token_paste_normalized)
source_has_zone_stream_preprocessor_token_paste(
    _token_paste_normalized _token_paste_detected)
if(NOT _token_paste_detected)
    message(FATAL_ERROR
        "Zone-stream seal no longer recognizes hash token-paste bypass")
endif()

set(_digraph_token_paste_bypass
    "%: define KISAK_STREAM_DIGRAPH(left, right) left %:%: right")
source_has_zone_stream_preprocessor_token_paste(
    _digraph_token_paste_bypass _digraph_token_paste_detected)
if(NOT _digraph_token_paste_detected)
    message(FATAL_ERROR
        "Zone-stream seal no longer recognizes digraph token-paste bypass")
endif()

set(_trigraph_token_paste_bypass
    "??=define KISAK_STREAM_TRIGRAPH(left, right) left ??=??= right")
source_has_zone_stream_preprocessor_token_paste(
    _trigraph_token_paste_bypass _trigraph_token_paste_detected)
if(NOT _trigraph_token_paste_detected)
    message(FATAL_ERROR
        "Zone-stream seal no longer recognizes trigraph token-paste bypass")
endif()

string(CONCAT _trigraph_splice_token_paste_bypass
    "#define KISAK_STREAM_TRIGRAPH_SPLICE(left, right) left #??/"
    "${_zone_stream_line_feed}# right")
source_has_zone_stream_preprocessor_token_paste(
    _trigraph_splice_token_paste_bypass
    _trigraph_splice_token_paste_detected)
if(NOT _trigraph_splice_token_paste_detected)
    message(FATAL_ERROR
        "Zone-stream seal misses trigraph-splice token-paste bypass")
endif()

string(CONCAT _comment_quote_token_paste_bypass
    "/* \"\n*/ %: define KISAK_STREAM_COMMENT_CAT(left, right) "
    "left %:%: right /* \" */")
source_has_zone_stream_preprocessor_token_paste(
    _comment_quote_token_paste_bypass _comment_quote_paste_detected)
if(NOT _comment_quote_paste_detected)
    message(FATAL_ERROR
        "Zone-stream seal misses comment-quote token-paste bypass")
endif()

set(_server_payload_macro_bypass
    "#define KISAK_STREAM_HASH_RUN ########################################")
remove_reviewed_zone_stream_token_text(
    "${_server_token_literal_path}"
    _server_payload_macro_bypass
    _server_payload_macro_reviewed)
source_has_zone_stream_preprocessor_token_paste(
    _server_payload_macro_reviewed _server_payload_macro_detected)
if(NOT _server_payload_macro_detected)
    message(FATAL_ERROR
        "Zone-stream server-literal review masks a macro hash run")
endif()

string(CONCAT _unqualified_pointer_bypass
    "auto invalidate = &TryInvalidateZoneStr${_zone_stream_backslash}"
    "${_zone_stream_line_feed}eams;")
normalize_zone_stream_phase2(
    _unqualified_pointer_bypass _unqualified_pointer_bypass_normalized)
source_has_identifier(
    _unqualified_pointer_bypass_normalized
    TryInvalidateZoneStreams _pointer_detected)
if(NOT _pointer_detected)
    message(FATAL_ERROR
        "Zone-stream seal no longer recognizes unqualified pointer bypass")
endif()

string(CONCAT _private_pointer_bypass
    "auto registry = &db// phase-3 separator\n"
    "::/**/zone_stream_ownership/**/::/**/detail/**/::/**/"
    "AliasRegistryForLegacyStream;")
source_has_qualified_ownership(_private_pointer_bypass _private_qualified)
source_has_identifier(
    _private_pointer_bypass AliasRegistryForLegacyStream _private_symbol)
if(NOT _private_qualified OR NOT _private_symbol)
    message(FATAL_ERROR
        "Zone-stream seal no longer recognizes private pointer bypass")
endif()

set(_compact_namespace_bypass
    "namespace/**/db/**/::/**/zone_stream_ownership { class Forged; }")
source_has_ownership_namespace_declaration(
    _compact_namespace_bypass _compact_namespace_detected)
set(_nested_namespace_bypass
    "namespace/**/db { namespace/**/zone_stream_ownership { class Forged; } }")
source_has_ownership_namespace_declaration(
    _nested_namespace_bypass _nested_namespace_detected)
if(NOT _compact_namespace_detected OR NOT _nested_namespace_detected)
    message(FATAL_ERROR
        "Zone-stream seal no longer recognizes manual namespace bypass")
endif()
require_contains(
    _stream "#include \"db_zone_stream_ownership_internal.h\""
    "legacy wrapper owns the sole private bridge")
require_contains(
    _stream "void __cdecl DB_InitStreams(XZoneMemory *zoneMem)"
    "sole legacy bind entrypoint remains explicit")
foreach(_marker IN ITEMS
    "std::extent_v<decltype(XZoneMemory::blocks)>"
    "std::extent_v<decltype(g_streamPosArray)>"
    "i < db::relocation::kBlockCount; ++i")
    require_contains(_stream "${_marker}" "canonical legacy block count")
endforeach()
require_contains(
    _source "db::relocation::kBlockCount == 9"
    "ownership definition pins the canonical block count")
require_contains(
    _com_error "__attribute__((format(__printf__, 2, 3)))"
    "portable Com_Error format checking")

# Freeze the still-legacy loader admission order until the atomic coordinator
# batch replaces it in one reviewed cutover.
require_ordered(
    _file_load "DB_InitStreams(g_load.zoneMem);" "Load_XAssetListCustom();"
    "legacy stream bind precedes asset loading")
require_ordered(
    _file_load "DB_FinishGeometryBlocks(g_load.zoneMem);"
    "DB_CompleteLoadingAsset();"
    "geometry closure precedes current admission")
require_ordered(
    _file_load "DB_CompleteLoadingAsset();" "Load_DelayStream();"
    "current admission order remains unchanged in neutral batch")

# Portable manifests, native-width layouts, runtime edge coverage, production
# API seal, and the explicit Windows x86 target/filter remain wired.
foreach(_marker IN ITEMS
    "database/db_stream.h"
    "database/db_stream_state.h"
    "database/db_zone_memory.h"
    "database/db_zone_stream_ownership.cpp"
    "database/db_zone_stream_ownership.h"
    "database/db_zone_stream_ownership_internal.h"
    "qcommon/com_error.h")
    require_contains(_manifest "${_marker}" "shared source manifest")
endforeach()
foreach(_marker IN ITEMS
    "RUNTIME_SIZE(StreamDelayInfo, 0x8, 0x10);"
    "RUNTIME_SIZE(StreamPosInfo, 0x8, 0x10);")
    require_contains(_state "${_marker}" "dual-width stream state layout")
endforeach()
foreach(_marker IN ITEMS
    "RUNTIME_SIZE(XBlock, 0x8, 0x10);"
    "RUNTIME_SIZE(XZoneMemory, 0x58, 0xB0);")
    require_contains(_memory "${_marker}" "dual-width zone memory layout")
endforeach()
foreach(_marker IN ITEMS
    "kisakcod-db-zone-stream-ownership-tests"
    "database-zone-stream-ownership"
    "kisakcod-db-zone-stream-ownership-production-seal-tests"
    "database-zone-stream-ownership-production-test-access-sealed"
    "database-zone-stream-ownership-source-invariants")
    require_contains(_tests "${_marker}" "portable test graph")
endforeach()
foreach(_marker IN ITEMS
    "kisakcod-db-zone-stream-ownership-tests"
    "kisakcod-db-zone-stream-ownership-production-seal-tests"
    "database-zone-stream-ownership-(runtime-contracts|production-test-access-sealed|source-invariants)")
    require_contains(_ci "${_marker}" "explicit Windows x86 CI wiring")
endforeach()
foreach(_marker IN ITEMS
    "TestConstructionAndKeyValidation();"
    "TestBindValidationAndFailureAtomicity();"
    "TestFullInvalidationAndStaleRetry();"
    "TestAliasEpochExhaustion();"
    "TestNativeWidthBlockAddresses();"
    "terminal retry returns before active inspection"
    "all nine block cursors scrubbed"
    "rejected bind preserves singleton stream state"
    "stale alias handle cannot publish after invalidation"
    "different active key cannot replace singleton owner")
    require_contains(_fixture "${_marker}" "exhaustive runtime regression")
endforeach()
foreach(_marker IN ITEMS
    "misaligned typed zone identity rejected before dereference"
    "active controller retains typed zone identity")
    require_contains(_fixture "${_marker}" "typed zone identity boundary")
endforeach()
foreach(_marker IN ITEMS
    "CanMutateReceiptKey"
    "CanMutateReceiptSelf"
    "CanReachBlocks"
    "CanReplaceReceipt"
    "CanMutatePhase"
    "using ZoneIdentityAccessor ="
    "decltype(&TryBindZoneStreams), BindFunction"
    "SplicedBindPointer"
    "CommentQualifiedBindPointer"
    "CommentNamespaceProbe")
    require_contains(_seal "${_marker}" "production API mutation seal")
endforeach()
