cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_ownership_header_path
    "${SOURCE_ROOT}/src/script/scr_string_transaction.h")
set(_string_source_path
    "${SOURCE_ROOT}/src/script/scr_stringlist.cpp")
set(_memory_header_path
    "${SOURCE_ROOT}/src/script/scr_memorytree.h")
set(_memory_source_path
    "${SOURCE_ROOT}/src/script/scr_memorytree.cpp")
set(_transaction_header_path
    "${SOURCE_ROOT}/src/database/db_script_string_transaction.h")
set(_transaction_source_path
    "${SOURCE_ROOT}/src/database/db_script_string_transaction.cpp")
set(_adapter_header_path
    "${SOURCE_ROOT}/src/database/db_script_string_adapter.h")
set(_adapter_source_path
    "${SOURCE_ROOT}/src/database/db_script_string_adapter.cpp")
set(_sync_header_path "${SOURCE_ROOT}/src/qcommon/sys_sync.h")
set(_platform_contract_path
    "${SOURCE_ROOT}/tests/platform_service_contract_tests.cpp")
set(_platform_runtime_path
    "${SOURCE_ROOT}/tests/platform_service_runtime_tests.cpp")
set(_memory_fixture_path
    "${SOURCE_ROOT}/tests/script_memorytree_try_tests.cpp")
set(_ownership_fixture_path
    "${SOURCE_ROOT}/tests/script_string_ownership_tests.cpp")
set(_ci_path "${SOURCE_ROOT}/.github/workflows/ci.yml")
set(_manifest_path "${SOURCE_ROOT}/scripts/common_files.cmake")
set(_tests_path "${SOURCE_ROOT}/tests/CMakeLists.txt")

foreach(_path IN ITEMS
    "${_ownership_header_path}"
    "${_string_source_path}"
    "${_memory_header_path}"
    "${_memory_source_path}"
    "${_transaction_header_path}"
    "${_transaction_source_path}"
    "${_adapter_header_path}"
    "${_adapter_source_path}"
    "${_sync_header_path}"
    "${_platform_contract_path}"
    "${_platform_runtime_path}"
    "${_memory_fixture_path}"
    "${_ownership_fixture_path}"
    "${_ci_path}"
    "${_manifest_path}"
    "${_tests_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR
            "Missing script-string ownership source: ${_path}")
    endif()
endforeach()

file(READ "${_ownership_header_path}" _ownership_header)
file(READ "${_string_source_path}" _string_source)
file(READ "${_memory_header_path}" _memory_header)
file(READ "${_memory_source_path}" _memory_source)
file(READ "${_transaction_header_path}" _transaction_header)
file(READ "${_transaction_source_path}" _transaction_source)
file(READ "${_adapter_header_path}" _adapter_header)
file(READ "${_adapter_source_path}" _adapter_source)
file(READ "${_sync_header_path}" _sync_header)
file(READ "${_platform_contract_path}" _platform_contract)
file(READ "${_platform_runtime_path}" _platform_runtime)
file(READ "${_memory_fixture_path}" _memory_fixture)
file(READ "${_ownership_fixture_path}" _ownership_fixture)
file(READ "${_ci_path}" _ci)
file(READ "${_manifest_path}" _manifest)
file(READ "${_tests_path}" _tests)

foreach(_var IN ITEMS
    _ownership_header
    _string_source
    _memory_header
    _memory_source
    _transaction_header
    _transaction_source
    _adapter_header
    _adapter_source
    _sync_header
    _platform_contract
    _platform_runtime
    _memory_fixture
    _ownership_fixture
    _ci
    _manifest
    _tests)
    string(REGEX REPLACE "[ \t\r\n]+" " " _normalized "${${_var}}")
    set(${_var} "${_normalized}")
endforeach()

function(require_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing script-string ownership invariant (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(require_not_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden script-string ownership regression (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(require_ordered SOURCE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${FIRST}" _first)
    string(FIND "${${SOURCE_VAR}}" "${SECOND}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered script-string ownership invariant "
            "(${DESCRIPTION})")
    endif()
endfunction()

function(require_literal_count SOURCE_VAR NEEDLE EXPECTED DESCRIPTION)
    set(_remaining "${${SOURCE_VAR}}")
    set(_count 0)
    string(LENGTH "${NEEDLE}" _needle_length)
    if(_needle_length EQUAL 0)
        message(FATAL_ERROR "Empty literal for ${DESCRIPTION}")
    endif()
    while(TRUE)
        string(FIND "${_remaining}" "${NEEDLE}" _position)
        if(_position EQUAL -1)
            break()
        endif()
        math(EXPR _count "${_count} + 1")
        math(EXPR _next "${_position} + ${_needle_length}")
        string(SUBSTRING "${_remaining}" ${_next} -1 _remaining)
    endwhile()
    if(NOT _count EQUAL EXPECTED)
        message(FATAL_ERROR
            "Wrong script-string ownership invariant count (${DESCRIPTION}): "
            "expected ${EXPECTED}, found ${_count} for '${NEEDLE}'")
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

# The public ownership boundary is deliberately small, typed, bounded to the
# current 16-bit runtime ID domain, and exception/report free at its ABI.
foreach(_marker IN ITEMS
    "inline constexpr std::uint32_t kDatabaseUserMask = UINT32_C(4);"
    "inline constexpr std::uint32_t kCurrentRuntimeStringLimit = UINT32_C(65536);"
    "return stringId > 0 && stringId < kCurrentRuntimeStringLimit;"
    "enum class AcquireStatus : std::uint8_t"
    "enum class TransferStatus : std::uint8_t"
    "enum class ReleaseStatus : std::uint8_t"
    "TryAcquireOrdinaryStringOfSize( const char *bytes, std::uint32_t byteCount, int type) noexcept;"
    "TryTransferOrdinaryToDatabaseUser( std::uint32_t stringId) noexcept;"
    "TryRemoveOrdinaryReference( std::uint32_t stringId) noexcept;"
    "TryRemoveDatabaseUserReference( std::uint32_t stringId) noexcept;")
    require_contains(_ownership_header "${_marker}" "typed no-report API")
endforeach()
require_contains(
    _string_source
    "#include \"scr_string_transaction.h\""
    "private implementation binding")

# Each ID-taking operation validates before entering the table, authenticates
# a live hash-linked allocation, and never falls back to a reporting legacy
# ownership API. Acquisition must also validate the caller's explicit byte
# count and final terminator instead of using strlen.
extract_slice(
    _string_source
    "AcquireResult TryAcquireOrdinaryStringOfSize("
    "TransferStatus TryTransferOrdinaryToDatabaseUser("
    _acquire
    "ordinary acquisition")
extract_slice(
    _string_source
    "TransferStatus TryTransferOrdinaryToDatabaseUser("
    "ReleaseStatus TryRemoveOrdinaryReference("
    _transfer
    "database-user transfer")
extract_slice(
    _string_source
    "ReleaseStatus TryRemoveOrdinaryReference("
    "ReleaseStatus TryRemoveDatabaseUserReference("
    _release_ordinary
    "ordinary rollback")
extract_slice(
    _string_source
    "ReleaseStatus TryRemoveDatabaseUserReference("
    "} // namespace script_string"
    _release_database
    "database-user rollback")
extract_slice(
    _string_source
    "SL_InternStatus SL_TryInternStringOfSize("
    "uint32_t SL_GetStringOfSize("
    _intern
    "report-free intern primitive")
extract_slice(
    _string_source
    "static uint32_t FindStringOfSize("
    "uint32_t SL_FindString("
    _find
    "bounded legacy lookup")
extract_slice(
    _string_source
    "SL_ResolveStatus SL_TryResolveLiveStringNoReport("
    "bool SL_CanDebugRemoveRefNoReport("
    _resolve
    "typed live-string resolution")

foreach(_var IN ITEMS
    _acquire
    _transfer
    _release_ordinary
    _release_database
    _intern)
    foreach(_forbidden IN ITEMS
        "Com_Error("
        "MT_Error("
        "iassert("
        "strlen("
        "SL_GetStringOfSize("
        "SL_TransferRefToUser("
        "SL_RemoveRefToString(")
        require_not_contains(
            ${_var} "${_forbidden}" "report-free ownership path")
    endforeach()
endforeach()
require_contains(
    _acquire
    "bytes[byteCount - 1] != '\\0'"
    "bounded acquisition terminator")
foreach(_var IN ITEMS _transfer _release_ordinary _release_database)
    require_ordered(
        ${_var}
        "if (!IsCurrentRuntimeStringId(stringId))"
        "Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);"
        "range check before table access")
    require_contains(
        ${_var}
        "SL_TryResolveLiveStringNoReport(stringId, &info)"
        "live allocation and hash authentication")
endforeach()
require_contains(
    _transfer
    "refCount <= SL_UserReferenceCount(users)"
    "ordinary-reference ownership check")
require_contains(
    _release_ordinary
    "refCount <= SL_UserReferenceCount( scr_string_atomic::User(info.packed))"
    "ordinary rollback ownership check")
require_contains(
    _release_database
    "(users & databaseUser) == 0"
    "targeted database-user ownership check")
require_contains(
    _intern
    "MT_TryAllocIndex("
    "failure-atomic allocator use")
require_contains(
    _intern
    "MT_TryFreeIndex("
    "failure-atomic allocation cleanup")
require_literal_count(
    _intern
    "SL_TryGetAllocatedStringByteCountNoReport("
    2
    "allocator-backed intern candidate lengths")
require_literal_count(
    _intern
    "candidateByteCount == len"
    2
    "full intern byte-count comparisons")
require_literal_count(
    _find
    "SL_TryGetAllocatedStringByteCountNoReport("
    2
    "allocator-backed legacy lookup lengths")
require_contains(
    _find
    "candidateByteCount != len || memcmp(refStr->str, str, len)"
    "full head byte-count guard before legacy comparison")
require_contains(
    _find
    "candidateByteCount == len && !memcmp(refStr->str, str, len)"
    "full collision-chain byte-count guard before legacy comparison")
require_contains(
    _resolve
    "allocationStatus == MT_AllocationInfoStatus::NotAllocatedNoChange"
    "benign unallocated resolution")
require_contains(
    _resolve
    "return SL_ResolveStatus::UnsafeFailure;"
    "corrupt live-state resolution")
require_contains(
    _transfer
    "resolveStatus == SL_ResolveStatus::NotAllocatedNoChange ? TransferStatus::OwnershipMismatchNoChange : TransferStatus::UnsafeFailure"
    "typed transfer resolve mapping")
foreach(_var IN ITEMS _release_ordinary _release_database)
    require_contains(
        ${_var}
        "resolveStatus == SL_ResolveStatus::NotAllocatedNoChange ? ReleaseStatus::OwnershipMismatchNoChange : ReleaseStatus::UnsafeFailure"
        "typed release resolve mapping")
endforeach()

# The memory-tree try surface must retain its explicit no-change status model,
# validate allocation identity before free, and publish output parameters only
# at the successful tail. The legacy wrappers remain outside these slices.
foreach(_marker IN ITEMS
    "enum class MT_AllocIndexStatus : uint8_t"
    "InsufficientCapacityNoChange"
    "enum class MT_FreeIndexStatus : uint8_t"
    "OwnershipMismatchNoChange"
    "enum class MT_AllocationInfoStatus : uint8_t"
    "NotAllocatedNoChange"
    "[[nodiscard]] MT_FreeIndexStatus MT_TryFreeIndex( uint32_t nodeNum, int numBytes) noexcept;"
    "[[nodiscard]] MT_AllocationInfoStatus MT_TryGetAllocationInfo( uint32_t nodeNum, MT_AllocationInfo *outInfo) noexcept;"
    "[[nodiscard]] MT_AllocIndexStatus MT_TryAllocIndex( int numBytes, int type, uint16_t *outIndex) noexcept;")
    require_contains(_memory_header "${_marker}" "failure-atomic allocator API")
endforeach()
extract_slice(
    _memory_source
    "MT_AllocIndexStatus MT_TryAllocIndex("
    "unsigned short MT_AllocIndex("
    _try_alloc
    "memory-tree allocation")
extract_slice(
    _memory_source
    "MT_AllocationInfoStatus MT_TryGetAllocationInfo("
    "bool MT_Realloc("
    _try_query
    "memory-tree allocation query")
extract_slice(
    _memory_source
    "MT_FreeIndexStatus MT_TryFreeIndex("
    "void MT_FreeIndex("
    _try_free
    "memory-tree free")
foreach(_var IN ITEMS _try_alloc _try_query _try_free)
    foreach(_forbidden IN ITEMS
        "Com_Error("
        "MT_Error("
        "iassert("
        "throw "
        "MT_RemoveHeadMemoryNode("
        "MT_RemoveMemoryNode("
        "MT_AddMemoryNode(")
        require_not_contains(${_var} "${_forbidden}" "allocator try path")
    endforeach()
endforeach()
foreach(_marker IN ITEMS
    "MT_RemoveHeadMemoryNodeCommitNoReport(newSize);"
    "MT_RemoveMemoryNodeCommitNoReport( static_cast<int>(lowBit ^ mergedNode), mergedSize)"
    "MT_AddMemoryNodeCommitNoReport(")
    require_contains(
        _memory_source "${_marker}" "assert-free allocator commit path")
endforeach()
extract_slice(
    _memory_source
    "// Mutation helpers used only after the complete partition"
    "} // namespace"
    _memory_commit_helpers
    "assert-free allocator commit helpers")
foreach(_forbidden IN ITEMS
    "iassert("
    "MyAssertHandler("
    "Com_Error("
    "MT_Error(")
    require_not_contains(
        _memory_commit_helpers
        "${_forbidden}"
        "assert-free allocator commit helpers")
endforeach()
require_ordered(
    _try_alloc
    "Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE); *outIndex = nodeNum;"
    "return MT_AllocIndexStatus::Success;"
    "successful allocation publication")
require_ordered(
    _try_query
    "if (status == MT_AllocationInfoStatus::Success)"
    "*outInfo = allocationInfo;"
    "successful query publication")
require_ordered(
    _try_free
    "MT_GetAllocationInfoLockedNoReport(nodeNum, &allocationInfo)"
    "scrMemTreeDebugGlob.mt_usage[nodeNum] = 0;"
    "allocation identity before free mutation")
foreach(_marker IN ITEMS
    "bool MT_IsGlobalPartitionValidNoReport() noexcept"
    "!MT_TryMarkPartitionRangeNoReport(nodeNum, size)"
    "!MT_TryRecordPartitionFreeNodeNoReport(child)"
    "partitionWord != UINT64_MAX"
    "return MT_IsBasicCoreStateValidNoReport() && MT_IsGlobalPartitionValidNoReport();")
    require_contains(
        _memory_source "${_marker}" "bounded complete allocator partition")
endforeach()
extract_slice(
    _memory_source
    "void MT_UnsafeErrorNoDump("
    "// The public try entries hold CRITSECT_MEMORY_TREE"
    _unsafe_memory_error
    "corruption-safe legacy diagnostic")
require_not_contains(
    _unsafe_memory_error
    "MT_DumpTree("
    "unsafe diagnostic must not traverse corrupt links")
require_contains(
    _unsafe_memory_error
    "Com_Error(ERR_FATAL, \"MT_Error: unsafe memory-tree state (KISAK)\\n\");"
    "bounded terminal unsafe diagnostic")

# A dedicated outer serializer avoids inverting DB hash and script-string
# locks. Tokens are explicit, non-copyable, and do not unlock from a destructor.
foreach(_marker IN ITEMS
    "ScriptStringTransactionToken(const ScriptStringTransactionToken &) = delete;"
    "ScriptStringTransactionToken(ScriptStringTransactionToken &&) = delete;"
    "~ScriptStringTransactionToken() noexcept = default;"
    "RUNTIME_SIZE(ScriptStringTransactionToken, 0x8, 0x8);"
    "TryBeginScriptStringTransaction( ScriptStringTransactionToken *token) noexcept;"
    "FinishScriptStringTransaction( ScriptStringTransactionToken *token) noexcept;"
    "OwnsScriptStringTransaction( const ScriptStringTransactionToken &token) noexcept;")
    require_contains(_transaction_header "${_marker}" "explicit serializer token")
endforeach()
foreach(_forbidden IN ITEMS
    "CRITSECT_SCRIPT_STRING"
    "db_hashCritSect"
    "Com_Error("
    "MT_Error(")
    require_not_contains(
        _transaction_source "${_forbidden}" "dedicated outer serializer")
endforeach()
require_contains(
    _transaction_source
    "Sys_EnterCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);"
    "dedicated serializer acquisition")
require_contains(
    _transaction_source
    "s_activeSerial = 0; token->serial_ = 0; token->active_ = false;"
    "explicit terminal token invalidation")
require_contains(
    _transaction_source
    "token.serial_ == s_activeSerial"
    "token authentication under serializer")
extract_slice(
    _transaction_source
    "ScriptStringTransactionStatus TryBeginScriptStringTransaction("
    "ScriptStringTransactionStatus FinishScriptStringTransaction("
    _begin_transaction
    "serializer begin")
extract_slice(
    _transaction_source
    "ScriptStringTransactionStatus FinishScriptStringTransaction("
    "bool OwnsScriptStringTransaction("
    _finish_transaction
    "serializer finish")
extract_slice(
    _transaction_source
    "bool OwnsScriptStringTransaction("
    "} // namespace db::script_string_transaction"
    _owns_transaction
    "serializer authentication")
foreach(_pair IN ITEMS
    "_begin_transaction;1;2"
    "_finish_transaction;1;3"
    "_owns_transaction;1;1")
    list(GET _pair 0 _slice_var)
    list(GET _pair 1 _enter_count)
    list(GET _pair 2 _leave_count)
    require_literal_count(
        ${_slice_var}
        "Sys_EnterCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);"
        ${_enter_count}
        "dedicated serializer enter")
    require_literal_count(
        ${_slice_var}
        "Sys_LeaveCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);"
        ${_leave_count}
        "dedicated serializer leave")
endforeach()

# The adapter owns the callback table and gates every callback-bearing journal
# transition with an authenticated transaction token.
require_not_contains(
    _adapter_header
    "ScriptStringJournalCallbacks"
    "private callback table")
foreach(_forbidden IN ITEMS
    "Com_Error("
    "MT_Error("
    "strlen("
    "SL_GetString"
    "SL_AddUser"
    "SL_TransferRefToUser"
    "SL_RemoveRefToString")
    require_not_contains(_adapter_source "${_forbidden}" "adapter boundary")
endforeach()
foreach(_marker IN ITEMS
    "script_string::TryAcquireOrdinaryStringOfSize("
    "script_string::TryTransferOrdinaryToDatabaseUser(stringId)"
    "script_string::TryRemoveOrdinaryReference(stringId)"
    "script_string::TryRemoveDatabaseUserReference(stringId)")
    require_contains(_adapter_source "${_marker}" "private no-report callback")
endforeach()
extract_slice(
    _adapter_source
    "TryStageScriptStringFromSource("
    "TryTransferNextScriptStringToDatabaseUser("
    _stage_adapter
    "stage adapter")
extract_slice(
    _adapter_source
    "TryTransferNextScriptStringToDatabaseUser("
    "TryRollbackNextScriptStringOwnership("
    _transfer_adapter
    "transfer adapter")
extract_slice(
    _adapter_source
    "TryRollbackNextScriptStringOwnership("
    "} // namespace db::script_string_adapter"
    _rollback_adapter
    "rollback adapter")
foreach(_pair IN ITEMS
    "_stage_adapter;script_string_journal::TryStageScriptString("
    "_transfer_adapter;script_string_journal::TryTransferNextScriptString("
    "_rollback_adapter;script_string_journal::TryRollbackNextScriptString(")
    list(GET _pair 0 _slice_var)
    list(GET _pair 1 _journal_call)
    require_ordered(
        ${_slice_var}
        "if (!script_string_transaction::OwnsScriptStringTransaction(transaction))"
        "${_journal_call}"
        "transaction gate before journal callback")
endforeach()

# Freeze both engine profiles' appended serializer slot and require compile-time
# plus runtime coverage and production manifest wiring.
foreach(_marker IN ITEMS
    "CRITSECT_DB_SCRIPT_STRING_TRANSACTION = 0x16,"
    "CRITSECT_COUNT = 0x17,"
    "CRITSECT_DB_SCRIPT_STRING_TRANSACTION,"
    "CRITSECT_COUNT,")
    require_contains(_sync_header "${_marker}" "critical-section ABI")
endforeach()
foreach(_marker IN ITEMS
    "static_assert(CRITSECT_DB_SCRIPT_STRING_TRANSACTION == 0x16);"
    "static_assert(CRITSECT_COUNT == 0x17);"
    "static_assert(CRITSECT_DB_SCRIPT_STRING_TRANSACTION == 0x23);"
    "static_assert(CRITSECT_COUNT == 0x24);")
    require_contains(_platform_contract "${_marker}" "MP/SP slot contract")
endforeach()
foreach(_marker IN ITEMS
    "bool TestScriptStringTransactionSerializer()"
    "transaction::TryBeginScriptStringTransaction(nullptr)"
    "transaction::TryBeginScriptStringTransaction(&nested)"
    "ScriptStringTransactionStatus::Busy"
    "transaction::FinishScriptStringTransaction(&owner)"
    "if (!TestScriptStringTransactionSerializer())")
    require_contains(_platform_runtime "${_marker}" "serializer runtime coverage")
endforeach()
foreach(_marker IN ITEMS
    "\${SRC_DIR}/database/db_script_string_adapter.cpp"
    "\${SRC_DIR}/database/db_script_string_adapter.h"
    "\${SRC_DIR}/database/db_script_string_transaction.cpp"
    "\${SRC_DIR}/database/db_script_string_transaction.h"
    "\${SRC_DIR}/script/scr_string_transaction.h")
    require_contains(_manifest "${_marker}" "production manifest")
endforeach()
require_contains(
    _tests
    "\${SRC_DIR}/database/db_script_string_transaction.cpp"
    "serializer runtime target linkage")
foreach(_marker IN ITEMS
    "add_executable(kisakcod-script-memorytree-try-tests"
    "script_memorytree_try_tests.cpp"
    "\${SRC_DIR}/script/scr_memorytree.cpp"
    "NAME script-memorytree-try-contracts")
    require_contains(_tests "${_marker}" "memory-tree runtime fixture wiring")
endforeach()
foreach(_marker IN ITEMS
    "TestInvalidAndNoChange()"
    "TestQueryAndFreeContracts()"
    "TestFullExhaustionAndRecovery()"
    "TestRandomizedIntervals()"
    "TestCorruptionRejection()"
    "MT_TryAllocIndex(13, 2, &outId)"
    "MT_AllocIndex(1, 1) == 0")
    require_contains(_memory_fixture "${_marker}" "memory-tree runtime coverage")
endforeach()

# The ownership runtime fixture compiles the exact file-local production
# string-list implementation once, links the allocator as its production
# translation unit, and must not carry a copied version of either algorithm.
extract_slice(
    _tests
    "add_executable(kisakcod-script-string-ownership-tests"
    "foreach(_profile mp sp)"
    _ownership_fixture_target
    "script-string ownership runtime target")
foreach(_marker IN ITEMS
    "script_string_ownership_tests.cpp"
    "\${SRC_DIR}/script/scr_memorytree.cpp"
    "NAME script-string-report-free-ownership-contracts")
    require_contains(
        _ownership_fixture_target "${_marker}"
        "string ownership runtime fixture wiring")
endforeach()
require_not_contains(
    _ownership_fixture_target
    "\${SRC_DIR}/script/scr_stringlist.cpp"
    "string-list production source compiled twice")
require_contains(
    _ownership_fixture
    "#include <script/scr_stringlist.cpp>"
    "exact production string-list translation unit")
foreach(_forbidden IN ITEMS
    "AcquireResult TryAcquireOrdinaryStringOfSize("
    "SL_InternStatus SL_TryInternStringOfSize("
    "MT_AllocIndexStatus MT_TryAllocIndex(")
    require_not_contains(
        _ownership_fixture "${_forbidden}"
        "copied production ownership algorithm")
endforeach()
foreach(_marker IN ITEMS
    "TestInvalidAndNoChange()"
    "TestRepeatedInternAndDatabaseTransfer()"
    "TestOrdinaryRollbackFreeAndReuse()"
    "TestEmbeddedNulByteCount()"
    "TestCollidingByteLengthBounds()"
    "TestLegacyBinaryInternCompatibility()"
    "TestMalformedStateFailsClosed()"
    "std::vector<char> longBytes(4867);"
    "GetRefString(shortResult.stringId)->str"
    "GetHashCode(shortBytes.data(), shortByteCount) == 1217"
    "GetHashCode(longBytes.data(), longByteCount) == 1217"
    "StateMatches(corruptHash)"
    "StateMatches(corruptDebug)"
    "StateMatches(corruptAllocation)")
    require_contains(
        _ownership_fixture "${_marker}"
        "string ownership runtime coverage")
endforeach()

require_contains(
    _acquire
    "SL_IsRepresentableRefStringBytesNoReport(bytes, byteCount)"
    "ambiguous packed string-length rejection")

# The measured production-capable Windows x86 lane uses an explicit target
# list and ctest filter, so keep every ownership runtime/source gate admitted
# there in addition to the five all-target portable jobs.
foreach(_marker IN ITEMS
    "kisakcod-script-string-atomic-tests"
    "kisakcod-script-memorytree-try-tests"
    "kisakcod-script-string-ownership-tests"
    "kisakcod-platform-service-runtime-tests"
    "script-string-packed-atomic-contracts"
    "script-memorytree-try-contracts"
    "script-string-report-free-ownership-contracts"
    "platform-service-runtime-contracts"
    "database-script-string-ownership-source-invariants")
    require_contains(_ci "${_marker}" "measured Windows x86 ownership gate")
endforeach()
foreach(_marker IN ITEMS
    "NAME database-script-string-ownership-source-invariants"
    "db_script_string_ownership_source_test.cmake")
    require_contains(_tests "${_marker}" "source-contract registration")
endforeach()

message(STATUS "Script-string ownership source contract passed")
