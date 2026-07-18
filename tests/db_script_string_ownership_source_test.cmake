cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_ownership_header_path
    "${SOURCE_ROOT}/src/script/scr_string_transaction.h")
set(_string_header_path
    "${SOURCE_ROOT}/src/script/scr_stringlist.h")
set(_string_source_path
    "${SOURCE_ROOT}/src/script/scr_stringlist.cpp")
set(_script_main_source_path
    "${SOURCE_ROOT}/src/script/scr_main.cpp")
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
set(_memory_lease_seal_path
    "${SOURCE_ROOT}/tests/script_memorytree_lease_api_seal_compile_tests.cpp")
set(_ownership_fixture_path
    "${SOURCE_ROOT}/tests/script_string_ownership_tests.cpp")
set(_ci_path "${SOURCE_ROOT}/.github/workflows/ci.yml")
set(_manifest_path "${SOURCE_ROOT}/scripts/common_files.cmake")
set(_tests_path "${SOURCE_ROOT}/tests/CMakeLists.txt")

foreach(_path IN ITEMS
    "${_ownership_header_path}"
    "${_string_header_path}"
    "${_string_source_path}"
    "${_script_main_source_path}"
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
    "${_memory_lease_seal_path}"
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
file(READ "${_string_header_path}" _string_header)
file(READ "${_string_source_path}" _string_source)
file(READ "${_script_main_source_path}" _script_main_source)
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
file(READ "${_memory_lease_seal_path}" _memory_lease_seal)
file(READ "${_ownership_fixture_path}" _ownership_fixture)
file(READ "${_ci_path}" _ci)
file(READ "${_manifest_path}" _manifest)
file(READ "${_tests_path}" _tests)

# Keep stack-backed/raw script-string representations confined to the audited
# translation units and established callback-free consumers. New lexical
# consumers require an explicit ownership-boundary review.
file(GLOB_RECURSE _script_ownership_census_paths
    LIST_DIRECTORIES false
    "${SOURCE_ROOT}/src/*.cpp"
    "${SOURCE_ROOT}/src/*.h")
set(_memory_pub_consumers
    "script/scr_memorytree.cpp"
    "script/scr_stringlist.cpp"
    "script/scr_stringlist.h"
    "script/scr_variable.cpp")
set(_debug_glob_consumers
    "script/scr_parser.cpp"
    "script/scr_parser.h"
    "script/scr_readwrite.cpp"
    "script/scr_stringlist.cpp"
    "script/scr_stringlist.h"
    "script/scr_variable.cpp"
    "script/scr_variable.h"
    "script/scr_vm.cpp")
foreach(_path IN LISTS _script_ownership_census_paths)
    file(READ "${_path}" _census_source)
    file(RELATIVE_PATH _relative "${SOURCE_ROOT}/src" "${_path}")
    string(FIND "${_census_source}" "SL_RefStringWord(" _word_access)
    if(NOT _word_access EQUAL -1
        AND NOT _relative STREQUAL "script/scr_stringlist.cpp")
        message(FATAL_ERROR
            "Raw RefString word accessor escaped scr_stringlist.cpp: ${_relative}")
    endif()
    string(REGEX MATCH "struct[ \t\r\n]+RefString[ \t\r\n]*\\{" _definition
        "${_census_source}")
    if(_definition
        AND NOT _relative STREQUAL "script/scr_stringlist.cpp")
        message(FATAL_ERROR
            "Complete RefString layout escaped scr_stringlist.cpp: ${_relative}")
    endif()
    string(FIND "${_census_source}" "scrMemTreePub" _memory_pub)
    if(NOT _memory_pub EQUAL -1)
        list(FIND _memory_pub_consumers "${_relative}" _consumer)
        if(_consumer EQUAL -1)
            message(FATAL_ERROR
                "New unaudited scrMemTreePub consumer: ${_relative}")
        endif()
    endif()
    string(FIND "${_census_source}" "scrStringDebugGlob" _debug_glob)
    if(NOT _debug_glob EQUAL -1)
        list(FIND _debug_glob_consumers "${_relative}" _consumer)
        if(_consumer EQUAL -1)
            message(FATAL_ERROR
                "New unaudited scrStringDebugGlob consumer: ${_relative}")
        endif()
    endif()
endforeach()

foreach(_var IN ITEMS
    _ownership_header
    _string_header
    _string_source
    _script_main_source
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
    _memory_lease_seal
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
extract_slice(
    _memory_source
    "void MT_RemoveHeadMemoryNode(int size)"
    "namespace { MT_FreeIndexStatus MT_TryFreeIndexImpl("
    _remove_head_memory_node
    "raw remove-head memory-tree wrapper")
extract_slice(
    _memory_source
    "bool __cdecl MT_RemoveMemoryNode(int oldNode, uint32_t size)"
    "void MT_Free(byte* p, int numBytes)"
    _remove_memory_node
    "raw remove memory-tree wrapper")
extract_slice(
    _memory_source
    "void MT_AddMemoryNode(int newNode, int size)"
    "void MT_Error(const char* funcName, int numBytes)"
    _add_memory_node
    "raw add memory-tree wrapper")
foreach(_var IN ITEMS
    _remove_head_memory_node
    _remove_memory_node
    _add_memory_node)
    require_ordered(
        ${_var}
        "Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE)"
        "MT_RejectUnleasedAccessForActiveLeaseLocked()"
        "raw memory-tree admission is lock-linearized")
    require_ordered(
        ${_var}
        "MT_RejectUnleasedAccessForActiveLeaseLocked()"
        "iassert("
        "frozen raw memory-tree rejection precedes diagnostics")
endforeach()
require_not_contains(
    _memory_source
    "MT_RejectUnleasedAccessForActiveLeaseNoReport"
    "separate legacy boundary check/use window")

# Legacy reads, mutations, and debug captures keep one memory-tree acquisition
# from boundary admission through their final allocator-derived read/commit.
# Assertions, engine reporters, and string formatting remain after release.
foreach(_marker IN ITEMS
    "MT_TryAllocIndexLegacy(numBytes, type, &nodeNum); Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);"
    "const bool oldValid = MT_TryGetSizeNoReport(oldNumBytes, &oldSize); const bool newValid = MT_TryGetSizeNoReport(newNumbytes, &newSize); Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);"
    "const MT_LegacyFreeAttempt attempt = MT_TryFreeIndexLegacyLockedNoReport(nodeNum, numBytes); Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE); MT_ReportLegacyFreeAttempt(attempt, numBytes);"
    "const bool aligned = inRange && (candidate - treeBegin) % sizeof(MemoryNode) == 0;"
    "attempt = MT_TryFreeIndexLegacyLockedNoReport(nodeNum, numBytes); } Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE); iassert(aligned);"
    "const bool valid = MT_TryGetSizeNoReport(numBytes, &size); Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);"
    "const bool validState = validNode && MT_AreBitTablesValidNoReport();"
    "const bool validRoot = validNode && (nodeNum == 0 || mt_freeNodeSizeShadow[nodeNum] != 0);"
    "const bool captured = MT_TryCaptureDumpSnapshotLocked(); Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);"
    "MT_GetAllocationInfoLockedNoReport(nodeNum, &info);"
    "return va( \"%s: '#%u' (%u)\"")
    require_contains(
        _memory_source "${_marker}"
        "lock-linearized legacy memory-tree wrapper")
endforeach()
foreach(_marker IN ITEMS
    "SL_DebugConvertToString("
    "(MemoryNode *)p - scrMemTreeGlob.nodes")
    require_not_contains(
        _memory_source "${_marker}"
        "unsafe legacy debug or pointer operation")
endforeach()
require_ordered(
    _memory_source
    "MT_TryCaptureDumpSnapshotLocked(); Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);"
    "Com_Printf(23, \"memory-tree dump unavailable: unsafe state\\n\");"
    "dump callback follows locked snapshot capture")
require_ordered(
    _memory_source
    "Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE); if (status == MT_AllocationInfoStatus::NotAllocatedNoChange)"
    "return va( \"%s: '#%u' (%u)\""
    "node formatting follows locked bounded capture")
extract_slice(
    _memory_source
    "MT_AllocationInfoStatus MT_GetAllocationInfoLockedNoReport("
    "constexpr uint32_t kPartitionWordBits"
    _allocation_info_local
    "authenticated local allocation query")
require_ordered(
    _allocation_info_local
    "mt_allocationMetadataShadow[nodeNum] != MT_PackAllocationMetadata(type, size)"
    "if (type == 0)"
    "shadow authentication before the unallocated classification")
foreach(_marker IN ITEMS
    "uint32_t low; uint32_t high;"
    "nodeNum < frame.low || nodeNum > frame.high"
    "frame.position - static_cast<int32_t>(nextLevel)"
    "frame.position + static_cast<int32_t>(nextLevel)"
    "parentScore <= MT_GetScoreNoReport(node.prev)"
    "parentScore <= MT_GetScoreNoReport(node.next)")
    require_contains(
        _memory_source "${_marker}"
        "free-tree branch geometry and priority authentication")
endforeach()
require_contains(
    _string_source
    "#include \"scr_string_transaction.h\""
    "private implementation binding")
require_not_contains(
    _memory_source
    "union MTnum_t"
    "inactive-union-member score decoding")

# Debug ownership initialization is callable independently of SL_Init. Keep
# its check/reset/publication transaction serialized, and make duplicate calls
# fail closed even when assertions are compiled out. SL_Init must reject every
# already-published full or debug-only state before resetting the allocator;
# each entry drops its own script-string acquisition before its diagnostic.
extract_slice(
    _string_source
    "void SL_Init()"
    "void SL_InitCheckLeaks()"
    _full_string_init
    "full string initialization")
extract_slice(
    _full_string_init
    "const bool stateAlreadyInitialized = scrStringGlob.inited != 0"
    "MT_Init();"
    _duplicate_full_init
    "duplicate full string initialization")
require_ordered(
    _full_string_init
    "Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);"
    "const bool stateAlreadyInitialized = scrStringGlob.inited != 0"
    "full initialization lock before state inspection")
require_ordered(
    _duplicate_full_init
    "Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);"
    "iassert(!stateAlreadyInitialized);"
    "duplicate full initialization unlock before assertion")
require_ordered(
    _duplicate_full_init
    "iassert(!stateAlreadyInitialized);"
    "return;"
    "duplicate full initialization fail-closed return")
extract_slice(
    _string_source
    "void SL_InitCheckLeaks()"
    "static uint32_t SL_ConvertFromRefString("
    _debug_init
    "debug ownership initialization")
extract_slice(
    _debug_init
    "const bool debugAlreadyInitialized = scrStringDebugGlob != nullptr;"
    "Com_Memset(&scrStringDebugGlobBuf, 0, sizeof(scrStringDebugGlobBuf));"
    _duplicate_debug_init
    "duplicate debug ownership initialization")
extract_slice(
    _string_source
    "Com_Memset(&scrStringDebugGlobBuf, 0, sizeof(scrStringDebugGlobBuf));"
    "static uint32_t SL_ConvertFromRefString("
    _debug_init_reset
    "debug ownership reset tail")
require_ordered(
    _debug_init
    "Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);"
    "const bool debugAlreadyInitialized = scrStringDebugGlob != nullptr;"
    "debug initialization lock before state inspection")
require_ordered(
    _duplicate_debug_init
    "Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);"
    "iassert(!debugAlreadyInitialized);"
    "duplicate debug initialization unlock before assertion")
require_ordered(
    _duplicate_debug_init
    "iassert(!debugAlreadyInitialized);"
    "return;"
    "duplicate debug initialization fail-closed return")
require_ordered(
    _debug_init_reset
    "Com_Memset(&scrStringDebugGlobBuf, 0, sizeof(scrStringDebugGlobBuf));"
    "scrStringDebugGlob = &scrStringDebugGlobBuf;"
    "debug reset before pointer publication")
require_ordered(
    _debug_init_reset
    "scrStringDebugGlob = &scrStringDebugGlobBuf;"
    "Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);"
    "debug pointer publication before lock release")

# Each ID-taking operation validates before entering the table, authenticates
# a live hash-linked allocation, and never falls back to a reporting legacy
# ownership API. Acquisition must also validate the caller's explicit byte
# count and final terminator instead of using strlen.
extract_slice(
    _string_source
    "AcquireResult TryAcquireOrdinaryStringOfSizeInternal("
    "TransferStatus TryTransferOrdinaryToDatabaseUserInternal("
    _acquire
    "ordinary acquisition")
extract_slice(
    _string_source
    "TransferStatus TryTransferOrdinaryToDatabaseUserInternal("
    "ReleaseStatus TryRemoveOrdinaryReferenceInternal("
    _transfer
    "database-user transfer")
extract_slice(
    _string_source
    "ReleaseStatus TryRemoveOrdinaryReferenceInternal("
    "ReleaseStatus TryRemoveDatabaseUserReferenceInternal("
    _release_ordinary
    "ordinary rollback")
extract_slice(
    _string_source
    "ReleaseStatus TryRemoveDatabaseUserReferenceInternal("
    "const MT_ValidationLeaseAdmission & OwnershipBatch::MakeMemoryTreeLeaseAdmission()"
    _release_database
    "database-user rollback")
extract_slice(
    _string_source
    "SL_InternStatus SL_TryInternStringOfSizeWithValidation("
    "SL_InternStatus SL_TryInternStringOfSize("
    _intern_impl
    "report-free intern implementation")
extract_slice(
    _string_source
    "SL_InternStatus SL_TryInternStringOfSize("
    "uint32_t SL_GetStringOfSize("
    _intern
    "report-free intern primitive")
extract_slice(
    _string_source
    "uint32_t SL_GetStringOfSize("
    "const char* SL_ConvertToString("
    _legacy_get
    "legacy intern wrapper")
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
extract_slice(
    _string_source
    "bool SL_IsFreeListHeadValidNoReport()"
    "bool SL_IsFreeEntryReachableNoReport("
    _free_list_validation
    "complete free-list validation")
extract_slice(
    _string_source
    "bool SL_TryBuildUnlinkPlanForScopeNoReport("
    "bool SL_TryBuildUnlinkPlanNoReport("
    _unlink_plan
    "report-free unlink planning")
extract_slice(
    _string_source
    "bool SL_IsFreeEntryLocallyLinkedNoReport("
    "bool SL_IsFreeListHeadValidNoReport("
    _local_free_list_validation
    "bounded legacy free-list validation")
extract_slice(
    _string_source
    "void SL_ShutdownSystem("
    "int SL_IsLowercaseString("
    _shutdown_system
    "legacy user shutdown")
extract_slice(
    _string_source
    "void SL_TransferSystem("
    "uint32_t SL_GetString_("
    _transfer_system
    "legacy user transfer")
extract_slice(
    _string_source
    "bool SL_IsCompleteSystemSweepStateValidNoReport() noexcept"
    "bool SL_TryBuildUnlinkPlanForScopeNoReport("
    _system_sweep_preflight
    "complete system sweep preflight")
extract_slice(
    _string_source
    "bool SL_TryFreeSystemSweepEntryNoReport( const uint32_t owningHash,"
    "bool SL_TryFreeResolvedStringNoReport("
    _system_sweep_free
    "constant-work system sweep free")
extract_slice(
    _string_source
    "static bool SL_FreeString( const uint32_t stringValue,"
    "namespace script_string"
    _legacy_free
    "legacy final free")
extract_slice(
    _string_source
    "bool SL_TryRemoveRefToStringLockedNoReport( const uint32_t stringValue,"
    "void SL_RemoveRefToStringOfSize("
    _legacy_remove
    "legacy ordinary release helper")
extract_slice(
    _string_source
    "void SL_RemoveRefToStringOfSize("
    "void __cdecl SL_AddUser("
    _legacy_remove_wrapper
    "legacy ordinary release wrapper")
extract_slice(
    _legacy_remove
    "if (!validFree)"
    "return validFree;"
    _legacy_remove_rollback
    "legacy ordinary release rollback")

require_contains(
    _free_list_validation
    "memset(sl_freeListVisited, 0, sizeof(sl_freeListVisited));"
    "free-list cycle detection")
require_contains(
    _free_list_validation
    "if (currentIndex >= STRINGLIST_SIZE) return false;"
    "free-list link bounds before scratch and table indexing")
require_contains(
    _free_list_validation
    "return scrStringGlob.hashTable[0].u.prev == previousIndex;"
    "free-list sentinel tail validation")
require_contains(
    _unlink_plan
    "SL_IsFreeListValidForScopeNoReport(scope)"
    "scope-selected free-list validation before unlink publication")

foreach(_marker IN ITEMS
    "previous >= STRINGLIST_SIZE || next >= STRINGLIST_SIZE"
    "static_cast<uint16_t>(previousEntry.status_next) == index"
    "nextEntry.u.prev == index"
    "headEntry.u.prev != 0"
    "static_cast<uint16_t>(tailEntry.status_next) != 0"
    "headNextEntry.u.prev == head"
    "static_cast<uint16_t>(tailPreviousEntry.status_next) == tail")
    require_contains(
        _local_free_list_validation "${_marker}"
        "bounded legacy free-list endpoint authentication")
endforeach()

foreach(_var IN ITEMS
    _acquire
    _transfer
    _release_ordinary
    _release_database
    _intern_impl
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
        "SL_IsTypedOwnershipAccessAuthorizedLocked(validationLease)"
        "if (!IsCurrentRuntimeStringId(stringId))"
        "batch authorization before range classification")
    require_contains(
        ${_var}
        "SL_TryResolveLiveStringNoReport( stringId, &info,"
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
    "SL_ValidationScope::Complete"
    "typed intern selects complete validation")
require_contains(
    _intern_impl
    "SL_TryAllocateStringMemoryNoReport("
    "scope-selected failure-atomic allocator use")
require_contains(
    _intern_impl
    "SL_TryFreeStringMemoryNoReport("
    "scope-selected failure-atomic allocation cleanup")
require_literal_count(
    _intern_impl
    "SL_TryGetAllocatedStringByteCountForScopeNoReport("
    2
    "allocator-backed intern candidate lengths")
require_literal_count(
    _intern_impl
    "candidateByteCount == len"
    2
    "full intern byte-count comparisons")
require_literal_count(
    _find
    "SL_TryGetAllocatedStringByteCountForScopeNoReport("
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
    _find
    "SL_ValidationScope::LegacyLocal"
    "bounded legacy lookup allocator validation")
foreach(_forbidden IN ITEMS "Com_Error(" "MT_Error(")
    require_not_contains(
        _find "${_forbidden}" "report-free locked legacy lookup")
endforeach()

require_contains(
    _legacy_get
    "SL_TryInternStringOfSizeWithValidation( str, user, len, type, &stringValue, SL_ValidationScope::LegacyLocal)"
    "legacy intern selects bounded validation")
require_not_contains(
    _legacy_get
    "Sys_EnterCriticalSection("
    "legacy reporting wrapper owns no lock")
require_ordered(
    _legacy_get
    "SL_TryInternStringOfSizeWithValidation("
    "switch (status)"
    "legacy intern maps status only after helper unlock")

foreach(_marker IN ITEMS
    "SL_TryBuildUnlinkPlanForScopeNoReport( stringValue, refString, byteCount, &unlink, SL_ValidationScope::LegacyLocal)"
    "MT_TryFreeIndexLegacy( stringValue, static_cast<int>(byteCount + kRefStringHeaderSize))"
    "SL_CommitUnlinkPlanNoReport(unlink);")
    require_contains(
        _legacy_free "${_marker}" "bounded legacy final-free transaction")
endforeach()
foreach(_forbidden IN ITEMS "Com_Error(" "MT_Error(" "iassert(")
    require_not_contains(
        _legacy_free "${_forbidden}"
        "report-free legacy final-free transaction")
endforeach()

foreach(_marker IN ITEMS
    "SL_IsDebugOwnershipExactNoReport(stringValue, packed)"
    "Sys_AtomicStore(SL_RefStringWord(refStr), packed);"
    "SL_DebugAddRefNoReport(stringValue);"
    "SL_DebugRemoveRefNoReport(stringValue);"
    "!result.reachedZero || SL_FreeString(stringValue, refStr, byteCount)")
    require_contains(
        _legacy_remove "${_marker}" "legacy final-release rollback")
endforeach()
require_ordered(
    _legacy_remove_rollback
    "Sys_AtomicStore(SL_RefStringWord(refStr), packed);"
    "SL_DebugAddRefNoReport(stringValue);"
    "legacy ownership rollback restores packed state before debug state")
require_contains(
    _legacy_remove_wrapper
    "const bool released = SL_TryRemoveRefToStringLockedNoReport( stringValue, len, true); Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING); (void)released; iassert(released);"
    "legacy release completes before unlock and reports after unlock")

foreach(_marker IN ITEMS
    "for (uint32_t owningHash = 1; owningHash < STRINGLIST_SIZE && !invalidTransition; ++owningHash)"
    "for (uint32_t visited = 0; visited < STRINGLIST_SIZE && !invalidTransition; ++visited)"
    "SL_TryGetAllocatedStringByteCountForScopeNoReport( stringValue, &refStr, &len, SL_ValidationScope::LegacyLocal)"
    "const uint32_t packedBefore = scr_string_atomic::Load(SL_RefStringWord(refStr));"
    "SL_DebugRemoveRefNoReport(stringValue);"
    "SL_TryFreeSystemSweepEntryNoReport( owningHash, targetIndex, previousIndex, stringValue, refStr, len)"
    "Sys_AtomicStore(SL_RefStringWord(refStr), packedBefore);"
    "SL_DebugAddRefNoReport(stringValue);"
    "scrStringGlob.nextFreeEntry = nullptr;")
    require_contains(
        _shutdown_system "${_marker}"
        "linear legacy shutdown rollback and termination")
endforeach()
require_ordered(
    _shutdown_system
    "SL_IsCompleteSystemSweepStateValidNoReport()"
    "for (uint32_t owningHash = 1;"
    "legacy shutdown preflights the complete table before mutation")
foreach(_forbidden IN ITEMS
    "SL_FreeString("
    "SL_TryBuildUnlinkPlanForScopeNoReport("
    "SL_TryBuildUnlinkPlanNoReport(")
    require_not_contains(
        _shutdown_system "${_forbidden}"
        "system shutdown cannot rewalk a collision chain per free")
endforeach()
foreach(_marker IN ITEMS
    "SL_IsFreeListLocallyValidNoReport()"
    "MT_TryGetAllocationInfoLegacy(stringValue, &allocationInfo)"
    "SL_IsExactStringAllocationNoReport(allocationInfo, byteCount)"
    "SL_IsDebugOwnershipExactNoReport(stringValue, packed)"
    "SL_IsHashEntryEncodingValidNoReport(target)"
    "MT_TryFreeIndexLegacy( stringValue, static_cast<int>(byteCount + kRefStringHeaderSize))"
    "SL_CommitUnlinkPlanNoReport(plan);")
    require_contains(
        _system_sweep_free "${_marker}"
        "constant-work authenticated system sweep free")
endforeach()
foreach(_forbidden IN ITEMS
    "for ("
    "while ("
    "SL_TryBuildUnlinkPlanForScopeNoReport("
    "SL_TryBuildUnlinkPlanNoReport(")
    require_not_contains(
        _system_sweep_free "${_forbidden}"
        "system sweep free cannot walk a collision chain")
endforeach()
require_contains(
    _shutdown_system
    "Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING); if (invalidTransition) Com_Error("
    "legacy shutdown reporter after unlock")
foreach(_marker IN ITEMS
    "SL_TryGetAllocatedStringByteCountForScopeNoReport( stringValue, &refStr, &byteCount, SL_ValidationScope::LegacyLocal)"
    "SL_DebugRemoveRefNoReport(stringValue);"
    "Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING); if (invalidTransition) Com_Error(")
    require_contains(
        _transfer_system "${_marker}"
        "bounded report-free legacy system transfer")
endforeach()
require_ordered(
    _transfer_system
    "SL_IsCompleteSystemSweepStateValidNoReport()"
    "for (uint32_t hash = 1;"
    "legacy transfer preflights the complete table before mutation")
foreach(_var IN ITEMS _shutdown_system _transfer_system)
    require_not_contains(
        ${_var}
        "SL_IsLegacyLookupHashStateValidNoReport(hash)"
        "quadratic per-physical-entry chain revalidation after global preflight")
endforeach()
foreach(_marker IN ITEMS
    "SL_IsHashEntryEncodingValidNoReport(entry)"
    "MT_TryValidateState()"
    "SL_IsFreeListHeadValidNoReport()"
    "memset(sl_systemSweepHashEntries, 0, sizeof(sl_systemSweepHashEntries));"
    "for (uint32_t owningHash = 1; owningHash < STRINGLIST_SIZE; ++owningHash)"
    "for (uint32_t visited = 0; visited < STRINGLIST_SIZE; ++visited)"
    "(sl_systemSweepHashEntries[entryByte] & entryMask) != 0"
    "(sl_systemSweepStringIds[stringByte] & stringMask) != 0"
    "GetHashCode(refString->str, byteCount) != owningHash"
    "SL_IsDebugOwnershipExactNoReport(stringValue, packed)"
    "aggregateRefCount > UINT32_MAX"
    "Sys_AtomicLoad(&scrStringDebugGlob->totalRefCount) != static_cast<uint32_t>(aggregateRefCount)")
    require_contains(
        _system_sweep_preflight "${_marker}"
        "linear complete hash/debug/allocator sweep preflight")
endforeach()
require_contains(
    _system_sweep_preflight
    "if (scope != SL_ValidationScope::Leased)"
    "legacy system sweeps must retain exclusive debug-slot validation")
foreach(_marker IN ITEMS
    "constexpr uint32_t kHashEntryBits = HASH_STAT_MASK | UINT32_C(0xFFFF);"
    "return (entry.status_next & ~kHashEntryBits) == 0;")
    require_contains(
        _string_source "${_marker}"
        "central hash-entry encoding authentication")
endforeach()
foreach(_marker IN ITEMS
    "savedHashEntry.status_next | UINT32_C(0x40000)"
    "TestShutdownMixedCollisionChain()"
    "emptyListScratchCleared")
    require_contains(
        _ownership_fixture "${_marker}"
        "hash/free-list/collision runtime regression")
endforeach()
require_ordered(
    _free_list_validation
    "memset(sl_freeListVisited, 0, sizeof(sl_freeListVisited));"
    "if (freeHead == 0)"
    "free-list scratch reset before valid empty-list return")
require_contains(
    _string_source
    "scrStringGlob.nextFreeEntry = nullptr; scrStringGlob.hashTable[0].status_next = 0;"
    "canonical hash iteration state on initialization")

foreach(_var IN ITEMS
    _acquire _transfer _release_ordinary _release_database _resolve _intern)
    require_not_contains(
        ${_var} "Legacy(" "typed ownership path cannot use bounded legacy APIs")
endforeach()
require_contains(
    _resolve
    "allocationStatus == MT_AllocationInfoStatus::NotAllocatedNoChange"
    "benign unallocated resolution")
require_contains(
    _resolve
    "SL_TryRecoverRefStringByteCountNoReport( info.refString, info.packed, allocationInfo, &info.byteCount)"
    "compact binary live-string resolution")
require_literal_count(
    _string_source
    "SL_TryRecoverRefStringByteCountNoReport("
    3
    "shared compact binary length recovery definition and callers")
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
foreach(_marker IN ITEMS
    "[[nodiscard]] MT_FreeIndexStatus MT_TryFreeIndexLegacy( uint32_t nodeNum, int numBytes) noexcept;"
    "[[nodiscard]] MT_AllocationInfoStatus MT_TryGetAllocationInfoLegacy( uint32_t nodeNum, MT_AllocationInfo *outInfo) noexcept;"
    "[[nodiscard]] MT_AllocIndexStatus MT_TryAllocIndexLegacy( int numBytes, int type, uint16_t *outIndex) noexcept;"
    "Typed transaction code must use the complete MT_Try* surface.")
    require_contains(
        _memory_header "${_marker}" "bounded legacy allocator API")
endforeach()
foreach(_marker IN ITEMS
    "enum class MT_ValidationLeaseStatus : uint8_t"
    "class MT_ValidationLeaseAdmission final"
    "static constinit MT_ValidationLeaseAdmission capability;"
    "return &admission == &Canonical();"
    "friend class script_string::OwnershipBatch;"
    "static_assert(!std::is_default_constructible_v<MT_ValidationLeaseAdmission>);"
    "static_assert(!std::is_trivially_copyable_v<MT_ValidationLeaseAdmission>);"
    "class MT_ValidationLease final"
    "~MT_ValidationLease() noexcept;"
    "RUNTIME_SIZE(MT_ValidationLease, 0x10, 0x10);"
    "MT_TryBeginValidationLease( MT_ValidationLease *lease, const MT_ValidationLeaseAdmission &admission) noexcept;"
    "MT_FinishValidationLease( MT_ValidationLease *lease, const MT_ValidationLeaseAdmission &admission) noexcept;"
    "MT_TryAllocIndexLeased( MT_ValidationLease &lease, int numBytes, int type, uint16_t *outIndex, const MT_ValidationLeaseAdmission &admission) noexcept;"
    "MT_TryGetAllocationInfoLeased( MT_ValidationLease &lease, uint32_t nodeNum, MT_AllocationInfo *outInfo, const MT_ValidationLeaseAdmission &admission) noexcept;"
    "MT_TryFreeIndexLeased( MT_ValidationLease &lease, uint32_t nodeNum, int numBytes, const MT_ValidationLeaseAdmission &admission) noexcept;")
    require_contains(
        _memory_header "${_marker}" "authenticated allocator lease API")
endforeach()
foreach(_marker IN ITEMS
    "if (!MT_ValidationLeaseAdmission::Authenticates(admission)) return MT_ValidationLeaseStatus::InvalidArgument;"
    "if (!MT_ValidationLeaseAdmission::Authenticates(admission)) return MT_AllocIndexStatus::InvalidArgumentNoChange;"
    "if (!MT_ValidationLeaseAdmission::Authenticates(admission)) return MT_AllocationInfoStatus::InvalidArgumentNoChange;"
    "if (!MT_ValidationLeaseAdmission::Authenticates(admission)) return MT_FreeIndexStatus::InvalidArgumentNoChange;")
    require_contains(
        _memory_source "${_marker}" "allocator lease operation capability")
endforeach()
extract_slice(
    _memory_source
    "MT_AllocIndexStatus MT_TryAllocIndexImpl("
    "MT_AllocationInfoStatus MT_TryGetAllocationInfoImpl("
    _try_alloc
    "memory-tree allocation")
extract_slice(
    _memory_source
    "MT_AllocationInfoStatus MT_TryGetAllocationInfoImpl("
    "} // namespace"
    _try_query
    "memory-tree allocation query")
extract_slice(
    _memory_source
    "MT_FreeIndexStatus MT_TryFreeIndexImpl("
    "} // namespace"
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
    "return MT_TryAllocIndexImpl( numBytes, type, outIndex, MT_ValidationPolicy::Complete, nullptr);"
    "return MT_TryAllocIndexImpl( numBytes, type, outIndex, MT_ValidationPolicy::LegacyLocal, nullptr);"
    "return MT_TryAllocIndexImpl( numBytes, type, outIndex, MT_ValidationPolicy::Leased, &lease);"
    "return MT_TryGetAllocationInfoImpl( nodeNum, outInfo, MT_ValidationPolicy::Complete, nullptr);"
    "return MT_TryGetAllocationInfoImpl( nodeNum, outInfo, MT_ValidationPolicy::LegacyLocal, nullptr);"
    "return MT_TryGetAllocationInfoImpl( nodeNum, outInfo, MT_ValidationPolicy::Leased, &lease);"
    "return MT_TryFreeIndexImpl( nodeNum, numBytes, MT_ValidationPolicy::Complete, nullptr);"
    "return MT_TryFreeIndexImpl( nodeNum, numBytes, MT_ValidationPolicy::LegacyLocal, nullptr);"
    "return MT_TryFreeIndexImpl( nodeNum, numBytes, MT_ValidationPolicy::Leased, &lease);")
    require_contains(
        _memory_source "${_marker}" "allocator validation policy routing")
endforeach()
require_contains(
    _try_alloc
    "MT_ValidatePolicyEntryLocked(policy, lease, false)"
    "allocation selects authenticated validation policy")
require_contains(
    _try_query
    "MT_ValidatePolicyEntryLocked(policy, lease, true)"
    "query selects authenticated validation policy")
require_contains(
    _try_free
    "MT_ValidatePolicyEntryLocked(policy, lease, false)"
    "free selects authenticated validation policy")
foreach(_marker IN ITEMS
    "enum class MT_ValidationPolicy : uint8_t { Complete, LegacyLocal, Leased, };"
    "return mt_activeValidationLeaseAddress != 0 || mt_activeValidationLeaseSerial != 0"
    "MT_IsValidationLeaseBoundaryFrozenLocked()"
    "enum class MT_ValidationLeaseLifecycle : uint8_t { Idle, Active, Poisoned, Frozen, };"
    "Registry consistency is deliberately by value."
    "MT_CanReadValidationLeaseSnapshotLocked("
    "mt_activeValidationLeaseAddress == leaseAddress && mt_activeValidationLeaseAddressMirror == leaseAddress"
    "thread_local uintptr_t mt_retainedValidationLeaseAddress = 0;"
    "mt_validationLeaseLifecycle = MT_ValidationLeaseLifecycle::Poisoned; mt_validationLeaseLifecycleMirror = MT_ValidationLeaseLifecycle::Poisoned;"
    "MT_FreezeValidationLeaseBoundaryLocked(); MT_ValidationLeaseAccess::Poison(*this);"
    "releaseRetainedAcquisition && !retainedAuthenticated"
    "MT_IsBasicAccountingStateValidNoReport() : MT_IsBasicCoreStateValidNoReport()"
    "policy == MT_ValidationPolicy::Complete ? MT_IsCoreStateValidNoReport()"
    "mt_nextValidationLeaseSerial == UINT64_MAX"
    "MT_ValidationLeaseAccess::CanCountMutation(*lease)"
    "MT_IsCoreStateValidNoReport(); MT_ClearValidationLeaseRegistryLocked();"
    "MT_RejectUnleasedAccessForActiveLeaseLocked()")
    require_contains(
        _memory_source "${_marker}" "fail-closed allocator lease policy")
endforeach()
extract_slice(
    _memory_source
    "bool MT_CanReadValidationLeaseSnapshotLocked( const MT_ValidationLease *const lease) noexcept {"
    "bool MT_RegistryNamesValidationLeaseStorageLocked( const MT_ValidationLease *const lease) noexcept {"
    _lease_snapshot_authentication
    "validation lease snapshot authentication")
require_contains(
    _lease_snapshot_authentication
    "return MT_OwnsValidationLeaseLocked(lease);"
    "snapshots authenticate the live token before member reads")
extract_slice(
    _memory_source
    "bool MT_OwnsValidationLeaseLocked( const MT_ValidationLease *const lease) noexcept {"
    "bool MT_IsValidationLeasePoisonedLocked( const MT_ValidationLease *const lease) noexcept {"
    _lease_owner_authentication
    "validation lease owner authentication")
require_ordered(
    _lease_owner_authentication
    "if (!lease) return false;"
    "mt_activeValidationLeaseAddress != leaseAddress"
    "null rejection precedes validation token address checks")
require_ordered(
    _lease_owner_authentication
    "mt_activeValidationLeaseAddressMirror != leaseAddress"
    "MT_ValidationLeaseAccess::Active(*lease)"
    "both validation token addresses precede token member reads")
require_ordered(
    _lease_owner_authentication
    "!MT_IsValidationLeaseRegistryConsistentLocked()"
    "MT_ValidationLeaseAccess::Active(*lease)"
    "registry authentication precedes token member reads")
foreach(_var IN ITEMS _try_alloc _try_free)
    require_not_contains(
        ${_var}
        "MT_IsFreeTreeForestValidNoReport()"
        "bounded legacy mutation cannot walk the complete free forest")
endforeach()

# Freeze the deliberately narrow compatibility surface. New Legacy callers
# must be reviewed instead of silently weakening typed transaction validation.
require_literal_count(
    _memory_source "MT_TryAllocIndexLegacy(" 2
    "legacy allocator definition and wrapper call")
require_literal_count(
    _memory_source "MT_TryGetAllocationInfoLegacy(" 1
    "legacy allocator query definition")
require_literal_count(
    _memory_source "MT_TryFreeIndexLegacy(" 2
    "legacy allocator free definition and wrapper call")
require_literal_count(
    _string_source "MT_TryAllocIndexLegacy(" 1
    "legacy string allocation helper")
require_literal_count(
    _string_source "MT_TryGetAllocationInfoLegacy(" 2
    "shared legacy string query helper and system sweep query")
require_literal_count(
    _string_source "MT_TryFreeIndexLegacy(" 3
    "legacy string cleanup, final, and sweep free")

foreach(_marker IN ITEMS
    "uint16_t mt_preflightVisitedNodes[MEMORY_NODE_COUNT];"
    "uint16_t mt_preflightTransactionNodes[MEMORY_NODE_COUNT];"
    "void MT_ClearRecordedNodes( uint8_t *const visited, const uint16_t *const nodes, const uint32_t count) noexcept"
    "for (uint32_t index = 0; index < mt_preflightVisitedCount; ++index)"
    "mt_preflightVisitedNodes[mt_preflightVisitedCount++] = nodeNum;")
    require_contains(
        _memory_source "${_marker}" "path-bounded preflight scratch reset")
endforeach()
require_not_contains(
    _memory_source
    "memset(mt_preflightVisited, 0, sizeof(mt_preflightVisited))"
    "global preflight bitset reset on bounded legacy path")
foreach(_marker IN ITEMS
    "uint16_t sl_hashChainVisitedEntries[STRINGLIST_SIZE];"
    "uint16_t sl_stringIdVisitedEntries[STRINGLIST_SIZE];"
    "SL_TryRecordHashEntryNoReport(entryIndex)"
    "sl_hashChainVisitedEntries[sl_hashChainVisitedCount++]"
    "sl_stringIdVisitedEntries[sl_stringIdVisitedCount++]")
    require_contains(
        _string_source "${_marker}"
        "collision-chain-bounded validation scratch reset")
endforeach()
require_not_contains(
    _string_source
    "memset(sl_hashChainVisited"
    "global hash-entry scratch reset on bounded legacy path")
require_not_contains(
    _string_source
    "memset(sl_stringIdVisited"
    "global string-ID scratch reset on bounded legacy path")
foreach(_marker IN ITEMS
    "static uint16_t mt_allocationMetadataShadow[MEMORY_NODE_COUNT];"
    "static uint8_t mt_freeNodeSizeShadow[MEMORY_NODE_COUNT];"
    "static uint16_t mt_freeTreeHeadShadow[MEMORY_NODE_BITS + 1];"
    "static MT_FreeNodeLinkShadow mt_freeNodeLinkShadow[MEMORY_NODE_COUNT];"
    "RUNTIME_SIZE(MT_FreeNodeLinkShadow, 0x4, 0x4);"
    "static uint32_t mt_freeNodeCountShadow;"
    "static uint32_t mt_freeNodeCountMirror;"
    "static int mt_totalAllocShadow;"
    "static int mt_totalAllocBucketsShadow;"
    "bool MT_IsAllocationIntervalClearNoReport("
    "bool MT_IsAllocationIntervalExactNoReport("
    "bool MT_AreFreeNodeLinksAuthenticatedNoReport("
    "bool MT_IsFreeTreeForestValidNoReport("
    "bool MT_IsFreeTreeIntervalValidNoReport("
    "return reachableCount == mt_freeNodeCountShadow;"
    "!recorded && (mt_freeNodeLinkShadow[nodeNum].prev != 0 || mt_freeNodeLinkShadow[nodeNum].next != 0)"
    "MT_IsFreeTreeIntervalValidNoReport( nodeNum, newSize, nodeNum, newSize)"
    "MT_IsFreeTreeIntervalValidNoReport( nodeNum, allocationInfo.size, 0, -1)"
    "MT_IsFreeTreeIntervalValidNoReport( buddyNode, mergedSize, static_cast<uint16_t>(buddyNode), mergedSize)"
    "mt_allocationMetadataShadow[nodeNum] = MT_PackAllocationMetadata("
    "mt_allocationMetadataShadow[nodeNum] = 0;")
    require_contains(
        _memory_source "${_marker}"
        "touched allocation interval and free-tree alias authentication")
endforeach()
require_contains(
    _memory_source
    "mt_freeNodeLinkShadow[0].next == 0 && MT_AreFreeTreeHeadsAuthenticatedNoReport()"
    "basic validation authenticates all fixed-width free-tree heads")
require_contains(
    _memory_source
    "mt_freeNodeCountShadow == mt_freeNodeCountMirror"
    "bounded validation authenticates free-node count accounting")
require_contains(
    _memory_source
    "if (node.prev != shadow.prev || node.next != shadow.next) return false;"
    "each consumed primary free-node link matches its shadow")
require_ordered(
    _memory_source
    "MT_IsFreeTreeForestValidNoReport() &&"
    "MT_IsGlobalPartitionValidNoReport();"
    "complete validation retains forest before partition validation")
require_contains(
    _memory_header
    "void MT_CorruptAllocationMetadataForTesting( uint32_t nodeNum, uint8_t type, uint8_t size) noexcept;"
    "test-only metadata corruption hook")
require_contains(
    _memory_header
    "void MT_CorruptFreeNodeMembershipForTesting( uint32_t nodeNum, uint8_t membership) noexcept;"
    "test-only free-membership corruption hook")
foreach(_marker IN ITEMS
    "scrMemTreeGlob.totalAlloc == mt_totalAllocShadow"
    "scrMemTreeGlob.totalAllocBuckets == mt_totalAllocBucketsShadow"
    "++mt_totalAllocShadow;"
    "--mt_totalAllocShadow;"
    "++mt_freeNodeCountShadow;"
    "--mt_freeNodeCountShadow;")
    require_contains(
        _memory_source "${_marker}"
        "authenticated allocator accounting mirrors")
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
    "// Mutation helpers used only after accounting, metadata"
    "} // namespace"
    _memory_commit_helpers
    "assert-free allocator commit helpers")
require_literal_count(
    _memory_commit_helpers
    "*parentNode ="
    12
    "primary free-tree cursor bindings and publications")
require_literal_count(
    _memory_commit_helpers
    "*parentShadow ="
    12
    "paired free-tree shadow cursor bindings and publications")
foreach(_marker IN ITEMS
    "scrMemTreeGlob.nodes[oldNode] = displacedValue; mt_freeNodeLinkShadow[oldNode] = { displacedValue.prev, displacedValue.next, };"
    "scrMemTreeGlob.nodes[newNode] = scrMemTreeGlob.nodes[node]; mt_freeNodeLinkShadow[newNode] = { scrMemTreeGlob.nodes[newNode].prev, scrMemTreeGlob.nodes[newNode].next, };"
    "scrMemTreeGlob.nodes[newNode].prev = 0; scrMemTreeGlob.nodes[newNode].next = 0; mt_freeNodeLinkShadow[newNode] = {};")
    require_contains(
        _memory_commit_helpers "${_marker}"
        "free-node primary copy/reset mirrors its topology shadow")
endforeach()
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
foreach(_marker IN ITEMS
    "MT_CompleteForestValidationCountForTesting() == 0"
    "TestRawFreeTreeMutatorsSynchronizeShadows()"
    "TestTopologyShadowCorruptionFailsClosed()"
    "sizeof(mt_freeNodeLinkShadow)")
    require_contains(
        _memory_fixture "${_marker}"
        "topology shadow runtime and deterministic cost coverage")
endforeach()
foreach(_marker IN ITEMS
    "++mt_freeNodeCountShadow"
    "mt_freeNodeCountMirror == mt_freeNodeCountShadow")
    require_contains(
        _memory_fixture "${_marker}"
        "one-sided free-node count corruption and synchronization")
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
    "return MT_IsBasicCoreStateValidNoReport() && MT_IsFreeTreeForestValidNoReport() && MT_IsGlobalPartitionValidNoReport();")
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

# Ownership batches retain the string lock before the allocator lease, keep
# only pointer-free by-value stack identities, rebuild the complete string
# certificate at the two boundaries, and route operations through leased
# bounded validation. They deliberately remain below the zone/controller
# layer and freeze permanently on abandoned lifetime.
foreach(_marker IN ITEMS
    "enum class OwnershipBatchStatus : std::uint8_t"
    "class OwnershipBatch final"
    "~OwnershipBatch() noexcept;"
    "OwnershipBatch(const OwnershipBatch &) = delete;"
    "OwnershipBatch(OwnershipBatch &&) = delete;"
    "RUNTIME_SIZE(OwnershipBatch, 0x20, 0x20);"
    "static_assert(std::is_standard_layout_v<OwnershipBatch>);"
    "static_assert(!std::is_trivially_destructible_v<OwnershipBatch>);"
    "static const MT_ValidationLeaseAdmission & MakeMemoryTreeLeaseAdmission() noexcept;"
    "static MT_ValidationLeaseStatus TryBeginMemoryTreeLease( MT_ValidationLease &lease) noexcept;"
    "static MT_ValidationLeaseStatus FinishMemoryTreeLease( MT_ValidationLease &lease) noexcept;"
    "static MT_AllocIndexStatus TryAllocateMemoryTreeIndex( MT_ValidationLease &lease, int numBytes, int type, std::uint16_t *outIndex) noexcept;"
    "static MT_AllocationInfoStatus TryGetMemoryTreeAllocation( MT_ValidationLease &lease, std::uint32_t nodeNum, MT_AllocationInfo *outInfo) noexcept;"
    "static MT_FreeIndexStatus TryFreeMemoryTreeIndex( MT_ValidationLease &lease, std::uint32_t nodeNum, int numBytes) noexcept;"
    "TryBeginOwnershipBatch( OwnershipBatch *batch) noexcept;"
    "FinishOwnershipBatch( OwnershipBatch *batch) noexcept;"
    "TryAcquireOrdinaryStringOfSize( OwnershipBatch &batch, const char *bytes, std::uint32_t byteCount, int type) noexcept;"
    "TryTransferOrdinaryToDatabaseUser( OwnershipBatch &batch, std::uint32_t stringId) noexcept;"
    "TryRemoveOrdinaryReference( OwnershipBatch &batch, std::uint32_t stringId) noexcept;"
    "TryRemoveDatabaseUserReference( OwnershipBatch &batch, std::uint32_t stringId) noexcept;")
    require_contains(
        _ownership_header "${_marker}" "authenticated ownership batch API")
endforeach()
foreach(_marker IN ITEMS
    "CRITSECT_SCRIPT_STRING first, then CRITSECT_MEMORY_TREE"
    "one bounded, callback-free ownership loop"
    "using only the four batch overloads below. No legacy string API, reporter,"
    "callback, or unrelated memory-tree work may run while a batch is active."
    "Storage-lifetime and thread-affinity contract"
    "an exactly authenticated destructor releases only the acquisitions proven to")
    require_contains(
        _ownership_header "${_marker}" "ownership batch scope contract")
endforeach()

foreach(_marker IN ITEMS
    "std::uint64_t sl_nextOwnershipBatchSerial = 0;"
    "std::uintptr_t sl_activeOwnershipBatchAddress = 0;"
    "std::uint64_t sl_activeOwnershipBatchSerial = 0;"
    "std::uintptr_t sl_activeOwnershipBatchAddressMirror = 0;"
    "std::uint64_t sl_activeOwnershipBatchSerialMirror = 0;"
    "std::uintptr_t sl_activeOwnershipBatchNestedLeaseAddress = 0;"
    "std::uintptr_t sl_activeOwnershipBatchNestedLeaseAddressMirror = 0;"
    "enum class SL_OwnershipBatchLifecycle : std::uint8_t { Idle, Active, Poisoned, Frozen, };"
    "thread_local std::uintptr_t sl_retainedOwnershipBatchAddress = 0;"
    "thread_local std::uintptr_t sl_retainedOwnershipBatchNestedLeaseAddress = 0;"
    "std::uint64_t sl_abandonedOwnershipBatchPoison = 0;"
    "std::uint64_t sl_abandonedOwnershipBatchPoisonMirror = 0;"
    "SL_IsOwnershipBatchRegistryConsistentLocked() noexcept"
    "sl_activeOwnershipBatchAddress != sl_activeOwnershipBatchAddressMirror"
    "sl_activeOwnershipBatchSerial != sl_activeOwnershipBatchSerialMirror"
    "Registry consistency is deliberately pointer-free."
    "SL_FreezeOwnershipBatchBoundaryLocked() noexcept"
    "SL_IsAuthorizedOwnershipLeaseLocked( MT_ValidationLease *const lease) noexcept")
    require_contains(
        _string_source "${_marker}" "mirrored ownership batch registry")
endforeach()
foreach(_forbidden IN ITEMS
    "script_string::OwnershipBatch *sl_activeOwnershipBatch"
    "reinterpret_cast<script_string::OwnershipBatch *>("
    "reinterpret_cast<OwnershipBatch *>("
    "sl_activeOwnershipBatch->")
    require_not_contains(
        _string_source "${_forbidden}"
        "stored ownership-batch pointer or address conversion")
endforeach()

extract_slice(
    _string_source
    "bool SL_OwnsOwnershipBatchLocked( const script_string::OwnershipBatch *const batch) noexcept {"
    "#if defined(KISAK_MEMORY_TREE_VALIDATION_TESTING)"
    _ownership_batch_auth
    "ownership batch live-storage authentication")
require_ordered(
    _ownership_batch_auth
    "sl_activeOwnershipBatchAddressMirror != address"
    "OwnershipBatchAccess::MemoryTreeLease(*batch)"
    "both outer addresses authenticate before nested member access")
require_ordered(
    _ownership_batch_auth
    "!SL_IsOwnershipBatchRegistryConsistentLocked()"
    "OwnershipBatchAccess::Active(*batch)"
    "pointer-free registry authentication precedes local member reads")

extract_slice(
    _string_source
    "OwnershipBatchStatus TryBeginOwnershipBatch("
    "OwnershipBatchStatus FinishOwnershipBatch("
    _ownership_batch_begin
    "ownership batch admission")
extract_slice(
    _string_source
    "OwnershipBatchStatus FinishOwnershipBatch("
    "AcquireResult TryAcquireOrdinaryStringOfSize("
    _ownership_batch_finish
    "ownership batch close")
require_ordered(
    _ownership_batch_begin
    "Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);"
    "SL_HasOwnershipBatchRegistryActivityLocked()"
    "frozen/active registry rejection precedes supplied token access")
require_ordered(
    _ownership_batch_begin
    "SL_HasOwnershipBatchRegistryActivityLocked()"
    "OwnershipBatchAccess::IsCanonicalClear(*batch)"
    "begin rejects abandonment before token member reads")
require_ordered(
    _ownership_batch_begin
    "Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);"
    "OwnershipBatchAccess::TryBeginMemoryTreeLease(memoryTreeLease)"
    "script lock before allocator lease admission")
require_ordered(
    _ownership_batch_begin
    "sl_nextOwnershipBatchSerial == UINT64_MAX"
    "OwnershipBatchAccess::TryBeginMemoryTreeLease(memoryTreeLease)"
    "serial exhaustion before retained lock admission")
require_ordered(
    _ownership_batch_begin
    "OwnershipBatchAccess::TryBeginMemoryTreeLease(memoryTreeLease)"
    "SL_IsCompleteStringStateValidForScopeNoReport( SL_ValidationScope::Leased, &memoryTreeLease)"
    "allocator lease before complete string validation")
require_ordered(
    _ownership_batch_begin
    "SL_IsCompleteStringStateValidForScopeNoReport( SL_ValidationScope::Leased, &memoryTreeLease)"
    "sl_activeOwnershipBatchAddress = address;"
    "complete validation before ownership publication")
require_ordered(
    _ownership_batch_begin
    "sl_activeOwnershipBatchAddress = address;"
    "sl_retainedOwnershipBatchAddress = address;"
    "global stack identity before retained TLS publication")
require_ordered(
    _ownership_batch_finish
    "SL_IsCompleteStringStateValidForScopeNoReport( SL_ValidationScope::Leased, &batch->memoryTreeLease_)"
    "OwnershipBatchAccess::FinishMemoryTreeLease( batch->memoryTreeLease_)"
    "complete string close before allocator close")
require_ordered(
    _ownership_batch_finish
    "SL_ClearOwnershipBatchRegistryLocked();"
    "OwnershipBatchAccess::Clear(*batch);"
    "registry invalidation before token clear")
require_ordered(
    _ownership_batch_finish
    "SL_ClearOwnershipBatchRegistryLocked();"
    "SL_ClearRetainedOwnershipBatchAuthenticationLocked();"
    "normal close clears global identity before retained TLS")

extract_slice(
    _string_source
    "OwnershipBatch::~OwnershipBatch() noexcept"
    "bool OwnershipBatch::active() const noexcept"
    _ownership_batch_destructor
    "ownership batch terminal destructor")
foreach(_marker IN ITEMS
    "SL_FreezeOwnershipBatchBoundaryLocked();"
    "MT_ValidationLease::AbandonFromOwnershipBatch( memoryTreeLease_)"
    "if (ownsRetainedScriptAcquisition && releasedNestedAcquisition) Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);")
    require_contains(
        _ownership_batch_destructor "${_marker}"
        "authenticated terminal ownership abandonment")
endforeach()
require_ordered(
    _ownership_batch_destructor
    "SL_FreezeOwnershipBatchBoundaryLocked();"
    "MT_ValidationLease::AbandonFromOwnershipBatch( memoryTreeLease_)"
    "outer Frozen publication precedes nested abandonment")
require_ordered(
    _ownership_batch_destructor
    "MT_ValidationLease::AbandonFromOwnershipBatch( memoryTreeLease_)"
    "SL_ClearRetainedOwnershipBatchAuthenticationLocked();"
    "nested exact release precedes outer retained-auth clear")
require_contains(
    _ownership_batch_destructor
    "} else { SL_FreezeOwnershipBatchBoundaryLocked(); }"
    "torn outer destructor freezes without a member write")

extract_slice(
    _memory_source
    "bool MT_ValidationLease::AbandonFromOwnershipBatch( MT_ValidationLease &lease) noexcept"
    "bool MT_ValidationLease::active() const noexcept"
    _nested_abandonment
    "friend-only nested lease abandonment")
require_ordered(
    _nested_abandonment
    "MT_OwnsValidationLeaseLocked(&lease)"
    "MT_FreezeValidationLeaseBoundaryLocked();"
    "nested live authentication precedes terminal freeze")
require_ordered(
    _nested_abandonment
    "MT_FreezeValidationLeaseBoundaryLocked();"
    "MT_ClearRetainedValidationLeaseAuthenticationLocked();"
    "nested Frozen publication precedes retained-auth clear")
require_ordered(
    _nested_abandonment
    "MT_FreezeValidationLeaseBoundaryLocked();"
    "Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);"
    "nested Frozen publication precedes every release")
require_contains(
    _nested_abandonment
    "if (ownsRetainedAcquisition) Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);"
    "nested retained release requires exact authentication")

require_ordered(
    _ownership_batch_finish
    "SL_OwnsOwnershipBatchLocked(batch)"
    "batch->memoryTreeLease_"
    "finish authenticates the live outer before nested member access")
extract_slice(
    _string_source
    "void SL_PoisonOwnershipBatchLocked("
    "bool SL_IsOwnershipBatchActiveNoReport() noexcept"
    _ownership_batch_poison
    "ownership batch poison publication")
require_ordered(
    _ownership_batch_poison
    "SL_OwnsOwnershipBatchLocked(batch)"
    "OwnershipBatchAccess::Poison(*batch)"
    "local poison requires full live-token authentication")
extract_slice(
    _string_source
    "void OwnershipBatch::SetAuthenticationFieldsForTesting("
    "void OwnershipBatch::SetOperationCountForTesting("
    _ownership_batch_test_auth_mutation
    "test-only ownership authentication mutation")
extract_slice(
    _string_source
    "void OwnershipBatch::SetOperationCountForTesting("
    "void OwnershipBatch::SetMemoryTreeMutationCountForTesting("
    _ownership_batch_test_operation_mutation
    "test-only ownership operation mutation")
extract_slice(
    _string_source
    "void OwnershipBatch::SetMemoryTreeMutationCountForTesting("
    "void ResetOwnershipValidationCountersForTesting() noexcept"
    _ownership_batch_test_nested_mutation
    "test-only nested ownership mutation")
require_ordered(
    _ownership_batch_test_auth_mutation
    "SL_RegistryNamesOwnershipBatchStorageLocked(this)"
    "serial_ = serial;"
    "test-only outer mutation authenticates live storage")
require_ordered(
    _ownership_batch_test_operation_mutation
    "SL_RegistryNamesOwnershipBatchStorageLocked(this)"
    "operationCount_ = operationCount;"
    "test-only operation mutation authenticates live storage")
require_ordered(
    _ownership_batch_test_nested_mutation
    "SL_RegistryNamesOwnershipBatchStorageLocked(this)"
    "memoryTreeLease_.SetMutationCountForTesting(mutationCount);"
    "test-only nested mutation authenticates live outer storage")

require_contains(
    _string_header
    "struct RefString;"
    "public RefString surface is opaque")
foreach(_forbidden IN ITEMS
    "volatile uint32_t data;"
    "SL_RefStringWord("
    "SL_AddUserInternal("
    "struct RefString {")
    require_not_contains(
        _string_header "${_forbidden}"
        "public RefString raw ownership surface")
endforeach()
require_contains(
    _string_source
    "struct RefString { volatile std::uint32_t data; char str[1]; };"
    "RefString layout is private to its authenticated owner")
require_not_contains(
    _string_header
    "SL_RefStringWord("
    "ownership API exposes no raw packed-word accessor")
require_literal_count(
    _string_source
    "return &refString->data;"
    2
    "private const/nonconst packed-word accessors")

extract_slice(
    _string_source
    "static bool SL_AddUserInternal("
    "void SL_AddRefToString(uint32_t stringValue)"
    _opaque_user_mutator
    "private opaque RefString user mutator")
require_ordered(
    _opaque_user_mutator
    "const bool addressValid = refStr != nullptr"
    "SL_TryResolveLegacyTransferTargetNoReport( static_cast<uint32_t>( (refStringAddress - memoryBegin) / MT_NODE_SIZE), &resolvedRefString, &byteCount)"
    "opaque RefString mutation validates range before allocation/hash")
require_ordered(
    _opaque_user_mutator
    "&& resolvedRefString == refStr"
    "&& SL_TryAddUserInternalNoReport(resolvedRefString, user);"
    "opaque RefString mutation requires exact identity before atomic access")
extract_slice(
    _string_source
    "void SL_AddRefToString(uint32_t stringValue)"
    "void SL_CheckExists(uint32_t stringValue)"
    _add_ref_mutator
    "legacy ID ref mutator")
require_ordered(
    _add_ref_mutator
    "SL_TryResolveLegacyTransferTargetNoReport( stringValue, &refStr, &byteCount)"
    "SL_TryAddUserInternalNoReport(refStr, 0)"
    "ID ref mutation authenticates exact string before atomic access")
extract_slice(
    _string_source
    "void __cdecl SL_AddUser(uint32_t stringValue, uint32_t user)"
    "void __cdecl Scr_SetString(uint16_t *to, uint32_t from)"
    _add_user_mutator
    "legacy ID user mutator")
require_ordered(
    _add_user_mutator
    "SL_TryResolveLegacyTransferTargetNoReport( stringValue, &refString, &byteCount)"
    "SL_TryAddUserInternalNoReport(refString, user)"
    "ID user mutation authenticates exact string before atomic access")
extract_slice(
    _string_source
    "uint32_t __cdecl SL_ConvertToLowercase("
    "void __cdecl CreateCanonicalFilename("
    _lowercase_conversion
    "lowercase conversion")
require_ordered(
    _lowercase_conversion
    "SL_TryResolveLegacyTransferTargetNoReport( stringValue, &refString, &byteCount)"
    "static_cast<unsigned char>(refString->str[index])"
    "lowercase conversion authenticates exact string before bounded access")

foreach(_marker IN ITEMS
    "enum class SL_ValidationScope : uint8_t { Complete, LegacyLocal, Leased, };"
    "OwnershipBatchAccess::TryAllocateMemoryTreeIndex( *validationLease, numBytes, type, outIndex)"
    "OwnershipBatchAccess::TryGetMemoryTreeAllocation( *validationLease, stringValue, outInfo)"
    "OwnershipBatchAccess::TryFreeMemoryTreeIndex( *validationLease, stringValue, numBytes)"
    "SL_IsFreeListCertificateMemberNoReport( const uint32_t entryIndex) noexcept"
    "SL_SetFreeListCertificateMemberNoReport( const uint32_t entryIndex, const bool member) noexcept"
    "SL_IsLeasedFreeListLocallyValidNoReport() noexcept"
    "SL_IsCompleteStringStateValidForScopeNoReport( const SL_ValidationScope scope, MT_ValidationLease* const validationLease) noexcept"
    "scrStringDebugGlob != &scrStringDebugGlobBuf"
    "SL_SetFreeListCertificateMemberNoReport(newIndex, false);"
    "SL_SetFreeListCertificateMemberNoReport(freedIndex, true);")
    require_contains(
        _string_source "${_marker}" "leased bounded string validation")
endforeach()

foreach(_var IN ITEMS _acquire _transfer _release_ordinary _release_database)
    require_contains(
        ${_var}
        "SL_IsTypedOwnershipAccessAuthorizedLocked(validationLease)"
        "typed ownership registry authorization")
    require_not_contains(
        ${_var}
        "SL_IsCompleteStringStateValidForScopeNoReport("
        "batch operation cannot rebuild the complete string certificate")
    require_not_contains(
        ${_var}
        "MT_TryValidateState("
        "batch operation cannot repeat complete allocator validation")
    foreach(_callback IN ITEMS
        "SL_TransferToCanonicalString"
        "SL_GetCanonicalString")
        require_not_contains(
            ${_var} "${_callback}"
            "callback-free ownership operation surface")
    endforeach()
endforeach()
require_ordered(
    _acquire
    "SL_IsTypedOwnershipAccessAuthorizedLocked(validationLease)"
    "bytes[byteCount - 1]"
    "ownership authorization precedes caller byte access")

extract_slice(
    _string_source
    "int SL_IsLowercaseString(uint32_t stringValue)"
    "void SL_TransferSystem(uint32_t from, uint32_t to)"
    _lowercase_reader
    "lowercase string reader")
require_ordered(
    _lowercase_reader
    "SL_TryResolveLegacyTransferTargetNoReport( stringValue, &refString, &byteCount)"
    "for (uint32_t index = 0; index + 1 < byteCount; ++index)"
    "lowercase reader authenticates exact allocation/hash before bounded scan")
require_contains(
    _lowercase_reader
    "static_cast<unsigned char>(tolower(value))"
    "lowercase reader uses unsigned-char ctype input")
extract_slice(
    _string_source
    "static uint32_t GetLowercaseStringOfSize("
    "uint32_t SL_GetLowercaseString_("
    _lowercase_intern
    "lowercase string intern")
require_contains(
    _lowercase_intern
    "static_cast<unsigned char>(str[i])"
    "lowercase intern uses unsigned-char ctype input")
extract_slice(
    _string_source
    "uint32_t SL_FindLowercaseString(const char* str)"
    "bool SL_TryRemoveRefToStringLockedNoReport("
    _lowercase_find
    "lowercase string lookup")
require_contains(
    _lowercase_find
    "static_cast<unsigned char>(str[i])"
    "lowercase lookup uses unsigned-char ctype input")
extract_slice(
    _string_source
    "void __cdecl CreateCanonicalFilename("
    "uint32_t __cdecl Scr_CreateCanonicalFilename("
    _canonical_filename
    "canonical filename folding")
require_contains(
    _canonical_filename
    "c = static_cast<unsigned char>(*filename++);"
    "canonical filename reads unsigned input bytes")
require_contains(
    _canonical_filename
    "static_cast<unsigned char>(c)"
    "canonical filename uses unsigned-char ctype input")
extract_slice(
    _string_source
    "const char* SL_ConvertToString(uint32_t stringValue)"
    "RefString* GetRefString(uint32_t stringValue)"
    _convert_reader
    "string conversion reader")
require_contains(
    _convert_reader
    "SL_TryResolveLegacyTransferTargetNoReport( stringValue, &refString, &byteCount)"
    "string conversion authenticates exact allocation/hash")
extract_slice(
    _string_source
    "RefString* GetRefString(uint32_t stringValue)"
    "int SL_GetStringLen(uint32_t stringValue)"
    _ref_string_readers
    "opaque RefString resolution")
require_literal_count(
    _ref_string_readers
    "SL_TryResolveLegacyTransferTargetNoReport("
    2
    "both RefString overloads use exact resolver")
require_ordered(
    _ref_string_readers
    "const bool addressValid = str != nullptr"
    "&& result->str == str;"
    "pointer resolution authenticates range before exact string pointer")
extract_slice(
    _string_source
    "int SL_GetStringLen(uint32_t stringValue)"
    "uint32_t SL_FindString(const char* str)"
    _string_length_reader
    "string length reader")
require_contains(
    _string_length_reader
    "SL_TryResolveLegacyTransferTargetNoReport( stringValue, &refString, &byteCount)"
    "string length authenticates exact allocation/hash")
extract_slice(
    _string_source
    "int SL_GetRefStringLen(RefString* refString)"
    "void SL_RemoveRefToString(uint32_t stringValue)"
    _ref_string_length_reader
    "RefString length reader")
require_ordered(
    _ref_string_length_reader
    "const bool addressValid = memoryBegin != 0"
    "SL_TryResolveLegacyTransferTargetNoReport( static_cast<uint32_t>( (refStringAddress - memoryBegin) / MT_NODE_SIZE), &resolvedRefString, &byteCount)"
    "RefString length validates range before exact allocation/hash")
require_contains(
    _ref_string_length_reader
    "&& resolvedRefString == refString;"
    "RefString length requires exact allocation identity")
extract_slice(
    _string_source
    "const char* __cdecl SL_DebugConvertToString(uint32_t stringValue)"
    "uint32_t SL_ConvertFromString(const char* str)"
    _debug_convert_reader
    "debug string reader")
require_ordered(
    _debug_convert_reader
    "SL_TryResolveLegacyTransferTargetNoReport( stringValue, &refString, &byteCount)"
    "isprint(static_cast<unsigned char>(refString->str[index]))"
    "debug reader authenticates and bounds unsigned-char inspection")
extract_slice(
    _string_source
    "uint32_t SL_ConvertFromString(const char* str)"
    "uint32_t SL_FindLowercaseString(const char* str)"
    _convert_from_reader
    "pointer-to-string-ID reader")
require_ordered(
    _convert_from_reader
    "const bool addressValid = str != nullptr"
    "SL_TryResolveLegacyTransferTargetNoReport( candidate, &refString, &byteCount)"
    "pointer conversion validates range before allocation/hash")
require_ordered(
    _convert_from_reader
    "SL_TryResolveLegacyTransferTargetNoReport( candidate, &refString, &byteCount)"
    "refString->str == str;"
    "pointer conversion requires exact payload identity")
extract_slice(
    _string_source
    "uint32_t SL_GetUser(uint32_t stringValue)"
    "const char *SL_ConvertToStringSafe(uint32_t stringValue)"
    _user_reader
    "string user reader")
require_ordered(
    _user_reader
    "SL_TryResolveLegacyTransferTargetNoReport( stringValue, &refString, &byteCount)"
    "SL_RefStringWord(refString)"
    "user reader authenticates exact string before packed read")
foreach(_marker IN ITEMS
    "OwnershipBatch::tryAuthenticateOperationLocked() noexcept"
    "canOperateNoLock()"
    "sl_ownershipBatchLifecycle == SL_OwnershipBatchLifecycle::Active"
    "operationCount_ != UINT32_MAX"
    "memoryTreeLease_.mutationCount() != UINT32_MAX"
    "++batch.operationCount_;"
    "batch.poisoned_ = true;")
    require_contains(
        _string_source "${_marker}" "overflow-safe batch operation accounting")
endforeach()

# Same-thread legacy/reporting/raw entries reject before inspecting untrusted
# caller data. Foreign threads block on the retained recursive script lock and
# resume normally after close.
foreach(_marker IN ITEMS
    "uint32_t __cdecl Scr_AllocString(char *s, int sys) { if (SL_IsOwnershipBatchActiveNoReport())"
    "void SL_CheckExists(uint32_t stringValue) { if (SL_IsOwnershipBatchActiveNoReport())"
    "void SL_ShutdownSystem(uint32_t user) { if (SL_IsOwnershipBatchActiveNoReport())"
    "int SL_IsLowercaseString(uint32_t stringValue) { if (SL_IsOwnershipBatchActiveNoReport())"
    "void SL_TransferSystem(uint32_t from, uint32_t to) { if (SL_IsOwnershipBatchActiveNoReport())"
    "uint32_t SL_GetString_(const char* str, uint32_t user, int type) { if (SL_IsOwnershipBatchActiveNoReport())"
    "uint32_t SL_GetStringOfSize(const char* str, uint32_t user, uint32_t len, int type) { if (SL_IsOwnershipBatchActiveNoReport())"
    "const char* SL_ConvertToString(uint32_t stringValue) { if (SL_IsOwnershipBatchActiveNoReport())"
    "RefString* GetRefString(uint32_t stringValue) { if (SL_IsOwnershipBatchActiveNoReport())"
    "RefString* GetRefString(const char* str) { if (SL_IsOwnershipBatchActiveNoReport())"
    "int SL_GetStringLen(uint32_t stringValue) { if (SL_IsOwnershipBatchActiveNoReport())"
    "uint32_t SL_FindString(const char* str) { if (SL_IsOwnershipBatchActiveNoReport())"
    "void __cdecl SL_TransferRefToUser(uint32_t stringValue, uint32_t user) { if (SL_IsOwnershipBatchActiveNoReport())"
    "uint32_t SL_GetStringForVector(const float* v) { if (SL_IsOwnershipBatchActiveNoReport())"
    "uint32_t SL_GetStringForInt(int i) { if (SL_IsOwnershipBatchActiveNoReport())"
    "uint32_t SL_GetStringForFloat(float f) { if (SL_IsOwnershipBatchActiveNoReport())"
    "uint32_t SL_GetString(const char* str, uint32_t user) { if (SL_IsOwnershipBatchActiveNoReport())"
    "uint32_t SL_GetLowercaseString_(const char* str, uint32_t user, int type) { if (SL_IsOwnershipBatchActiveNoReport())"
    "int SL_GetRefStringLen(RefString* refString) { if (SL_IsOwnershipBatchActiveNoReport())"
    "void SL_RemoveRefToString(uint32_t stringValue) { if (SL_IsOwnershipBatchActiveNoReport())"
    "uint32_t SL_FindLowercaseString(const char* str) { if (SL_IsOwnershipBatchActiveNoReport())"
    "void SL_RemoveRefToStringOfSize(uint32_t stringValue, uint32_t len) { if (SL_IsOwnershipBatchActiveNoReport())"
    "void __cdecl SL_AddUser(uint32_t stringValue, uint32_t user) { if (SL_IsOwnershipBatchActiveNoReport())"
    "void __cdecl Scr_SetString(uint16_t *to, uint32_t from) { if (SL_IsOwnershipBatchActiveNoReport())"
    "uint32_t __cdecl SL_ConvertToLowercase(uint32_t stringValue, uint32_t user, int type) { if (SL_IsOwnershipBatchActiveNoReport())"
    "const char* __cdecl SL_DebugConvertToString(uint32_t stringValue) { if (SL_IsOwnershipBatchActiveNoReport())"
    "uint32_t SL_ConvertFromString(const char* str) { if (SL_IsOwnershipBatchActiveNoReport())"
    "uint32_t __cdecl Scr_CreateCanonicalFilename(const char *filename) { if (SL_IsOwnershipBatchActiveNoReport())"
    "void Scr_SetStringFromCharString(uint16_t *to, const char *from) { if (SL_IsOwnershipBatchActiveNoReport())"
    "uint32_t SL_GetUser(uint32_t stringValue) { if (SL_IsOwnershipBatchActiveNoReport())"
    "const char *SL_ConvertToStringSafe(uint32_t stringValue) { if (SL_IsOwnershipBatchActiveNoReport())")
    require_contains(
        _string_source "${_marker}" "same-thread legacy batch exclusion")
endforeach()
require_contains(
    _string_source
    "uint32_t SL_GetLowercaseString(const char* str, uint32_t user) { return SL_GetLowercaseString_(str, user, 6); }"
    "lowercase wrapper delegates to the gated implementation")
foreach(_marker IN ITEMS
    "void SL_Init() { Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING); if (SL_HasOwnershipBatchRegistryActivityLocked())"
    "void SL_InitCheckLeaks() { Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING); if (SL_HasOwnershipBatchRegistryActivityLocked())"
    "void SL_AddRefToString(uint32_t stringValue) { PROF_SCOPED(\"SL_AddRefToString\"); Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING); if (SL_HasOwnershipBatchRegistryActivityLocked())"
    "void SL_Shutdown() { Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING); if (SL_HasOwnershipBatchRegistryActivityLocked())"
    "static bool SL_AddUserInternal(RefString* const refStr, const uint32_t user) { Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING); if (SL_HasOwnershipBatchRegistryActivityLocked())"
    "static bool SL_FreeString( const uint32_t stringValue, RefString* const refString, const uint32_t byteCount) { PROF_SCOPED(\"SL_FreeString\"); Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING); if (SL_HasOwnershipBatchRegistryActivityLocked())"
    "bool SL_TryFreeSystemSweepEntryNoReport( const uint32_t owningHash, const uint32_t targetIndex, const uint32_t previousIndex, const uint32_t stringValue, RefString* const refString, const uint32_t byteCount) noexcept { if (SL_HasOwnershipBatchRegistryActivityLocked())")
    require_contains(
        _string_source "${_marker}" "locked raw mutation batch exclusion")
endforeach()

extract_slice(
    _string_source
    "bool SL_TryResetCanonicalStringState("
    "static uint32_t SL_DebugRefCount("
    _canonical_reset
    "canonical string reset gate")
require_ordered(
    _canonical_reset
    "Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);"
    "SL_HasOwnershipBatchRegistryActivityLocked()"
    "canonical reset acquires before boundary admission")
require_ordered(
    _canonical_reset
    "SL_HasOwnershipBatchRegistryActivityLocked()"
    "memset(canonicalStrings, 0, sizeof(canonicalStrings));"
    "canonical reset rejects before supplied storage dereference")
require_ordered(
    _canonical_reset
    "memset(canonicalStrings, 0, sizeof(canonicalStrings));"
    "*canonicalCount = 0;"
    "canonical map clear precedes count publication")
extract_slice(
    _script_main_source
    "void SL_BeginLoadScripts()"
    "int __cdecl Scr_ScanFile("
    _begin_load_scripts
    "canonical reset wrapper")
require_contains(
    _begin_load_scripts
    "if (!SL_TryResetCanonicalStringState( scrCompilePub.canonicalStrings, &scrVarPub.canonicalStrCount))"
    "script-load reset checks the gated helper")
require_ordered(
    _begin_load_scripts
    "if (!SL_TryResetCanonicalStringState("
    "std::abort();"
    "script-load reset fails fast without a reporter")
foreach(_forbidden IN ITEMS
    "(void)SL_TryResetCanonicalStringState("
    "memset("
    "canonicalStrCount =")
    require_not_contains(
        _begin_load_scripts "${_forbidden}"
        "script-load wrapper cannot bypass canonical reset gate")
endforeach()

# A dedicated outer serializer avoids inverting DB hash and script-string
# locks. Tokens are explicit, non-copyable, and do not unlock from a destructor.
foreach(_marker IN ITEMS
    "ScriptStringTransactionToken(const ScriptStringTransactionToken &) = delete;"
    "ScriptStringTransactionToken(ScriptStringTransactionToken &&) = delete;"
    "~ScriptStringTransactionToken() noexcept = default;"
    "RUNTIME_SIZE(ScriptStringTransactionToken, 0x8, 0x8);"
    "bool canonicalInactive() const noexcept;"
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
    "owner.canonicalInactive()"
    "nested.canonicalInactive()"
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
    "add_library(kisakcod-script-memorytree-lease-api-seal OBJECT"
    "script_memorytree_lease_api_seal_compile_tests.cpp"
    "add_executable(kisakcod-script-memorytree-try-tests"
    "script_memorytree_try_tests.cpp"
    "\${SRC_DIR}/script/scr_memorytree.cpp"
    "NAME script-memorytree-try-contracts")
    require_contains(_tests "${_marker}" "memory-tree runtime fixture wiring")
endforeach()
require_contains(
    _ci
    "kisakcod-script-memorytree-lease-api-seal"
    "production allocator lease API seal in explicit Windows x86 build")
foreach(_marker IN ITEMS
    "static_assert(!std::is_default_constructible_v<MT_ValidationLeaseAdmission>);"
    "static_assert(!std::is_trivially_copyable_v<MT_ValidationLeaseAdmission>);"
    "static_assert(!std::is_default_constructible_v<MT_ValidationLease>);"
    "static_assert(!std::is_destructible_v<MT_ValidationLease>);"
    "static_assert(!HasUngatedFinish<MT_ValidationLease>);"
    "static_assert(!HasUngatedAllocation<MT_ValidationLease>);"
    "static_assert(!HasUngatedQuery<MT_ValidationLease>);"
    "static_assert(!HasUngatedFree<MT_ValidationLease>);")
    require_contains(
        _memory_lease_seal "${_marker}" "production allocator lease API seal")
endforeach()
foreach(_marker IN ITEMS
    "TestInvalidAndNoChange()"
    "TestQueryAndFreeContracts()"
    "TestLegacyLocalValidationScope()"
    "TestLegacyTouchedIntervalCorruption()"
    "TestLegacyFragmentedForestCost()"
    "TestValidationLeaseCapabilityAuthentication()"
    "invalid lease capability reached the allocator lock"
    "invalid lease operation capability reached the allocator lock"
    "TestValidationLeasePolicies()"
    "TestValidationLeaseAuthenticationAndOverflow()"
    "TestValidationLeaseCorruption()"
    "TestValidationLeaseReentryAndForeignThread()"
    "TestValidationLeaseAbandonedLifetime()"
    "authenticated destructor retained the owner lock"
    "torn destructor guessed the retained acquisition"
    "MT_SetRetainedValidationLeaseAuthenticationForTesting("
    "foreign thread entered through a torn retained boundary"
    "unrelated canonical destructor revoked the active lease"
    "arbitrary mirrored address was dereferenced or admitted"
    "blocked snapshots read a destroyed lease"
    "foreign allocator access bypassed retained lock"
    "mutation counter wrapped"
    "pointer-only registry admitted legacy allocation"
    "legacy query trusted cleared primary metadata"
    "legacy query trusted corrupted allocation count"
    "legacy allocation trusted an orphaned free head"
    "legacy allocation trusted swapped free-tree branches"
    "legacy allocation trusted inverted free-tree priority"
    "legacy free trusted a larger-ancestor free alias"
    "fragmented legacy mutations used a complete partition scan"
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
    "KISAK_MEMORY_TREE_VALIDATION_TESTING=1"
    "KISAK_SCRIPT_STRING_PERF_TESTING=1"
    "NAME script-string-report-free-ownership-contracts")
    require_contains(
        _ownership_fixture_target "${_marker}"
        "string ownership runtime fixture wiring")
endforeach()
require_contains(
    _tests
    "KISAK_MEMORY_TREE_VALIDATION_TESTING=1"
    "memory-tree lease runtime fixture wiring")
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
require_not_contains(
    _ownership_fixture
    "std::this_thread::sleep_for("
    "deterministic foreign serialization coverage")
foreach(_marker IN ITEMS
    "TestInitCheckLeaksRetainsLock()"
    "TestFullInitRejectsDebugOnlyState()"
    "TestDuplicateInitCheckLeaksNoChange()"
    "TestDuplicateInitNoChange()"
    "g_scriptStringMutex.try_lock()"
    "expectedAssertions = 1"
    "expectedAssertions = 0"
    "duplicate-debug-init-live-reference"
    "duplicate-full-init-live-reference"
    "debug-only initialization changed ownership state"
    "StateMatches(beforeDuplicate)"
    "TestInvalidAndNoChange()"
    "TestRepeatedInternAndDatabaseTransfer()"
    "TestOrdinaryRollbackFreeAndReuse()"
    "TestEmbeddedNulByteCount()"
    "TestCollidingByteLengthBounds()"
    "TestLegacyBinaryInternCompatibility()"
    "TestLegacyFreeListSpliceBoundaries()"
    "TestLegacyEmptyAndOneNodeFreeList()"
    "TestShutdownStaleIterationRollback()"
    "TestSystemIterationAuthenticatesPhysicalEntries()"
    "per-ID debug corruption changed ownership state"
    "aggregate debug corruption changed ownership state"
    "forged shutdown entry changed ownership state"
    "TestLegacyCompatibilityAvoidsCompleteScans()"
    "TestLegacyHashScratchResetIsChainBounded()"
    "TestLegacyLocalCorruptionAndReporterUnwind()"
    "MT_CompleteValidationCountForTesting() == 0"
    "sl_completeFreeListValidationCount == 0"
    "sl_hashValidationScratchResetEntryCount == expectedResetEntries"
    "g_reporterSawOwnedLock"
    "scrStringGlob.nextFreeEntry == nullptr"
    "legacy binary report-free transfer failed"
    "TestMalformedStateFailsClosed()"
    "std::vector<char> longBytes(4867);"
    "GetRefString(shortResult.stringId)->str"
    "GetHashCode(shortBytes.data(), shortByteCount) == 1217"
    "GetHashCode(longBytes.data(), longByteCount) == 1217"
    "StateMatches(corruptHash)"
    "StateMatches(corruptDebug)"
    "StateMatches(corruptAllocation)"
    "TestOwnershipBatchLifecycle()"
    "TestOwnershipBatchAllowsSharedVectorDebugSlots()"
    "TestOwnershipBatchRejectsUnauthorizedEntries()"
    "TestOwnershipBatchLocalPoisoning()"
    "TestOwnershipBatchBoundaryValidation()"
    "TestOwnershipBatchAuthenticationAndOverflow()"
    "TestOwnershipBatchRejectsRawAllocatorMutation()"
    "TestOwnershipBatchForeignSerialization()"
    "TestOwnershipBatchForeignReaderSerialization()"
    "TestOwnershipBatchCanonicalResetGate()"
    "TestOwnershipBatchAbandonedWaiters()"
    "TestOwnershipBatchOuterAuthorityTears()"
    "TestOwnershipBatchNestedAuthorityTears()"
    "TestOwnershipBatchUnrelatedDestruction()"
    "TestLegacyReadersAuthenticateExactStrings()"
    "TestLegacyMutatorsAuthenticateExactStrings()"
    "TestLegacyCharacterFoldingUsesUnsignedInput()"
    "batch operation rebuilt the complete string certificate"
    "inconsistent batch serial mirror authenticated an operation"
    "inconsistent batch address mirror authenticated an operation"
    "ownership batch serial wrapped"
    "ownership operation counter wrapped"
    "allocator lease mutation counter entered string mutation"
    "raw allocator mutation entered an ownership batch"
    "raw targeted removal entered an ownership batch"
    "raw allocator reader entered an ownership batch"
    "raw allocator diagnostic entered an ownership batch"
    "batch admission dereferenced a noncanonical debug pointer"
    "batch admission trusted shifted per-ID debug accounting"
    "foreign ownership caller bypassed the retained script lock"
    "foreign reader bypassed the retained script lock"
    "blocked snapshots read destroyed ownership-batch storage"
    "torn outer authority released the retained script lock"
    "torn nested authority released retained allocator lock"
    "unrelated destructor revoked the live owner"
    "arbitrary integer string address was dereferenced"
    "lowercase conversion exposed non-string"
    "string length exposed non-string"
    "RefString length exposed non-string"
    "opaque pointer user mutation accepted non-string storage"
    "ID ref mutation accepted a non-string allocation"
    "ID user mutation accepted a non-string allocation"
    "exact opaque pointer mutation rejected a valid string"
    "lowercase intern mishandled a high-bit byte"
    "lowercase lookup mishandled a high-bit byte"
    "canonical filename mishandled a high-bit byte")
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
