cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

function(read_normalized relative_path output_name)
    file(READ "${SOURCE_ROOT}/${relative_path}" content)
    string(REPLACE "\r" "" content "${content}")
    set(${output_name} "${content}" PARENT_SCOPE)
endfunction()

function(require_text content needle description)
    string(FIND "${content}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Missing ${description}: ${needle}")
    endif()
endfunction()

function(reject_text content needle description)
    string(FIND "${content}" "${needle}" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR "Found forbidden ${description}: ${needle}")
    endif()
endfunction()

function(require_count content needle expected description)
    set(remainder "${content}")
    set(actual 0)
    string(LENGTH "${needle}" needle_length)
    while(TRUE)
        string(FIND "${remainder}" "${needle}" position)
        if(position EQUAL -1)
            break()
        endif()
        math(EXPR actual "${actual} + 1")
        math(EXPR next "${position} + ${needle_length}")
        string(SUBSTRING "${remainder}" ${next} -1 remainder)
    endwhile()
    if(NOT actual EQUAL expected)
        message(FATAL_ERROR
            "Expected ${expected} ${description}, found ${actual}: ${needle}")
    endif()
endfunction()

function(require_order content first second description)
    string(FIND "${content}" "${first}" first_position)
    string(FIND "${content}" "${second}" second_position)
    if(first_position EQUAL -1 OR second_position EQUAL -1
       OR NOT first_position LESS second_position)
        message(FATAL_ERROR
            "Missing or misordered ${description}: ${first} -> ${second}")
    endif()
endfunction()

function(extract_slice content begin_marker end_marker output_name)
    string(FIND "${content}" "${begin_marker}" begin_position)
    string(FIND "${content}" "${end_marker}" end_position)
    if(begin_position EQUAL -1 OR end_position LESS_EQUAL begin_position)
        message(FATAL_ERROR
            "Cannot extract ordered slice ${begin_marker} -> ${end_marker}")
    endif()
    math(EXPR length "${end_position} - ${begin_position}")
    string(SUBSTRING "${content}" ${begin_position} ${length} slice)
    set(${output_name} "${slice}" PARENT_SCOPE)
endfunction()

# Exercise normalization with actual CR/LF bytes so source gates behave the
# same against Windows checkouts and native Unix trees.
string(ASCII 13 carriage_return)
string(ASCII 10 line_feed)
set(normalization_probe
    "alpha${carriage_return}${line_feed}beta${carriage_return}${line_feed}")
string(REPLACE "${carriage_return}" "" normalization_probe
    "${normalization_probe}")
if(NOT normalization_probe STREQUAL "alpha${line_feed}beta${line_feed}")
    message(FATAL_ERROR "Physical-memory CRLF normalization self-probe failed")
endif()

read_normalized("src/universal/physicalmemory.h" legacy_header)
read_normalized("src/universal/physicalmemory_runtime.h" runtime_header)
read_normalized("src/universal/physicalmemory.cpp" source)
read_normalized("src/qcommon/common.cpp" common_source)
read_normalized("src/database/db_registry.cpp" registry_source)
read_normalized("tests/physicalmemory_legacy_tests.cpp" legacy_fixture)
read_normalized("tests/physicalmemory_runtime_tests.cpp" runtime_fixture)
read_normalized(
    "tests/physicalmemory_runtime_production_seal_tests.cpp"
    production_seal_fixture)
read_normalized(
    "tests/physicalmemory_runtime_production_seal_test.cmake"
    production_symbol_seal)
read_normalized("tests/CMakeLists.txt" test_manifest)
read_normalized("scripts/common_files.cmake" production_manifest)
read_normalized(".github/workflows/ci.yml" workflow)

# Legacy public layouts and signatures remain frozen on x86 and native64.
foreach(marker IN ITEMS
    "inline constexpr std::uint32_t MAX_PHYSICAL_ALLOCATIONS = 32u;"
    "PhysicalMemoryAllocation allocList[MAX_PHYSICAL_ALLOCATIONS];"
    "RUNTIME_SIZE(PhysicalMemoryAllocation, 0x8, 0x10);"
    "RUNTIME_SIZE(PhysicalMemoryPrim, 0x10C, 0x210);"
    "RUNTIME_SIZE(PhysicalMemory, 0x21C, 0x428);"
    "void KISAK_CDECL PMem_Init();"
    "void KISAK_CDECL PMem_BeginAlloc("
    "void KISAK_CDECL PMem_BeginAllocInPrim("
    "void KISAK_CDECL PMem_EndAlloc("
    "void KISAK_CDECL PMem_EndAllocInPrim("
    "void KISAK_CDECL PMem_Free("
    "void KISAK_CDECL PMem_FreeInPrim("
    "void KISAK_CDECL PMem_FreeIndex("
    "int KISAK_CDECL PMem_GetOverAllocatedSize("
    "uint8_t *KISAK_CDECL PMem_Alloc("
    "uint8_t *KISAK_CDECL PMem_TryAlloc("
    "uint32_t KISAK_CDECL PMem_GetFreeAmount(")
    require_text("${legacy_header}" "${marker}" "legacy PMem ABI marker")
endforeach()
reject_text("${legacy_header}" "extern PhysicalMemory g_mem"
    "mutable global PMem declaration")
reject_text("${source}" "__cdecl PMem_"
    "hard-coded nonportable PMem calling convention")
require_text("${legacy_fixture}"
    "void MyAssertHandler(\n    const char *filename,\n    const int line,\n    const int type,\n    const char *format,"
    "MSVC-compatible variadic assert fixture signature")
reject_text("${legacy_fixture}" "const char *const filename"
    "MSVC-incompatible top-level pointer const in assert fixture")

# The new include is intentionally narrow and freezes every report-free result
# field, including native64's ordinary tail padding.
foreach(marker IN ITEMS
    "namespace pmem_runtime"
    "enum class InitializationPhase : std::uint8_t"
    "Uninitialized,"
    "Initializing,"
    "Ready,"
    "Poisoned,"
    "enum class InitializationStatus : std::uint8_t"
    "ReserveFailed,"
    "CommitFailed,"
    "ReleaseFailed,"
    "CorruptState,"
    "enum class AllocationStatus : std::uint8_t"
    "InvalidRequest,"
    "NotReady,"
    "ScopeInactive,"
    "Exhausted,"
    "std::uint64_t additionalBytes = 0;"
    "std::uint8_t *address = nullptr;"
    "std::uint8_t reserved[3]{};"
    "RUNTIME_SIZE(AllocationResult, 0x10, 0x18);"
    "RUNTIME_OFFSET(AllocationResult, status, 0xC, 0x10);"
    "enum class ProcessInitAllocationStatus : std::uint8_t"
    "AlreadyComplete,"
    "WrongPhase,"
    "DIAGNOSTIC_ENTRIES_PER_PRIM = 32u;"
    "DIAGNOSTIC_NAME_CAPACITY = 19u;"
    "enum class DiagnosticEntryKind : std::uint8_t"
    "enum class DiagnosticSnapshotStatus : std::uint8_t"
    "struct DiagnosticEntry final"
    "char name[DIAGNOSTIC_NAME_CAPACITY]{};"
    "RUNTIME_SIZE(DiagnosticEntry, 0x18, 0x18);"
    "RUNTIME_OFFSET(DiagnosticEntry, kind, 0x17, 0x17);"
    "struct DiagnosticSnapshot final"
    "RUNTIME_SIZE(DiagnosticSnapshot, 0x610, 0x610);"
    "RUNTIME_OFFSET(DiagnosticSnapshot, status, 0x60C, 0x60C);"
    "TryInitialize() noexcept;"
    "TryAllocate("
    "std::uint32_t allocType) noexcept;"
    "TryCaptureDiagnosticSnapshot() noexcept;")
    require_text("${runtime_header}" "${marker}" "narrow runtime API marker")
endforeach()
foreach(marker IN ITEMS
    "TryBeginProcessInitAllocation() noexcept;"
    "TryEndProcessInitAllocation() noexcept;")
    require_text("${runtime_header}" "${marker}"
        "hidden-controller process-init operation")
endforeach()
foreach(forbidden IN ITEMS
    "class ProcessInitController"
    "struct ProcessInitControl"
    "ProcessInitializationController"
    "AllocationReceipt")
    reject_text("${runtime_header}" "${forbidden}"
        "public mutable process-init controller authority")
endforeach()
extract_slice("${runtime_header}"
    "struct DiagnosticEntry final"
    "[[nodiscard]] InitializationStatus"
    diagnostic_api_types)
foreach(pointer_marker IN ITEMS "*" "&" "uintptr")
    reject_text("${diagnostic_api_types}" "${pointer_marker}"
        "pointer-like field in diagnostic snapshot API")
endforeach()
foreach(forbidden IN ITEMS
    "physicalmemory.h"
    "qcommon/"
    "assertive.h"
    "PhysicalMemoryGlobalStateTestAccess"
    "std::mutex"
    "std::atomic")
    reject_text("${runtime_header}" "${forbidden}"
        "broad dependency in narrow runtime API")
endforeach()

# The helper name itself is absent from a production include outside the one
# macro gate; an opaque forward declaration before/after the gate is forbidden.
set(test_gate "#if defined(KISAK_PHYSICAL_MEMORY_RUNTIME_TESTING)")
set(test_gate_end "#endif\n\nvoid KISAK_CDECL PMem_Init();")
string(FIND "${legacy_header}" "${test_gate}" test_gate_begin)
string(FIND "${legacy_header}" "${test_gate_end}" test_gate_end_position)
if(test_gate_begin EQUAL -1
   OR test_gate_end_position LESS_EQUAL test_gate_begin)
    message(FATAL_ERROR "Malformed global PMem test-access declaration gate")
endif()
string(SUBSTRING "${legacy_header}" 0 ${test_gate_begin} before_test_gate)
string(SUBSTRING "${legacy_header}" ${test_gate_end_position} -1 after_test_gate)
reject_text("${before_test_gate}" "PhysicalMemoryGlobalStateTestAccess"
    "PMem test-access name before its macro gate")
reject_text("${after_test_gate}" "PhysicalMemoryGlobalStateTestAccess"
    "PMem test-access name after its macro gate")
require_text("${production_manifest}"
    "\"\${SRC_DIR}/universal/physicalmemory_runtime.h\""
    "runtime header production build enrollment")

# Hidden state consists of one legacy memory image, one witnessed runtime
# control/extent, and a per-thread compatibility shortfall.
foreach(marker IN ITEMS
    "struct RetainedExtent final"
    "struct OwnedAllocationName final"
    "struct OwnedNameState final"
    "enum class ProcessInitPhase : std::uint8_t"
    "struct ProcessInitControl final"
    "struct RuntimeControl final"
    "PhysicalMemory g_mem{};"
    "thread_local int g_overAllocatedSize{};"
    "RuntimeControl g_runtime{};"
    "constexpr char kProcessInitAllocationName[] = \"$init\";"
    "ProcessInitControl processInit{};"
    "constexpr std::uint32_t kPhysicalMemorySize = UINT32_C(0x08000000);"
    "RuntimeWitnessFor("
    "RuntimeReservedIsZero("
    "RuntimeControlIsCanonical("
    "ProcessInitWitnessFor("
    "ProcessInitControlIsCanonical("
    "ProcessInitControlIsDormant("
    "ProcessInitBindingIsCoherent("
    "SetProcessInitPhase("
    "AddressRangeIsValid("
    "AddressRangesOverlap("
    "RetainedExtentIsDisjointFromControl("
    "OwnedNamesArePristine("
    "OwnedNamesMatchMemory("
    "PhysicalMemoryIsPristine("
    "UninitializedStateIsCoherent("
    "ReadyStateIsCoherent("
    "InitializingStateIsCoherent("
    "PoisonedStateIsCoherent("
    "GetRuntimeReadiness("
    "g_runtime.extent.size == kPhysicalMemorySize"
    "&g_mem, sizeof(g_mem)"
    "&g_runtime, sizeof(g_runtime)")
    require_text("${source}" "${marker}" "hidden coherent runtime marker")
endforeach()
require_text("${source}"
    "kProcessInitAllocationName,\n               sizeof(kProcessInitAllocationName)"
    "retained extent disjoint from stable process-init name")
require_text("${source}"
    "|| !ProcessInitBindingIsCoherent())"
    "Ready state authenticates process-init controller relation")
require_count("${source}"
    "ProcessInitControlIsDormant(g_runtime.processInit)" 3
    "non-ready state predicates requiring dormant process-init controller")
foreach(forbidden IN ITEMS
    "strlen(" "strcpy(" "strncpy(" "snprintf(" "sprintf(")
    reject_text("${source}" "${forbidden}"
        "unbounded/formatted owned-name copy")
endforeach()
reject_text("${source}"
    "&g_overAllocatedSize, sizeof(g_overAllocatedSize)"
    "caller-dependent TLS extent authority")
reject_text("${source}" "std::lock_guard"
    "RAII lock across nonlocal engine reporters")
reject_text("${source}" "std::unique_lock"
    "RAII lock across nonlocal engine reporters")
reject_text("${source}" "CRITSECT_CONSOLE"
    "second PMem serializer")

extract_slice("${source}"
    "pmem_runtime::InitializationStatus KISAK_CDECL"
    "pmem_runtime::AllocationResult KISAK_CDECL pmem_runtime::TryAllocate("
    init_slice)
foreach(marker IN ITEMS
    "SetRuntimePhase(&g_runtime, InitializationPhase::Initializing);"
    "Sys_VirtualMemoryReserve(kPhysicalMemorySize);"
    "const RetainedExtent candidateExtent{base, kPhysicalMemorySize};"
    "Sys_VirtualMemoryCommit(reservation, kPhysicalMemorySize)"
    "Sys_VirtualMemoryRelease(reservation)"
    "PublishPoisonedExtent(base, kPhysicalMemorySize);"
    "g_runtime.extent = candidateExtent;"
    "SetRuntimePhase(&g_runtime, InitializationPhase::Ready);"
    "InitializationStatus::ReserveFailed"
    "InitializationStatus::CommitFailed"
    "InitializationStatus::ReleaseFailed"
    "InitializationStatus::AlreadyInitialized"
    "InitializationStatus::Busy"
    "InitializationStatus::Poisoned"
    "InitializationStatus::CorruptState")
    require_text("${init_slice}" "${marker}" "coherent initialization marker")
endforeach()
require_order("${init_slice}"
    "SetRuntimePhase(&g_runtime, InitializationPhase::Initializing);"
    "Sys_VirtualMemoryReserve(kPhysicalMemorySize);"
    "Initializing publication before reserve")
require_order("${init_slice}"
    "Sys_VirtualMemoryReserve(kPhysicalMemorySize);"
    "Sys_VirtualMemoryCommit(reservation, kPhysicalMemorySize)"
    "reserve before commit")
require_text("${init_slice}"
    "g_mem = initialized;\n    g_runtime.extent = candidateExtent;\n    SetRuntimePhase(&g_runtime, InitializationPhase::Ready);"
    "atomic memory/extent/Ready publication")
foreach(forbidden IN ITEMS
    "MyAssertHandler("
    "Com_Printf("
    "Com_Error("
    "Sys_OutOfMemErrorInternal("
    "PMem_InitPhysicalMemory(")
    reject_text("${init_slice}" "${forbidden}"
        "reporting call in TryInitialize")
endforeach()
require_count("${source}" "Sys_VirtualMemoryReserve(" 1
    "physical-memory reserve site")
require_count("${source}" "Sys_VirtualMemoryCommit(" 1
    "physical-memory commit site")
require_count("${source}" "Sys_VirtualMemoryRelease(" 3
    "owned-reservation cleanup sites")

extract_slice("${source}"
    "pmem_runtime::AllocationResult TryAllocateNoLock("
    "pmem_runtime::AllocationResult TryAllocateAndPublishLegacyShortfall("
    allocation_core)
foreach(marker IN ITEMS
    "GetRuntimeReadiness()"
    "AllocationStatus::InvalidRequest"
    "AllocationStatus::NotReady"
    "AllocationStatus::ScopeInactive"
    "AllocationStatus::CorruptState"
    "AllocationStatus::Exhausted"
    "const std::uint64_t baseRemainder ="
    "const std::uint64_t firstAligned = lowPosition + lowPadding;"
    "const std::uint64_t requiredEnd = firstAligned + size;"
    "result.additionalBytes = requiredEnd - highPosition;"
    "allocationPosition = rawPosition - absoluteRemainder;"
    "result.address = &g_mem.buf["
    "result.status = pmem_runtime::AllocationStatus::Success;")
    require_text("${allocation_core}" "${marker}"
        "report-free allocation marker")
endforeach()
foreach(forbidden IN ITEMS
    "Sys_EnterCriticalSection"
    "Sys_LeaveCriticalSection"
    "MyAssertHandler("
    "Com_Printf("
    "Com_Error("
    "Sys_OutOfMemErrorInternal("
    "Sys_VirtualMemory")
    reject_text("${allocation_core}" "${forbidden}"
        "dependency in private allocation core")
endforeach()

extract_slice("${source}"
    "pmem_runtime::AllocationResult TryAllocateAndPublishLegacyShortfall("
    "} // namespace"
    legacy_allocation_bridge)
require_order("${legacy_allocation_bridge}"
    "Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);"
    "TryAllocateNoLock(size, alignment, type, allocType);"
    "legacy allocation lock before core")
require_order("${legacy_allocation_bridge}"
    "TryAllocateNoLock(size, alignment, type, allocType);"
    "g_overAllocatedSize = LegacyShortfallFor(result);"
    "allocation and TLS publication")
require_order("${legacy_allocation_bridge}"
    "g_overAllocatedSize = LegacyShortfallFor(result);"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);"
    "TLS publication before allocation unlock")

extract_slice("${source}"
    "pmem_runtime::AllocationResult KISAK_CDECL pmem_runtime::TryAllocate("
    "void KISAK_CDECL PMem_Init()"
    public_try_allocate)
require_order("${public_try_allocate}"
    "Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);"
    "TryAllocateNoLock(size, alignment, type, allocType);"
    "public report-free allocation lock")
require_order("${public_try_allocate}"
    "TryAllocateNoLock(size, alignment, type, allocType);"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);"
    "public report-free allocation unlock")

extract_slice("${source}"
    "std::uint8_t *KISAK_CDECL PMem_Alloc("
    "std::uint8_t *KISAK_CDECL PMem_TryAlloc("
    fatal_alloc)
require_count("${fatal_alloc}"
    "TryAllocateAndPublishLegacyShortfall(" 1
    "single fatal allocation attempt")
reject_text("${fatal_alloc}" "pmem_runtime::TryAllocate("
    "TOCTOU allocation precheck/call split")
require_order("${fatal_alloc}"
    "TryAllocateAndPublishLegacyShortfall("
    "Com_Error(ERR_FATAL, \"Invalid physical-memory allocation request\");"
    "unlock before invalid-request reporter")
require_order("${fatal_alloc}"
    "TryAllocateAndPublishLegacyShortfall("
    "Sys_OutOfMemErrorInternal(\".\\\\universal\\\\physicalmemory.cpp\", 0);"
    "unlock before OOM reporter")

extract_slice("${source}"
    "std::uint8_t *KISAK_CDECL PMem_TryAlloc("
    "std::uint32_t KISAK_CDECL PMem_GetFreeAmount("
    legacy_try_alloc)
require_count("${legacy_try_alloc}"
    "TryAllocateAndPublishLegacyShortfall(" 1
    "single legacy try-allocation attempt")

# Global lifecycle wrappers lock exactly the hidden global operation, leave,
# and only then invoke the reporter. Caller-owned *InPrim operations do not
# acquire the global serializer and preserve the legacy diagnostic mapping.
foreach(wrapper IN ITEMS PMem_BeginAlloc PMem_EndAlloc PMem_Free)
    if(wrapper STREQUAL "PMem_BeginAlloc")
        set(next "PMem_BeginAllocInPrim")
        set(core
            "TryBeginGlobalAllocNoReport(allocType, name);")
    elseif(wrapper STREQUAL "PMem_EndAlloc")
        set(next "PMem_EndAllocInPrim")
        set(core
            "TryEndGlobalAllocNoReport(allocType, name);")
    else()
        set(next "PMem_FreeInPrim")
        set(core
            "TryFreeGlobalAllocNoReport(allocType, name);")
    endif()
    extract_slice("${source}"
        "void KISAK_CDECL ${wrapper}("
        "void KISAK_CDECL ${next}("
        wrapper_slice)
    require_count("${wrapper_slice}"
        "Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);" 1
        "${wrapper} lock acquisition")
    require_count("${wrapper_slice}"
        "Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);" 1
        "${wrapper} lock release")
    require_order("${wrapper_slice}"
        "Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);"
        "${core}"
        "${wrapper} lock before global lifecycle core")
    require_order("${wrapper_slice}"
        "${core}"
        "Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);"
        "${wrapper} global lifecycle core before unlock")
    require_order("${wrapper_slice}"
        "Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);"
        "ReportLegacyDiagnostic(result, allocType);"
        "${wrapper} explicit unlock before report")
endforeach()

extract_slice("${source}"
    "int KISAK_CDECL PMem_GetOverAllocatedSize("
    "std::uint8_t *KISAK_CDECL PMem_Alloc("
    overallocated_getter)
require_order("${overallocated_getter}"
    "Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);"
    "const int shortfall = g_overAllocatedSize;"
    "over-allocation getter lock before TLS read")
require_order("${overallocated_getter}"
    "const int shortfall = g_overAllocatedSize;"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);"
    "over-allocation getter TLS read before unlock")

extract_slice("${source}"
    "std::uint32_t KISAK_CDECL PMem_GetFreeAmount("
    "return freeAmount;" free_amount_getter)
require_order("${free_amount_getter}"
    "Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);"
    "const std::uint32_t freeAmount = ReadyStateIsCoherent()"
    "free-amount getter lock before coherent read")
require_order("${free_amount_getter}"
    "? g_mem.prim[1].pos - g_mem.prim[0].pos"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);"
    "free-amount getter read before unlock")

extract_slice("${source}"
    "void KISAK_CDECL PMem_BeginAllocInPrim("
    "void KISAK_CDECL PMem_EndAlloc("
    begin_in_prim_public)
extract_slice("${source}"
    "void KISAK_CDECL PMem_EndAllocInPrim("
    "void KISAK_CDECL PMem_Free("
    end_in_prim_public)
extract_slice("${source}"
    "void KISAK_CDECL PMem_FreeInPrim("
    "void KISAK_CDECL PMem_FreeIndex("
    free_in_prim_public)
extract_slice("${source}"
    "void KISAK_CDECL PMem_FreeIndex("
    "int KISAK_CDECL PMem_GetOverAllocatedSize("
    free_index_public)
foreach(slice_name IN ITEMS
    begin_in_prim_public end_in_prim_public
    free_in_prim_public free_index_public)
    reject_text("${${slice_name}}" "Sys_EnterCriticalSection"
        "global locking in caller-owned ${slice_name}")
endforeach()
foreach(marker IN ITEMS
    "TryBeginAllocInPrimNoReport(prim, name)"
    "TryEndAllocInPrimNoReport(prim, name)"
    "TryFreeInPrimNoReport(prim, name)"
    "TryFreeIndexNoReport(prim, allocIndex)")
    require_text("${source}" "${marker}"
        "caller-owned report-free helper delegation")
endforeach()

# Typed indexing, validation-before-mutation, and the fixed middle-hole format
# remain intact inside the private report-free helpers.
foreach(marker IN ITEMS
    "if (!prim)"
    "if (!name)"
    "if (prim->allocListCount >= MAX_PHYSICAL_ALLOCATIONS)"
    "prim->allocList[prim->allocListCount++]"
    "if (prim->allocListCount > MAX_PHYSICAL_ALLOCATIONS)"
    "prim->allocList[prim->allocListCount - 1]"
    "if (allocIndex >= prim->allocListCount)"
    "entry = &prim->allocList[allocIndex];"
    "entry = &prim->allocList[prim->allocListCount - 1];"
    "hole.diagnostic = LegacyDiagnostic::FreeHole;"
    "hole.borrowedName = name;"
    "CopyBoundedName(result.ownedName, result.borrowedName);"
    "result.ownsName = true;"
    "? result.ownedName"
    ": result.borrowedName;"
    "\"freeing '%s' caused a memory hole\\n\", reportedName")
    require_text("${source}" "${marker}" "bounded legacy lifecycle marker")
endforeach()

extract_slice("${source}"
    "LegacyOperationResult ValidateBeginAllocInPrimNoReport("
    "void BeginAllocInPrimNoReport("
    begin_validation)
extract_slice("${source}"
    "void BeginAllocInPrimNoReport("
    "LegacyOperationResult TryBeginAllocInPrimNoReport("
    begin_mutation)
extract_slice("${source}"
    "LegacyOperationResult TryBeginAllocInPrimNoReport("
    "LegacyOperationResult TryBeginGlobalAllocNoReport("
    begin_core)
extract_slice("${source}"
    "LegacyOperationResult TryEndAllocInPrimNoReport("
    "LegacyOperationResult TryFreeIndexNoReport("
    end_core)
extract_slice("${source}"
    "LegacyOperationResult TryFreeIndexNoReport("
    "LegacyOperationResult TryFreeInPrimNoReport("
    free_index_core)
extract_slice("${source}"
    "LegacyOperationResult TryFreeInPrimNoReport("
    "LegacyOperationResult ReadinessDiagnostic("
    free_in_prim_core)
require_order("${begin_validation}" "if (!prim)" "if (!name)"
    "BeginInPrim null guards")
require_order("${begin_validation}" "if (!name)" "if (prim->allocName)"
    "BeginInPrim name before active state")
require_order("${begin_validation}" "if (prim->allocName)"
    "if (prim->allocListCount >= MAX_PHYSICAL_ALLOCATIONS)"
    "BeginInPrim active state before capacity")
require_order("${source}"
    "LegacyOperationResult ValidateBeginAllocInPrimNoReport("
    "prim->allocName = name;"
    "BeginInPrim validation before mutation")
require_order("${begin_core}"
    "ValidateBeginAllocInPrimNoReport(prim, name);"
    "BeginAllocInPrimNoReport(prim, name);"
    "BeginInPrim validation before mutation delegate")
require_order("${end_core}" "if (!prim)" "if (!name)"
    "EndInPrim null guards")
require_order("${end_core}" "if (!name)" "if (prim->allocName != name)"
    "EndInPrim name before identity")
require_order("${end_core}" "if (!prim->allocListCount)"
    "if (prim->allocListCount > MAX_PHYSICAL_ALLOCATIONS)"
    "EndInPrim zero count before bound")
require_order("${end_core}"
    "if (prim->allocListCount > MAX_PHYSICAL_ALLOCATIONS)"
    "prim->allocList[prim->allocListCount - 1]"
    "EndInPrim bound before tail dereference")
require_order("${end_core}" "if (entry.name != name)"
    "prim->allocName = nullptr;"
    "EndInPrim tail identity before mutation")
require_order("${free_index_core}" "if (!prim)" "if (prim->allocName)"
    "FreeIndex null before active state")
require_order("${free_index_core}" "if (!prim->allocListCount)"
    "if (prim->allocListCount > MAX_PHYSICAL_ALLOCATIONS)"
    "FreeIndex zero count before bound")
require_order("${free_index_core}"
    "if (prim->allocListCount > MAX_PHYSICAL_ALLOCATIONS)"
    "if (allocIndex >= prim->allocListCount)"
    "FreeIndex count before index")
require_order("${free_index_core}"
    "if (allocIndex >= prim->allocListCount)"
    "entry = &prim->allocList[allocIndex];"
    "FreeIndex index before dereference")
require_order("${free_index_core}" "if (!name)" "entry->name = nullptr;"
    "FreeIndex name before mutation")
require_order("${free_in_prim_core}" "if (!prim)" "if (!name)"
    "FreeInPrim null guards")
require_order("${free_in_prim_core}" "if (!name)"
    "if (prim->allocListCount > MAX_PHYSICAL_ALLOCATIONS)"
    "FreeInPrim name before bound")
require_order("${free_in_prim_core}"
    "if (prim->allocListCount > MAX_PHYSICAL_ALLOCATIONS)"
    "for (std::uint32_t index = 0; index < prim->allocListCount; ++index)"
    "FreeInPrim bound before scan")
foreach(core_name IN ITEMS
    begin_validation begin_mutation begin_core
    end_core free_index_core free_in_prim_core)
    foreach(forbidden IN ITEMS
        "Sys_EnterCriticalSection" "MyAssertHandler(" "Com_Error("
        "Com_Printf(" "Sys_OutOfMemErrorInternal(")
        reject_text("${${core_name}}" "${forbidden}"
            "report/lock dependency in ${core_name}")
    endforeach()
endforeach()
foreach(forbidden IN ITEMS
    "*(&prim->pos + 2 * prim->allocListCount)"
    "&prim->allocListCount + 2 * prim->allocListCount"
    "va(\"freeing"
    "const char *v3")
    reject_text("${source}" "${forbidden}" "legacy x86/raw diagnostic pattern")
endforeach()

# Owned global names retain caller identity separately from bounded text. All
# sidecar changes occur inside the wrapper's single serializer, while the
# report result owns any middle-hole name consumed after unlock.
foreach(marker IN ITEMS
    "constexpr std::size_t kOwnedNameCapacity = MAX_QPATH;"
    "static_assert(kOwnedNameCapacity == 64u);"
    "std::uintptr_t identity = 0;"
    "std::uintptr_t identityWitness = 0;"
    "char text[kOwnedNameCapacity]{};"
    "OwnedNameIdentityWitness("
    "owned = CaptureOwnedName(name, allocType, prim.allocListCount);"
    "BeginAllocInPrimNoReport(&prim, owned.text);"
    "owned.identity != reinterpret_cast<std::uintptr_t>(name)"
    "g_runtime.ownedNames.names[allocType][allocIndex] = {};"
    "g_runtime.ownedNames.names[allocType][index] = {};"
    "g_runtime.ownedNames.names[allocType][index].identity == identity")
    require_text("${source}" "${marker}" "owned global name marker")
endforeach()

# The pointer-free capture core reads one coherent instant without callbacks;
# its public wrapper owns the one lock. Dump consumes only that returned value.
extract_slice("${source}"
    "TryCaptureDiagnosticSnapshotNoLock() noexcept"
    "void ReportLegacyDiagnostic("
    diagnostic_core)
foreach(marker IN ITEMS
    "GetRuntimeReadiness()"
    "DiagnosticSnapshotStatus::CorruptState"
    "snapshot.highCount = high.allocListCount;"
    "snapshot.lowCount = low.allocListCount;"
    "snapshot.freeBytes = high.pos - low.pos;"
    "DiagnosticEntryKind::Allocation"
    "DiagnosticEntryKind::Hole"
    "CopyBoundedName(entry.name, \"<hole>\");"
    "DiagnosticSnapshotStatus::Success")
    require_text("${diagnostic_core}" "${marker}"
        "bounded diagnostic capture marker")
endforeach()
foreach(forbidden IN ITEMS
    "Sys_EnterCriticalSection" "Sys_LeaveCriticalSection"
    "MyAssertHandler(" "Com_Printf(" "Com_Error("
    "ConvertToMB(" "Sys_OutOfMemErrorInternal("
    "Sys_VirtualMemory" "new " "malloc(")
    reject_text("${diagnostic_core}" "${forbidden}"
        "callback/allocation in diagnostic capture core")
endforeach()

extract_slice("${source}"
    "pmem_runtime::TryCaptureDiagnosticSnapshot() noexcept"
    "void KISAK_CDECL PMem_Init()"
    public_diagnostic_capture)
require_order("${public_diagnostic_capture}"
    "Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);"
    "TryCaptureDiagnosticSnapshotNoLock();"
    "diagnostic capture lock before core")
require_order("${public_diagnostic_capture}"
    "TryCaptureDiagnosticSnapshotNoLock();"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);"
    "diagnostic capture core before unlock")

# The process-lifetime `$init` controller is hidden inside the same serialized
# runtime. Its report-free, no-argument API can neither expose nor duplicate
# the owned-name/index-zero authority. Dormant remains compatible with the
# still-enrolled legacy startup until all five lifecycle sites move together.
extract_slice("${source}"
    "pmem_runtime::ProcessInitAllocationStatus KISAK_CDECL"
    "void KISAK_CDECL PMem_Init()"
    process_init_operations)
foreach(marker IN ITEMS
    "TryBeginProcessInitAllocation() noexcept"
    "TryEndProcessInitAllocation() noexcept"
    "GetRuntimeReadiness()"
    "ProcessInitAllocationStatus::NotReady"
    "ProcessInitAllocationStatus::CorruptState"
    "ProcessInitAllocationStatus::Busy"
    "ProcessInitAllocationStatus::WrongPhase"
    "ProcessInitAllocationStatus::AlreadyComplete"
    "TryBeginGlobalAllocNoReport("
    "1, kProcessInitAllocationName"
    "TryEndAllocInPrimNoReport(&high, high.allocName)"
    "SetProcessInitPhase("
    "ProcessInitPhase::Begun"
    "ProcessInitPhase::Ended"
    "ReadyStateIsCoherent()")
    require_text("${process_init_operations}" "${marker}"
        "serialized process-init operation marker")
endforeach()
require_count("${process_init_operations}"
    "Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);" 2
    "process-init serializer acquisitions")
require_count("${process_init_operations}"
    "Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);" 2
    "process-init serializer releases")
foreach(forbidden IN ITEMS
    "MyAssertHandler("
    "Com_Printf("
    "Com_Error("
    "Sys_OutOfMemErrorInternal("
    "PMem_BeginAlloc("
    "PMem_EndAlloc("
    "PMem_Free("
    "physical_memory::"
    "AllocationReceipt")
    reject_text("${process_init_operations}" "${forbidden}"
        "report, legacy call, or duplicate receipt authority in process-init API")
endforeach()
require_count("${source}" "TryBeginProcessInitAllocation()" 1
    "production process-init Begin definition without caller")
require_count("${source}" "TryEndProcessInitAllocation()" 1
    "production process-init End definition without caller")
foreach(marker IN ITEMS
    "g_runtime.processInit.phase != ProcessInitPhase::Dormant"
    "g_runtime.processInit.phase == ProcessInitPhase::Begun"
    "allocType == 1 && allocIndex == 0"
    "LegacyDiagnostic::ProcessInitProtected")
    require_text("${source}" "${marker}"
        "legacy protection for process-owned high index zero")
endforeach()

require_count("${common_source}"
    "PMem_BeginAlloc(comInitAllocName, 1u);" 1
    "legacy process-init Begin site before atomic cutover")
require_count("${common_source}"
    "PMem_EndAlloc(comInitAllocName, 1u);" 1
    "legacy process-init End site before atomic cutover")
require_order("${common_source}"
    "PMem_EndAlloc(comInitAllocName, 1u);"
    "DB_SetInitializing(0);"
    "legacy process-init End before initializing-flag clear")
require_count("${registry_source}"
    "PMem_BeginAlloc(zone->name, g_zoneAllocType);" 1
    "legacy zone Begin site before atomic cutover")
require_count("${registry_source}"
    "PMem_EndAlloc(zone->name, g_zoneAllocType);" 1
    "legacy zone End site before atomic cutover")
require_count("${registry_source}"
    "PMem_Free(zone->name, zone->allocType);" 1
    "legacy zone Free site before atomic cutover")
foreach(production_text IN ITEMS common_source registry_source)
    reject_text("${${production_text}}"
        "TryBeginProcessInitAllocation"
        "partial production process-init Begin enrollment")
    reject_text("${${production_text}}"
        "TryEndProcessInitAllocation"
        "partial production process-init End enrollment")
endforeach()
file(GLOB_RECURSE process_init_production_sources LIST_DIRECTORIES false
    "${SOURCE_ROOT}/src/*")
foreach(path IN LISTS process_init_production_sources)
    if(path STREQUAL
           "${SOURCE_ROOT}/src/universal/physicalmemory_runtime.h"
       OR path STREQUAL
           "${SOURCE_ROOT}/src/universal/physicalmemory.cpp")
        continue()
    endif()
    file(READ "${path}" process_init_production_content)
    foreach(operation IN ITEMS
        TryBeginProcessInitAllocation
        TryEndProcessInitAllocation)
        reject_text("${process_init_production_content}" "${operation}"
            "process-init operation enrollment outside atomic cutover in ${path}")
    endforeach()
endforeach()

extract_slice("${source}"
    "void KISAK_CDECL PMem_DumpMemStats()"
    "void KISAK_CDECL PMem_InitPhysicalMemory("
    dump_slice)
foreach(marker IN ITEMS
    "pmem_runtime::TryCaptureDiagnosticSnapshot();"
    "DiagnosticSnapshotStatus::NotReady"
    "DiagnosticSnapshotStatus::Success"
    "%-18.18s %5.1f\\n"
    "free physical      %5.1f\\n"
    "physical memory unavailable (not initialized)\\n"
    "physical memory unavailable (corrupt state)\\n"
    "remaining != 0")
    require_text("${dump_slice}" "${marker}" "snapshot-only dump marker")
endforeach()
foreach(forbidden IN ITEMS
    "g_mem" "g_runtime" "ownedNames" "allocList"
    "Sys_EnterCriticalSection" "Sys_LeaveCriticalSection"
    "PMem_GetFreeAmount(" "MyAssertHandler("
    "static DiagnosticSnapshot")
    reject_text("${dump_slice}" "${forbidden}"
        "live/global read in snapshot-only dump")
endforeach()
require_order("${dump_slice}"
    "pmem_runtime::TryCaptureDiagnosticSnapshot();"
    "ConvertToMB("
    "dump capture before conversion/reporting")

# Generic initialization rejects every null/zero input before mutation.
extract_slice("${source}"
    "void KISAK_CDECL PMem_InitPhysicalMemory("
    "void KISAK_CDECL PMem_BeginAlloc("
    init_memory)
foreach(marker IN ITEMS "if (!pmem)" "if (!memory)" "if (!memorySize)")
    require_text("${init_memory}" "${marker}"
        "physical-memory initializer guard")
endforeach()
require_count("${init_memory}" "return;" 3
    "physical-memory initializer failure return")
require_order("${init_memory}" "if (!memorySize)"
    "InitializePhysicalMemoryNoReport(pmem, memory, memorySize);"
    "all initializer guards before mutation")

# The macro-gated by-value seam covers the complete hidden state but emits no
# production helper symbol. Tests compare members, never native padding.
foreach(marker IN ITEMS
    "class PhysicalMemoryGlobalStateTestAccess final"
    "static constexpr std::size_t OWNED_NAME_CAPACITY = 64u;"
    "struct OwnedNameSnapshot final"
    "struct OwnedNameBindingSnapshot final"
    "std::uint8_t type = UINT8_MAX;"
    "std::uint8_t index = UINT8_MAX;"
    "std::uintptr_t identity = 0;"
    "std::uintptr_t identityWitness = 0;"
    "OwnedNameSnapshot ownedNames[2][MAX_PHYSICAL_ALLOCATIONS]{};"
    "OwnedNameBindingSnapshot allocNameBindings[2]{};"
    "allocationNameBindings[2][MAX_PHYSICAL_ALLOCATIONS]{};"
    "std::uint8_t *retainedBase = nullptr;"
    "std::uint32_t retainedSize = 0;"
    "std::uint8_t initializationPhase = 0;"
    "std::uint8_t runtimeReserved[3]{};"
    "std::uint32_t initializationWitness = 0;"
    "std::uint8_t processInitPhase = 0;"
    "std::uint8_t processInitReserved[3]{};"
    "std::uint32_t processInitWitness = 0;"
    "ProcessInitAllocationNameAddress() noexcept;"
    "MakeCanonicalReady("
    "Snapshot Capture() noexcept;"
    "static void Install(const Snapshot &snapshot) noexcept;")
    require_text("${legacy_header}" "${marker}" "macro-gated state seam")
endforeach()
foreach(marker IN ITEMS
    "LogicalizeTestNamePointer("
    "TestOwnedNameBinding *const binding"
    "binding->type = static_cast<std::uint8_t>(type);"
    "binding->index = static_cast<std::uint8_t>(index);"
    "reinterpret_cast<std::uintptr_t>(&g_runtime)"
    "reinterpret_cast<std::uintptr_t>(&g_mem)"
    "reinterpret_cast<std::uintptr_t>(&g_overAllocatedSize)"
    "snapshot.allocationNameBindings[type][index]"
    "snapshot.allocNameBindings[type]"
    "snapshot.ownedNames[binding.type][binding.index]"
    "snapshot.processInitPhase ="
    "snapshot.processInitReserved[0]"
    "snapshot.processInitWitness"
    "g_runtime.processInit.phase ="
    "g_runtime.processInit.reserved[0]"
    "g_runtime.processInit.witness"
    "reinterpret_cast<std::uintptr_t>(kProcessInitAllocationName)"
    "g_mem.prim[type].allocList[index].name == logicalPointer"
    "g_mem.prim[type].allocName == logicalPointer")
    require_text("${source}" "${marker}"
        "alias-free complete PMem test snapshot marker")
endforeach()
foreach(marker IN ITEMS
    "expired-caller-storage"
    "g_reuseSidecarOnAssert"
    "aliased.allocationNameBindings[0][0]"
    "rebound.allocationNameBindings[0][0].index == 1"
    "noOwnedBinding.type == UINT8_MAX"
    "noOwnedBinding.index == UINT8_MAX"
    "expectedLongReport")
    require_text("${runtime_fixture}${legacy_fixture}" "${marker}"
        "owned-name lifetime and binding regression fixture")
endforeach()
foreach(fixture IN ITEMS legacy_fixture runtime_fixture)
    reject_text("${${fixture}}" "std::memcmp"
        "padding-sensitive PMem fixture comparison")
    reject_text("${${fixture}}" "std::memcpy"
        "padding-sensitive PMem fixture snapshot")
endforeach()
foreach(fixture IN ITEMS legacy_fixture runtime_fixture)
    reject_text("${${fixture}}" "std::array<std::byte"
        "padding-sensitive byte image")
    reject_text("${${fixture}}" "&StateAccess::Capture()"
        "address-taking from by-value runtime capture")
    reject_text("${${fixture}}" "&PhysicalMemoryStateAccess::Capture()"
        "address-taking from by-value legacy capture")
endforeach()
require_count("${legacy_header}"
    "KISAK_PHYSICAL_MEMORY_RUNTIME_TESTING" 1
    "global PMem declaration gate")
require_count("${source}"
    "KISAK_PHYSICAL_MEMORY_RUNTIME_TESTING" 1
    "global PMem implementation gate")

# Only the two dedicated fixtures may define the test seam. Production source,
# build manifests, and workflows may never select it.
file(GLOB_RECURSE production_build_manifests LIST_DIRECTORIES false
    "${SOURCE_ROOT}/scripts/*"
    "${SOURCE_ROOT}/milesEq/*.bat")
file(GLOB root_build_manifests LIST_DIRECTORIES false
    "${SOURCE_ROOT}/*.bat" "${SOURCE_ROOT}/*.cmd"
    "${SOURCE_ROOT}/*.ps1" "${SOURCE_ROOT}/*.sh")
list(APPEND production_build_manifests
    "${SOURCE_ROOT}/CMakeLists.txt" ${root_build_manifests})
foreach(path IN LISTS production_build_manifests)
    file(READ "${path}" content)
    reject_text("${content}" "KISAK_PHYSICAL_MEMORY_RUNTIME_TESTING"
        "PMem test seam in production manifest ${path}")
endforeach()
file(GLOB_RECURSE production_sources LIST_DIRECTORIES false
    "${SOURCE_ROOT}/src/*")
foreach(path IN LISTS production_sources)
    if(path STREQUAL "${SOURCE_ROOT}/src/universal/physicalmemory.h"
       OR path STREQUAL "${SOURCE_ROOT}/src/universal/physicalmemory.cpp")
        continue()
    endif()
    file(READ "${path}" content)
    reject_text("${content}" "KISAK_PHYSICAL_MEMORY_RUNTIME_TESTING"
        "PMem test seam outside declaration/implementation gates ${path}")
endforeach()
reject_text("${workflow}" "KISAK_PHYSICAL_MEMORY_RUNTIME_TESTING"
    "PMem test seam in hosted workflow")
file(GLOB_RECURSE hosted_workflows LIST_DIRECTORIES false
    "${SOURCE_ROOT}/.github/workflows/*.yml"
    "${SOURCE_ROOT}/.github/workflows/*.yaml")
foreach(path IN LISTS hosted_workflows)
    file(READ "${path}" content)
    reject_text("${content}" "KISAK_PHYSICAL_MEMORY_RUNTIME_TESTING"
        "PMem test seam in hosted workflow ${path}")
endforeach()
require_count("${test_manifest}"
    "KISAK_PHYSICAL_MEMORY_RUNTIME_TESTING" 2
    "dedicated PMem fixture definitions")

# Runtime coverage, macro-off object proof, and hosted selection travel with
# the implementation.
foreach(marker IN ITEMS
    "add_executable(kisakcod-physicalmemory-legacy-tests"
    "add_executable(kisakcod-physicalmemory-runtime-tests"
    "physicalmemory_runtime_tests.cpp"
    "NAME universal-physicalmemory-runtime-control"
    "KISAK_MP"
    "add_library(kisakcod-physicalmemory-runtime-production-object OBJECT"
    "NAME universal-physicalmemory-runtime-production-test-access-sealed"
    "READELF_TOOL=\${CMAKE_READELF}"
    "physicalmemory_runtime_production_seal_test.cmake")
    require_text("${test_manifest}" "${marker}" "PMem test/build enrollment")
endforeach()
foreach(marker IN ITEMS
    "TestInitFailuresRetryAndDoubleInit();"
    "TestConcurrentAndReentrantInit();"
    "TestInitCorruptionOwnershipExits();"
    "TestInitPhysicalMemoryFailureAtomicity();"
    "TestAllocationStatusesAndAtomicity();"
    "TestAbsoluteAlignmentAndExactHighShortfall();"
    "TestContention();"
    "TestLegacyShortfallTlsAndReports();"
    "TestLifecycleSerializationAndReportOrdering();"
    "TestDiagnosticStatusAndStableNames();"
    "TestDiagnosticAccountingAndDumpOrder();"
    "TestDiagnosticCapacityAndSidecarCorruption();"
    "TestDumpReentryAndSnapshotContention();"
    "TestProcessInitControllerLifecycleAndLegacyCoexistence();"
    "TestProcessInitControllerCorruptionAndAtomicity();"
    "TestProcessInitControllerConcurrencyAndDisjointness();"
    "TestExtentPhaseAndTopologyCorruption();"
    "HookAction::CorruptReserved"
    "CheckCanonicalPoisoned();"
    "AllocationStatus::CorruptState"
    "std::adjacent_find"
    "PMem_GetOverAllocatedSize() == INT_MAX"
    "CheckUnlockedService();")
    require_text("${runtime_fixture}" "${marker}"
        "focused PMem runtime coverage")
endforeach()
foreach(marker IN ITEMS
    "#include <universal/physicalmemory_runtime.h>"
    "std::is_standard_layout_v<pmem_runtime::AllocationResult>"
    "std::is_standard_layout_v<pmem_runtime::DiagnosticSnapshot>"
    "sizeof(pmem_runtime::DiagnosticSnapshot) == 0x610"
    "noexcept(pmem_runtime::TryCaptureDiagnosticSnapshot())"
    "sizeof(pmem_runtime::ProcessInitAllocationStatus) == 1"
    "noexcept(pmem_runtime::TryBeginProcessInitAllocation())"
    "noexcept(pmem_runtime::TryEndProcessInitAllocation())"
    "class PhysicalMemoryGlobalStateTestAccess final")
    require_text("${production_seal_fixture}" "${marker}"
        "macro-off compile seal marker")
endforeach()
foreach(marker IN ITEMS
    "g_mem"
    "g_runtime"
    "g_overAllocatedSize"
    "kProcessInitAllocationName"
    "local process-init name"
    "__MergedGlobals"
    "named local TLS shortfall"
    "READELF_TOOL"
    "TLS[ \\t]+LOCAL[ \\t]+DEFAULT"
    "OBJECT[ \\t]+LOCAL[ \\t]+DEFAULT"
    "PhysicalMemoryGlobalStateTestAccess")
    require_text("${production_symbol_seal}" "${marker}"
        "strong production object-symbol seal")
endforeach()
foreach(marker IN ITEMS
    "kisakcod-physicalmemory-runtime-tests"
    "kisakcod-physicalmemory-runtime-production-seal-tests"
    "universal-physicalmemory-legacy-(layout-and-indexing|source-invariants)"
    "universal-physicalmemory-runtime-(control|production-test-access-sealed)"
    "universal-physicalmemory-checked-(scopes|api-sealed|source-invariants)")
    require_text("${workflow}" "${marker}" "hosted PMem selection")
endforeach()
