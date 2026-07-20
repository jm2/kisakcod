#include <database/db_zone_runtime_facade.h>
#include <database/db_zone_memory.h>
#include <qcommon/sys_sync.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <thread>

namespace
{
using db::registry_ownership::RegistryOwnershipBulkResult;
using db::registry_ownership::RegistryOwnershipName;
using db::registry_ownership::RegistryOwnershipStatus;
using db::zone_load::ZoneLoadContextKey;
using db::zone_runtime::ZoneRuntimeFacade;
using db::zone_runtime::ZoneRuntimeFacadeStatus;
using db::zone_runtime::ZoneRuntimeFacadeTestAccess;
using db::zone_runtime::ZoneRuntimeTableStatus;

enum class LookupPublication : std::uint8_t
{
    Canonical,
    Empty,
    WrongKey,
    WrongEntry,
};

std::atomic<bool> g_runtimeLocked{false};
ZoneRuntimeTableStatus g_releaseStatus = ZoneRuntimeTableStatus::Success;
ZoneRuntimeTableStatus g_tableOperationStatus =
    ZoneRuntimeTableStatus::Success;
ZoneRuntimeTableStatus g_tableAuthenticationStatus =
    ZoneRuntimeTableStatus::Success;
RegistryOwnershipStatus g_registryOperationStatus =
    RegistryOwnershipStatus::Success;
bool g_registryActive = false;
bool g_registryUnsafe = false;
std::uint32_t g_initializeCalls = 0;
std::uint32_t g_claimCalls = 0;
std::uint32_t g_lookupCalls = 0;
std::uint32_t g_compositeCalls = 0;
std::uint32_t g_registryBeginCalls = 0;
std::uint32_t g_registryBorrowCalls = 0;
std::uint32_t g_registryFinishCalls = 0;
std::uint32_t g_registryAddUserCalls = 0;
std::uint32_t g_registryBulkAddCalls = 0;
std::uint32_t g_registryInternCalls = 0;
std::uint32_t g_registryScalarReAddCalls = 0;
std::uint32_t g_registryBulkReAddCalls = 0;
std::uint32_t g_registryTransferCalls = 0;
std::uint32_t g_registryShutdownCalls = 0;
std::uint32_t g_borrowSlot = 0;
ZoneLoadContextKey g_borrowKey{};
std::uint32_t g_lastStringId = 0;
LookupPublication g_lookupPublication = LookupPublication::Canonical;
bool g_corruptRuntimeAfterTableOperation = false;
bool g_generationBindingPristine = false;
std::uint32_t g_expectedCompositeSlot = 0;
ZoneLoadContextKey g_expectedCompositeKey{};
db::zone_runtime::ZoneRuntimeGenerationCallbacks g_expectedCallbacks{};
const XZoneMemory *g_expectedZoneIdentity = nullptr;
std::array<db::relocation::BlockView, db::relocation::kBlockCount>
    g_expectedBlocks{};
std::size_t g_expectedBlockCount = 0;
db::zone_pending_copy::PendingCopyDrainCallback g_expectedDrain{};
bool g_callbackArgumentsMatched = false;
bool g_streamArgumentsMatched = false;
bool g_drainArgumentsMatched = false;

db::zone_script_string_ownership::ZoneScriptStringUnpublishStatus
EnsureForwardingUnreachable(void *const context) noexcept
{
    return context
        ? db::zone_script_string_ownership::
            ZoneScriptStringUnpublishStatus::Success
        : db::zone_script_string_ownership::
            ZoneScriptStringUnpublishStatus::UnsafeFailure;
}

db::zone_load::ZoneLoadCleanupCallbackStatus PerformForwardingCleanup(
    void *const context,
    const db::zone_load::ZoneLoadCleanupOperation) noexcept
{
    return context
        ? db::zone_load::ZoneLoadCleanupCallbackStatus::Success
        : db::zone_load::ZoneLoadCleanupCallbackStatus::UnsafeFailure;
}

void CompleteForwardingAdmission(void *) noexcept
{
}

void AdmitForwardingLive(void *) noexcept
{
}

db::zone_pending_copy::PendingCopyDrainCallbackStatus
ConsumeForwardingPendingCopy(
    void *const context,
    const db::zone_pending_copy::PendingCopyRecord) noexcept
{
    return context
        ? db::zone_pending_copy::PendingCopyDrainCallbackStatus::Success
        : db::zone_pending_copy::
            PendingCopyDrainCallbackStatus::UnsafeFailure;
}

[[nodiscard]] bool Check(
    const bool condition,
    const char *const message) noexcept
{
    if (!condition)
        std::fprintf(stderr, "zone runtime facade test failed: %s\n", message);
    return condition;
}

void ResetHarness() noexcept
{
    g_runtimeLocked.store(false, std::memory_order_relaxed);
    ZoneRuntimeFacadeTestAccess::ResetForTesting();
    g_releaseStatus = ZoneRuntimeTableStatus::Success;
    g_tableOperationStatus = ZoneRuntimeTableStatus::Success;
    g_tableAuthenticationStatus = ZoneRuntimeTableStatus::Success;
    g_registryOperationStatus = RegistryOwnershipStatus::Success;
    g_registryActive = false;
    g_registryUnsafe = false;
    g_initializeCalls = 0;
    g_claimCalls = 0;
    g_lookupCalls = 0;
    g_compositeCalls = 0;
    g_registryBeginCalls = 0;
    g_registryBorrowCalls = 0;
    g_registryFinishCalls = 0;
    g_registryAddUserCalls = 0;
    g_registryBulkAddCalls = 0;
    g_registryInternCalls = 0;
    g_registryScalarReAddCalls = 0;
    g_registryBulkReAddCalls = 0;
    g_registryTransferCalls = 0;
    g_registryShutdownCalls = 0;
    g_borrowSlot = 0;
    g_borrowKey = {};
    g_lastStringId = 0;
    g_lookupPublication = LookupPublication::Canonical;
    g_corruptRuntimeAfterTableOperation = false;
    g_generationBindingPristine = false;
    g_expectedCompositeSlot = 0;
    g_expectedCompositeKey = {};
    g_expectedCallbacks = {};
    g_expectedZoneIdentity = nullptr;
    g_expectedBlocks = {};
    g_expectedBlockCount = 0;
    g_expectedDrain = {};
    g_callbackArgumentsMatched = false;
    g_streamArgumentsMatched = false;
    g_drainArgumentsMatched = false;
}

void CorruptRuntimeAfterTableOperationIfRequested() noexcept
{
    if (!g_corruptRuntimeAfterTableOperation)
        return;
    ZoneRuntimeFacadeTestAccess::SetGlobalStateForTesting(
        0, 0, 0, 0, 0, 0, 0, 0);
}

[[nodiscard]] bool TestBasicAccessAndTableForwarding() noexcept
{
    ResetHarness();
    if (!Check(ZoneRuntimeFacade::FinishAccess()
            == ZoneRuntimeFacadeStatus::InvalidState,
            "idle finish did not reject")
        || !Check(!g_runtimeLocked.load(std::memory_order_relaxed),
            "idle finish retained its probe lock")
        || !Check(ZoneRuntimeFacade::TryInitializeRuntimeTable()
            == ZoneRuntimeTableStatus::InvalidState,
            "table operation succeeded without access")
        || !Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "initial access failed")
        || !Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::InvalidState,
            "nested access did not reject")
        || !Check(ZoneRuntimeFacade::TryInitializeRuntimeTable()
            == ZoneRuntimeTableStatus::Success,
            "initialized table wrapper failed")
        || !Check(g_initializeCalls == 1,
            "initialized table wrapper did not forward once"))
    {
        return false;
    }

    ZoneLoadContextKey key{};
    if (!Check(ZoneRuntimeFacade::TryClaimGeneration(7, &key)
            == ZoneRuntimeTableStatus::Success,
            "claim wrapper failed")
        || !Check(g_claimCalls == 1 && key.slot == 7
                && key.generation == 91,
            "claim wrapper changed forwarding semantics")
        || !Check(ZoneRuntimeFacade::FinishAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "access finish failed")
        || !Check(!g_runtimeLocked.load(std::memory_order_relaxed),
            "successful finish retained serializer"))
    {
        return false;
    }
    return true;
}

[[nodiscard]] bool TestForeignContention() noexcept
{
    ResetHarness();
    if (!Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "contention owner begin failed"))
    {
        return false;
    }

    std::atomic<ZoneRuntimeFacadeStatus> beginStatus{
        ZoneRuntimeFacadeStatus::Success};
    std::atomic<ZoneRuntimeFacadeStatus> finishStatus{
        ZoneRuntimeFacadeStatus::Success};
    std::thread contender([&]() {
        beginStatus.store(
            ZoneRuntimeFacade::TryBeginAccess(),
            std::memory_order_release);
        finishStatus.store(
            ZoneRuntimeFacade::FinishAccess(),
            std::memory_order_release);
    });
    contender.join();
    return Check(beginStatus.load(std::memory_order_acquire)
            == ZoneRuntimeFacadeStatus::Busy,
            "foreign begin did not fail nonblocking")
        && Check(finishStatus.load(std::memory_order_acquire)
                == ZoneRuntimeFacadeStatus::Busy,
            "foreign finish could inspect or release owner state")
        && Check(g_runtimeLocked.load(std::memory_order_relaxed),
            "foreign operation released owner serializer")
        && Check(ZoneRuntimeFacade::FinishAccess()
                == ZoneRuntimeFacadeStatus::Success,
            "owner could not finish after contention");
}

[[nodiscard]] bool TestRegistryScopeAndForwarding() noexcept
{
    ResetHarness();
    if (!Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "registry owner begin failed")
        || !Check(ZoneRuntimeFacade::TryBeginStandaloneRegistryOwnership()
            == RegistryOwnershipStatus::Success,
            "standalone registry begin failed")
        || !Check(ZoneRuntimeFacade::FinishAccess()
            == ZoneRuntimeFacadeStatus::InvalidState,
            "runtime finish released active registry scope")
        || !Check(ZoneRuntimeFacade::TryClaimGeneration(3, nullptr)
            == ZoneRuntimeTableStatus::InvalidState,
            "table operation crossed retained registry/hash scope")
        || !Check(g_claimCalls == 0,
            "blocked table operation reached table backend"))
    {
        return false;
    }

    RegistryOwnershipBulkResult bulk{};
    RegistryOwnershipName name{};
    const std::uint32_t ids[]{11, 12};
    const char *const names[]{"a", "b"};
    if (!Check(ZoneRuntimeFacade::TryAddDatabaseUser4(37)
            == RegistryOwnershipStatus::Success,
            "scalar registry wrapper failed")
        || !Check(g_lastStringId == 37,
            "scalar registry wrapper changed argument")
        || !Check(ZoneRuntimeFacade::TryAddDatabaseUsers4(ids, 2, &bulk)
            == RegistryOwnershipStatus::Success,
            "bulk registry wrapper failed")
        || !Check(bulk.addedCount == 2 && bulk.unchangedCount == 5,
            "bulk registry output was not forwarded")
        || !Check(ZoneRuntimeFacade::TryInternBoundedName(
                "name", 5, &name)
            == RegistryOwnershipStatus::Success,
            "name registry wrapper failed")
        || !Check(name.stringId == 71 && name.canonicalName != nullptr,
            "name registry output was not forwarded")
        || !Check(ZoneRuntimeFacade::TryReAddRetainedDefaultName("a")
            == RegistryOwnershipStatus::Success,
            "scalar re-add wrapper failed")
        || !Check(ZoneRuntimeFacade::TryReAddRetainedDefaultNames(
                names, 2, &bulk)
            == RegistryOwnershipStatus::Success,
            "bulk re-add wrapper failed")
        || !Check(ZoneRuntimeFacade::TryTransferDatabaseUsers4To8()
            == RegistryOwnershipStatus::Success,
            "transfer wrapper failed")
        || !Check(ZoneRuntimeFacade::TryShutdownDatabaseUser8()
            == RegistryOwnershipStatus::Success,
            "shutdown wrapper failed")
        || !Check(ZoneRuntimeFacade::FinishRegistryOwnership()
            == RegistryOwnershipStatus::Success,
            "registry finish failed")
        || !Check(g_registryBeginCalls == 1
                && g_registryBorrowCalls == 0
                && g_registryFinishCalls == 1
                && g_registryAddUserCalls == 1
                && g_registryBulkAddCalls == 1
                && g_registryInternCalls == 1
                && g_registryScalarReAddCalls == 1
                && g_registryBulkReAddCalls == 1
                && g_registryTransferCalls == 1
                && g_registryShutdownCalls == 1,
            "registry wrappers did not reach their exact backends once")
        || !Check(ZoneRuntimeFacade::FinishAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "runtime finish failed after registry close"))
    {
        return false;
    }
    return true;
}

[[nodiscard]] bool TestBorrowAndStatusMapping() noexcept
{
    ResetHarness();
    const ZoneLoadContextKey key{UINT64_C(12), 4, 0};
    if (!Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "borrow owner begin failed")
        || !Check(ZoneRuntimeFacade::TryBorrowRegistryOwnership(4, key)
            == RegistryOwnershipStatus::Success,
            "borrow registry wrapper failed")
        || !Check(g_borrowSlot == 4 && g_borrowKey == key,
            "borrow wrapper changed exact generation key")
        || !Check(ZoneRuntimeFacade::FinishRegistryOwnership()
            == RegistryOwnershipStatus::Success,
            "borrowed registry finish failed"))
    {
        return false;
    }

    g_tableOperationStatus = ZoneRuntimeTableStatus::StaleKey;
    if (!Check(ZoneRuntimeFacade::TryBorrowRegistryOwnership(4, key)
            == RegistryOwnershipStatus::InvalidKey,
            "stale table key did not map to invalid registry key")
        || !Check(!g_registryActive,
            "rejected borrow published registry scope")
        || !Check(ZoneRuntimeFacade::FinishAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "borrow mapping owner finish failed"))
    {
        return false;
    }
    return true;
}

[[nodiscard]] bool TestReleaseSafetyAndUnsafePoison() noexcept
{
    ResetHarness();
    if (!Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "release-safety owner begin failed"))
    {
        return false;
    }
    g_releaseStatus = ZoneRuntimeTableStatus::Busy;
    if (!Check(ZoneRuntimeFacade::FinishAccess()
            == ZoneRuntimeFacadeStatus::InvalidState,
            "retained child transaction did not block finish")
        || !Check(g_runtimeLocked.load(std::memory_order_relaxed),
            "blocked finish released serializer"))
    {
        return false;
    }
    g_releaseStatus = ZoneRuntimeTableStatus::Success;
    if (!Check(ZoneRuntimeFacade::FinishAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "release-safety retry failed"))
    {
        return false;
    }

    ResetHarness();
    if (!Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "unsafe owner begin failed"))
    {
        return false;
    }
    g_registryOperationStatus = RegistryOwnershipStatus::UnsafeFailure;
    if (!Check(ZoneRuntimeFacade::TryBeginStandaloneRegistryOwnership()
            == RegistryOwnershipStatus::UnsafeFailure,
            "unsafe child result was not preserved")
        || !Check(ZoneRuntimeFacade::FinishAccess()
            == ZoneRuntimeFacadeStatus::UnsafeFailure,
            "unsafe child did not poison runtime boundary")
        || !Check(g_runtimeLocked.load(std::memory_order_relaxed),
            "unsafe runtime boundary unlocked"))
    {
        return false;
    }
    return true;
}

[[nodiscard]] bool TestCoordinatorValidationUnsafePoison() noexcept
{
    ResetHarness();
    if (!Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "coordinator-validation owner begin failed"))
    {
        return false;
    }
    g_registryUnsafe = true;
    return Check(ZoneRuntimeFacade::FinishAccess()
            == ZoneRuntimeFacadeStatus::UnsafeFailure,
            "unsafe coordinator validation did not poison runtime")
        && Check(g_runtimeLocked.load(std::memory_order_relaxed),
            "unsafe coordinator validation released serializer")
        && Check(ZoneRuntimeFacade::TryBeginAccess()
                == ZoneRuntimeFacadeStatus::UnsafeFailure,
            "poisoned coordinator boundary accepted nested access");
}

[[nodiscard]] bool TestTableUnsafeAndPostAuthenticationPoison() noexcept
{
    ResetHarness();
    if (!Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "unsafe-table owner begin failed"))
    {
        return false;
    }
    g_tableOperationStatus = ZoneRuntimeTableStatus::UnsafeFailure;
    if (!Check(ZoneRuntimeFacade::TryInitializeRuntimeTable()
            == ZoneRuntimeTableStatus::UnsafeFailure,
            "unsafe table result was not preserved")
        || !Check(g_initializeCalls == 1,
            "unsafe table operation did not forward exactly once")
        || !Check(ZoneRuntimeFacade::FinishAccess()
                == ZoneRuntimeFacadeStatus::UnsafeFailure,
            "unsafe table result did not poison runtime")
        || !Check(g_runtimeLocked.load(std::memory_order_relaxed),
            "unsafe table result released serializer"))
    {
        return false;
    }

    ResetHarness();
    if (!Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "post-authentication owner begin failed"))
    {
        return false;
    }
    g_corruptRuntimeAfterTableOperation = true;
    return Check(ZoneRuntimeFacade::TryInitializeRuntimeTable()
            == ZoneRuntimeTableStatus::UnsafeFailure,
            "torn post-authentication boundary was accepted")
        && Check(g_initializeCalls == 1,
            "post-authentication fixture did not forward exactly once")
        && Check(g_runtimeLocked.load(std::memory_order_relaxed),
            "post-authentication failure released serializer");
}

[[nodiscard]] bool TestUnknownTableStatusPoisons() noexcept
{
    constexpr ZoneRuntimeTableStatus unknown =
        static_cast<ZoneRuntimeTableStatus>(0xFF);

    ResetHarness();
    if (!Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "unknown-table owner begin failed"))
    {
        return false;
    }
    g_tableOperationStatus = unknown;
    if (!Check(ZoneRuntimeFacade::TryInitializeRuntimeTable()
            == ZoneRuntimeTableStatus::UnsafeFailure,
            "unknown table result escaped facade")
        || !Check(g_initializeCalls == 1,
            "unknown table result changed exact forwarding")
        || !Check(ZoneRuntimeFacade::FinishAccess()
                == ZoneRuntimeFacadeStatus::UnsafeFailure,
            "unknown table result did not poison runtime")
        || !Check(g_runtimeLocked.load(std::memory_order_relaxed),
            "unknown table result released serializer"))
    {
        return false;
    }

    ResetHarness();
    if (!Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "unknown-lookup owner begin failed"))
    {
        return false;
    }
    g_tableOperationStatus = unknown;
    const ZoneLoadContextKey key{UINT64_C(41), 5, 0};
    return Check(ZoneRuntimeFacade::TryBorrowRegistryOwnership(5, key)
            == RegistryOwnershipStatus::UnsafeFailure,
            "unknown lookup result escaped facade")
        && Check(g_lookupCalls == 1 && g_registryBorrowCalls == 0,
            "unknown lookup result changed exact forwarding")
        && Check(g_borrowSlot == 5 && g_borrowKey == key,
            "unknown lookup changed forwarded key")
        && Check(ZoneRuntimeFacade::FinishAccess()
                == ZoneRuntimeFacadeStatus::UnsafeFailure,
            "unknown lookup result did not poison runtime")
        && Check(g_runtimeLocked.load(std::memory_order_relaxed),
            "unknown lookup result released serializer");
}

[[nodiscard]] bool TestUnknownRegistryStatusPoisons() noexcept
{
    constexpr RegistryOwnershipStatus unknown =
        static_cast<RegistryOwnershipStatus>(0xFF);

    ResetHarness();
    if (!Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "unknown-registry-begin owner begin failed"))
    {
        return false;
    }
    g_registryOperationStatus = unknown;
    if (!Check(ZoneRuntimeFacade::TryBeginStandaloneRegistryOwnership()
            == RegistryOwnershipStatus::UnsafeFailure,
            "unknown registry-begin result escaped facade")
        || !Check(g_registryBeginCalls == 1 && !g_registryActive,
            "unknown registry-begin changed exact forwarding")
        || !Check(ZoneRuntimeFacade::FinishAccess()
                == ZoneRuntimeFacadeStatus::UnsafeFailure,
            "unknown registry-begin result did not poison runtime")
        || !Check(g_runtimeLocked.load(std::memory_order_relaxed),
            "unknown registry-begin result released serializer"))
    {
        return false;
    }

    ResetHarness();
    if (!Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "unknown-registry-operation owner begin failed")
        || !Check(ZoneRuntimeFacade::TryBeginStandaloneRegistryOwnership()
            == RegistryOwnershipStatus::Success,
            "unknown-registry-operation scope begin failed"))
    {
        return false;
    }
    g_registryOperationStatus = unknown;
    return Check(ZoneRuntimeFacade::TryAddDatabaseUser4(89)
            == RegistryOwnershipStatus::UnsafeFailure,
            "unknown registry operation result escaped facade")
        && Check(g_registryBeginCalls == 1
                && g_registryAddUserCalls == 1 && g_lastStringId == 89,
            "unknown registry operation changed exact forwarding")
        && Check(ZoneRuntimeFacade::FinishAccess()
                == ZoneRuntimeFacadeStatus::UnsafeFailure,
            "unknown registry operation did not poison runtime")
        && Check(g_runtimeLocked.load(std::memory_order_relaxed),
            "unknown registry operation released serializer");
}

[[nodiscard]] bool TestImpossibleLookupSuccessPoisons() noexcept
{
    ResetHarness();
    if (!Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "empty-lookup owner begin failed"))
    {
        return false;
    }
    g_lookupPublication = LookupPublication::Empty;
    const ZoneLoadContextKey key{UINT64_C(31), 6, 0};
    if (!Check(ZoneRuntimeFacade::TryBorrowRegistryOwnership(6, key)
            == RegistryOwnershipStatus::UnsafeFailure,
            "successful empty lookup did not fail closed")
        || !Check(g_lookupCalls == 1 && g_registryBorrowCalls == 0,
            "successful empty lookup changed forwarding count")
        || !Check(!g_registryActive,
            "successful empty lookup reached coordinator")
        || !Check(ZoneRuntimeFacade::FinishAccess()
                == ZoneRuntimeFacadeStatus::UnsafeFailure,
            "successful empty lookup did not poison runtime")
        || !Check(g_runtimeLocked.load(std::memory_order_relaxed),
            "successful empty lookup released serializer"))
    {
        return false;
    }

    ResetHarness();
    if (!Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "wrong-key lookup owner begin failed"))
    {
        return false;
    }
    g_lookupPublication = LookupPublication::WrongKey;
    if (!Check(ZoneRuntimeFacade::TryBorrowRegistryOwnership(6, key)
            == RegistryOwnershipStatus::UnsafeFailure,
            "successful wrong-key lookup did not fail closed")
        || !Check(g_lookupCalls == 1 && g_registryBorrowCalls == 0,
            "successful wrong-key lookup changed forwarding count")
        || !Check(ZoneRuntimeFacade::FinishAccess()
                == ZoneRuntimeFacadeStatus::UnsafeFailure,
            "successful wrong-key lookup did not poison runtime")
        || !Check(g_runtimeLocked.load(std::memory_order_relaxed),
            "successful wrong-key lookup released serializer"))
    {
        return false;
    }

    ResetHarness();
    if (!Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "wrong-entry lookup owner begin failed"))
    {
        return false;
    }
    g_lookupPublication = LookupPublication::WrongEntry;
    return Check(ZoneRuntimeFacade::TryBorrowRegistryOwnership(6, key)
            == RegistryOwnershipStatus::UnsafeFailure,
            "successful wrong-entry lookup did not fail closed")
        && Check(g_lookupCalls == 1 && g_registryBorrowCalls == 0,
            "successful wrong-entry lookup changed forwarding count")
        && Check(ZoneRuntimeFacade::FinishAccess()
                == ZoneRuntimeFacadeStatus::UnsafeFailure,
            "successful wrong-entry lookup did not poison runtime")
        && Check(g_runtimeLocked.load(std::memory_order_relaxed),
            "successful wrong-entry lookup released serializer");
}

[[nodiscard]] bool TestMisalignedOutputRejectedUnchanged() noexcept
{
    ResetHarness();
    if (!Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "misaligned-output owner begin failed"))
    {
        return false;
    }

    alignas(ZoneLoadContextKey) unsigned char storage[
        sizeof(ZoneLoadContextKey) + alignof(ZoneLoadContextKey)]{};
    for (unsigned char &byte : storage)
        byte = 0xA5;
    auto *const misaligned = reinterpret_cast<ZoneLoadContextKey *>(
        storage + 1);
    if (!Check(ZoneRuntimeFacade::TryClaimGeneration(9, misaligned)
            == ZoneRuntimeTableStatus::InvalidArgument,
            "misaligned key output was accepted")
        || !Check(g_claimCalls == 0,
            "misaligned key output reached table backend"))
    {
        return false;
    }
    for (const unsigned char byte : storage)
    {
        if (!Check(byte == 0xA5,
                "rejected key output changed caller storage"))
        {
            return false;
        }
    }
    auto *const tableAlias = reinterpret_cast<ZoneLoadContextKey *>(
        &db::zone_runtime::ProductionZoneRuntimeTable());
    if (!Check(ZoneRuntimeFacade::TryClaimGeneration(9, tableAlias)
            == ZoneRuntimeTableStatus::InvalidArgument,
            "table-span key output was accepted")
        || !Check(g_claimCalls == 0,
            "table-span key output reached table backend"))
    {
        return false;
    }
    return Check(ZoneRuntimeFacade::FinishAccess()
            == ZoneRuntimeFacadeStatus::Success,
        "rejected-output owner could not finish");
}

[[nodiscard]] bool TestRegistryOutputAdmissionPrecedesAliasChecks() noexcept
{
    ResetHarness();
    alignas(RegistryOwnershipBulkResult) unsigned char bulkStorage[
        sizeof(RegistryOwnershipBulkResult)
        + alignof(RegistryOwnershipBulkResult)]{};
    alignas(RegistryOwnershipName) unsigned char nameStorage[
        sizeof(RegistryOwnershipName) + alignof(RegistryOwnershipName)]{};
    auto *const misalignedBulk =
        reinterpret_cast<RegistryOwnershipBulkResult *>(bulkStorage + 1);
    auto *const misalignedName =
        reinterpret_cast<RegistryOwnershipName *>(nameStorage + 1);
    const std::uint32_t ids[]{1};
    const char *const names[]{"retained"};

    if (!Check(ZoneRuntimeFacade::TryAddDatabaseUsers4(
                ids, 1, misalignedBulk)
            == RegistryOwnershipStatus::InvalidState,
            "bulk add exposed output validation without runtime access")
        || !Check(ZoneRuntimeFacade::TryInternBoundedName(
                "name", 4, misalignedName)
            == RegistryOwnershipStatus::InvalidState,
            "name intern exposed output validation without runtime access")
        || !Check(ZoneRuntimeFacade::TryReAddRetainedDefaultNames(
                names, 1, misalignedBulk)
            == RegistryOwnershipStatus::InvalidState,
            "bulk re-add exposed output validation without runtime access")
        || !Check(g_registryBulkAddCalls == 0
                && g_registryInternCalls == 0
                && g_registryBulkReAddCalls == 0,
            "rejected no-access output reached registry backend"))
    {
        return false;
    }

    if (!Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "registry output-precedence owner begin failed"))
    {
        return false;
    }
    if (!Check(ZoneRuntimeFacade::TryAddDatabaseUsers4(
                ids, 1, misalignedBulk)
            == RegistryOwnershipStatus::InvalidState,
            "bulk add masked an inactive registry scope")
        || !Check(ZoneRuntimeFacade::TryInternBoundedName(
                "name", 4, misalignedName)
            == RegistryOwnershipStatus::InvalidState,
            "name intern masked an inactive registry scope")
        || !Check(ZoneRuntimeFacade::TryReAddRetainedDefaultNames(
                names, 1, misalignedBulk)
            == RegistryOwnershipStatus::InvalidState,
            "bulk re-add masked an inactive registry scope")
        || !Check(ZoneRuntimeFacade::TryBeginStandaloneRegistryOwnership()
            == RegistryOwnershipStatus::Success,
            "output-precedence registry scope begin failed")
        || !Check(ZoneRuntimeFacade::TryAddDatabaseUsers4(
                ids, 1, misalignedBulk)
            == RegistryOwnershipStatus::InvalidArgument,
            "active bulk add accepted a misaligned output")
        || !Check(ZoneRuntimeFacade::TryInternBoundedName(
                "name", 4, misalignedName)
            == RegistryOwnershipStatus::InvalidArgument,
            "active name intern accepted a misaligned output")
        || !Check(ZoneRuntimeFacade::TryReAddRetainedDefaultNames(
                names, 1, misalignedBulk)
            == RegistryOwnershipStatus::InvalidArgument,
            "active bulk re-add accepted a misaligned output")
        || !Check(ZoneRuntimeFacade::FinishRegistryOwnership()
            == RegistryOwnershipStatus::Success,
            "output-precedence registry scope finish failed")
        || !Check(ZoneRuntimeFacade::FinishAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "output-precedence runtime finish failed"))
    {
        return false;
    }

    ResetHarness();
    if (!Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "unsafe-coordinator output-precedence begin failed"))
    {
        return false;
    }
    g_registryUnsafe = true;
    if (!Check(ZoneRuntimeFacade::TryAddDatabaseUsers4(
                ids, 1, misalignedBulk)
            == RegistryOwnershipStatus::UnsafeFailure,
            "bulk add masked an unsafe coordinator")
        || !Check(ZoneRuntimeFacade::FinishAccess()
            == ZoneRuntimeFacadeStatus::UnsafeFailure,
            "unsafe coordinator did not poison outer access")
        || !Check(g_runtimeLocked.load(std::memory_order_relaxed),
            "unsafe coordinator released outer serializer"))
    {
        return false;
    }

    ResetHarness();
    if (!Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "torn-runtime output-precedence begin failed"))
    {
        return false;
    }
    ZoneRuntimeFacadeTestAccess::SetGlobalStateForTesting(
        0, 0, 0, 0, 0, 0, 0, 0);
    return Check(ZoneRuntimeFacade::TryAddDatabaseUsers4(
                ids, 1, misalignedBulk)
            == RegistryOwnershipStatus::UnsafeFailure,
            "bulk add masked a torn runtime boundary")
        && Check(ZoneRuntimeFacade::TryInternBoundedName(
                "name", 4, misalignedName)
                == RegistryOwnershipStatus::UnsafeFailure,
            "name intern masked a poisoned runtime boundary")
        && Check(ZoneRuntimeFacade::TryReAddRetainedDefaultNames(
                names, 1, misalignedBulk)
                == RegistryOwnershipStatus::UnsafeFailure,
            "bulk re-add masked a poisoned runtime boundary")
        && Check(g_registryBulkAddCalls == 0
                && g_registryInternCalls == 0
                && g_registryBulkReAddCalls == 0,
            "rejected unsafe output reached registry backend")
        && Check(ZoneRuntimeFacade::FinishAccess()
                == ZoneRuntimeFacadeStatus::UnsafeFailure,
            "torn output-precedence boundary did not remain poisoned")
        && Check(g_runtimeLocked.load(std::memory_order_relaxed),
            "torn output-precedence boundary released serializer");
}

[[nodiscard]] bool TestRegistryInputAuthorityAliasesRejected() noexcept
{
    ResetHarness();
    auto &table = db::zone_runtime::ProductionZoneRuntimeTable();
    static_assert(
        alignof(db::zone_runtime::ZoneRuntimeTable) >= alignof(std::uint32_t));
    static_assert(
        alignof(db::zone_runtime::ZoneRuntimeTable) >= alignof(const char *));
    const auto *const idsAlias =
        reinterpret_cast<const std::uint32_t *>(&table);
    const auto *const bytesAlias = reinterpret_cast<const char *>(&table);
    const auto *const namesAlias =
        reinterpret_cast<const char *const *>(&table);
    RegistryOwnershipBulkResult bulk{};
    RegistryOwnershipName name{};

    if (!Check(ZoneRuntimeFacade::TryAddDatabaseUsers4(
                idsAlias, 1, &bulk)
            == RegistryOwnershipStatus::InvalidState,
            "bulk input alias exposed validation without runtime access")
        || !Check(ZoneRuntimeFacade::TryInternBoundedName(
                bytesAlias, 1, &name)
            == RegistryOwnershipStatus::InvalidState,
            "name input alias exposed validation without runtime access")
        || !Check(ZoneRuntimeFacade::TryReAddRetainedDefaultNames(
                namesAlias, 1, &bulk)
            == RegistryOwnershipStatus::InvalidState,
            "retained-name array alias exposed validation without access")
        || !Check(g_registryBulkAddCalls == 0
                && g_registryInternCalls == 0
                && g_registryBulkReAddCalls == 0,
            "no-access input alias reached registry backend")
        || !Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "input-alias runtime owner begin failed")
        || !Check(ZoneRuntimeFacade::TryAddDatabaseUsers4(
                idsAlias, 1, &bulk)
            == RegistryOwnershipStatus::InvalidState,
            "bulk input alias masked inactive registry scope")
        || !Check(ZoneRuntimeFacade::TryInternBoundedName(
                bytesAlias, 1, &name)
            == RegistryOwnershipStatus::InvalidState,
            "name input alias masked inactive registry scope")
        || !Check(ZoneRuntimeFacade::TryReAddRetainedDefaultNames(
                namesAlias, 1, &bulk)
            == RegistryOwnershipStatus::InvalidState,
            "retained-name array alias masked inactive registry scope")
        || !Check(ZoneRuntimeFacade::TryBeginStandaloneRegistryOwnership()
            == RegistryOwnershipStatus::Success,
            "input-alias registry scope begin failed"))
    {
        return false;
    }

    std::array<unsigned char, sizeof(db::zone_runtime::ZoneRuntimeTable)>
        tableBefore{};
    std::memcpy(tableBefore.data(), &table, tableBefore.size());
    if (!Check(ZoneRuntimeFacade::TryAddDatabaseUsers4(
                idsAlias, 1, &bulk)
            == RegistryOwnershipStatus::InvalidArgument,
            "active bulk input accepted table authority alias")
        || !Check(ZoneRuntimeFacade::TryInternBoundedName(
                bytesAlias, 1, &name)
            == RegistryOwnershipStatus::InvalidArgument,
            "active name input accepted table authority alias")
        || !Check(ZoneRuntimeFacade::TryReAddRetainedDefaultNames(
                namesAlias, 1, &bulk)
            == RegistryOwnershipStatus::InvalidArgument,
            "active retained-name array accepted table authority alias")
        || !Check(g_registryBulkAddCalls == 0
                && g_registryInternCalls == 0
                && g_registryBulkReAddCalls == 0,
            "rejected authority input reached registry backend")
        || !Check(std::memcmp(tableBefore.data(), &table, tableBefore.size())
                == 0,
            "rejected authority input changed runtime table"))
    {
        return false;
    }

    const char *const externalNames[]{bytesAlias};
    if (!Check(ZoneRuntimeFacade::TryReAddRetainedDefaultName(bytesAlias)
            == RegistryOwnershipStatus::Success,
            "scalar retained identity was widened into a facade span check")
        || !Check(ZoneRuntimeFacade::TryReAddRetainedDefaultNames(
                externalNames, 1, &bulk)
            == RegistryOwnershipStatus::Success,
            "retained scalar pointee was widened into a facade span check")
        || !Check(g_registryScalarReAddCalls == 1
                && g_registryBulkReAddCalls == 1,
            "lower-owned retained identities did not forward exactly once")
        || !Check(ZoneRuntimeFacade::FinishRegistryOwnership()
            == RegistryOwnershipStatus::Success,
            "input-alias registry scope finish failed")
        || !Check(ZoneRuntimeFacade::FinishAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "input-alias runtime owner finish failed"))
    {
        return false;
    }
    return true;
}

[[nodiscard]] bool TestRegistryUnreadInvalidInputsStillForward() noexcept
{
    ResetHarness();
    if (!Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "invalid-input runtime owner begin failed")
        || !Check(ZoneRuntimeFacade::TryBeginStandaloneRegistryOwnership()
            == RegistryOwnershipStatus::Success,
            "invalid-input registry scope begin failed"))
    {
        return false;
    }

    auto &table = db::zone_runtime::ProductionZoneRuntimeTable();
    const auto *const idsAlias =
        reinterpret_cast<const std::uint32_t *>(&table);
    const auto *const bytesAlias = reinterpret_cast<const char *>(&table);
    const auto *const namesAlias =
        reinterpret_cast<const char *const *>(&table);
    RegistryOwnershipBulkResult bulk{};
    RegistryOwnershipName name{};
    constexpr std::uint32_t overBulkCapacity =
        script_string::kRegistryOwnershipBulkCapacity + UINT32_C(1);

    if (!Check(ZoneRuntimeFacade::TryAddDatabaseUsers4(
                idsAlias, 0, &bulk)
            == RegistryOwnershipStatus::InvalidArgument,
            "zero-count bulk alias did not preserve backend status")
        || !Check(ZoneRuntimeFacade::TryAddDatabaseUsers4(
                idsAlias, overBulkCapacity, &bulk)
            == RegistryOwnershipStatus::CapacityExceeded,
            "over-cap bulk alias did not preserve capacity status")
        || !Check(ZoneRuntimeFacade::TryAddDatabaseUsers4(nullptr, 1, &bulk)
            == RegistryOwnershipStatus::InvalidArgument,
            "null bulk input did not forward")
        || !Check(ZoneRuntimeFacade::TryAddDatabaseUsers4(
                idsAlias, 1, nullptr)
            == RegistryOwnershipStatus::InvalidArgument,
            "null bulk output did not precede input alias validation")
        || !Check(g_registryBulkAddCalls == 4,
            "unread invalid bulk inputs did not forward exactly once")
        || !Check(ZoneRuntimeFacade::TryInternBoundedName(
                bytesAlias, 0, &name)
            == RegistryOwnershipStatus::InvalidArgument,
            "zero-size name alias did not preserve backend status")
        || !Check(ZoneRuntimeFacade::TryInternBoundedName(
                bytesAlias, UINT32_C(65532), &name)
            == RegistryOwnershipStatus::InvalidArgument,
            "oversize name alias did not preserve backend status")
        || !Check(ZoneRuntimeFacade::TryInternBoundedName(nullptr, 1, &name)
            == RegistryOwnershipStatus::InvalidArgument,
            "null name input did not forward")
        || !Check(ZoneRuntimeFacade::TryInternBoundedName(
                bytesAlias, 1, nullptr)
            == RegistryOwnershipStatus::InvalidArgument,
            "null name output did not precede input alias validation")
        || !Check(g_registryInternCalls == 4,
            "unread invalid name inputs did not forward exactly once")
        || !Check(ZoneRuntimeFacade::TryReAddRetainedDefaultNames(
                namesAlias, 0, &bulk)
            == RegistryOwnershipStatus::InvalidArgument,
            "zero-count retained array did not preserve backend status")
        || !Check(ZoneRuntimeFacade::TryReAddRetainedDefaultNames(
                namesAlias, overBulkCapacity, &bulk)
            == RegistryOwnershipStatus::CapacityExceeded,
            "over-cap retained array did not preserve capacity status")
        || !Check(ZoneRuntimeFacade::TryReAddRetainedDefaultNames(
                nullptr, 1, &bulk)
            == RegistryOwnershipStatus::InvalidArgument,
            "null retained array did not forward")
        || !Check(ZoneRuntimeFacade::TryReAddRetainedDefaultNames(
                namesAlias, 1, nullptr)
            == RegistryOwnershipStatus::InvalidArgument,
            "null retained output did not precede input alias validation")
        || !Check(g_registryBulkReAddCalls == 4,
            "unread invalid retained arrays did not forward exactly once")
        || !Check(ZoneRuntimeFacade::FinishRegistryOwnership()
            == RegistryOwnershipStatus::Success,
            "invalid-input registry scope finish failed")
        || !Check(ZoneRuntimeFacade::FinishAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "invalid-input runtime owner finish failed"))
    {
        return false;
    }
    return true;
}

[[nodiscard]] bool TestLegacyScriptStringPathRejected() noexcept
{
    ResetHarness();
    g_generationBindingPristine = true;
    if (!Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "legacy-script owner begin failed"))
    {
        return false;
    }

    const ZoneLoadContextKey key{UINT64_C(52), 8, 0};
    db::script_string_journal::ScriptStringJournal journal{};
    db::script_string_journal::ScriptStringJournalEntry storage[1]{};
    const db::script_string_adapter::ScriptStringSourceView source{
        "x", 1, 0};
    std::uint32_t stringId = 0xA5A5A5A5;
    if (!Check(ZoneRuntimeFacade::TryBeginScriptStringOwnership(
                8, key, &journal, storage, 1, 1)
            == ZoneRuntimeTableStatus::InvalidPhase,
            "facade entered unterminable legacy script ownership")
        || !Check(ZoneRuntimeFacade::TryStageScriptString(
                8, key, source, &stringId)
            == ZoneRuntimeTableStatus::InvalidPhase,
            "facade staged through a legacy script binding")
        || !Check(stringId == 0xA5A5A5A5,
            "legacy script rejection changed caller output")
        || !Check(ZoneRuntimeFacade::TrySealScriptStrings(8, key)
            == ZoneRuntimeTableStatus::InvalidPhase,
            "facade sealed through a legacy script binding")
        || !Check(ZoneRuntimeFacade::TryBeginScriptStringTransfer(8, key)
            == ZoneRuntimeTableStatus::InvalidPhase,
            "facade began transfer through a legacy script binding")
        || !Check(ZoneRuntimeFacade::TryTransferNextScriptString(8, key)
            == ZoneRuntimeTableStatus::InvalidPhase,
            "facade transferred through a legacy script binding")
        || !Check(ZoneRuntimeFacade::TryPrepareScriptStringCommit(8, key)
            == ZoneRuntimeTableStatus::InvalidPhase,
            "facade prepared commit through a legacy script binding")
        || !Check(g_compositeCalls == 0,
            "legacy script rejection reached a dual-mode backend")
        || !Check(ZoneRuntimeFacade::FinishAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "legacy script rejection retained outer access"))
    {
        return false;
    }
    return true;
}

[[nodiscard]] bool TestCompositeWrapperForwarding() noexcept
{
    ResetHarness();
    if (!Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::Success,
            "composite-wrapper owner begin failed"))
    {
        return false;
    }

    const ZoneLoadContextKey key{UINT64_C(45), 8, 0};
    std::uint32_t callbackContext = UINT32_C(0xC011BAC);
    const db::zone_runtime::ZoneRuntimeGenerationCallbacks callbacks{
        &callbackContext,
        EnsureForwardingUnreachable,
        PerformForwardingCleanup,
        CompleteForwardingAdmission,
        AdmitForwardingLive,
    };
    alignas(std::max_align_t) unsigned char slab[128]{};
    const db::zone_runtime_storage::ZoneRuntimeStoragePlan plan{};
    XZoneMemory zone{};
    alignas(16) std::array<std::array<std::uint8_t, 64>,
        db::relocation::kBlockCount> blockStorage{};
    std::array<db::relocation::BlockView,
        db::relocation::kBlockCount> blocks{};
    for (std::size_t index = 0; index < blocks.size(); ++index)
    {
        zone.blocks[index].data = blockStorage[index].data();
        zone.blocks[index].size =
            static_cast<std::uint32_t>(blockStorage[index].size());
        blocks[index] = {
            reinterpret_cast<std::uintptr_t>(blockStorage[index].data()),
            zone.blocks[index].size,
        };
    }
    auto authorityAliasBlocks = blocks;
    authorityAliasBlocks[0] = {
        reinterpret_cast<std::uintptr_t>(
            &db::zone_runtime::ProductionZoneRuntimeTable()),
        static_cast<std::uint32_t>(sizeof(std::uint32_t)),
    };
    db::script_string_journal::ScriptStringJournal journal{};
    db::script_string_journal::ScriptStringJournalEntry journalStorage[2]{};
    const db::script_string_adapter::ScriptStringSourceView source{
        "x", 1, 0};
    std::uint32_t drainContext = UINT32_C(0xD8A1);
    const db::zone_pending_copy::PendingCopyDrainCallback drain{
        &drainContext, ConsumeForwardingPendingCopy};
    pmem_runtime::AllocationResult allocation{};
    std::uint32_t stringId = 0;
    g_expectedCompositeSlot = 8;
    g_expectedCompositeKey = key;
    g_expectedCallbacks = callbacks;
    g_expectedZoneIdentity = &zone;
    g_expectedBlocks = blocks;
    g_expectedBlockCount = blocks.size();
    g_expectedDrain = drain;

    if (!Check(ZoneRuntimeFacade::TryBindGenerationCallbacks(
                8, key, callbacks)
            == ZoneRuntimeTableStatus::Success,
            "callback-binding wrapper failed")
        || !Check(ZoneRuntimeFacade::TryBeginPhysicalAllocation(
                8, key, "fixture", 2)
            == ZoneRuntimeTableStatus::Success,
            "allocation-begin wrapper failed")
        || !Check(ZoneRuntimeFacade::TryAllocateMemory(
                8, key, 64, 16, 2, &allocation)
            == ZoneRuntimeTableStatus::Success,
            "allocation wrapper failed")
        || !Check(allocation.additionalBytes == 17,
            "allocation wrapper did not publish backend output")
        || !Check(ZoneRuntimeFacade::TryBindStorage(
                8, key, slab, sizeof(slab), &plan)
            == ZoneRuntimeTableStatus::Success,
            "storage-binding wrapper failed")
        || !Check(ZoneRuntimeFacade::TryBeginStreamGeneration(8, key)
            == ZoneRuntimeTableStatus::Success,
            "stream-generation wrapper failed")
        || !Check(ZoneRuntimeFacade::TryBindStreams(
                8,
                key,
                &zone,
                authorityAliasBlocks.data(),
                authorityAliasBlocks.size())
            == ZoneRuntimeTableStatus::InvalidArgument,
            "stream payload accepted protected table authority")
        || !Check(g_compositeCalls == 5,
            "rejected stream payload reached the table adapter")
        || !Check(ZoneRuntimeFacade::TryBindStreams(
                8, key, &zone, blocks.data(), blocks.size())
            == ZoneRuntimeTableStatus::Success,
            "stream-binding wrapper failed")
        || !Check(ZoneRuntimeFacade::TryBeginPendingCopies(8, key)
            == ZoneRuntimeTableStatus::Success,
            "pending-copy begin wrapper failed")
        || !Check(ZoneRuntimeFacade::TryAppendPendingCopy(8, key, 27)
            == ZoneRuntimeTableStatus::Success,
            "pending-copy append wrapper failed")
        || !Check(ZoneRuntimeFacade::TryBeginScriptStringOwnership(
                8, key, &journal, journalStorage, 2, 1)
            == ZoneRuntimeTableStatus::Success,
            "script-string begin wrapper failed")
        || !Check(ZoneRuntimeFacade::TryStageScriptString(
                8, key, source, &stringId)
            == ZoneRuntimeTableStatus::Success,
            "script-string stage wrapper failed")
        || !Check(stringId == 73,
            "script-string wrapper did not publish backend output")
        || !Check(ZoneRuntimeFacade::TrySealScriptStrings(8, key)
            == ZoneRuntimeTableStatus::Success,
            "script-string seal wrapper failed")
        || !Check(ZoneRuntimeFacade::TryBeginScriptStringTransfer(8, key)
            == ZoneRuntimeTableStatus::Success,
            "script-string transfer-begin wrapper failed")
        || !Check(ZoneRuntimeFacade::TryTransferNextScriptString(8, key)
            == ZoneRuntimeTableStatus::Success,
            "script-string transfer-next wrapper failed")
        || !Check(ZoneRuntimeFacade::TryPrepareScriptStringCommit(8, key)
            == ZoneRuntimeTableStatus::Success,
            "script-string commit-prepare wrapper failed")
        || !Check(ZoneRuntimeFacade::TryPrepareAdmission(8, key)
            == ZoneRuntimeTableStatus::Success,
            "admission-prepare wrapper failed")
        || !Check(ZoneRuntimeFacade::TryInvalidateStreams(8, key)
            == ZoneRuntimeTableStatus::Success,
            "stream-invalidation wrapper failed")
        || !Check(ZoneRuntimeFacade::TryEndPhysicalAllocation(8, key)
            == ZoneRuntimeTableStatus::Success,
            "allocation-end wrapper failed")
        || !Check(ZoneRuntimeFacade::TryCommitGeneration(8, key)
            == ZoneRuntimeTableStatus::Success,
            "generation-commit wrapper failed")
        || !Check(ZoneRuntimeFacade::TryBeginGenerationAbandonment(8, key)
            == ZoneRuntimeTableStatus::Success,
            "abandonment-begin wrapper failed")
        || !Check(ZoneRuntimeFacade::TryContinueGenerationAbandonment(8, key)
            == ZoneRuntimeTableStatus::Success,
            "abandonment-continue wrapper failed")
        || !Check(ZoneRuntimeFacade::TryUnloadGeneration(8, key)
            == ZoneRuntimeTableStatus::Success,
            "generation-unload wrapper failed")
        || !Check(ZoneRuntimeFacade::TryBeginPendingCopyDrain(drain)
            == ZoneRuntimeTableStatus::Success,
            "pending-copy drain-begin wrapper failed")
        || !Check(ZoneRuntimeFacade::TryDrainNextPendingCopy()
            == ZoneRuntimeTableStatus::Success,
            "pending-copy drain-next wrapper failed")
        || !Check(ZoneRuntimeFacade::TryFinishPendingCopyDrain()
            == ZoneRuntimeTableStatus::Success,
            "pending-copy drain-finish wrapper failed")
        || !Check(ZoneRuntimeFacade::TryResetTerminalReceipt(8, key)
            == ZoneRuntimeTableStatus::Success,
            "terminal-reset wrapper failed")
        || !Check(g_compositeCalls == 25,
            "composite wrappers did not each forward exactly once")
        || !Check(g_callbackArgumentsMatched,
            "callback wrapper changed callback identities or context")
        || !Check(g_streamArgumentsMatched,
            "stream wrapper changed zone, block descriptors, or count")
        || !Check(g_drainArgumentsMatched,
            "drain wrapper changed callback identity or context")
        || !Check(ZoneRuntimeFacade::FinishAccess()
                == ZoneRuntimeFacadeStatus::Success,
            "composite-wrapper owner could not finish"))
    {
        return false;
    }
    return true;
}

[[nodiscard]] bool TestTornAndExhaustedBoundaries() noexcept
{
    ResetHarness();
    const std::uintptr_t identity =
        ZoneRuntimeFacadeTestAccess::CurrentThreadIdentity();
    ZoneRuntimeFacadeTestAccess::SetThreadStateForTesting(
        1, 1, identity, identity + 1, 8, 8);
    if (!Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::UnsafeFailure,
            "torn TLS boundary was accepted")
        || !Check(!g_runtimeLocked.load(std::memory_order_relaxed),
            "torn TLS attempted serializer acquisition"))
    {
        return false;
    }

    ResetHarness();
    ZoneRuntimeFacadeTestAccess::SetGlobalStateForTesting(
        0,
        0,
        0,
        0,
        0,
        0,
        (std::numeric_limits<std::uint64_t>::max)(),
        (std::numeric_limits<std::uint64_t>::max)());
    if (!Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::UnsafeFailure,
            "serial exhaustion did not fail closed")
        || !Check(g_runtimeLocked.load(std::memory_order_relaxed),
            "serial exhaustion released serializer"))
    {
        return false;
    }

    ResetHarness();
    ZoneRuntimeFacadeTestAccess::SetGlobalStateForTesting(
        0, 0, 0, 0, 0, 0, 3, 4);
    return Check(ZoneRuntimeFacade::TryBeginAccess()
            == ZoneRuntimeFacadeStatus::UnsafeFailure,
            "torn global serial mirror was accepted")
        && Check(g_runtimeLocked.load(std::memory_order_relaxed),
            "torn global boundary released serializer");
}

[[nodiscard]] bool TestThreadExitDoesNotUnlock() noexcept
{
    ResetHarness();
    std::atomic<ZoneRuntimeFacadeStatus> status{
        ZoneRuntimeFacadeStatus::InvalidState};
    std::thread owner([&]() {
        status.store(
            ZoneRuntimeFacade::TryBeginAccess(),
            std::memory_order_release);
    });
    owner.join();
    return Check(status.load(std::memory_order_acquire)
            == ZoneRuntimeFacadeStatus::Success,
            "thread-exit owner could not begin")
        && Check(g_runtimeLocked.load(std::memory_order_relaxed),
            "thread exit implicitly unlocked serializer")
        && Check(ZoneRuntimeFacade::TryBeginAccess()
                == ZoneRuntimeFacadeStatus::Busy,
            "thread-exit abandonment permitted a new owner");
}
} // namespace

bool KISAK_CDECL Sys_TryLockWrite(FastCriticalSection *const critSect)
{
    bool expected = false;
    if (!g_runtimeLocked.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
    {
        return false;
    }
    critSect->writeCount = 1;
    return true;
}

void KISAK_CDECL Sys_UnlockWrite(FastCriticalSection *const critSect)
{
    if (!g_runtimeLocked.load(std::memory_order_acquire))
        std::abort();
    critSect->writeCount = 0;
    g_runtimeLocked.store(false, std::memory_order_release);
}

bool KISAK_CDECL Sys_IsWriteLocked(const FastCriticalSection *const)
{
    return g_runtimeLocked.load(std::memory_order_acquire);
}

namespace physical_memory
{
AllocationReceipt::AllocationReceipt() noexcept = default;
AllocationReceipt::~AllocationReceipt() noexcept = default;
} // namespace physical_memory

namespace db::zone_pending_copy
{
PendingCopyAdmissionReceipt::PendingCopyAdmissionReceipt() noexcept = default;
PendingCopyAdmissionReceipt::~PendingCopyAdmissionReceipt() noexcept = default;
PendingCopyLedger::PendingCopyLedger() noexcept = default;
PendingCopyLedger::~PendingCopyLedger() noexcept = default;
} // namespace db::zone_pending_copy

namespace db::zone_runtime
{
namespace
{
ZoneRuntimeTable g_testTable{};
ZoneRuntimeEntry g_testEntry{};
zone_script_string_ownership::ZoneScriptStringOwnershipController
    g_testController{};
} // namespace

ZoneRuntimeTable &ProductionZoneRuntimeTable() noexcept
{
    return g_testTable;
}

ZoneRuntimeTableStatus ZoneRuntimeTable::validateReleaseSafety() noexcept
{
    return g_releaseStatus;
}

ZoneRuntimeTableStatus ZoneRuntimeTable::validateInitializedHeader() noexcept
{
    return g_tableAuthenticationStatus;
}

ZoneRuntimeTableStatus ZoneRuntimeTable::authenticateExactEntry(
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &) noexcept
{
    return g_tableAuthenticationStatus;
}

ZoneRuntimeTableStatus TryInitializeZoneRuntimeTable(ZoneRuntimeTable *) noexcept
{
    ++g_initializeCalls;
    CorruptRuntimeAfterTableOperationIfRequested();
    return g_tableOperationStatus;
}

ZoneRuntimeTableStatus TryClaimZoneRuntimeGeneration(
    ZoneRuntimeTable *,
    const std::uint32_t physicalSlot,
    zone_load::ZoneLoadContextKey *const inOutKey) noexcept
{
    ++g_claimCalls;
    if (g_tableOperationStatus == ZoneRuntimeTableStatus::Success && inOutKey)
        *inOutKey = zone_load::ZoneLoadContextKey{
            UINT64_C(91), physicalSlot, 0};
    CorruptRuntimeAfterTableOperationIfRequested();
    return g_tableOperationStatus;
}

ZoneRuntimeTableStatus TryGetZoneRuntimeGeneration(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    ZoneRuntimeGenerationView *const outView) noexcept
{
    ++g_lookupCalls;
    g_borrowSlot = physicalSlot;
    g_borrowKey = key;
    if (g_tableOperationStatus == ZoneRuntimeTableStatus::Success && outView
        && table && physicalSlot < table->entries_.size())
    {
        switch (g_lookupPublication)
        {
        case LookupPublication::Canonical:
            *outView = ZoneRuntimeGenerationView{
                key, &table->entries_[physicalSlot]};
            break;
        case LookupPublication::Empty:
            break;
        case LookupPublication::WrongKey:
        {
            ZoneLoadContextKey wrongKey = key;
            ++wrongKey.generation;
            *outView = ZoneRuntimeGenerationView{
                wrongKey, &table->entries_[physicalSlot]};
            break;
        }
        case LookupPublication::WrongEntry:
            *outView = ZoneRuntimeGenerationView{key, &g_testEntry};
            break;
        }
    }
    CorruptRuntimeAfterTableOperationIfRequested();
    return g_tableOperationStatus;
}

namespace
{
[[nodiscard]] ZoneRuntimeTableStatus CompleteCompositeStub() noexcept
{
    ++g_compositeCalls;
    CorruptRuntimeAfterTableOperationIfRequested();
    return g_tableOperationStatus;
}
} // namespace

ZoneRuntimeTableStatus TryBindZoneRuntimeGenerationCallbacks(
    ZoneRuntimeTable *,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const ZoneRuntimeGenerationCallbacks &callbacks) noexcept
{
    g_callbackArgumentsMatched = physicalSlot == g_expectedCompositeSlot
        && key == g_expectedCompositeKey
        && callbacks.context == g_expectedCallbacks.context
        && callbacks.ensureUnreachable
            == g_expectedCallbacks.ensureUnreachable
        && callbacks.performExternalCleanup
            == g_expectedCallbacks.performExternalCleanup
        && callbacks.completePendingAdmission
            == g_expectedCallbacks.completePendingAdmission
        && callbacks.admitLive == g_expectedCallbacks.admitLive;
    return CompleteCompositeStub();
}

ZoneRuntimeTableStatus TryBeginZoneRuntimePhysicalAllocation(
    ZoneRuntimeTable *,
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &,
    const char *,
    std::uint32_t) noexcept
{
    return CompleteCompositeStub();
}

ZoneRuntimeTableStatus TryAllocateZoneRuntimeMemory(
    ZoneRuntimeTable *,
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t,
    pmem_runtime::AllocationResult *const outResult) noexcept
{
    const ZoneRuntimeTableStatus status = CompleteCompositeStub();
    if ((status == ZoneRuntimeTableStatus::Success
            || status == ZoneRuntimeTableStatus::CapacityExceeded)
        && outResult)
    {
        *outResult = pmem_runtime::AllocationResult{
            UINT64_C(17),
            reinterpret_cast<std::uint8_t *>(
                static_cast<std::uintptr_t>(0x1000)),
            pmem_runtime::AllocationStatus::Success,
            {0, 0, 0}};
    }
    return status;
}

ZoneRuntimeTableStatus TryBindZoneRuntimeStorage(
    ZoneRuntimeTable *,
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &,
    void *,
    std::size_t,
    const zone_runtime_storage::ZoneRuntimeStoragePlan *) noexcept
{
    return CompleteCompositeStub();
}

ZoneRuntimeTableStatus TryBeginZoneRuntimeStreamGeneration(
    ZoneRuntimeTable *,
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &) noexcept
{
    return CompleteCompositeStub();
}

ZoneRuntimeTableStatus TryBindZoneRuntimeStreams(
    ZoneRuntimeTable *,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const XZoneMemory *const zoneIdentity,
    const relocation::BlockView *const blocks,
    const std::size_t blockCount) noexcept
{
    g_streamArgumentsMatched = physicalSlot == g_expectedCompositeSlot
        && key == g_expectedCompositeKey
        && zoneIdentity == g_expectedZoneIdentity
        && blocks != nullptr
        && blockCount == g_expectedBlockCount;
    if (g_streamArgumentsMatched)
    {
        for (std::size_t index = 0; index < blockCount; ++index)
        {
            if (blocks[index].base != g_expectedBlocks[index].base
                || blocks[index].size != g_expectedBlocks[index].size)
            {
                g_streamArgumentsMatched = false;
                break;
            }
        }
    }
    return CompleteCompositeStub();
}

ZoneRuntimeTableStatus TryBeginZoneRuntimePendingCopies(
    ZoneRuntimeTable *,
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &) noexcept
{
    return CompleteCompositeStub();
}

ZoneRuntimeTableStatus TryAppendZoneRuntimePendingCopy(
    ZoneRuntimeTable *,
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &,
    std::uint32_t) noexcept
{
    return CompleteCompositeStub();
}

ZoneRuntimeTableStatus TryBeginZoneRuntimeScriptStringOwnership(
    ZoneRuntimeTable *,
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &,
    script_string_journal::ScriptStringJournal *,
    script_string_journal::ScriptStringJournalEntry *,
    std::uint32_t,
    std::uint32_t) noexcept
{
    return CompleteCompositeStub();
}

ZoneRuntimeTableStatus TryStageZoneRuntimeScriptString(
    ZoneRuntimeTable *,
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &,
    const script_string_adapter::ScriptStringSourceView &,
    std::uint32_t *const outStringId) noexcept
{
    const ZoneRuntimeTableStatus status = CompleteCompositeStub();
    if (status == ZoneRuntimeTableStatus::Success && outStringId)
        *outStringId = 73;
    return status;
}

ZoneRuntimeTableStatus TrySealZoneRuntimeScriptStrings(
    ZoneRuntimeTable *,
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &) noexcept
{
    return CompleteCompositeStub();
}

ZoneRuntimeTableStatus TryBeginZoneRuntimeScriptStringTransfer(
    ZoneRuntimeTable *,
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &) noexcept
{
    return CompleteCompositeStub();
}

ZoneRuntimeTableStatus TryTransferNextZoneRuntimeScriptString(
    ZoneRuntimeTable *,
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &) noexcept
{
    return CompleteCompositeStub();
}

ZoneRuntimeTableStatus TryPrepareZoneRuntimeScriptStringCommit(
    ZoneRuntimeTable *,
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &) noexcept
{
    return CompleteCompositeStub();
}

ZoneRuntimeTableStatus TryPrepareZoneRuntimeAdmission(
    ZoneRuntimeTable *,
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &) noexcept
{
    return CompleteCompositeStub();
}

ZoneRuntimeTableStatus TryInvalidateZoneRuntimeStreams(
    ZoneRuntimeTable *,
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &) noexcept
{
    return CompleteCompositeStub();
}

ZoneRuntimeTableStatus TryEndZoneRuntimePhysicalAllocation(
    ZoneRuntimeTable *,
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &) noexcept
{
    return CompleteCompositeStub();
}

ZoneRuntimeTableStatus TryCommitZoneRuntimeGeneration(
    ZoneRuntimeTable *,
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &) noexcept
{
    return CompleteCompositeStub();
}

ZoneRuntimeTableStatus TryBeginZoneRuntimeGenerationAbandonment(
    ZoneRuntimeTable *,
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &) noexcept
{
    return CompleteCompositeStub();
}

ZoneRuntimeTableStatus TryContinueZoneRuntimeGenerationAbandonment(
    ZoneRuntimeTable *,
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &) noexcept
{
    return CompleteCompositeStub();
}

ZoneRuntimeTableStatus TryUnloadZoneRuntimeGeneration(
    ZoneRuntimeTable *,
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &) noexcept
{
    return CompleteCompositeStub();
}

ZoneRuntimeTableStatus TryBeginZoneRuntimePendingCopyDrain(
    ZoneRuntimeTable *,
    const zone_pending_copy::PendingCopyDrainCallback &callback) noexcept
{
    g_drainArgumentsMatched =
        callback.context == g_expectedDrain.context
        && callback.consume == g_expectedDrain.consume;
    return CompleteCompositeStub();
}

ZoneRuntimeTableStatus TryDrainNextZoneRuntimePendingCopy(
    ZoneRuntimeTable *) noexcept
{
    return CompleteCompositeStub();
}

ZoneRuntimeTableStatus TryFinishZoneRuntimePendingCopyDrain(
    ZoneRuntimeTable *) noexcept
{
    return CompleteCompositeStub();
}

ZoneRuntimeTableStatus TryResetZoneRuntimeTerminalReceipt(
    ZoneRuntimeTable *,
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &) noexcept
{
    return CompleteCompositeStub();
}

const zone_script_string_ownership::ZoneScriptStringOwnershipController &
ZoneRuntimeEntry::scriptStringOwnership() const noexcept
{
    return g_testController;
}

bool ZoneRuntimeEntry::generationBindingPristine() const noexcept
{
    return g_generationBindingPristine;
}
} // namespace db::zone_runtime

namespace db::registry_ownership
{
RegistryOwnershipStatus
RegistryOwnershipCoordinatorFacade::TryBeginStandalone() noexcept
{
    ++g_registryBeginCalls;
    if (g_registryOperationStatus == RegistryOwnershipStatus::Success)
        g_registryActive = true;
    return g_registryOperationStatus;
}

RegistryOwnershipStatus RegistryOwnershipCoordinatorFacade::TryBorrow(
    const zone_script_string_ownership::ZoneScriptStringOwnershipController &,
    const zone_load::ZoneLoadContextKey &) noexcept
{
    ++g_registryBorrowCalls;
    if (g_registryOperationStatus == RegistryOwnershipStatus::Success)
        g_registryActive = true;
    return g_registryOperationStatus;
}

RegistryOwnershipStatus RegistryOwnershipCoordinatorFacade::Finish() noexcept
{
    ++g_registryFinishCalls;
    if (!g_registryActive)
        return RegistryOwnershipStatus::InvalidState;
    if (g_registryOperationStatus == RegistryOwnershipStatus::Success)
        g_registryActive = false;
    return g_registryOperationStatus;
}

RegistryOwnershipStatus
RegistryOwnershipCoordinatorFacade::ValidateInactive() noexcept
{
    if (g_registryUnsafe)
        return RegistryOwnershipStatus::UnsafeFailure;
    return g_registryActive
        ? RegistryOwnershipStatus::InvalidState
        : RegistryOwnershipStatus::Success;
}

RegistryOwnershipStatus
RegistryOwnershipCoordinatorFacade::ValidateActive() noexcept
{
    if (g_registryUnsafe)
        return RegistryOwnershipStatus::UnsafeFailure;
    return g_registryActive
        ? RegistryOwnershipStatus::Success
        : RegistryOwnershipStatus::InvalidState;
}

bool RegistryOwnershipCoordinatorFacade::WritableOutputIsSeparated(
    const void *const output,
    const std::size_t outputSize,
    const std::size_t outputAlignment) noexcept
{
    return output && outputSize != 0 && outputAlignment != 0
        && reinterpret_cast<std::uintptr_t>(output) % outputAlignment == 0;
}

RegistryOwnershipStatus RegistryOwnershipCoordinatorFacade::
TryAddDatabaseUser4(const std::uint32_t stringId) noexcept
{
    ++g_registryAddUserCalls;
    g_lastStringId = stringId;
    return g_registryOperationStatus;
}

RegistryOwnershipStatus RegistryOwnershipCoordinatorFacade::
TryAddDatabaseUsers4(
    const std::uint32_t *const stringIds,
    const std::uint32_t count,
    RegistryOwnershipBulkResult *const outResult) noexcept
{
    ++g_registryBulkAddCalls;
    if (!outResult || !stringIds || count == 0)
        return RegistryOwnershipStatus::InvalidArgument;
    if (count > script_string::kRegistryOwnershipBulkCapacity)
        return RegistryOwnershipStatus::CapacityExceeded;
    if (g_registryOperationStatus == RegistryOwnershipStatus::Success
        && outResult)
    {
        *outResult = RegistryOwnershipBulkResult{count, 5};
    }
    return g_registryOperationStatus;
}

RegistryOwnershipStatus RegistryOwnershipCoordinatorFacade::
TryInternBoundedName(
    const char *const bytes,
    const std::uint32_t byteCount,
    RegistryOwnershipName *const outName) noexcept
{
    ++g_registryInternCalls;
    if (!outName || !bytes || byteCount == 0
        || byteCount > UINT32_C(65531))
    {
        return RegistryOwnershipStatus::InvalidArgument;
    }
    if (g_registryOperationStatus == RegistryOwnershipStatus::Success
        && outName)
    {
        *outName = RegistryOwnershipName{71, "canonical"};
    }
    return g_registryOperationStatus;
}

RegistryOwnershipStatus RegistryOwnershipCoordinatorFacade::
TryReAddRetainedDefaultName(const char *const name) noexcept
{
    ++g_registryScalarReAddCalls;
    if (!name)
        return RegistryOwnershipStatus::InvalidArgument;
    return g_registryOperationStatus;
}

RegistryOwnershipStatus RegistryOwnershipCoordinatorFacade::
TryReAddRetainedDefaultNames(
    const char *const *const names,
    const std::uint32_t count,
    RegistryOwnershipBulkResult *const outResult) noexcept
{
    ++g_registryBulkReAddCalls;
    if (!outResult || !names || count == 0)
        return RegistryOwnershipStatus::InvalidArgument;
    if (count > script_string::kRegistryOwnershipBulkCapacity)
        return RegistryOwnershipStatus::CapacityExceeded;
    if (g_registryOperationStatus == RegistryOwnershipStatus::Success
        && outResult)
    {
        *outResult = RegistryOwnershipBulkResult{count, 0};
    }
    return g_registryOperationStatus;
}

RegistryOwnershipStatus RegistryOwnershipCoordinatorFacade::
TryTransferDatabaseUsers4To8() noexcept
{
    ++g_registryTransferCalls;
    return g_registryOperationStatus;
}

RegistryOwnershipStatus
RegistryOwnershipCoordinatorFacade::TryShutdownDatabaseUser8() noexcept
{
    ++g_registryShutdownCalls;
    return g_registryOperationStatus;
}
} // namespace db::registry_ownership

int main()
{
    const bool ok = TestBasicAccessAndTableForwarding()
        && TestForeignContention()
        && TestRegistryScopeAndForwarding()
        && TestBorrowAndStatusMapping()
        && TestReleaseSafetyAndUnsafePoison()
        && TestCoordinatorValidationUnsafePoison()
        && TestTableUnsafeAndPostAuthenticationPoison()
        && TestUnknownTableStatusPoisons()
        && TestUnknownRegistryStatusPoisons()
        && TestImpossibleLookupSuccessPoisons()
        && TestMisalignedOutputRejectedUnchanged()
        && TestRegistryOutputAdmissionPrecedesAliasChecks()
        && TestRegistryInputAuthorityAliasesRejected()
        && TestRegistryUnreadInvalidInputsStillForward()
        && TestLegacyScriptStringPathRejected()
        && TestCompositeWrapperForwarding()
        && TestTornAndExhaustedBoundaries()
        && TestThreadExitDoesNotUnlock();
    return ok ? 0 : 1;
}
