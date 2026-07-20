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
    "TryInitialize() noexcept;"
    "TryAllocate("
    "std::uint32_t allocType) noexcept;")
    require_text("${runtime_header}" "${marker}" "narrow runtime API marker")
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
    "struct RuntimeControl final"
    "PhysicalMemory g_mem{};"
    "thread_local int g_overAllocatedSize{};"
    "RuntimeControl g_runtime{};"
    "constexpr std::uint32_t kPhysicalMemorySize = UINT32_C(0x08000000);"
    "RuntimeWitnessFor("
    "RuntimeReservedIsZero("
    "RuntimeControlIsCanonical("
    "AddressRangeIsValid("
    "AddressRangesOverlap("
    "RetainedExtentIsDisjointFromControl("
    "PhysicalMemoryIsPristine("
    "ReadyStateIsCoherent("
    "InitializingStateIsCoherent("
    "PoisonedStateIsCoherent("
    "GetRuntimeReadiness("
    "g_runtime.extent.size == kPhysicalMemorySize"
    "&g_mem, sizeof(g_mem)"
    "&g_runtime, sizeof(g_runtime)")
    require_text("${source}" "${marker}" "hidden coherent runtime marker")
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
            "TryBeginAllocInPrimNoReport(&g_mem.prim[allocType], name);")
    elseif(wrapper STREQUAL "PMem_EndAlloc")
        set(next "PMem_EndAllocInPrim")
        set(core
            "TryEndAllocInPrimNoReport(&g_mem.prim[allocType], name);")
    else()
        set(next "PMem_FreeInPrim")
        set(core
            "TryFreeInPrimNoReport(&g_mem.prim[allocType], name);")
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
    "return {LegacyDiagnostic::FreeHole, name};"
    "\"freeing '%s' caused a memory hole\\n\", result.name")
    require_text("${source}" "${marker}" "bounded legacy lifecycle marker")
endforeach()

extract_slice("${source}"
    "LegacyOperationResult TryBeginAllocInPrimNoReport("
    "LegacyOperationResult TryEndAllocInPrimNoReport("
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
require_order("${begin_core}" "if (!prim)" "if (!name)"
    "BeginInPrim null guards")
require_order("${begin_core}" "if (!name)" "if (prim->allocName)"
    "BeginInPrim name before active state")
require_order("${begin_core}" "if (prim->allocName)"
    "if (prim->allocListCount >= MAX_PHYSICAL_ALLOCATIONS)"
    "BeginInPrim active state before capacity")
require_order("${begin_core}"
    "if (prim->allocListCount >= MAX_PHYSICAL_ALLOCATIONS)"
    "prim->allocName = name;"
    "BeginInPrim validation before mutation")
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
foreach(core_name IN ITEMS begin_core end_core free_index_core free_in_prim_core)
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
    "std::uint8_t *retainedBase = nullptr;"
    "std::uint32_t retainedSize = 0;"
    "std::uint8_t initializationPhase = 0;"
    "std::uint8_t runtimeReserved[3]{};"
    "std::uint32_t initializationWitness = 0;"
    "MakeCanonicalReady("
    "Snapshot Capture() noexcept;"
    "static void Install(const Snapshot &snapshot) noexcept;")
    require_text("${legacy_header}" "${marker}" "macro-gated state seam")
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
    "class PhysicalMemoryGlobalStateTestAccess final")
    require_text("${production_seal_fixture}" "${marker}"
        "macro-off compile seal marker")
endforeach()
foreach(marker IN ITEMS
    "g_mem"
    "g_runtime"
    "g_overAllocatedSize"
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
