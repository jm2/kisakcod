#include <universal/physicalmemory.h>
#include <universal/physicalmemory_runtime.h>

#include <qcommon/com_error.h>
#include <qcommon/sys_sync.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <climits>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
using StateAccess = PhysicalMemoryGlobalStateTestAccess;
using pmem_runtime::AllocationResult;
using pmem_runtime::AllocationStatus;
using pmem_runtime::DiagnosticEntryKind;
using pmem_runtime::DiagnosticSnapshot;
using pmem_runtime::DiagnosticSnapshotStatus;
using pmem_runtime::InitializationPhase;
using pmem_runtime::InitializationStatus;
using pmem_runtime::ProcessInitAllocationStatus;

constexpr std::uint32_t kRuntimeSize = UINT32_C(0x08000000);
constexpr std::uint8_t kProcessInitDormant = 0;
constexpr std::uint8_t kProcessInitBegun = 1;
constexpr std::uint8_t kProcessInitEnded = 2;
constexpr std::uint32_t kProcessInitBegunWitness = UINT32_C(0x4245474E);
constexpr std::uint32_t kProcessInitEndedWitness = UINT32_C(0x454E4444);
alignas(64) std::array<std::uint8_t, kRuntimeSize + 64u> g_backing{};

std::atomic<int> g_failures{};
std::recursive_mutex g_criticalSection;
thread_local int g_lockDepth;
std::atomic<int> g_enterCalls{};
std::atomic<int> g_leaveCalls{};
std::atomic<int> g_reentryViolations{};
std::atomic<int> g_wrongCriticalSections{};

std::atomic<int> g_reserveCalls{};
std::atomic<int> g_commitCalls{};
std::atomic<int> g_releaseCalls{};
std::atomic<bool> g_reserveSucceeds{true};
std::atomic<bool> g_commitSucceeds{true};
std::atomic<bool> g_releaseSucceeds{true};
std::atomic<std::uint32_t> g_backingOffset{};
std::atomic<std::uintptr_t> g_reservationOverride{};

std::mutex g_blockMutex;
std::condition_variable g_blockCondition;
bool g_blockReserve;
bool g_reserveEntered;
bool g_releaseReserve;

enum class HookAction : std::uint8_t
{
    None,
    Reenter,
    CorruptReserved,
};

std::atomic<HookAction> g_reserveHook{HookAction::None};
std::atomic<HookAction> g_commitHook{HookAction::None};
std::atomic<HookAction> g_releaseHook{HookAction::None};
std::atomic<int> g_hookBusyResults{};
std::atomic<int> g_hookNotReadyResults{};
std::atomic<int> g_hookLocalReservationResults{};

std::atomic<int> g_assertReports{};
std::atomic<int> g_errorReports{};
std::atomic<int> g_oomReports{};
std::atomic<int> g_printReports{};
std::atomic<int> g_reportLockViolations{};
std::atomic<int> g_reportReentries{};
std::atomic<int> g_reportedShortfall{};

struct PrintRecord final
{
    char format[64]{};
    char name[64]{};
    double value = 0.0;
};

constexpr std::size_t kMaximumReportRecords = 256u;
std::array<PrintRecord, kMaximumReportRecords> g_printRecords{};
std::array<int, kMaximumReportRecords> g_convertInputs{};
std::atomic<std::size_t> g_printRecordCount{};
std::atomic<std::size_t> g_convertInputCount{};
char g_holeReportName[StateAccess::OWNED_NAME_CAPACITY]{};
char *g_mutateNameOnAssert;
std::size_t g_mutateNameCapacity;
bool g_reuseSidecarOnAssert;
const char *g_holeReentryTailName;
const char *g_holeReentryReuseName;
std::atomic<bool> g_dumpMutationHook{};
const char *g_dumpMutationOldName;
const char *g_dumpMutationNewName;

template <std::size_t Capacity>
void CopyText(char (&destination)[Capacity], const char *const source)
{
    for (char &value : destination)
        value = 0;
    if (!source)
        return;
    for (std::size_t index = 0; index + 1 < Capacity; ++index)
    {
        destination[index] = source[index];
        if (!source[index])
            return;
    }
}

void Check(
    const bool condition,
    const char *const expression,
    const int line)
{
    if (condition)
        return;
    std::fprintf(stderr, "line %d: check failed: %s\n", line, expression);
    g_failures.fetch_add(1, std::memory_order_relaxed);
}

#define CHECK(expression) \
    Check(static_cast<bool>(expression), #expression, __LINE__)

bool SameAllocation(
    const PhysicalMemoryAllocation &left,
    const PhysicalMemoryAllocation &right)
{
    return left.name == right.name && left.pos == right.pos;
}

bool SamePrim(
    const PhysicalMemoryPrim &left,
    const PhysicalMemoryPrim &right)
{
    if (left.allocName != right.allocName
        || left.allocListCount != right.allocListCount
        || left.pos != right.pos)
    {
        return false;
    }
    for (std::uint32_t index = 0; index < MAX_PHYSICAL_ALLOCATIONS; ++index)
    {
        if (!SameAllocation(left.allocList[index], right.allocList[index]))
            return false;
    }
    return true;
}

bool SameMemory(
    const PhysicalMemory &left,
    const PhysicalMemory &right)
{
    return left.buf == right.buf
        && SamePrim(left.prim[0], right.prim[0])
        && SamePrim(left.prim[1], right.prim[1]);
}

bool SameSnapshot(
    const StateAccess::Snapshot &left,
    const StateAccess::Snapshot &right)
{
    if (!SameMemory(left.memory, right.memory)
        || left.overAllocatedSize != right.overAllocatedSize
        || left.retainedBase != right.retainedBase
        || left.retainedSize != right.retainedSize
        || left.initializationPhase != right.initializationPhase
        || left.runtimeReserved[0] != right.runtimeReserved[0]
        || left.runtimeReserved[1] != right.runtimeReserved[1]
        || left.runtimeReserved[2] != right.runtimeReserved[2]
        || left.initializationWitness != right.initializationWitness
        || left.processInitPhase != right.processInitPhase
        || left.processInitReserved[0] != right.processInitReserved[0]
        || left.processInitReserved[1] != right.processInitReserved[1]
        || left.processInitReserved[2] != right.processInitReserved[2]
        || left.processInitWitness != right.processInitWitness)
    {
        return false;
    }
    for (std::uint32_t type = 0; type < 2; ++type)
    {
        if (left.allocNameBindings[type].type
                != right.allocNameBindings[type].type
            || left.allocNameBindings[type].index
                != right.allocNameBindings[type].index)
        {
            return false;
        }
        for (std::uint32_t index = 0;
             index < MAX_PHYSICAL_ALLOCATIONS;
             ++index)
        {
            const auto &leftName = left.ownedNames[type][index];
            const auto &rightName = right.ownedNames[type][index];
            const auto &leftBinding =
                left.allocationNameBindings[type][index];
            const auto &rightBinding =
                right.allocationNameBindings[type][index];
            if (leftName.identity != rightName.identity
                || leftName.identityWitness != rightName.identityWitness
                || leftBinding.type != rightBinding.type
                || leftBinding.index != rightBinding.index)
                return false;
            for (std::size_t byte = 0;
                 byte < StateAccess::OWNED_NAME_CAPACITY;
                 ++byte)
            {
                if (leftName.text[byte] != rightName.text[byte])
                    return false;
            }
        }
    }
    return true;
}

bool IsPristineMemory(const PhysicalMemory &memory)
{
    PhysicalMemory pristine{};
    return SameMemory(memory, pristine);
}

void CheckProcessInitState(
    const StateAccess::Snapshot &snapshot,
    const std::uint8_t phase,
    const std::uint32_t witness)
{
    CHECK(snapshot.processInitPhase == phase);
    CHECK(snapshot.processInitReserved[0] == 0);
    CHECK(snapshot.processInitReserved[1] == 0);
    CHECK(snapshot.processInitReserved[2] == 0);
    CHECK(snapshot.processInitWitness == witness);
}

void CheckResult(
    const AllocationResult &result,
    const AllocationStatus status,
    const std::uint64_t additionalBytes,
    std::uint8_t *const address)
{
    CHECK(result.status == status);
    CHECK(result.additionalBytes == additionalBytes);
    CHECK(result.address == address);
    CHECK(result.reserved[0] == 0);
    CHECK(result.reserved[1] == 0);
    CHECK(result.reserved[2] == 0);
}

bool DiagnosticNameIsCanonical(
    const char (&name)[pmem_runtime::DIAGNOSTIC_NAME_CAPACITY])
{
    bool terminated = false;
    for (const char value : name)
    {
        if (terminated && value)
            return false;
        if (!value)
            terminated = true;
    }
    return terminated;
}

void CheckDiagnosticPayloadIsZero(const DiagnosticSnapshot &snapshot)
{
    CHECK(snapshot.highCount == 0);
    CHECK(snapshot.lowCount == 0);
    CHECK(snapshot.freeBytes == 0);
    CHECK(snapshot.reserved[0] == 0);
    CHECK(snapshot.reserved[1] == 0);
    CHECK(snapshot.reserved[2] == 0);
    for (std::uint32_t index = 0;
         index < pmem_runtime::DIAGNOSTIC_ENTRIES_PER_PRIM;
         ++index)
    {
        const std::array<const pmem_runtime::DiagnosticEntry *, 2> entries{
            &snapshot.high[index],
            &snapshot.low[index],
        };
        for (const pmem_runtime::DiagnosticEntry *const entry : entries)
        {
            CHECK(entry->bytes == 0);
            CHECK(entry->kind == DiagnosticEntryKind::Unused);
            for (const char value : entry->name)
                CHECK(value == 0);
        }
    }
}

void CheckUnlockedService()
{
    if (g_lockDepth != 0)
        g_reportLockViolations.fetch_add(1, std::memory_order_relaxed);
}

void RunHook(const HookAction action)
{
    CheckUnlockedService();
    if (action == HookAction::None)
        return;
    if (action == HookAction::CorruptReserved)
    {
        StateAccess::Snapshot state = StateAccess::Capture();
        state.runtimeReserved[1] ^= UINT8_C(0x5A);
        StateAccess::Install(state);
        return;
    }

    const StateAccess::Snapshot during = StateAccess::Capture();
    if (during.initializationPhase
            == static_cast<std::uint8_t>(InitializationPhase::Initializing)
        && during.retainedBase == nullptr && during.retainedSize == 0)
    {
        g_hookLocalReservationResults.fetch_add(1, std::memory_order_relaxed);
    }
    if (pmem_runtime::TryInitialize() == InitializationStatus::Busy)
        g_hookBusyResults.fetch_add(1, std::memory_order_relaxed);
    if (pmem_runtime::TryAllocate(1, 1, 0, 0).status
        == AllocationStatus::NotReady)
    {
        g_hookNotReadyResults.fetch_add(1, std::memory_order_relaxed);
    }
    CHECK(PMem_GetFreeAmount() == 0);
}

void ResetRuntime()
{
    CHECK(g_lockDepth == 0);
    CHECK(g_enterCalls.load(std::memory_order_relaxed)
        == g_leaveCalls.load(std::memory_order_relaxed));
    CHECK(g_reentryViolations.load(std::memory_order_relaxed) == 0);
    CHECK(g_wrongCriticalSections.load(std::memory_order_relaxed) == 0);
    CHECK(g_reportLockViolations.load(std::memory_order_relaxed) == 0);
    StateAccess::Install({});
    g_reserveSucceeds.store(true, std::memory_order_relaxed);
    g_commitSucceeds.store(true, std::memory_order_relaxed);
    g_releaseSucceeds.store(true, std::memory_order_relaxed);
    g_backingOffset.store(0, std::memory_order_relaxed);
    g_reservationOverride.store(0, std::memory_order_relaxed);
    g_reserveHook.store(HookAction::None, std::memory_order_relaxed);
    g_commitHook.store(HookAction::None, std::memory_order_relaxed);
    g_releaseHook.store(HookAction::None, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_blockMutex);
        g_blockReserve = false;
        g_reserveEntered = false;
        g_releaseReserve = false;
    }
    g_reserveCalls.store(0, std::memory_order_relaxed);
    g_commitCalls.store(0, std::memory_order_relaxed);
    g_releaseCalls.store(0, std::memory_order_relaxed);
    g_enterCalls.store(0, std::memory_order_relaxed);
    g_leaveCalls.store(0, std::memory_order_relaxed);
    g_hookBusyResults.store(0, std::memory_order_relaxed);
    g_hookNotReadyResults.store(0, std::memory_order_relaxed);
    g_hookLocalReservationResults.store(0, std::memory_order_relaxed);
    g_assertReports.store(0, std::memory_order_relaxed);
    g_errorReports.store(0, std::memory_order_relaxed);
    g_oomReports.store(0, std::memory_order_relaxed);
    g_printReports.store(0, std::memory_order_relaxed);
    g_reportReentries.store(0, std::memory_order_relaxed);
    g_reportedShortfall.store(0, std::memory_order_relaxed);
    g_printRecordCount.store(0, std::memory_order_relaxed);
    g_convertInputCount.store(0, std::memory_order_relaxed);
    g_printRecords = {};
    g_convertInputs = {};
    for (char &value : g_holeReportName)
        value = 0;
    g_mutateNameOnAssert = nullptr;
    g_mutateNameCapacity = 0;
    g_reuseSidecarOnAssert = false;
    g_holeReentryTailName = nullptr;
    g_holeReentryReuseName = nullptr;
    g_dumpMutationHook.store(false, std::memory_order_relaxed);
    g_dumpMutationOldName = nullptr;
    g_dumpMutationNewName = nullptr;
    CHECK(g_lockDepth == 0);
}

void ResetDumpRecords()
{
    g_printReports.store(0, std::memory_order_relaxed);
    g_printRecordCount.store(0, std::memory_order_relaxed);
    g_convertInputCount.store(0, std::memory_order_relaxed);
    g_printRecords = {};
    g_convertInputs = {};
}

std::uint8_t *ExpectedBase()
{
    return g_backing.data()
        + g_backingOffset.load(std::memory_order_relaxed);
}

void *ExpectedReservation()
{
    const std::uintptr_t overrideAddress =
        g_reservationOverride.load(std::memory_order_relaxed);
    return overrideAddress
        ? reinterpret_cast<void *>(overrideAddress)
        : static_cast<void *>(ExpectedBase());
}

void InitializeReady(const std::uint32_t offset = 0)
{
    ResetRuntime();
    g_backingOffset.store(offset, std::memory_order_relaxed);
    CHECK(pmem_runtime::TryInitialize() == InitializationStatus::Success);
    const StateAccess::Snapshot state = StateAccess::Capture();
    CHECK(state.memory.buf == ExpectedBase());
    CHECK(state.retainedBase == ExpectedBase());
    CHECK(state.retainedSize == kRuntimeSize);
    CHECK(state.initializationPhase
        == static_cast<std::uint8_t>(InitializationPhase::Ready));
    CHECK(state.runtimeReserved[0] == 0);
    CHECK(state.runtimeReserved[1] == 0);
    CHECK(state.runtimeReserved[2] == 0);
}

void BeginBothScopes(const char *const lowName, const char *const highName)
{
    PMem_BeginAlloc(lowName, 0);
    PMem_BeginAlloc(highName, 1);
    CHECK(g_assertReports.load(std::memory_order_relaxed) == 0);
}

void TestResultLayoutAndDefaults()
{
    static_assert(std::is_standard_layout_v<AllocationResult>);
    static_assert(std::is_trivially_copyable_v<AllocationResult>);
    static_assert(sizeof(AllocationStatus) == 1);
    static_assert(sizeof(InitializationPhase) == 1);
    static_assert(sizeof(InitializationStatus) == 1);
    static_assert(sizeof(ProcessInitAllocationStatus) == 1);
    static_assert(noexcept(pmem_runtime::TryBeginProcessInitAllocation()));
    static_assert(noexcept(pmem_runtime::TryEndProcessInitAllocation()));
#if UINTPTR_MAX == UINT32_MAX
    CHECK(sizeof(AllocationResult) == 0x10);
    CHECK(offsetof(AllocationResult, status) == 0xC);
#else
    CHECK(sizeof(AllocationResult) == 0x18);
    CHECK(offsetof(AllocationResult, status) == 0x10);
#endif
    const AllocationResult result{};
    CheckResult(result, AllocationStatus::InvalidRequest, 0, nullptr);

    static_assert(std::is_standard_layout_v<pmem_runtime::DiagnosticEntry>);
    static_assert(std::is_trivially_copyable_v<pmem_runtime::DiagnosticEntry>);
    static_assert(std::is_standard_layout_v<DiagnosticSnapshot>);
    static_assert(std::is_trivially_copyable_v<DiagnosticSnapshot>);
    static_assert(sizeof(DiagnosticEntryKind) == 1);
    static_assert(sizeof(DiagnosticSnapshotStatus) == 1);
    static_assert(sizeof(pmem_runtime::DiagnosticEntry) == 0x18);
    static_assert(offsetof(pmem_runtime::DiagnosticEntry, bytes) == 0x0);
    static_assert(offsetof(pmem_runtime::DiagnosticEntry, name) == 0x4);
    static_assert(offsetof(pmem_runtime::DiagnosticEntry, kind) == 0x17);
    static_assert(sizeof(DiagnosticSnapshot) == 0x610);
    static_assert(offsetof(DiagnosticSnapshot, high) == 0x0);
    static_assert(offsetof(DiagnosticSnapshot, low) == 0x300);
    static_assert(offsetof(DiagnosticSnapshot, highCount) == 0x600);
    static_assert(offsetof(DiagnosticSnapshot, lowCount) == 0x604);
    static_assert(offsetof(DiagnosticSnapshot, freeBytes) == 0x608);
    static_assert(offsetof(DiagnosticSnapshot, status) == 0x60C);
    static_assert(offsetof(DiagnosticSnapshot, reserved) == 0x60D);
    static_assert(noexcept(pmem_runtime::TryCaptureDiagnosticSnapshot()));
    const DiagnosticSnapshot diagnostic{};
    CHECK(diagnostic.status == DiagnosticSnapshotStatus::NotReady);
    CheckDiagnosticPayloadIsZero(diagnostic);
    const StateAccess::OwnedNameBindingSnapshot noOwnedBinding{};
    CHECK(noOwnedBinding.type == UINT8_MAX);
    CHECK(noOwnedBinding.index == UINT8_MAX);
    CheckProcessInitState(
        StateAccess::Capture(), kProcessInitDormant, 0);
}

void TestInitFailuresRetryAndDoubleInit()
{
    ResetRuntime();
    g_reserveSucceeds.store(false, std::memory_order_relaxed);
    CHECK(pmem_runtime::TryInitialize()
        == InitializationStatus::ReserveFailed);
    CHECK(g_reserveCalls.load() == 1);
    CHECK(g_commitCalls.load() == 0);
    CHECK(g_releaseCalls.load() == 0);
    StateAccess::Snapshot state = StateAccess::Capture();
    CHECK(IsPristineMemory(state.memory));
    CHECK(state.initializationPhase
        == static_cast<std::uint8_t>(InitializationPhase::Uninitialized));
    CHECK(state.retainedBase == nullptr);
    CHECK(state.retainedSize == 0);

    g_reserveSucceeds.store(true, std::memory_order_relaxed);
    CHECK(pmem_runtime::TryInitialize() == InitializationStatus::Success);
    CHECK(pmem_runtime::TryInitialize()
        == InitializationStatus::AlreadyInitialized);
    CHECK(g_reserveCalls.load() == 2);
    CHECK(g_commitCalls.load() == 1);
    CHECK(g_releaseCalls.load() == 0);

    ResetRuntime();
    g_commitSucceeds.store(false, std::memory_order_relaxed);
    CHECK(pmem_runtime::TryInitialize()
        == InitializationStatus::CommitFailed);
    CHECK(g_reserveCalls.load() == 1);
    CHECK(g_commitCalls.load() == 1);
    CHECK(g_releaseCalls.load() == 1);
    state = StateAccess::Capture();
    CHECK(IsPristineMemory(state.memory));
    CHECK(state.initializationPhase
        == static_cast<std::uint8_t>(InitializationPhase::Uninitialized));
    CHECK(state.retainedBase == nullptr);
    CHECK(state.retainedSize == 0);

    g_commitSucceeds.store(true, std::memory_order_relaxed);
    CHECK(pmem_runtime::TryInitialize() == InitializationStatus::Success);

    ResetRuntime();
    g_reservationOverride.store(
        std::numeric_limits<std::uintptr_t>::max() - 16u,
        std::memory_order_relaxed);
    CHECK(pmem_runtime::TryInitialize()
        == InitializationStatus::CorruptState);
    CHECK(g_commitCalls.load() == 0);
    CHECK(g_releaseCalls.load() == 1);
    state = StateAccess::Capture();
    CHECK(IsPristineMemory(state.memory));
    CHECK(state.initializationPhase
        == static_cast<std::uint8_t>(InitializationPhase::Uninitialized));
    CHECK(state.retainedBase == nullptr);
    g_reservationOverride.store(0, std::memory_order_relaxed);
    CHECK(pmem_runtime::TryInitialize() == InitializationStatus::Success);

    ResetRuntime();
    g_commitSucceeds.store(false, std::memory_order_relaxed);
    g_releaseSucceeds.store(false, std::memory_order_relaxed);
    CHECK(pmem_runtime::TryInitialize()
        == InitializationStatus::ReleaseFailed);
    state = StateAccess::Capture();
    CHECK(IsPristineMemory(state.memory));
    CHECK(state.initializationPhase
        == static_cast<std::uint8_t>(InitializationPhase::Poisoned));
    CHECK(state.retainedBase == ExpectedBase());
    CHECK(state.retainedSize == kRuntimeSize);
    CHECK(pmem_runtime::TryInitialize() == InitializationStatus::Poisoned);
    CHECK(g_reserveCalls.load() == 1);
}

void TestConcurrentAndReentrantInit()
{
    ResetRuntime();
    {
        std::lock_guard<std::mutex> lock(g_blockMutex);
        g_blockReserve = true;
    }
    g_reserveHook.store(HookAction::Reenter, std::memory_order_relaxed);
    std::atomic<InitializationStatus> background{
        InitializationStatus::CorruptState};
    std::thread initializer([&background]() {
        background.store(
            pmem_runtime::TryInitialize(), std::memory_order_relaxed);
    });
    {
        std::unique_lock<std::mutex> lock(g_blockMutex);
        g_blockCondition.wait(lock, []() { return g_reserveEntered; });
    }
    CHECK(pmem_runtime::TryInitialize() == InitializationStatus::Busy);
    CHECK(PMem_GetFreeAmount() == 0);
    {
        std::lock_guard<std::mutex> lock(g_blockMutex);
        g_releaseReserve = true;
    }
    g_blockCondition.notify_all();
    initializer.join();
    CHECK(background.load() == InitializationStatus::Success);
    CHECK(g_hookBusyResults.load() == 1);
    CHECK(g_hookNotReadyResults.load() == 1);
    CHECK(g_hookLocalReservationResults.load() == 1);
    CHECK(g_reentryViolations.load() == 0);
    CHECK(g_reportLockViolations.load() == 0);

    ResetRuntime();
    g_commitHook.store(HookAction::Reenter, std::memory_order_relaxed);
    CHECK(pmem_runtime::TryInitialize() == InitializationStatus::Success);
    CHECK(g_hookBusyResults.load() == 1);
    CHECK(g_hookNotReadyResults.load() == 1);
    CHECK(g_hookLocalReservationResults.load() == 1);

    ResetRuntime();
    g_commitSucceeds.store(false, std::memory_order_relaxed);
    g_releaseHook.store(HookAction::Reenter, std::memory_order_relaxed);
    CHECK(pmem_runtime::TryInitialize()
        == InitializationStatus::CommitFailed);
    CHECK(g_hookBusyResults.load() == 1);
    CHECK(g_hookNotReadyResults.load() == 1);
    CHECK(g_hookLocalReservationResults.load() == 1);
}

void CheckCanonicalPoisoned()
{
    const StateAccess::Snapshot state = StateAccess::Capture();
    CHECK(IsPristineMemory(state.memory));
    CHECK(state.initializationPhase
        == static_cast<std::uint8_t>(InitializationPhase::Poisoned));
    CHECK(state.retainedBase == ExpectedBase());
    CHECK(state.retainedSize == kRuntimeSize);
    CHECK(state.runtimeReserved[0] == 0);
    CHECK(state.runtimeReserved[1] == 0);
    CHECK(state.runtimeReserved[2] == 0);
}

void TestInitCorruptionOwnershipExits()
{
    ResetRuntime();
    g_reserveHook.store(
        HookAction::CorruptReserved, std::memory_order_relaxed);
    CHECK(pmem_runtime::TryInitialize()
        == InitializationStatus::CorruptState);
    CHECK(g_commitCalls.load() == 0);
    CHECK(g_releaseCalls.load() == 1);

    ResetRuntime();
    g_reserveHook.store(
        HookAction::CorruptReserved, std::memory_order_relaxed);
    g_releaseSucceeds.store(false, std::memory_order_relaxed);
    CHECK(pmem_runtime::TryInitialize()
        == InitializationStatus::ReleaseFailed);
    CheckCanonicalPoisoned();

    ResetRuntime();
    g_commitSucceeds.store(false, std::memory_order_relaxed);
    g_commitHook.store(
        HookAction::CorruptReserved, std::memory_order_relaxed);
    CHECK(pmem_runtime::TryInitialize()
        == InitializationStatus::CorruptState);
    CHECK(g_releaseCalls.load() == 1);

    ResetRuntime();
    g_commitSucceeds.store(false, std::memory_order_relaxed);
    g_commitHook.store(
        HookAction::CorruptReserved, std::memory_order_relaxed);
    g_releaseSucceeds.store(false, std::memory_order_relaxed);
    CHECK(pmem_runtime::TryInitialize()
        == InitializationStatus::ReleaseFailed);
    CheckCanonicalPoisoned();

    ResetRuntime();
    g_commitHook.store(
        HookAction::CorruptReserved, std::memory_order_relaxed);
    CHECK(pmem_runtime::TryInitialize()
        == InitializationStatus::CorruptState);
    CHECK(g_releaseCalls.load() == 1);

    ResetRuntime();
    g_commitHook.store(
        HookAction::CorruptReserved, std::memory_order_relaxed);
    g_releaseSucceeds.store(false, std::memory_order_relaxed);
    CHECK(pmem_runtime::TryInitialize()
        == InitializationStatus::ReleaseFailed);
    CheckCanonicalPoisoned();

    ResetRuntime();
    g_commitSucceeds.store(false, std::memory_order_relaxed);
    g_releaseHook.store(
        HookAction::CorruptReserved, std::memory_order_relaxed);
    CHECK(pmem_runtime::TryInitialize()
        == InitializationStatus::CorruptState);

    ResetRuntime();
    g_commitSucceeds.store(false, std::memory_order_relaxed);
    g_releaseSucceeds.store(false, std::memory_order_relaxed);
    g_releaseHook.store(
        HookAction::CorruptReserved, std::memory_order_relaxed);
    CHECK(pmem_runtime::TryInitialize()
        == InitializationStatus::ReleaseFailed);
    CheckCanonicalPoisoned();
    CHECK(g_reportLockViolations.load() == 0);
}

void TestInitPhysicalMemoryFailureAtomicity()
{
    ResetRuntime();
    std::array<std::uint8_t, 32> bytes{};
    PhysicalMemory memory{};
    memory.buf = bytes.data() + 1;
    memory.prim[0].pos = 7;
    memory.prim[1].pos = 19;
    const PhysicalMemory before = memory;

    PMem_InitPhysicalMemory(
        nullptr, bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    CHECK(g_assertReports.load() == 1);
    CHECK(SameMemory(memory, before));
    PMem_InitPhysicalMemory(
        &memory, nullptr, static_cast<std::uint32_t>(bytes.size()));
    CHECK(g_assertReports.load() == 2);
    CHECK(SameMemory(memory, before));
    PMem_InitPhysicalMemory(&memory, bytes.data(), 0);
    CHECK(g_assertReports.load() == 3);
    CHECK(SameMemory(memory, before));
    CHECK(g_reportLockViolations.load() == 0);

    PMem_InitPhysicalMemory(
        &memory, bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    CHECK(memory.buf == bytes.data());
    CHECK(memory.prim[0].pos == 0);
    CHECK(memory.prim[1].pos == bytes.size());
}

void TestAllocationStatusesAndAtomicity()
{
    ResetRuntime();
    CheckResult(
        pmem_runtime::TryAllocate(1, 1, 0, 0),
        AllocationStatus::NotReady, 0, nullptr);
    for (const AllocationResult result : {
             pmem_runtime::TryAllocate(0, 1, 0, 0),
             pmem_runtime::TryAllocate(1, 0, 0, 0),
             pmem_runtime::TryAllocate(1, 3, 0, 0),
             pmem_runtime::TryAllocate(1, 1, 0, 2),
             pmem_runtime::TryAllocate(1, 1, 0, UINT32_MAX),
         })
    {
        CheckResult(result, AllocationStatus::InvalidRequest, 0, nullptr);
    }

    InitializeReady();
    const StateAccess::Snapshot before = StateAccess::Capture();
    CheckResult(
        pmem_runtime::TryAllocate(4, 4, 0, 0),
        AllocationStatus::ScopeInactive, 0, nullptr);
    CHECK(SameSnapshot(StateAccess::Capture(), before));

    static char lowName[] = "allocation-low";
    PMem_BeginAlloc(lowName, 0);
    const StateAccess::Snapshot begun = StateAccess::Capture();
    const AllocationResult low = pmem_runtime::TryAllocate(5, 8, 0, 0);
    CheckResult(low, AllocationStatus::Success, 0, ExpectedBase());
    CHECK(reinterpret_cast<std::uintptr_t>(low.address) % 8u == 0);
    const StateAccess::Snapshot allocated = StateAccess::Capture();
    CHECK(allocated.memory.prim[0].pos == 5);

    const AllocationResult exhausted = pmem_runtime::TryAllocate(
        kRuntimeSize, 1, 0, 0);
    CheckResult(exhausted, AllocationStatus::Exhausted, 5, nullptr);
    const StateAccess::Snapshot afterFailure = StateAccess::Capture();
    CHECK(SameMemory(afterFailure.memory, allocated.memory));
    CHECK(afterFailure.overAllocatedSize == begun.overAllocatedSize);
}

void TestAbsoluteAlignmentAndExactHighShortfall()
{
    static char lowName[] = "align-low";
    static char highName[] = "align-high";
    InitializeReady(3);
    BeginBothScopes(lowName, highName);
    const AllocationResult low = pmem_runtime::TryAllocate(7, 16, 0, 0);
    CHECK(low.status == AllocationStatus::Success);
    CHECK(reinterpret_cast<std::uintptr_t>(low.address) % 16u == 0);
    const AllocationResult high = pmem_runtime::TryAllocate(9, 32, 0, 1);
    CHECK(high.status == AllocationStatus::Success);
    CHECK(reinterpret_cast<std::uintptr_t>(high.address) % 32u == 0);

    InitializeReady(11);
    BeginBothScopes(lowName, highName);
    CHECK(pmem_runtime::TryAllocate(kRuntimeSize - 8u, 1, 0, 1).status
        == AllocationStatus::Success);
    const StateAccess::Snapshot beforeOne = StateAccess::Capture();
    const AllocationResult one = pmem_runtime::TryAllocate(4, 16, 0, 1);
    CheckResult(one, AllocationStatus::Exhausted, 1, nullptr);
    CHECK(SameSnapshot(StateAccess::Capture(), beforeOne));

    InitializeReady(0);
    BeginBothScopes(lowName, highName);
    CHECK(pmem_runtime::TryAllocate(5, 1, 0, 0).status
        == AllocationStatus::Success);
    CHECK(pmem_runtime::TryAllocate(kRuntimeSize - 8u, 1, 0, 1).status
        == AllocationStatus::Success);
    const StateAccess::Snapshot beforeFour = StateAccess::Capture();
    const AllocationResult four = pmem_runtime::TryAllocate(4, 4, 0, 1);
    CheckResult(four, AllocationStatus::Exhausted, 4, nullptr);
    CHECK(SameSnapshot(StateAccess::Capture(), beforeFour));
}

void TestContention()
{
    static char lowName[] = "contended-low";
    static char highName[] = "contended-high";
    InitializeReady();
    BeginBothScopes(lowName, highName);
    constexpr int kThreadCount = 8;
    constexpr int kAllocationsPerThread = 512;
    std::array<std::vector<std::uintptr_t>, kThreadCount> addresses;
    std::array<std::thread, kThreadCount> threads;
    for (int threadIndex = 0; threadIndex < kThreadCount; ++threadIndex)
    {
        addresses[threadIndex].reserve(kAllocationsPerThread);
        threads[threadIndex] = std::thread([threadIndex, &addresses]() {
            const std::uint32_t type = static_cast<std::uint32_t>(
                threadIndex & 1);
            for (int allocation = 0;
                 allocation < kAllocationsPerThread;
                 ++allocation)
            {
                const AllocationResult result =
                    pmem_runtime::TryAllocate(8, 8, 0, type);
                CHECK(result.status == AllocationStatus::Success);
                CHECK(result.additionalBytes == 0);
                CHECK(result.reserved[0] == 0);
                addresses[threadIndex].push_back(
                    reinterpret_cast<std::uintptr_t>(result.address));
            }
        });
    }
    for (std::thread &thread : threads)
        thread.join();

    std::vector<std::uintptr_t> all;
    all.reserve(kThreadCount * kAllocationsPerThread);
    for (const auto &threadAddresses : addresses)
        all.insert(all.end(), threadAddresses.begin(), threadAddresses.end());
    std::sort(all.begin(), all.end());
    CHECK(std::adjacent_find(all.begin(), all.end()) == all.end());
    for (const std::uintptr_t address : all)
        CHECK(address % 8u == 0);
    CHECK(PMem_GetFreeAmount()
        == kRuntimeSize
            - kThreadCount * kAllocationsPerThread * 8u);
    CHECK(g_reentryViolations.load() == 0);
    CHECK(g_enterCalls.load() == g_leaveCalls.load());
}

void TestLegacyShortfallTlsAndReports()
{
    static char lowName[] = "tls-low";
    InitializeReady();
    PMem_BeginAlloc(lowName, 0);
    CHECK(PMem_TryAlloc(kRuntimeSize + 3u, 1, 0, 0) == nullptr);
    CHECK(PMem_GetOverAllocatedSize() == 3);

    std::array<int, 2> shortfalls{};
    std::thread first([&shortfalls]() {
        CHECK(PMem_TryAlloc(kRuntimeSize + 5u, 1, 0, 0) == nullptr);
        shortfalls[0] = PMem_GetOverAllocatedSize();
    });
    std::thread second([&shortfalls]() {
        CHECK(PMem_TryAlloc(kRuntimeSize + 9u, 1, 0, 0) == nullptr);
        shortfalls[1] = PMem_GetOverAllocatedSize();
    });
    first.join();
    second.join();
    CHECK(shortfalls[0] == 5);
    CHECK(shortfalls[1] == 9);
    CHECK(PMem_GetOverAllocatedSize() == 3);

    const AllocationResult reportFree = pmem_runtime::TryAllocate(
        kRuntimeSize + 20u, 1, 0, 0);
    CheckResult(reportFree, AllocationStatus::Exhausted, 20, nullptr);
    CHECK(PMem_GetOverAllocatedSize() == 3);

    CHECK(PMem_TryAlloc(
        UINT32_MAX, UINT32_C(0x80000000), 0, 0) == nullptr);
    CHECK(PMem_GetOverAllocatedSize() == INT_MAX);

    ResetRuntime();
    CHECK(PMem_Alloc(1, 1, 0, 0) == nullptr);
    CHECK(g_errorReports.load() == 1);
    CHECK(g_reportedShortfall.load() == INT_MAX);
    CHECK(g_reportLockViolations.load() == 0);
    CHECK(g_reportReentries.load() == 1);

    InitializeReady();
    CHECK(PMem_Alloc(1, 1, 0, 0) == nullptr);
    CHECK(g_errorReports.load() == 1);
    CHECK(g_reportedShortfall.load() == INT_MAX);
    CHECK(g_reportLockViolations.load() == 0);

    InitializeReady();
    PMem_BeginAlloc(lowName, 0);
    CHECK(PMem_Alloc(kRuntimeSize + 7u, 1, 0, 0) == nullptr);
    CHECK(g_oomReports.load() == 1);
    CHECK(g_reportedShortfall.load() == 7);
    CHECK(g_reportLockViolations.load() == 0);

    ResetRuntime();
    g_reserveSucceeds.store(false, std::memory_order_relaxed);
    PMem_Init();
    CHECK(g_oomReports.load() == 1);
    CHECK(g_reportLockViolations.load() == 0);
}

void TestLifecycleSerializationAndReportOrdering()
{
    static char first[] = "first";
    static char middle[] = "middle";
    static char last[] = "last";
    static char afterHole[] = "after-hole";
    ResetRuntime();
    PMem_BeginAlloc(first, 0);
    CHECK(g_assertReports.load() == 1);
    CHECK(g_reportLockViolations.load() == 0);
    CHECK(g_reportReentries.load() == 1);

    InitializeReady();
    PMem_BeginAlloc(first, 0);
    CHECK(PMem_TryAlloc(8, 1, 0, 0) == ExpectedBase());
    PMem_EndAlloc(first, 0);
    PMem_BeginAlloc(middle, 0);
    CHECK(PMem_TryAlloc(8, 1, 0, 0) == ExpectedBase() + 8);
    PMem_EndAlloc(middle, 0);
    PMem_BeginAlloc(last, 0);
    CHECK(PMem_TryAlloc(8, 1, 0, 0) == ExpectedBase() + 16);
    PMem_EndAlloc(last, 0);
    PMem_Free(middle, 0);
    CHECK(g_assertReports.load() == 1);
    CHECK(g_reportLockViolations.load() == 0);
    CHECK(g_reportReentries.load() == 1);
    const StateAccess::Snapshot state = StateAccess::Capture();
    CHECK(state.memory.prim[0].allocListCount == 3);
    CHECK(state.memory.prim[0].allocList[1].name == nullptr);
    PMem_BeginAlloc(afterHole, 0);
    CHECK(PMem_TryAlloc(8, 1, 0, 0) == ExpectedBase() + 24);
    PMem_EndAlloc(afterHole, 0);
    CHECK(g_assertReports.load() == 1);

    g_enterCalls.store(0);
    g_leaveCalls.store(0);
    (void)PMem_GetFreeAmount();
    CHECK(g_enterCalls.load() == 1);
    CHECK(g_leaveCalls.load() == 1);
    g_enterCalls.store(0);
    g_leaveCalls.store(0);
    (void)PMem_GetOverAllocatedSize();
    CHECK(g_enterCalls.load() == 1);
    CHECK(g_leaveCalls.load() == 1);
}

void BeginAllocateEnd(
    char *name,
    std::uint32_t allocType,
    std::uint32_t bytes);

void TestDiagnosticStatusAndStableNames()
{
    ResetRuntime();
    const StateAccess::Snapshot pristine = StateAccess::Capture();
    DiagnosticSnapshot diagnostic =
        pmem_runtime::TryCaptureDiagnosticSnapshot();
    CHECK(diagnostic.status == DiagnosticSnapshotStatus::NotReady);
    CheckDiagnosticPayloadIsZero(diagnostic);
    CHECK(SameSnapshot(StateAccess::Capture(), pristine));

    StateAccess::Snapshot corrupt{};
    corrupt.memory.prim[0].pos = 1;
    StateAccess::Install(corrupt);
    const StateAccess::Snapshot corruptBefore = StateAccess::Capture();
    diagnostic = pmem_runtime::TryCaptureDiagnosticSnapshot();
    CHECK(diagnostic.status == DiagnosticSnapshotStatus::CorruptState);
    CheckDiagnosticPayloadIsZero(diagnostic);
    CHECK(SameSnapshot(StateAccess::Capture(), corruptBefore));

    InitializeReady();
    char longName[] = "123456789012345678-zone-%n-owned-name";
    char fullOwnedName[StateAccess::OWNED_NAME_CAPACITY]{};
    CopyText(fullOwnedName, longName);
    char displayName[pmem_runtime::DIAGNOSTIC_NAME_CAPACITY]{};
    CopyText(displayName, longName);
    PMem_BeginAlloc(longName, 0);
    const StateAccess::Snapshot beforeRejectedBegin = StateAccess::Capture();
    PMem_BeginAlloc(
        reinterpret_cast<const char *>(static_cast<std::uintptr_t>(1)), 0);
    CHECK(SameSnapshot(StateAccess::Capture(), beforeRejectedBegin));
    CHECK(PMem_TryAlloc(4, 1, 0, 0) == ExpectedBase());
    PMem_EndAlloc(longName, 0);
    for (char &value : longName)
        value = 'm';

    diagnostic = pmem_runtime::TryCaptureDiagnosticSnapshot();
    CHECK(diagnostic.status == DiagnosticSnapshotStatus::Success);
    CHECK(diagnostic.lowCount == 1);
    CHECK(diagnostic.low[0].kind == DiagnosticEntryKind::Allocation);
    CHECK(std::strcmp(diagnostic.low[0].name, displayName) == 0);
    CHECK(DiagnosticNameIsCanonical(diagnostic.low[0].name));
    StateAccess::Snapshot state = StateAccess::Capture();
    CHECK(std::strcmp(state.ownedNames[0][0].text, fullOwnedName) == 0);
    CHECK(state.ownedNames[0][0].identity
        == reinterpret_cast<std::uintptr_t>(longName));

    PMem_Free(longName, 0);
    char shortName[] = "z";
    PMem_BeginAlloc(shortName, 0);
    CHECK(PMem_TryAlloc(3, 1, 0, 0) == ExpectedBase());
    PMem_EndAlloc(shortName, 0);
    state = StateAccess::Capture();
    CHECK(state.ownedNames[0][0].text[0] == 'z');
    for (std::size_t index = 1;
         index < StateAccess::OWNED_NAME_CAPACITY;
         ++index)
    {
        CHECK(state.ownedNames[0][0].text[index] == 0);
    }

    InitializeReady();
    char emptyName[] = "";
    std::array<char, 19> exactDisplayName{};
    exactDisplayName.fill('e');
    exactDisplayName.back() = '\0';
    std::array<char, StateAccess::OWNED_NAME_CAPACITY> exactOwnedName{};
    exactOwnedName.fill('s');
    exactOwnedName.back() = '\0';
    std::array<char, StateAccess::OWNED_NAME_CAPACITY + 24> oversizedName{};
    oversizedName.fill('l');
    oversizedName.back() = '\0';
    const std::array<char *, 4> boundaryNames{{
        emptyName,
        exactDisplayName.data(),
        exactOwnedName.data(),
        oversizedName.data(),
    }};
    for (char *const name : boundaryNames)
    {
        PMem_BeginAlloc(name, 0);
        PMem_EndAlloc(name, 0);
        state = StateAccess::Capture();
        const std::size_t expectedLength =
            std::strlen(name) < StateAccess::OWNED_NAME_CAPACITY - 1
            ? std::strlen(name)
            : StateAccess::OWNED_NAME_CAPACITY - 1;
        CHECK(std::strncmp(
            state.ownedNames[0][0].text, name, expectedLength) == 0);
        CHECK(state.ownedNames[0][0].text[expectedLength] == '\0');
        for (std::size_t index = expectedLength + 1;
             index < StateAccess::OWNED_NAME_CAPACITY;
             ++index)
        {
            CHECK(state.ownedNames[0][0].text[index] == '\0');
        }
        diagnostic = pmem_runtime::TryCaptureDiagnosticSnapshot();
        CHECK(diagnostic.status == DiagnosticSnapshotStatus::Success);
        CHECK(diagnostic.lowCount == 1);
        const std::size_t expectedDisplayLength =
            expectedLength < pmem_runtime::DIAGNOSTIC_NAME_CAPACITY - 1
            ? expectedLength
            : pmem_runtime::DIAGNOSTIC_NAME_CAPACITY - 1;
        CHECK(std::strncmp(
            diagnostic.low[0].name, name, expectedDisplayLength) == 0);
        CHECK(diagnostic.low[0].name[expectedDisplayLength] == '\0');
        CHECK(DiagnosticNameIsCanonical(diagnostic.low[0].name));
        PMem_Free(name, 0);
    }

    InitializeReady();
    char *expiredName = new char[32];
    std::snprintf(expiredName, 32, "%s", "expired-caller-storage");
    PMem_BeginAlloc(expiredName, 0);
    PMem_EndAlloc(expiredName, 0);
    delete[] expiredName;
    diagnostic = pmem_runtime::TryCaptureDiagnosticSnapshot();
    CHECK(diagnostic.status == DiagnosticSnapshotStatus::Success);
    CHECK(std::strcmp(
        diagnostic.low[0].name, "expired-caller-sto") == 0);
    ResetDumpRecords();
    PMem_DumpMemStats();
    CHECK(std::strcmp(
        g_printRecords[1].name, "expired-caller-sto") == 0);

    InitializeReady();
    char first[] = "first-owned";
    char middle[] = "middle-owned-%n-before-caller-mutation";
    char last[] = "last-owned";
    char expectedHole[StateAccess::OWNED_NAME_CAPACITY]{};
    CopyText(expectedHole, middle);
    const std::array<std::pair<char *, std::uint32_t>, 3> scopes{{
        {first, 2},
        {middle, 3},
        {last, 4},
    }};
    for (const auto &[name, bytes] : scopes)
    {
        PMem_BeginAlloc(name, 0);
        CHECK(PMem_TryAlloc(bytes, 1, 0, 0) != nullptr);
        PMem_EndAlloc(name, 0);
    }
    g_mutateNameOnAssert = middle;
    g_mutateNameCapacity = sizeof(middle);
    PMem_Free(middle, 0);
    CHECK(std::strcmp(g_holeReportName, expectedHole) == 0);
    state = StateAccess::Capture();
    CHECK(state.memory.prim[0].allocList[1].name == nullptr);
    CHECK(state.ownedNames[0][1].identity == 0);
    CHECK(state.ownedNames[0][1].identityWitness == 0);
    for (const char value : state.ownedNames[0][1].text)
        CHECK(value == 0);
    PMem_Free(last, 0);
    state = StateAccess::Capture();
    CHECK(state.memory.prim[0].allocListCount == 1);
    for (std::uint32_t index = 1; index < 3; ++index)
    {
        CHECK(state.ownedNames[0][index].identity == 0);
        CHECK(state.ownedNames[0][index].identityWitness == 0);
        for (const char value : state.ownedNames[0][index].text)
            CHECK(value == 0);
    }

    InitializeReady();
    char reentryFirst[] = "reentry-first";
    char reentryMiddle[] = "reentry-middle-before-reuse";
    char reentryTail[] = "reentry-tail";
    char reentryReuse[] = "reentry-replacement";
    BeginAllocateEnd(reentryFirst, 0, 1);
    BeginAllocateEnd(reentryMiddle, 0, 1);
    BeginAllocateEnd(reentryTail, 0, 1);
    g_reuseSidecarOnAssert = true;
    g_holeReentryTailName = reentryTail;
    g_holeReentryReuseName = reentryReuse;
    PMem_Free(reentryMiddle, 0);
    CHECK(std::strcmp(
        g_holeReportName, "reentry-middle-before-reuse") == 0);
    state = StateAccess::Capture();
    CHECK(state.memory.prim[0].allocListCount == 2);
    CHECK(std::strcmp(state.ownedNames[0][1].text, reentryReuse) == 0);

    InitializeReady();
    char firstIdentity[] = "equal-text";
    char secondIdentity[] = "equal-text";
    PMem_BeginAlloc(firstIdentity, 0);
    const StateAccess::Snapshot beforeWrongEnd = StateAccess::Capture();
    PMem_EndAlloc(secondIdentity, 0);
    CHECK(SameSnapshot(StateAccess::Capture(), beforeWrongEnd));
    PMem_EndAlloc(firstIdentity, 0);
    PMem_BeginAlloc(firstIdentity, 0);
    CHECK(PMem_TryAlloc(1, 1, 0, 0) != nullptr);
    PMem_EndAlloc(firstIdentity, 0);
    PMem_Free(firstIdentity, 0);
    state = StateAccess::Capture();
    CHECK(state.memory.prim[0].allocListCount == 2);
    CHECK(state.memory.prim[0].allocList[0].name == nullptr);
    CHECK(state.memory.prim[0].allocList[1].name == firstIdentity);
    PMem_Free(firstIdentity, 0);
    state = StateAccess::Capture();
    CHECK(state.memory.prim[0].allocListCount == 0);
}

void BeginAllocateEnd(
    char *const name,
    const std::uint32_t allocType,
    const std::uint32_t bytes)
{
    PMem_BeginAlloc(name, allocType);
    CHECK(PMem_TryAlloc(bytes, 1, 0, allocType) != nullptr);
    PMem_EndAlloc(name, allocType);
}

void TestDiagnosticAccountingAndDumpOrder()
{
    InitializeReady();
    char lowNames[3][16]{{"low-four"}, {"low-five"}, {"low-six"}};
    char highNames[3][16]{{"high-four"}, {"high-five"}, {"high-six"}};
    constexpr std::array<std::uint32_t, 3> sizes{4, 5, 6};
    for (std::uint32_t index = 0; index < sizes.size(); ++index)
    {
        BeginAllocateEnd(lowNames[index], 0, sizes[index]);
        BeginAllocateEnd(highNames[index], 1, sizes[index]);
    }
    PMem_Free(lowNames[1], 0);
    PMem_Free(highNames[1], 1);

    const DiagnosticSnapshot snapshot =
        pmem_runtime::TryCaptureDiagnosticSnapshot();
    CHECK(snapshot.status == DiagnosticSnapshotStatus::Success);
    CHECK(snapshot.highCount == 3);
    CHECK(snapshot.lowCount == 3);
    std::uint32_t accounted = snapshot.freeBytes;
    for (std::uint32_t index = 0; index < sizes.size(); ++index)
    {
        CHECK(snapshot.high[index].bytes == sizes[index]);
        CHECK(snapshot.low[index].bytes == sizes[index]);
        accounted += snapshot.high[index].bytes;
        accounted += snapshot.low[index].bytes;
        const DiagnosticEntryKind expectedKind = index == 1
            ? DiagnosticEntryKind::Hole
            : DiagnosticEntryKind::Allocation;
        CHECK(snapshot.high[index].kind == expectedKind);
        CHECK(snapshot.low[index].kind == expectedKind);
        CHECK(std::strcmp(
            snapshot.high[index].name,
            index == 1 ? "<hole>" : highNames[index]) == 0);
        CHECK(std::strcmp(
            snapshot.low[index].name,
            index == 1 ? "<hole>" : lowNames[index]) == 0);
        CHECK(DiagnosticNameIsCanonical(snapshot.high[index].name));
        CHECK(DiagnosticNameIsCanonical(snapshot.low[index].name));
    }
    CHECK(accounted == kRuntimeSize);

    ResetDumpRecords();
    PMem_DumpMemStats();
    CHECK(g_convertInputCount.load() == 7);
    CHECK(g_printRecordCount.load() == 8);
    const std::array<int, 7> expectedConversions{
        4,
        5,
        6,
        static_cast<int>(kRuntimeSize - 30u),
        6,
        5,
        4,
    };
    for (std::size_t index = 0; index < expectedConversions.size(); ++index)
        CHECK(g_convertInputs[index] == expectedConversions[index]);
    CHECK(std::strcmp(g_printRecords[0].name, "high-four") == 0);
    CHECK(std::strcmp(g_printRecords[1].name, "<hole>") == 0);
    CHECK(std::strcmp(g_printRecords[2].name, "high-six") == 0);
    CHECK(std::strcmp(
        g_printRecords[3].format,
        "free physical      %5.1f\n") == 0);
    CHECK(std::strcmp(g_printRecords[4].name, "low-six") == 0);
    CHECK(std::strcmp(g_printRecords[5].name, "<hole>") == 0);
    CHECK(std::strcmp(g_printRecords[6].name, "low-four") == 0);
    CHECK(std::strcmp(
        g_printRecords[7].format,
        "------------------------\n") == 0);

    InitializeReady();
    ResetDumpRecords();
    PMem_DumpMemStats();
    CHECK(g_convertInputCount.load() == 1);
    CHECK(g_convertInputs[0] == static_cast<int>(kRuntimeSize));
    CHECK(g_printRecordCount.load() == 2);
    CHECK(std::strcmp(
        g_printRecords[0].format,
        "free physical      %5.1f\n") == 0);
    CHECK(std::strcmp(
        g_printRecords[1].format,
        "------------------------\n") == 0);
}

void CheckCorruptDiagnosticSnapshot(
    const StateAccess::Snapshot &corrupt)
{
    StateAccess::Install(corrupt);
    const StateAccess::Snapshot before = StateAccess::Capture();
    const DiagnosticSnapshot diagnostic =
        pmem_runtime::TryCaptureDiagnosticSnapshot();
    CHECK(diagnostic.status == DiagnosticSnapshotStatus::CorruptState);
    CheckDiagnosticPayloadIsZero(diagnostic);
    CHECK(SameSnapshot(StateAccess::Capture(), before));
}

void TestDiagnosticCapacityAndSidecarCorruption()
{
    InitializeReady();
    char names[2][MAX_PHYSICAL_ALLOCATIONS][24]{};
    for (std::uint32_t type = 0; type < 2; ++type)
    {
        for (std::uint32_t index = 0;
             index < MAX_PHYSICAL_ALLOCATIONS;
             ++index)
        {
            std::snprintf(
                names[type][index],
                sizeof(names[type][index]),
                "%s-%02u",
                type ? "high" : "low",
                index);
            BeginAllocateEnd(names[type][index], type, 1);
        }
    }
    const DiagnosticSnapshot full =
        pmem_runtime::TryCaptureDiagnosticSnapshot();
    CHECK(full.status == DiagnosticSnapshotStatus::Success);
    CHECK(full.highCount == MAX_PHYSICAL_ALLOCATIONS);
    CHECK(full.lowCount == MAX_PHYSICAL_ALLOCATIONS);
    std::uint32_t accounted = full.freeBytes;
    for (std::uint32_t index = 0;
         index < MAX_PHYSICAL_ALLOCATIONS;
         ++index)
    {
        CHECK(full.high[index].bytes == 1);
        CHECK(full.low[index].bytes == 1);
        CHECK(full.high[index].kind == DiagnosticEntryKind::Allocation);
        CHECK(full.low[index].kind == DiagnosticEntryKind::Allocation);
        accounted += full.high[index].bytes + full.low[index].bytes;
    }
    CHECK(accounted == kRuntimeSize);
    const StateAccess::Snapshot beforeFullBegin = StateAccess::Capture();
    PMem_BeginAlloc(
        reinterpret_cast<const char *>(static_cast<std::uintptr_t>(1)), 0);
    CHECK(SameSnapshot(StateAccess::Capture(), beforeFullBegin));
    ResetDumpRecords();
    PMem_DumpMemStats();
    CHECK(g_convertInputCount.load() == 65);
    CHECK(g_printRecordCount.load() == 66);
    CHECK(std::strcmp(g_printRecords[0].name, "high-00") == 0);
    CHECK(std::strcmp(g_printRecords[31].name, "high-31") == 0);
    CHECK(std::strcmp(
        g_printRecords[32].format,
        "free physical      %5.1f\n") == 0);
    CHECK(std::strcmp(g_printRecords[33].name, "low-31") == 0);
    CHECK(std::strcmp(g_printRecords[64].name, "low-00") == 0);
    CHECK(std::strcmp(
        g_printRecords[65].format,
        "------------------------\n") == 0);

    InitializeReady();
    char scope[] = "sidecar-corrupt";
    BeginAllocateEnd(scope, 0, 8);
    const StateAccess::Snapshot canonical = StateAccess::Capture();

    StateAccess::Snapshot corrupt = canonical;
    corrupt.ownedNames[0][0].identity ^= 1;
    CheckCorruptDiagnosticSnapshot(corrupt);

    corrupt = canonical;
    for (char &value : corrupt.ownedNames[0][0].text)
        value = 'u';
    CheckCorruptDiagnosticSnapshot(corrupt);

    corrupt = canonical;
    corrupt.ownedNames[0][0].text[StateAccess::OWNED_NAME_CAPACITY - 1]
        = 'd';
    CheckCorruptDiagnosticSnapshot(corrupt);

    corrupt = canonical;
    corrupt.memory.prim[0].allocList[0].name = nullptr;
    CheckCorruptDiagnosticSnapshot(corrupt);

    corrupt = canonical;
    corrupt.ownedNames[0][1].identity = 1;
    corrupt.ownedNames[0][1].text[0] = 'x';
    CheckCorruptDiagnosticSnapshot(corrupt);

    corrupt = canonical;
    corrupt.memory.prim[0].allocList[0].name = "wrong-binding";
    CheckCorruptDiagnosticSnapshot(corrupt);

    corrupt = canonical;
    corrupt.memory.prim[0].allocListCount =
        MAX_PHYSICAL_ALLOCATIONS + 1;
    CheckCorruptDiagnosticSnapshot(corrupt);

    InitializeReady();
    char aliasedIdentity[] = "shared-caller-identity";
    BeginAllocateEnd(aliasedIdentity, 0, 1);
    BeginAllocateEnd(aliasedIdentity, 0, 1);
    const StateAccess::Snapshot aliased = StateAccess::Capture();
    CHECK(aliased.ownedNames[0][0].identity
        == aliased.ownedNames[0][1].identity);
    CHECK(aliased.allocationNameBindings[0][0].index == 0);
    CHECK(aliased.allocationNameBindings[0][1].index == 1);
    corrupt = aliased;
    corrupt.allocationNameBindings[0][0] =
        aliased.allocationNameBindings[0][1];
    StateAccess::Install(corrupt);
    const StateAccess::Snapshot rebound = StateAccess::Capture();
    CHECK(rebound.allocationNameBindings[0][0].type == 0);
    CHECK(rebound.allocationNameBindings[0][0].index == 1);
    CheckCorruptDiagnosticSnapshot(corrupt);

    ResetDumpRecords();
    PMem_DumpMemStats();
    CHECK(g_convertInputCount.load() == 0);
    CHECK(g_printRecordCount.load() == 1);
    CHECK(std::strcmp(
        g_printRecords[0].format,
        "physical memory unavailable (corrupt state)\n") == 0);

    ResetRuntime();
    ResetDumpRecords();
    PMem_DumpMemStats();
    CHECK(g_convertInputCount.load() == 0);
    CHECK(g_printRecordCount.load() == 1);
    CHECK(std::strcmp(
        g_printRecords[0].format,
        "physical memory unavailable (not initialized)\n") == 0);
}

void CheckConcurrentDiagnostic(const DiagnosticSnapshot &snapshot)
{
    CHECK(snapshot.status == DiagnosticSnapshotStatus::Success);
    CHECK(snapshot.highCount <= MAX_PHYSICAL_ALLOCATIONS);
    CHECK(snapshot.lowCount <= MAX_PHYSICAL_ALLOCATIONS);
    std::uint32_t accounted = snapshot.freeBytes;
    for (std::uint32_t index = 0; index < snapshot.highCount; ++index)
    {
        const auto &entry = snapshot.high[index];
        CHECK(entry.kind == DiagnosticEntryKind::Allocation);
        CHECK(DiagnosticNameIsCanonical(entry.name));
        accounted += entry.bytes;
    }
    for (std::uint32_t index = 0; index < snapshot.lowCount; ++index)
    {
        const auto &entry = snapshot.low[index];
        CHECK(entry.kind == DiagnosticEntryKind::Allocation);
        CHECK(DiagnosticNameIsCanonical(entry.name));
        accounted += entry.bytes;
    }
    CHECK(accounted == kRuntimeSize);
}

void TestDumpReentryAndSnapshotContention()
{
    InitializeReady();
    static char oldName[] = "outer-old-name";
    static char newName[] = "nested-new-name";
    BeginAllocateEnd(oldName, 0, 5);
    ResetDumpRecords();
    g_dumpMutationOldName = oldName;
    g_dumpMutationNewName = newName;
    g_dumpMutationHook.store(true, std::memory_order_relaxed);
    PMem_DumpMemStats();
    CHECK(g_convertInputCount.load() == 4);
    CHECK(g_convertInputs[0] == static_cast<int>(kRuntimeSize - 5u));
    CHECK(g_convertInputs[1] == static_cast<int>(kRuntimeSize - 7u));
    CHECK(g_convertInputs[2] == 7);
    CHECK(g_convertInputs[3] == 5);
    CHECK(g_printRecordCount.load() == 6);
    CHECK(std::strcmp(g_printRecords[1].name, newName) == 0);
    CHECK(std::strcmp(g_printRecords[4].name, oldName) == 0);
    const DiagnosticSnapshot afterReentry =
        pmem_runtime::TryCaptureDiagnosticSnapshot();
    CHECK(afterReentry.lowCount == 1);
    CHECK(std::strcmp(afterReentry.low[0].name, newName) == 0);
    CHECK(g_reportLockViolations.load() == 0);

    InitializeReady();
    static char lowName[] = "capture-low";
    static char highName[] = "capture-high";
    std::atomic<bool> writerDone{false};
    std::thread writer([&writerDone]() {
        for (int iteration = 0; iteration < 250; ++iteration)
        {
            BeginAllocateEnd(lowName, 0, 1);
            PMem_Free(lowName, 0);
            BeginAllocateEnd(highName, 1, 1);
            PMem_Free(highName, 1);
        }
        writerDone.store(true, std::memory_order_release);
    });
    std::array<std::thread, 4> readers;
    for (std::thread &reader : readers)
    {
        reader = std::thread([&writerDone]() {
            int minimumReads = 250;
            while (!writerDone.load(std::memory_order_acquire)
                   || minimumReads-- > 0)
            {
                CheckConcurrentDiagnostic(
                    pmem_runtime::TryCaptureDiagnosticSnapshot());
            }
        });
    }
    writer.join();
    for (std::thread &reader : readers)
        reader.join();
    CHECK(g_assertReports.load() == 0);
    CHECK(g_reportLockViolations.load() == 0);
    CHECK(g_reentryViolations.load() == 0);
    CHECK(g_enterCalls.load() == g_leaveCalls.load());
}

void TestProcessInitControllerLifecycleAndLegacyCoexistence()
{
    ResetRuntime();
    CHECK(pmem_runtime::TryBeginProcessInitAllocation()
        == ProcessInitAllocationStatus::NotReady);
    CHECK(pmem_runtime::TryEndProcessInitAllocation()
        == ProcessInitAllocationStatus::NotReady);
    CheckProcessInitState(
        StateAccess::Capture(), kProcessInitDormant, 0);

    InitializeReady();
    const StateAccess::Snapshot dormant = StateAccess::Capture();
    CHECK(pmem_runtime::TryEndProcessInitAllocation()
        == ProcessInitAllocationStatus::WrongPhase);
    CHECK(SameSnapshot(StateAccess::Capture(), dormant));

    static char legacyInitName[] = "$init";
    PMem_BeginAlloc(legacyInitName, 1);
    CHECK(PMem_TryAlloc(8, 8, 0, 1) != nullptr);
    PMem_EndAlloc(legacyInitName, 1);
    CHECK(g_assertReports.load() == 0);
    const StateAccess::Snapshot legacyEnded = StateAccess::Capture();
    CHECK(pmem_runtime::TryBeginProcessInitAllocation()
        == ProcessInitAllocationStatus::Busy);
    CHECK(pmem_runtime::TryEndProcessInitAllocation()
        == ProcessInitAllocationStatus::WrongPhase);
    CHECK(SameSnapshot(StateAccess::Capture(), legacyEnded));
    PMem_Free(legacyInitName, 1);
    CHECK(g_assertReports.load() == 0);

    CHECK(pmem_runtime::TryBeginProcessInitAllocation()
        == ProcessInitAllocationStatus::Success);
    StateAccess::Snapshot begun = StateAccess::Capture();
    CheckProcessInitState(
        begun, kProcessInitBegun, kProcessInitBegunWitness);
    const std::uintptr_t processName =
        StateAccess::ProcessInitAllocationNameAddress();
    CHECK(processName != 0);
    CHECK(begun.memory.prim[1].allocListCount == 1);
    CHECK(begun.memory.prim[1].allocName
        == reinterpret_cast<const char *>(processName));
    CHECK(begun.memory.prim[1].allocList[0].name
        == reinterpret_cast<const char *>(processName));
    CHECK(begun.memory.prim[1].allocList[0].pos == kRuntimeSize);
    CHECK(begun.ownedNames[1][0].identity == processName);
    CHECK(std::strcmp(begun.ownedNames[1][0].text, "$init") == 0);
    CHECK(pmem_runtime::TryBeginProcessInitAllocation()
        == ProcessInitAllocationStatus::Busy);
    CHECK(SameSnapshot(StateAccess::Capture(), begun));

    const char *const protectedName =
        reinterpret_cast<const char *>(processName);
    PMem_EndAlloc(protectedName, 1);
    CHECK(g_assertReports.load() == 1);
    CHECK(SameSnapshot(StateAccess::Capture(), begun));
    g_assertReports.store(0, std::memory_order_relaxed);
    PMem_Free(protectedName, 1);
    CHECK(g_assertReports.load() == 1);
    CHECK(SameSnapshot(StateAccess::Capture(), begun));
    g_assertReports.store(0, std::memory_order_relaxed);

    const AllocationResult processAllocation =
        pmem_runtime::TryAllocate(64, 16, 0, 1);
    CHECK(processAllocation.status == AllocationStatus::Success);
    CHECK(pmem_runtime::TryEndProcessInitAllocation()
        == ProcessInitAllocationStatus::Success);
    const StateAccess::Snapshot ended = StateAccess::Capture();
    CheckProcessInitState(
        ended, kProcessInitEnded, kProcessInitEndedWitness);
    CHECK(ended.memory.prim[1].allocListCount == 1);
    CHECK(ended.memory.prim[1].allocName == nullptr);
    CHECK(ended.memory.prim[1].allocList[0].pos == kRuntimeSize);
    CHECK(ended.ownedNames[1][0].identity == processName);
    CHECK(pmem_runtime::TryEndProcessInitAllocation()
        == ProcessInitAllocationStatus::AlreadyComplete);
    CHECK(pmem_runtime::TryBeginProcessInitAllocation()
        == ProcessInitAllocationStatus::AlreadyComplete);
    CHECK(SameSnapshot(StateAccess::Capture(), ended));

    static char zoneOne[] = "post-init-zone-one";
    static char zoneTwo[] = "post-init-zone-two";
    BeginAllocateEnd(zoneOne, 1, 16);
    BeginAllocateEnd(zoneTwo, 1, 16);
    PMem_Free(zoneOne, 1);
    CHECK(g_assertReports.load() == 1);
    g_assertReports.store(0, std::memory_order_relaxed);
    PMem_Free(zoneTwo, 1);
    CHECK(g_assertReports.load() == 0);
    const StateAccess::Snapshot collapsed = StateAccess::Capture();
    CheckProcessInitState(
        collapsed, kProcessInitEnded, kProcessInitEndedWitness);
    CHECK(collapsed.memory.prim[1].allocListCount == 1);
    CHECK(collapsed.memory.prim[1].allocList[0].pos == kRuntimeSize);
    CHECK(collapsed.ownedNames[1][0].identity == processName);

    PMem_Free(protectedName, 1);
    CHECK(g_assertReports.load() == 1);
    CHECK(SameSnapshot(StateAccess::Capture(), collapsed));
    CHECK(g_reportLockViolations.load() == 0);
}

void TestProcessInitControllerCorruptionAndAtomicity()
{
    InitializeReady();
    const StateAccess::Snapshot dormant = StateAccess::Capture();

    auto checkCorrupt = [](const StateAccess::Snapshot &corrupt) {
        StateAccess::Install(corrupt);
        const StateAccess::Snapshot before = StateAccess::Capture();
        CHECK(pmem_runtime::TryBeginProcessInitAllocation()
            == ProcessInitAllocationStatus::CorruptState);
        CHECK(SameSnapshot(StateAccess::Capture(), before));
        CHECK(pmem_runtime::TryEndProcessInitAllocation()
            == ProcessInitAllocationStatus::CorruptState);
        CHECK(SameSnapshot(StateAccess::Capture(), before));
    };

    StateAccess::Snapshot corrupt = dormant;
    corrupt.processInitPhase = UINT8_MAX;
    corrupt.processInitWitness = UINT32_MAX;
    checkCorrupt(corrupt);

    corrupt = dormant;
    corrupt.processInitReserved[1] = UINT8_C(0x7A);
    checkCorrupt(corrupt);

    corrupt = dormant;
    corrupt.processInitPhase = kProcessInitBegun;
    corrupt.processInitWitness = kProcessInitBegunWitness;
    checkCorrupt(corrupt);

    StateAccess::Install(dormant);
    CHECK(pmem_runtime::TryBeginProcessInitAllocation()
        == ProcessInitAllocationStatus::Success);
    const StateAccess::Snapshot begun = StateAccess::Capture();

    corrupt = begun;
    corrupt.processInitWitness ^= UINT32_C(0x100);
    checkCorrupt(corrupt);

    corrupt = begun;
    corrupt.ownedNames[1][0].identity ^=
        static_cast<std::uintptr_t>(0x20);
    checkCorrupt(corrupt);

    corrupt = begun;
    corrupt.ownedNames[1][0].identityWitness ^=
        static_cast<std::uintptr_t>(0x40);
    checkCorrupt(corrupt);

    corrupt = begun;
    corrupt.ownedNames[1][0].text[0] = '!';
    checkCorrupt(corrupt);

    corrupt = begun;
    corrupt.ownedNames[1][0]
        .text[StateAccess::OWNED_NAME_CAPACITY - 1] = 'x';
    checkCorrupt(corrupt);

    corrupt = begun;
    corrupt.allocationNameBindings[1][0] = {};
    checkCorrupt(corrupt);

    corrupt = begun;
    corrupt.allocNameBindings[1] = {};
    checkCorrupt(corrupt);

    corrupt = begun;
    --corrupt.memory.prim[1].allocList[0].pos;
    checkCorrupt(corrupt);

    corrupt = begun;
    corrupt.memory.prim[1].allocListCount = 2;
    checkCorrupt(corrupt);

    StateAccess::Install(begun);
    CHECK(pmem_runtime::TryEndProcessInitAllocation()
        == ProcessInitAllocationStatus::Success);
    const StateAccess::Snapshot ended = StateAccess::Capture();

    corrupt = ended;
    corrupt.processInitPhase = kProcessInitBegun;
    corrupt.processInitWitness = kProcessInitBegunWitness;
    checkCorrupt(corrupt);

    corrupt = ended;
    corrupt.memory.prim[1].allocList[0].name = nullptr;
    corrupt.allocationNameBindings[1][0] = {};
    checkCorrupt(corrupt);

    ResetRuntime();
    g_commitSucceeds.store(false, std::memory_order_relaxed);
    g_releaseSucceeds.store(false, std::memory_order_relaxed);
    CHECK(pmem_runtime::TryInitialize()
        == InitializationStatus::ReleaseFailed);
    const StateAccess::Snapshot poisoned = StateAccess::Capture();
    CheckProcessInitState(poisoned, kProcessInitDormant, 0);
    CHECK(pmem_runtime::TryBeginProcessInitAllocation()
        == ProcessInitAllocationStatus::NotReady);
    CHECK(pmem_runtime::TryEndProcessInitAllocation()
        == ProcessInitAllocationStatus::NotReady);
    CHECK(SameSnapshot(StateAccess::Capture(), poisoned));
    CHECK(g_assertReports.load() == 0);
    CHECK(g_errorReports.load() == 0);
    CHECK(g_oomReports.load() == 0);
    CHECK(g_printReports.load() == 0);
}

void TestProcessInitControllerConcurrencyAndDisjointness()
{
    InitializeReady();
    std::atomic<bool> beginGo{false};
    std::array<ProcessInitAllocationStatus, 2> beginStatuses{
        ProcessInitAllocationStatus::CorruptState,
        ProcessInitAllocationStatus::CorruptState,
    };
    std::array<std::thread, 2> beginThreads;
    for (std::size_t index = 0; index < beginThreads.size(); ++index)
    {
        beginThreads[index] = std::thread([&beginGo, &beginStatuses, index]() {
            while (!beginGo.load(std::memory_order_acquire))
                std::this_thread::yield();
            beginStatuses[index] =
                pmem_runtime::TryBeginProcessInitAllocation();
        });
    }
    beginGo.store(true, std::memory_order_release);
    for (std::thread &thread : beginThreads)
        thread.join();
    std::sort(beginStatuses.begin(), beginStatuses.end());
    CHECK(beginStatuses[0] == ProcessInitAllocationStatus::Success);
    CHECK(beginStatuses[1] == ProcessInitAllocationStatus::Busy);

    static char lowName[] = "concurrent-low-during-init-end";
    PMem_BeginAlloc(lowName, 0);
    std::atomic<bool> operationGo{false};
    std::atomic<int> allocationSuccesses{};
    ProcessInitAllocationStatus endStatus =
        ProcessInitAllocationStatus::CorruptState;
    std::thread allocator([&operationGo, &allocationSuccesses]() {
        while (!operationGo.load(std::memory_order_acquire))
            std::this_thread::yield();
        for (int iteration = 0; iteration < 500; ++iteration)
        {
            if (pmem_runtime::TryAllocate(1, 1, 0, 0).status
                == AllocationStatus::Success)
            {
                allocationSuccesses.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });
    std::thread ender([&operationGo, &endStatus]() {
        while (!operationGo.load(std::memory_order_acquire))
            std::this_thread::yield();
        endStatus = pmem_runtime::TryEndProcessInitAllocation();
    });
    operationGo.store(true, std::memory_order_release);
    allocator.join();
    ender.join();
    CHECK(endStatus == ProcessInitAllocationStatus::Success);
    CHECK(allocationSuccesses.load() == 500);
    PMem_EndAlloc(lowName, 0);
    CHECK(g_assertReports.load() == 0);
    const StateAccess::Snapshot concurrentEnded = StateAccess::Capture();
    CheckProcessInitState(
        concurrentEnded, kProcessInitEnded, kProcessInitEndedWitness);
    CHECK(concurrentEnded.memory.prim[1].allocListCount == 1);
    CHECK(concurrentEnded.memory.prim[1].allocList[0].pos == kRuntimeSize);

    ResetRuntime();
    const std::uintptr_t nameAddress =
        StateAccess::ProcessInitAllocationNameAddress();
    g_reservationOverride.store(nameAddress, std::memory_order_relaxed);
    CHECK(pmem_runtime::TryInitialize()
        == InitializationStatus::CorruptState);
    CHECK(g_commitCalls.load() == 0);
    CHECK(g_releaseCalls.load() == 1);
    const StateAccess::Snapshot rejected = StateAccess::Capture();
    CHECK(IsPristineMemory(rejected.memory));
    CHECK(rejected.retainedBase == nullptr);
    CHECK(rejected.retainedSize == 0);
    CheckProcessInitState(rejected, kProcessInitDormant, 0);
    CHECK(g_reportLockViolations.load() == 0);
    CHECK(g_reentryViolations.load() == 0);
    CHECK(g_enterCalls.load() == g_leaveCalls.load());
}

void TestExtentPhaseAndTopologyCorruption()
{
    static char scope[] = "corruption";
    ResetRuntime();
    StateAccess::Snapshot corrupt{};
    corrupt.memory.prim[0].pos = 1;
    StateAccess::Install(corrupt);
    CHECK(pmem_runtime::TryInitialize()
        == InitializationStatus::CorruptState);
    CheckResult(
        pmem_runtime::TryAllocate(1, 1, 0, 0),
        AllocationStatus::CorruptState, 0, nullptr);

    ResetRuntime();
    {
        std::lock_guard<std::mutex> lock(g_blockMutex);
        g_blockReserve = true;
    }
    std::atomic<InitializationStatus> background{
        InitializationStatus::CorruptState};
    std::thread initializer([&background]() {
        background.store(
            pmem_runtime::TryInitialize(), std::memory_order_relaxed);
    });
    {
        std::unique_lock<std::mutex> lock(g_blockMutex);
        g_blockCondition.wait(lock, []() { return g_reserveEntered; });
    }
    const StateAccess::Snapshot initializing = StateAccess::Capture();
    CheckResult(
        pmem_runtime::TryAllocate(1, 1, 0, 0),
        AllocationStatus::NotReady, 0, nullptr);
    {
        std::lock_guard<std::mutex> lock(g_blockMutex);
        g_releaseReserve = true;
    }
    g_blockCondition.notify_all();
    initializer.join();
    CHECK(background.load() == InitializationStatus::Success);
    corrupt = initializing;
    corrupt.memory.prim[1].pos = 1;
    StateAccess::Install(corrupt);
    CheckResult(
        pmem_runtime::TryAllocate(1, 1, 0, 0),
        AllocationStatus::CorruptState, 0, nullptr);

    ResetRuntime();
    g_commitSucceeds.store(false, std::memory_order_relaxed);
    g_releaseSucceeds.store(false, std::memory_order_relaxed);
    CHECK(pmem_runtime::TryInitialize()
        == InitializationStatus::ReleaseFailed);
    const StateAccess::Snapshot poisoned = StateAccess::Capture();
    CheckResult(
        pmem_runtime::TryAllocate(1, 1, 0, 0),
        AllocationStatus::NotReady, 0, nullptr);
    corrupt = poisoned;
    --corrupt.retainedSize;
    StateAccess::Install(corrupt);
    CheckResult(
        pmem_runtime::TryAllocate(1, 1, 0, 0),
        AllocationStatus::CorruptState, 0, nullptr);
    corrupt = poisoned;
    corrupt.memory.prim[0].pos = 1;
    StateAccess::Install(corrupt);
    CheckResult(
        pmem_runtime::TryAllocate(1, 1, 0, 0),
        AllocationStatus::CorruptState, 0, nullptr);

    InitializeReady();
    const StateAccess::Snapshot ready = StateAccess::Capture();

    corrupt = ready;
    corrupt.initializationWitness ^= UINT32_C(0x10);
    StateAccess::Install(corrupt);
    CHECK(pmem_runtime::TryInitialize()
        == InitializationStatus::CorruptState);
    CheckResult(
        pmem_runtime::TryAllocate(1, 1, 0, 0),
        AllocationStatus::CorruptState, 0, nullptr);

    corrupt = ready;
    corrupt.initializationPhase = UINT8_C(0xFF);
    corrupt.initializationWitness = UINT32_MAX;
    StateAccess::Install(corrupt);
    CHECK(pmem_runtime::TryInitialize()
        == InitializationStatus::CorruptState);

    corrupt = ready;
    corrupt.runtimeReserved[2] = 1;
    StateAccess::Install(corrupt);
    CHECK(pmem_runtime::TryInitialize()
        == InitializationStatus::CorruptState);

    corrupt = ready;
    ++corrupt.retainedBase;
    StateAccess::Install(corrupt);
    CheckResult(
        pmem_runtime::TryAllocate(1, 1, 0, 0),
        AllocationStatus::CorruptState, 0, nullptr);

    corrupt = ready;
    corrupt.retainedSize = kRuntimeSize - 1u;
    StateAccess::Install(corrupt);
    CheckResult(
        pmem_runtime::TryAllocate(1, 1, 0, 0),
        AllocationStatus::CorruptState, 0, nullptr);

    corrupt = ready;
    corrupt.memory.buf = reinterpret_cast<std::uint8_t *>(
        std::numeric_limits<std::uintptr_t>::max() - 16u);
    corrupt.retainedBase = corrupt.memory.buf;
    corrupt.retainedSize = 128;
    corrupt.memory.prim[1].pos = 128;
    StateAccess::Install(corrupt);
    CheckResult(
        pmem_runtime::TryAllocate(1, 1, 0, 0),
        AllocationStatus::CorruptState, 0, nullptr);

    corrupt = ready;
    corrupt.memory.prim[0].pos = 1;
    StateAccess::Install(corrupt);
    CheckResult(
        pmem_runtime::TryAllocate(1, 1, 0, 0),
        AllocationStatus::CorruptState, 0, nullptr);

    corrupt = ready;
    corrupt.memory.prim[0].allocListCount = 1;
    corrupt.memory.prim[0].allocList[0] = {nullptr, 0};
    StateAccess::Install(corrupt);
    CheckResult(
        pmem_runtime::TryAllocate(1, 1, 0, 0),
        AllocationStatus::CorruptState, 0, nullptr);

    corrupt = ready;
    corrupt.memory.prim[0].allocListCount =
        MAX_PHYSICAL_ALLOCATIONS + 1u;
    StateAccess::Install(corrupt);
    CheckResult(
        pmem_runtime::TryAllocate(1, 1, 0, 0),
        AllocationStatus::CorruptState, 0, nullptr);

    StateAccess::Install(ready);
    PMem_BeginAlloc(scope, 0);
    const StateAccess::Snapshot active = StateAccess::Capture();
    corrupt = active;
    corrupt.memory.prim[0].allocList[0].pos = 1;
    StateAccess::Install(corrupt);
    const StateAccess::Snapshot before = StateAccess::Capture();
    CheckResult(
        pmem_runtime::TryAllocate(1, 1, 0, 0),
        AllocationStatus::CorruptState, 0, nullptr);
    CHECK(SameSnapshot(StateAccess::Capture(), before));
}
} // namespace

void MyAssertHandler(
    const char *,
    const int,
    const int,
    const char *format,
    ...)
{
    CheckUnlockedService();
    g_assertReports.fetch_add(1, std::memory_order_relaxed);
    if (std::strcmp(format, "freeing '%s' caused a memory hole\n") == 0)
    {
        if (g_mutateNameOnAssert)
        {
            for (std::size_t index = 0;
                 index < g_mutateNameCapacity;
                 ++index)
            {
                g_mutateNameOnAssert[index] = 'x';
            }
        }
        if (g_reuseSidecarOnAssert)
        {
            g_reuseSidecarOnAssert = false;
            PMem_Free(g_holeReentryTailName, 0);
            PMem_BeginAlloc(g_holeReentryReuseName, 0);
            PMem_EndAlloc(g_holeReentryReuseName, 0);
        }
        va_list arguments;
        va_start(arguments, format);
        const char *const reportedName = va_arg(arguments, const char *);
        CopyText(g_holeReportName, reportedName);
        va_end(arguments);
    }
    g_reportedShortfall.store(
        PMem_GetOverAllocatedSize(), std::memory_order_relaxed);
    g_reportReentries.fetch_add(1, std::memory_order_relaxed);
}

char *KISAK_CDECL va(const char *, ...)
{
    CheckUnlockedService();
    static char empty[] = "";
    return empty;
}

void KISAK_CDECL Com_Printf(
    const int channel,
    const char *format,
    ...)
{
    CheckUnlockedService();
    g_printReports.fetch_add(1, std::memory_order_relaxed);
    CHECK(channel == 16);
    const std::size_t index =
        g_printRecordCount.fetch_add(1, std::memory_order_relaxed);
    if (index >= g_printRecords.size())
        return;
    PrintRecord &record = g_printRecords[index];
    CopyText(record.format, format);
    va_list arguments;
    va_start(arguments, format);
    if (std::strcmp(format, "%-18.18s %5.1f\n") == 0)
    {
        CopyText(record.name, va_arg(arguments, const char *));
        record.value = va_arg(arguments, double);
    }
    else if (std::strcmp(format, "free physical      %5.1f\n") == 0)
    {
        record.value = va_arg(arguments, double);
    }
    va_end(arguments);
}

void KISAK_CDECL Com_Error(const errorParm_t, const char *, ...)
{
    CheckUnlockedService();
    g_errorReports.fetch_add(1, std::memory_order_relaxed);
    g_reportedShortfall.store(
        PMem_GetOverAllocatedSize(), std::memory_order_relaxed);
    g_reportReentries.fetch_add(1, std::memory_order_relaxed);
}

double KISAK_CDECL ConvertToMB(const int bytes)
{
    CheckUnlockedService();
    const std::size_t index =
        g_convertInputCount.fetch_add(1, std::memory_order_relaxed);
    if (index < g_convertInputs.size())
        g_convertInputs[index] = bytes;
    if (g_dumpMutationHook.exchange(false, std::memory_order_relaxed))
    {
        PMem_Free(g_dumpMutationOldName, 0);
        PMem_BeginAlloc(g_dumpMutationNewName, 0);
        CHECK(PMem_TryAlloc(7, 1, 0, 0) == ExpectedBase());
        PMem_EndAlloc(g_dumpMutationNewName, 0);
        PMem_DumpMemStats();
    }
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

void *KISAK_CDECL Sys_VirtualMemoryReserve(const std::size_t size)
{
    CheckUnlockedService();
    CHECK(size == kRuntimeSize);
    g_reserveCalls.fetch_add(1, std::memory_order_relaxed);
    RunHook(g_reserveHook.load(std::memory_order_relaxed));
    {
        std::unique_lock<std::mutex> lock(g_blockMutex);
        if (g_blockReserve)
        {
            g_reserveEntered = true;
            g_blockCondition.notify_all();
            g_blockCondition.wait(lock, []() { return g_releaseReserve; });
        }
    }
    return g_reserveSucceeds.load(std::memory_order_relaxed)
        ? ExpectedReservation()
        : nullptr;
}

bool KISAK_CDECL Sys_VirtualMemoryCommit(
    void *memory,
    const std::size_t size)
{
    CheckUnlockedService();
    CHECK(memory == ExpectedReservation());
    CHECK(size == kRuntimeSize);
    g_commitCalls.fetch_add(1, std::memory_order_relaxed);
    RunHook(g_commitHook.load(std::memory_order_relaxed));
    return g_commitSucceeds.load(std::memory_order_relaxed);
}

bool KISAK_CDECL Sys_VirtualMemoryRelease(void *memory)
{
    CheckUnlockedService();
    CHECK(memory == ExpectedReservation());
    g_releaseCalls.fetch_add(1, std::memory_order_relaxed);
    RunHook(g_releaseHook.load(std::memory_order_relaxed));
    return g_releaseSucceeds.load(std::memory_order_relaxed);
}

void KISAK_CDECL Sys_OutOfMemErrorInternal(const char *, const int)
{
    CheckUnlockedService();
    g_oomReports.fetch_add(1, std::memory_order_relaxed);
    g_reportedShortfall.store(
        PMem_GetOverAllocatedSize(), std::memory_order_relaxed);
    g_reportReentries.fetch_add(1, std::memory_order_relaxed);
}

void KISAK_CDECL Sys_EnterCriticalSection(const int criticalSection)
{
    if (criticalSection != CRITSECT_PHYSICAL_MEMORY)
        g_wrongCriticalSections.fetch_add(1, std::memory_order_relaxed);
    if (g_lockDepth != 0)
        g_reentryViolations.fetch_add(1, std::memory_order_relaxed);
    g_criticalSection.lock();
    ++g_lockDepth;
    g_enterCalls.fetch_add(1, std::memory_order_relaxed);
}

void KISAK_CDECL Sys_LeaveCriticalSection(const int criticalSection)
{
    if (criticalSection != CRITSECT_PHYSICAL_MEMORY)
        g_wrongCriticalSections.fetch_add(1, std::memory_order_relaxed);
    CHECK(g_lockDepth == 1);
    --g_lockDepth;
    g_leaveCalls.fetch_add(1, std::memory_order_relaxed);
    g_criticalSection.unlock();
}

int main()
{
    TestResultLayoutAndDefaults();
    TestInitFailuresRetryAndDoubleInit();
    TestConcurrentAndReentrantInit();
    TestInitCorruptionOwnershipExits();
    TestInitPhysicalMemoryFailureAtomicity();
    TestAllocationStatusesAndAtomicity();
    TestAbsoluteAlignmentAndExactHighShortfall();
    TestContention();
    TestLegacyShortfallTlsAndReports();
    TestLifecycleSerializationAndReportOrdering();
    TestDiagnosticStatusAndStableNames();
    TestDiagnosticAccountingAndDumpOrder();
    TestDiagnosticCapacityAndSidecarCorruption();
    TestDumpReentryAndSnapshotContention();
    TestProcessInitControllerLifecycleAndLegacyCoexistence();
    TestProcessInitControllerCorruptionAndAtomicity();
    TestProcessInitControllerConcurrencyAndDisjointness();
    TestExtentPhaseAndTopologyCorruption();
    CHECK(g_lockDepth == 0);
    CHECK(g_wrongCriticalSections.load() == 0);
    CHECK(g_reentryViolations.load() == 0);
    CHECK(g_reportLockViolations.load() == 0);
    CHECK(g_enterCalls.load() == g_leaveCalls.load());

    const int failures = g_failures.load(std::memory_order_relaxed);
    if (failures)
        std::fprintf(stderr, "%d physical-memory runtime test(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
