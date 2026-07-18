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
# fail closed even when assertions are compiled out. The diagnostic must run
# only after releasing the script-string lock.
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
    "void SL_RemoveRefToStringOfSize("
    "void __cdecl SL_AddUser("
    _legacy_remove
    "legacy ordinary release")
extract_slice(
    _legacy_remove
    "if (!validFree)"
    "(void)validFree;"
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
    "scope == SL_ValidationScope::Complete ? SL_IsFreeListHeadValidNoReport() : SL_IsFreeListLocallyValidNoReport()"
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
    "!result.reachedZero || SL_FreeString(stringValue, refStr, len)")
    require_contains(
        _legacy_remove "${_marker}" "legacy final-release rollback")
endforeach()
require_ordered(
    _legacy_remove_rollback
    "Sys_AtomicStore(SL_RefStringWord(refStr), packed);"
    "Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);"
    "legacy ownership rollback before unlock")
require_ordered(
    _legacy_remove
    "Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);"
    "iassert(validFree);"
    "legacy release reporter after unlock")

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
    "aggregateRefCount > UINT32_MAX"
    "Sys_AtomicLoad(&scrStringDebugGlob->totalRefCount) != static_cast<uint32_t>(aggregateRefCount)"
    "SL_DebugRefCount(stringValue) != 0")
    require_contains(
        _system_sweep_preflight "${_marker}"
        "linear complete hash/debug/allocator sweep preflight")
endforeach()
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
    "return MT_TryAllocIndexImpl(numBytes, type, outIndex, true);"
    "return MT_TryAllocIndexImpl(numBytes, type, outIndex, false);"
    "return MT_TryGetAllocationInfoImpl(nodeNum, outInfo, true);"
    "return MT_TryGetAllocationInfoImpl(nodeNum, outInfo, false);"
    "return MT_TryFreeIndexImpl(nodeNum, numBytes, true);"
    "return MT_TryFreeIndexImpl(nodeNum, numBytes, false);")
    require_contains(
        _memory_source "${_marker}" "complete versus bounded allocator scope")
endforeach()
require_contains(
    _try_alloc
    "completeValidation ? MT_IsCoreStateValidNoReport() : MT_IsBasicCoreStateValidNoReport()"
    "bounded allocation validates touched free-tree state")
require_contains(
    _try_query
    "completeValidation ? MT_IsCoreStateValidNoReport() : MT_IsBasicAccountingStateValidNoReport()"
    "bounded query validates only consulted accounting")
require_contains(
    _try_free
    "completeValidation ? MT_IsCoreStateValidNoReport() : MT_IsBasicCoreStateValidNoReport()"
    "bounded free validates touched free-tree state")
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
    _string_source "MT_TryGetAllocationInfoLegacy(" 3
    "legacy string candidate, unlink, and sweep queries")
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
extract_slice(
    _memory_source
    "void MT_RemoveHeadMemoryNode(int size)"
    "namespace { MT_FreeIndexStatus MT_TryFreeIndexImpl("
    _raw_remove_head
    "synchronized raw head removal")
extract_slice(
    _memory_source
    "bool __cdecl MT_RemoveMemoryNode(int oldNode, uint32_t size)"
    "void MT_Free(byte* p, int numBytes)"
    _raw_remove
    "synchronized raw targeted removal")
extract_slice(
    _memory_source
    "void MT_AddMemoryNode(int newNode, int size)"
    "void MT_Error(const char* funcName, int numBytes)"
    _raw_add
    "synchronized raw insertion")
require_contains(
    _raw_remove_head
    "MT_RemoveHeadMemoryNodeCommitNoReport(size);"
    "raw head removal delegates to synchronized commit")
require_contains(
    _raw_remove
    "return MT_RemoveMemoryNodeCommitNoReport( oldNode, static_cast<int>(size));"
    "raw targeted removal delegates to synchronized commit")
require_contains(
    _raw_add
    "MT_AddMemoryNodeCommitNoReport(newNode, size);"
    "raw insertion delegates to synchronized commit")
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
    "add_executable(kisakcod-script-memorytree-try-tests"
    "script_memorytree_try_tests.cpp"
    "\${SRC_DIR}/script/scr_memorytree.cpp"
    "NAME script-memorytree-try-contracts")
    require_contains(_tests "${_marker}" "memory-tree runtime fixture wiring")
endforeach()
foreach(_marker IN ITEMS
    "TestInvalidAndNoChange()"
    "TestQueryAndFreeContracts()"
    "TestLegacyLocalValidationScope()"
    "TestLegacyTouchedIntervalCorruption()"
    "TestLegacyFragmentedForestCost()"
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
    "KISAK_SCRIPT_STRING_PERF_TESTING=1"
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
    "TestInitCheckLeaksRetainsLock()"
    "TestDuplicateInitCheckLeaksNoChange()"
    "g_scriptStringMutex.try_lock()"
    "expectedAssertions = 1"
    "expectedAssertions = 0"
    "duplicate-debug-init-live-reference"
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
