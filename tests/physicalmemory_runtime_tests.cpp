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
#include <vector>

namespace
{
using StateAccess = PhysicalMemoryGlobalStateTestAccess;
using pmem_runtime::AllocationResult;
using pmem_runtime::AllocationStatus;
using pmem_runtime::InitializationPhase;
using pmem_runtime::InitializationStatus;

constexpr std::uint32_t kRuntimeSize = UINT32_C(0x08000000);
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
    return SameMemory(left.memory, right.memory)
        && left.overAllocatedSize == right.overAllocatedSize
        && left.retainedBase == right.retainedBase
        && left.retainedSize == right.retainedSize
        && left.initializationPhase == right.initializationPhase
        && left.runtimeReserved[0] == right.runtimeReserved[0]
        && left.runtimeReserved[1] == right.runtimeReserved[1]
        && left.runtimeReserved[2] == right.runtimeReserved[2]
        && left.initializationWitness == right.initializationWitness;
}

bool IsPristineMemory(const PhysicalMemory &memory)
{
    PhysicalMemory pristine{};
    return SameMemory(memory, pristine);
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
    CHECK(g_lockDepth == 0);
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
#if UINTPTR_MAX == UINT32_MAX
    CHECK(sizeof(AllocationResult) == 0x10);
    CHECK(offsetof(AllocationResult, status) == 0xC);
#else
    CHECK(sizeof(AllocationResult) == 0x18);
    CHECK(offsetof(AllocationResult, status) == 0x10);
#endif
    const AllocationResult result{};
    CheckResult(result, AllocationStatus::InvalidRequest, 0, nullptr);
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

    PMem_InitPhysicalMemory(nullptr, bytes.data(), bytes.size());
    CHECK(g_assertReports.load() == 1);
    CHECK(SameMemory(memory, before));
    PMem_InitPhysicalMemory(&memory, nullptr, bytes.size());
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
    const char *,
    ...)
{
    CheckUnlockedService();
    g_assertReports.fetch_add(1, std::memory_order_relaxed);
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

void KISAK_CDECL Com_Printf(const int, const char *, ...)
{
    CheckUnlockedService();
    g_printReports.fetch_add(1, std::memory_order_relaxed);
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
    void *const memory,
    const std::size_t size)
{
    CheckUnlockedService();
    CHECK(memory == ExpectedReservation());
    CHECK(size == kRuntimeSize);
    g_commitCalls.fetch_add(1, std::memory_order_relaxed);
    RunHook(g_commitHook.load(std::memory_order_relaxed));
    return g_commitSucceeds.load(std::memory_order_relaxed);
}

bool KISAK_CDECL Sys_VirtualMemoryRelease(void *const memory)
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
