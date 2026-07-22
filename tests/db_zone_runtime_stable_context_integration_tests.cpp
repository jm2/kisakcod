#include <database/db_registry_ownership_coordinator.h>
#include <database/db_relocation.h>
#include <database/db_script_string_journal.h>
#include <database/db_zone_memory.h>
#include <database/db_zone_runtime_facade.h>
#include <database/db_zone_runtime_storage.h>
#include <qcommon/com_error.h>
#include <qcommon/sys_memory.h>
#include <qcommon/sys_sync.h>
#include <qcommon/sys_time.h>
#include <script/scr_string_transaction.h>
#include <script/scr_stringlist.cpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

FastCriticalSection db_hashCritSect{};

namespace
{
using db::registry_ownership::RegistryOwnershipBulkResult;
using db::registry_ownership::RegistryOwnershipStatus;
using db::zone_load::ZoneLoadCleanupCallbackStatus;
using db::zone_load::ZoneLoadCleanupOperation;
using db::zone_load::ZoneLoadContextKey;
using db::zone_runtime::ProductionZoneRuntimeTable;
using db::zone_runtime::ZoneRuntimeCallbackContext;
using db::zone_runtime::ZoneRuntimeExecutionMode;
using db::zone_runtime::ZoneRuntimeFacade;
using db::zone_runtime::ZoneRuntimeFacadeStatus;
using db::zone_runtime::ZoneRuntimeGenerationCallbacks;
using db::zone_runtime::ZoneRuntimeGenerationView;
using db::zone_runtime::ZoneRuntimePendingCopyView;
using db::zone_runtime::ZoneRuntimeTableStatus;
using db::zone_script_string_ownership::ZoneScriptStringUnpublishStatus;

constexpr std::uint32_t kPhysicalSlot = 4;
constexpr std::uint32_t kAliasProbeSlot = 5;
constexpr std::uint32_t kNoRegistryCallbackSlot = 6;
constexpr std::uint32_t kStorageCapacity = 1;
constexpr std::uint32_t kStreamBlockBytes = 64;
constexpr std::uint32_t kPhysicalMemoryBytes = UINT32_C(0x08000000);

alignas(64) std::array<
    std::uint8_t,
    static_cast<std::size_t>(kPhysicalMemoryBytes) + 64u> g_pmemBacking{};
std::array<std::recursive_mutex, CRITSECT_COUNT> g_criticalSections{};
thread_local std::array<std::uint32_t, CRITSECT_COUNT>
    g_criticalSectionDepth{};

struct CallbackProbe final
{
    const ZoneRuntimeCallbackContext *context = nullptr;
    ZoneLoadContextKey key{};
    RegistryOwnershipStatus firstBorrow =
        RegistryOwnershipStatus::InvalidState;
    RegistryOwnershipStatus retryBorrow =
        RegistryOwnershipStatus::InvalidState;
    RegistryOwnershipStatus omittedFinishBorrow =
        RegistryOwnershipStatus::InvalidState;
    RegistryOwnershipStatus ordinaryCallbackBorrow =
        RegistryOwnershipStatus::InvalidState;
    RegistryOwnershipStatus standaloneCallbackBegin =
        RegistryOwnershipStatus::InvalidState;
    RegistryOwnershipStatus callbackBankRegistryAlias =
        RegistryOwnershipStatus::Success;
    ZoneRuntimeTableStatus callbackBankFacadeAlias =
        ZoneRuntimeTableStatus::Success;
    ZoneRuntimeTableStatus callbackBankTableAlias =
        ZoneRuntimeTableStatus::Success;
    RegistryOwnershipStatus addStatus =
        RegistryOwnershipStatus::InvalidState;
    RegistryOwnershipStatus finishStatus =
        RegistryOwnershipStatus::InvalidState;
    std::uint32_t expectedStringId = 0;
    std::uint32_t ensureCalls = 0;
    std::uint32_t cleanupCalls = 0;
    bool argumentsStable = true;
    bool omitFinish = false;
};

CallbackProbe g_callbackProbe{};

struct NoRegistryCallbackProbe final
{
    RegistryOwnershipStatus ordinaryBorrow =
        RegistryOwnershipStatus::InvalidState;
    RegistryOwnershipStatus standaloneBegin =
        RegistryOwnershipStatus::InvalidState;
    RegistryOwnershipStatus unexpectedStandaloneFinish =
        RegistryOwnershipStatus::InvalidState;
    std::uint32_t ensureCalls = 0;
};

NoRegistryCallbackProbe g_noRegistryCallbackProbe{};

[[nodiscard]] bool Check(
    const bool condition,
    const char *const message) noexcept
{
    if (!condition)
    {
        std::fprintf(
            stderr,
            "stable facade/table/controller/coordinator/registry "
            "integration test failed: %s\n",
            message);
    }
    return condition;
}

void ObserveCallbackArguments(
    const ZoneRuntimeCallbackContext *const context,
    const ZoneLoadContextKey key) noexcept
{
    if (!g_callbackProbe.context)
    {
        g_callbackProbe.context = context;
        g_callbackProbe.key = key;
        return;
    }
    if (g_callbackProbe.context != context || g_callbackProbe.key != key)
        g_callbackProbe.argumentsStable = false;
}

ZoneScriptStringUnpublishStatus EnsureGenerationUnreachable(
    const ZoneRuntimeCallbackContext *const context,
    const ZoneLoadContextKey key) noexcept
{
    ObserveCallbackArguments(context, key);
    ++g_callbackProbe.ensureCalls;
    if (!context || !static_cast<bool>(key)
        || key.slot != kPhysicalSlot)
    {
        return ZoneScriptStringUnpublishStatus::UnsafeFailure;
    }

    if (g_callbackProbe.omitFinish)
    {
        if (g_callbackProbe.ensureCalls != 1)
            return ZoneScriptStringUnpublishStatus::UnsafeFailure;
        g_callbackProbe.omittedFinishBorrow =
            ZoneRuntimeFacade::TryBorrowRegistryOwnershipFromCallback(
                context, key);
        return g_callbackProbe.omittedFinishBorrow
                == RegistryOwnershipStatus::Success
            ? ZoneScriptStringUnpublishStatus::Success
            : ZoneScriptStringUnpublishStatus::UnsafeFailure;
    }

    if (g_callbackProbe.ensureCalls == 1)
    {
        auto *const facadeAlias = reinterpret_cast<
            ZoneRuntimePendingCopyView *>(
                const_cast<ZoneRuntimeCallbackContext *>(context));
        g_callbackProbe.callbackBankFacadeAlias =
            ZoneRuntimeFacade::TryGetPendingCopyView(
                key.slot, key, facadeAlias);

        auto *const tableAlias = reinterpret_cast<
            ZoneRuntimeGenerationView *>(
                const_cast<ZoneRuntimeCallbackContext *>(context));
        g_callbackProbe.callbackBankTableAlias =
            db::zone_runtime::TryGetZoneRuntimeGeneration(
                &ProductionZoneRuntimeTable(),
                key.slot,
                key,
                tableAlias);

        g_callbackProbe.firstBorrow =
            ZoneRuntimeFacade::TryBorrowRegistryOwnershipFromCallback(
                context, key);
        return g_callbackProbe.firstBorrow
                == RegistryOwnershipStatus::Busy
            ? ZoneScriptStringUnpublishStatus::Retry
            : ZoneScriptStringUnpublishStatus::UnsafeFailure;
    }

    if (g_callbackProbe.ensureCalls == 2)
    {
        g_callbackProbe.ordinaryCallbackBorrow =
            ZoneRuntimeFacade::TryBorrowRegistryOwnership(key.slot, key);
        g_callbackProbe.standaloneCallbackBegin =
            ZoneRuntimeFacade::TryBeginStandaloneRegistryOwnership();
        if (g_callbackProbe.ordinaryCallbackBorrow
                != RegistryOwnershipStatus::Busy
            || g_callbackProbe.standaloneCallbackBegin
                != RegistryOwnershipStatus::Busy)
        {
            return ZoneScriptStringUnpublishStatus::UnsafeFailure;
        }

        g_callbackProbe.retryBorrow =
            ZoneRuntimeFacade::TryBorrowRegistryOwnershipFromCallback(
                context, key);
        if (g_callbackProbe.retryBorrow
            != RegistryOwnershipStatus::Success)
        {
            return ZoneScriptStringUnpublishStatus::UnsafeFailure;
        }

        RegistryOwnershipBulkResult aliasOutput{
            UINT32_C(0xA5A55A5A), UINT32_C(0x5A5AA5A5)};
        const auto *const callbackBankIds =
            reinterpret_cast<const std::uint32_t *>(context);
        g_callbackProbe.callbackBankRegistryAlias =
            ZoneRuntimeFacade::TryAddDatabaseUsers4(
                callbackBankIds, 1, &aliasOutput);
        if (g_callbackProbe.callbackBankRegistryAlias
                != RegistryOwnershipStatus::InvalidArgument
            || aliasOutput.addedCount != UINT32_C(0xA5A55A5A)
            || aliasOutput.unchangedCount != UINT32_C(0x5A5AA5A5))
        {
            return ZoneScriptStringUnpublishStatus::UnsafeFailure;
        }

        g_callbackProbe.addStatus =
            ZoneRuntimeFacade::TryAddDatabaseUser4(
                g_callbackProbe.expectedStringId);
        g_callbackProbe.finishStatus =
            ZoneRuntimeFacade::FinishRegistryOwnership();
        return g_callbackProbe.addStatus
                    == RegistryOwnershipStatus::Success
                && g_callbackProbe.finishStatus
                    == RegistryOwnershipStatus::Success
            ? ZoneScriptStringUnpublishStatus::Success
            : ZoneScriptStringUnpublishStatus::UnsafeFailure;
    }

    return ZoneScriptStringUnpublishStatus::Success;
}

ZoneLoadCleanupCallbackStatus PerformExternalCleanup(
    const ZoneRuntimeCallbackContext *const context,
    const ZoneLoadContextKey key,
    const ZoneLoadCleanupOperation) noexcept
{
    ObserveCallbackArguments(context, key);
    ++g_callbackProbe.cleanupCalls;
    return context && static_cast<bool>(key)
        ? ZoneLoadCleanupCallbackStatus::Success
        : ZoneLoadCleanupCallbackStatus::UnsafeFailure;
}

ZoneScriptStringUnpublishStatus EnsureNoRegistryCallbackUnreachable(
    const ZoneRuntimeCallbackContext *const context,
    const ZoneLoadContextKey key) noexcept
{
    ++g_noRegistryCallbackProbe.ensureCalls;
    if (!context || !static_cast<bool>(key)
        || key.slot != kNoRegistryCallbackSlot)
    {
        return ZoneScriptStringUnpublishStatus::UnsafeFailure;
    }

    g_noRegistryCallbackProbe.ordinaryBorrow =
        ZoneRuntimeFacade::TryBorrowRegistryOwnership(key.slot, key);
    g_noRegistryCallbackProbe.standaloneBegin =
        ZoneRuntimeFacade::TryBeginStandaloneRegistryOwnership();
    if (g_noRegistryCallbackProbe.standaloneBegin
        == RegistryOwnershipStatus::Success)
    {
        g_noRegistryCallbackProbe.unexpectedStandaloneFinish =
            ZoneRuntimeFacade::FinishRegistryOwnership();
    }

    return g_noRegistryCallbackProbe.ordinaryBorrow
                == RegistryOwnershipStatus::Busy
            && (g_noRegistryCallbackProbe.standaloneBegin
                    == RegistryOwnershipStatus::Busy
                || (g_noRegistryCallbackProbe.standaloneBegin
                        == RegistryOwnershipStatus::Success
                    && g_noRegistryCallbackProbe.unexpectedStandaloneFinish
                        == RegistryOwnershipStatus::Success))
        ? ZoneScriptStringUnpublishStatus::Success
        : ZoneScriptStringUnpublishStatus::UnsafeFailure;
}

ZoneLoadCleanupCallbackStatus PerformNoRegistryCleanup(
    const ZoneRuntimeCallbackContext *const context,
    const ZoneLoadContextKey key,
    const ZoneLoadCleanupOperation) noexcept
{
    return context && static_cast<bool>(key)
            && key.slot == kNoRegistryCallbackSlot
        ? ZoneLoadCleanupCallbackStatus::Success
        : ZoneLoadCleanupCallbackStatus::UnsafeFailure;
}

void CompleteNoRegistryAdmission(
    const ZoneRuntimeCallbackContext *,
    const ZoneLoadContextKey) noexcept
{
}

void AdmitNoRegistryLive(
    const ZoneRuntimeCallbackContext *,
    const ZoneLoadContextKey) noexcept
{
}

void CompletePendingAdmission(
    const ZoneRuntimeCallbackContext *const context,
    const ZoneLoadContextKey key) noexcept
{
    ObserveCallbackArguments(context, key);
}

void AdmitLive(
    const ZoneRuntimeCallbackContext *const context,
    const ZoneLoadContextKey key) noexcept
{
    ObserveCallbackArguments(context, key);
}

[[nodiscard]] ZoneRuntimeGenerationCallbacks
MakeNoRegistryCallbacks() noexcept
{
    return {
        nullptr,
        EnsureNoRegistryCallbackUnreachable,
        PerformNoRegistryCleanup,
        CompleteNoRegistryAdmission,
        AdmitNoRegistryLive,
    };
}

[[nodiscard]] ZoneRuntimeGenerationCallbacks MakeCallbacks() noexcept
{
    return {
        nullptr,
        EnsureGenerationUnreachable,
        PerformExternalCleanup,
        CompletePendingAdmission,
        AdmitLive,
    };
}

struct RuntimeFixture final
{
    ZoneLoadContextKey key{};
    db::zone_runtime_storage::ZoneRuntimeStoragePlan storagePlan{};
    pmem_runtime::AllocationResult slab{};
    XZoneMemory zone{};
    std::array<
        db::relocation::BlockView,
        db::relocation::kBlockCount> blockViews{};
    std::array<
        pmem_runtime::AllocationResult,
        db::relocation::kBlockCount> blockAllocations{};

    [[nodiscard]] bool enroll(
        const bool initializeTable = true) noexcept
    {
        if (initializeTable
            && ZoneRuntimeFacade::TryInitializeRuntimeTable()
                != ZoneRuntimeTableStatus::Success)
        {
            return false;
        }

        auto *const tableAlias = reinterpret_cast<ZoneLoadContextKey *>(
            &ProductionZoneRuntimeTable());
        if (ZoneRuntimeFacade::TryClaimGeneration(
                kAliasProbeSlot, tableAlias)
            != ZoneRuntimeTableStatus::InvalidArgument)
        {
            return false;
        }

        const ZoneRuntimeGenerationCallbacks callbacks = MakeCallbacks();
        return ZoneRuntimeFacade::TryClaimGeneration(kPhysicalSlot, &key)
                    == ZoneRuntimeTableStatus::Success
            && ZoneRuntimeFacade::TryBindGenerationCallbacks(
                   kPhysicalSlot, key, callbacks)
                == ZoneRuntimeTableStatus::Success;
    }

    [[nodiscard]] bool setUpStorage() noexcept
    {
        if (ZoneRuntimeFacade::TryBeginPhysicalAllocation(
                kPhysicalSlot, key, "stable_full_chain", 0)
                != ZoneRuntimeTableStatus::Success
            || db::zone_runtime_storage::TryPlanZoneRuntimeStorage(
                   kStorageCapacity, 4096, &storagePlan)
                != db::zone_runtime_storage::
                    ZoneRuntimeStorageStatus::Success
            || ZoneRuntimeFacade::TryAllocateMemory(
                   kPhysicalSlot,
                   key,
                   storagePlan.totalBytes,
                   16,
                   0,
                   &slab)
                != ZoneRuntimeTableStatus::Success
            || slab.status != pmem_runtime::AllocationStatus::Success
            || !slab.address)
        {
            return false;
        }
        return ZoneRuntimeFacade::TryBindStorage(
                   kPhysicalSlot,
                   key,
                   slab.address,
                   storagePlan.totalBytes,
                   &storagePlan)
            == ZoneRuntimeTableStatus::Success;
    }

    [[nodiscard]] bool setUpStreams() noexcept
    {
        if (ZoneRuntimeFacade::TryBeginStreamGeneration(
                kPhysicalSlot, key)
            != ZoneRuntimeTableStatus::Success)
        {
            return false;
        }
        for (std::size_t index = 0; index < blockViews.size(); ++index)
        {
            pmem_runtime::AllocationResult &allocation =
                blockAllocations[index];
            if (ZoneRuntimeFacade::TryAllocateMemory(
                    kPhysicalSlot,
                    key,
                    kStreamBlockBytes,
                    16,
                    0,
                    &allocation)
                    != ZoneRuntimeTableStatus::Success
                || allocation.status
                    != pmem_runtime::AllocationStatus::Success
                || !allocation.address)
            {
                return false;
            }
            zone.blocks[index].data = allocation.address;
            zone.blocks[index].size = kStreamBlockBytes;
            blockViews[index] = {
                reinterpret_cast<std::uintptr_t>(allocation.address),
                kStreamBlockBytes,
            };
        }
        return ZoneRuntimeFacade::TryBindStreams(
                   kPhysicalSlot,
                   key,
                   &zone,
                   blockViews.data(),
                   blockViews.size())
            == ZoneRuntimeTableStatus::Success;
    }

    [[nodiscard]] bool beginOwnership() noexcept
    {
        if (ZoneRuntimeFacade::TryBeginPendingCopies(kPhysicalSlot, key)
            != ZoneRuntimeTableStatus::Success)
        {
            return false;
        }
        auto *const slabBytes = slab.address;
        auto *const journal = reinterpret_cast<
            db::script_string_journal::ScriptStringJournal *>(
                slabBytes + storagePlan.scriptStringJournal.offset);
        auto *const entries = reinterpret_cast<
            db::script_string_journal::ScriptStringJournalEntry *>(
                slabBytes + storagePlan.scriptStringEntries.offset);
        return ZoneRuntimeFacade::TryBeginScriptStringOwnership(
                   kPhysicalSlot,
                   key,
                   journal,
                   entries,
                   storagePlan.scriptStringCapacity,
                   0)
            == ZoneRuntimeTableStatus::Success;
    }
};

[[nodiscard]] bool DriveAbandonmentToTerminal(
    const ZoneLoadContextKey &key) noexcept
{
    for (std::size_t attempt = 0; attempt < 32; ++attempt)
    {
        const db::zone_runtime::ZoneRuntimeEntry *entry = nullptr;
        const ZoneRuntimeTableStatus lookup =
            db::zone_runtime::TryGetZoneRuntimeEntry(
                &ProductionZoneRuntimeTable(), key.slot, &entry);
        if (lookup != ZoneRuntimeTableStatus::Success
            || !entry || entry->key() != key)
        {
            return false;
        }
        if (entry->executionMode() == ZoneRuntimeExecutionMode::Terminal)
        {
            return true;
        }
        const ZoneRuntimeTableStatus status =
            ZoneRuntimeFacade::TryContinueGenerationAbandonment(
                key.slot, key);
        if (status != ZoneRuntimeTableStatus::Success
            && status != ZoneRuntimeTableStatus::Retry)
        {
            return false;
        }
    }
    return false;
}

[[nodiscard]] bool TestNoRegistryCallbackAdmissionGate() noexcept
{
    g_noRegistryCallbackProbe = {};
    ZoneLoadContextKey key{};
    const ZoneRuntimeGenerationCallbacks callbacks =
        MakeNoRegistryCallbacks();
    if (!Check(
            ZoneRuntimeFacade::TryInitializeRuntimeTable()
                == ZoneRuntimeTableStatus::Success,
            "no-registry callback table initialization failed")
        || !Check(
            ZoneRuntimeFacade::TryClaimGeneration(
                kNoRegistryCallbackSlot, &key)
                == ZoneRuntimeTableStatus::Success,
            "no-registry callback generation claim failed")
        || !Check(
            ZoneRuntimeFacade::TryBindGenerationCallbacks(
                kNoRegistryCallbackSlot, key, callbacks)
                == ZoneRuntimeTableStatus::Success,
            "no-registry callback binding failed"))
    {
        return false;
    }

    const ZoneRuntimeTableStatus begin =
        ZoneRuntimeFacade::TryBeginGenerationAbandonment(
            kNoRegistryCallbackSlot, key);
    if (!Check(
            begin == ZoneRuntimeTableStatus::Success
                || begin == ZoneRuntimeTableStatus::Retry,
            "callbacks-bound abandonment did not start")
        || !Check(DriveAbandonmentToTerminal(key),
            "callbacks-bound abandonment did not reach terminal"))
    {
        return false;
    }

    const bool gateHeld = Check(
            g_noRegistryCallbackProbe.ensureCalls != 0,
            "no-registry callback was not exercised")
        && Check(
            g_noRegistryCallbackProbe.ordinaryBorrow
                == RegistryOwnershipStatus::Busy,
            "ordinary registry borrow crossed a no-registry callback")
        && Check(
            g_noRegistryCallbackProbe.standaloneBegin
                == RegistryOwnershipStatus::Busy,
            "standalone registry admission crossed a no-registry callback")
        && Check(
            g_noRegistryCallbackProbe.unexpectedStandaloneFinish
                == RegistryOwnershipStatus::InvalidState,
            "no-registry callback unexpectedly enrolled the coordinator");
    const bool reset = Check(
        ZoneRuntimeFacade::TryResetTerminalReceipt(
            kNoRegistryCallbackSlot, key)
            == ZoneRuntimeTableStatus::Success,
        "no-registry callback terminal receipt did not reset");
    return gateHeld && reset;
}

[[nodiscard]] bool TestStableCallbackRetryChain() noexcept
{
    if (!Check(
            pmem_runtime::TryInitialize()
                == pmem_runtime::InitializationStatus::Success,
            "physical-memory runtime did not initialize")
        || !Check(
            ZoneRuntimeFacade::TryBeginAccess()
                == ZoneRuntimeFacadeStatus::Success,
            "facade access did not begin"))
    {
        return false;
    }

    SL_Init();
    constexpr char stringValue[] = "stable-callback-registry-chain";
    const script_string::AcquireResult acquired =
        script_string::TryAcquireOrdinaryStringOfSize(
            stringValue,
            static_cast<std::uint32_t>(sizeof(stringValue)),
            15);
    if (!Check(
            acquired.status == script_string::AcquireStatus::Acquired
                && acquired.stringId != 0,
            "real script-string registry did not acquire the seed string"))
    {
        return false;
    }
    g_callbackProbe.expectedStringId = acquired.stringId;

    if (!TestNoRegistryCallbackAdmissionGate())
        return false;

    RuntimeFixture fixture{};
    if (!Check(fixture.enroll(false),
            "runtime generation enrollment failed")
        || !Check(fixture.setUpStorage(), "runtime storage setup failed")
        || !Check(fixture.setUpStreams(), "runtime stream setup failed")
        || !Check(fixture.beginOwnership(),
            "runtime script-string ownership begin failed"))
    {
        return false;
    }

    Sys_LockWrite(&db_hashCritSect);
    const ZoneRuntimeTableStatus firstAbandonment =
        ZoneRuntimeFacade::TryBeginGenerationAbandonment(
            fixture.key.slot, fixture.key);
    Sys_UnlockWrite(&db_hashCritSect);
    if (!Check(firstAbandonment == ZoneRuntimeTableStatus::Retry,
            "hash contention did not map callback Busy to table Retry")
        || !Check(g_callbackProbe.firstBorrow
                == RegistryOwnershipStatus::Busy,
            "callback borrow did not observe real hash contention")
        || !Check(g_callbackProbe.callbackBankFacadeAlias
                == ZoneRuntimeTableStatus::InvalidArgument,
            "facade accepted callback-bank output alias")
        || !Check(g_callbackProbe.callbackBankTableAlias
                == ZoneRuntimeTableStatus::InvalidArgument,
            "table accepted callback-bank output alias"))
    {
        return false;
    }

    const ZoneRuntimeTableStatus retry =
        ZoneRuntimeFacade::TryContinueGenerationAbandonment(
            fixture.key.slot, fixture.key);
    if (!Check(retry == ZoneRuntimeTableStatus::Success,
            "uncontended callback retry did not succeed")
        || !Check(g_callbackProbe.ordinaryCallbackBorrow
                == RegistryOwnershipStatus::Busy,
            "ordinary registry borrow crossed the callback window")
        || !Check(g_callbackProbe.standaloneCallbackBegin
                == RegistryOwnershipStatus::Busy,
            "standalone registry admission crossed the callback window")
        || !Check(g_callbackProbe.retryBorrow
                == RegistryOwnershipStatus::Success,
            "typed callback borrow did not enroll the coordinator")
        || !Check(g_callbackProbe.callbackBankRegistryAlias
                == RegistryOwnershipStatus::InvalidArgument,
            "registry facade accepted callback-bank input alias")
        || !Check(g_callbackProbe.addStatus
                == RegistryOwnershipStatus::Success,
            "real registry add path did not succeed")
        || !Check(g_callbackProbe.finishStatus
                == RegistryOwnershipStatus::Success,
            "callback registry coordinator did not finish")
        || !Check(DriveAbandonmentToTerminal(fixture.key),
            "abandonment did not reach a terminal receipt"))
    {
        return false;
    }

    if (!Check(
            ZoneRuntimeFacade::TryResetTerminalReceipt(
                fixture.key.slot, fixture.key)
                == ZoneRuntimeTableStatus::Success,
            "terminal receipt reset failed")
        || !Check(g_callbackProbe.context != nullptr
                && g_callbackProbe.key == fixture.key
                && g_callbackProbe.argumentsStable,
            "typed stable callback context/key drifted")
        || !Check(
            ZoneRuntimeFacade::TryBorrowRegistryOwnershipFromCallback(
                g_callbackProbe.context, fixture.key)
                == RegistryOwnershipStatus::InvalidState,
            "terminal callback context remained live"))
    {
        return false;
    }

    ZoneLoadContextKey staleKey = fixture.key;
    ++staleKey.generation;
    if (!Check(
            ZoneRuntimeFacade::TryBorrowRegistryOwnershipFromCallback(
                g_callbackProbe.context, staleKey)
                == RegistryOwnershipStatus::InvalidKey,
            "terminal callback context accepted a stale key")
        || !Check(
            ZoneRuntimeFacade::FinishAccess()
                == ZoneRuntimeFacadeStatus::Success,
            "facade access did not retire after terminal reset")
        || !Check(
            script_string::TryRemoveOrdinaryReference(acquired.stringId)
                == script_string::ReleaseStatus::Success,
            "ordinary seed reference cleanup failed"))
    {
        return false;
    }

    SL_ShutdownSystem(script_string::kDatabaseUserMask);
    SL_Shutdown();
    return true;
}

[[nodiscard]] bool TestOmittedCallbackFinishFailsClosed() noexcept
{
    if (!Check(
            pmem_runtime::TryInitialize()
                == pmem_runtime::InitializationStatus::Success,
            "omit-finish physical-memory runtime did not initialize")
        || !Check(
            ZoneRuntimeFacade::TryBeginAccess()
                == ZoneRuntimeFacadeStatus::Success,
            "omit-finish facade access did not begin"))
    {
        return false;
    }

    SL_Init();
    g_callbackProbe.omitFinish = true;
    RuntimeFixture fixture{};
    if (!Check(fixture.enroll(),
            "omit-finish runtime generation enrollment failed")
        || !Check(fixture.setUpStorage(),
            "omit-finish runtime storage setup failed")
        || !Check(fixture.setUpStreams(),
            "omit-finish runtime stream setup failed")
        || !Check(fixture.beginOwnership(),
            "omit-finish ownership begin failed"))
    {
        return false;
    }

    const ZoneRuntimeTableStatus abandonment =
        ZoneRuntimeFacade::TryBeginGenerationAbandonment(
            fixture.key.slot, fixture.key);
    const db::zone_runtime::ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus entryStatus =
        db::zone_runtime::TryGetZoneRuntimeEntry(
            &ProductionZoneRuntimeTable(), fixture.key.slot, &entry);
    return Check(
               g_callbackProbe.omittedFinishBorrow
                   == RegistryOwnershipStatus::Success,
               "omit-finish callback borrow did not enroll")
        && Check(abandonment == ZoneRuntimeTableStatus::UnsafeFailure,
            "omitted callback coordinator finish did not fail closed")
        && Check(entryStatus == ZoneRuntimeTableStatus::Success
                && entry
                && entry->executionMode()
                    != ZoneRuntimeExecutionMode::Terminal
                && !entry->generationBindingPristine(),
            "omit-finish table became safely retirable")
        && Check(
            ZoneRuntimeFacade::FinishAccess()
                == ZoneRuntimeFacadeStatus::UnsafeFailure,
            "omit-finish facade access retired")
        && Check(
            ZoneRuntimeFacade::TryBeginAccess()
                == ZoneRuntimeFacadeStatus::UnsafeFailure,
            "omit-finish facade boundary reopened");
}
} // namespace

void KISAK_CDECL Sys_Sleep(const std::uint32_t)
{
    std::this_thread::yield();
}

void KISAK_CDECL Sys_EnterCriticalSection(const int criticalSection)
{
    if (criticalSection < 0 || criticalSection >= CRITSECT_COUNT)
        std::abort();
    const std::size_t index = static_cast<std::size_t>(criticalSection);
    g_criticalSections[index].lock();
    ++g_criticalSectionDepth[index];
}

void KISAK_CDECL Sys_LeaveCriticalSection(const int criticalSection)
{
    if (criticalSection < 0 || criticalSection >= CRITSECT_COUNT)
        std::abort();
    const std::size_t index = static_cast<std::size_t>(criticalSection);
    if (g_criticalSectionDepth[index] == 0)
        std::abort();
    --g_criticalSectionDepth[index];
    g_criticalSections[index].unlock();
}

std::size_t KISAK_CDECL Sys_VirtualMemoryPageSize()
{
    return 64;
}

void *KISAK_CDECL Sys_VirtualMemoryReserve(const std::size_t size)
{
    return size == kPhysicalMemoryBytes
        ? static_cast<void *>(g_pmemBacking.data())
        : nullptr;
}

bool KISAK_CDECL Sys_VirtualMemoryCommit(
    void *const address,
    const std::size_t size)
{
    return address == g_pmemBacking.data()
        && size == kPhysicalMemoryBytes;
}

bool KISAK_CDECL Sys_VirtualMemoryDecommit(
    void *const,
    const std::size_t)
{
    return false;
}

bool KISAK_CDECL Sys_VirtualMemoryRelease(void *const address)
{
    return address == g_pmemBacking.data();
}

void Com_Memset(
    void *const destination,
    const int value,
    const std::size_t count)
{
    std::memset(destination, value, count);
}

void MyAssertHandler(const char *, int, int, const char *, ...)
{
    std::abort();
}

void KISAK_CDECL Com_Error(errorParm_t, const char *, ...)
{
    std::abort();
}

void KISAK_CDECL Com_Printf(int, const char *, ...)
{
}

double KISAK_CDECL ConvertToMB(const int bytes)
{
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

void KISAK_CDECL Sys_OutOfMemErrorInternal(const char *, int)
{
    std::abort();
}

char *KISAK_CDECL va(const char *, ...)
{
    static thread_local char empty[1]{};
    return empty;
}

int main(const int argc, char **const argv)
{
    if (argc == 1)
        return TestStableCallbackRetryChain() ? 0 : 1;
    if (argc == 2 && std::strcmp(argv[1], "--omit-finish") == 0)
    {
        const int result = TestOmittedCallbackFinishFailsClosed() ? 0 : 1;
        // This mode deliberately proves that retained authority cannot be
        // retired. Avoid running unrelated static destructors while those
        // fail-closed process-lifetime locks are still held.
        std::_Exit(result);
    }
    return 2;
}
