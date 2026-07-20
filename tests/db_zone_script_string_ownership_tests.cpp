#include <database/db_zone_script_string_ownership.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include <script/scr_string_transaction.h>

void MyAssertHandler(const char *, int, int, const char *, ...)
{
    std::abort();
}

namespace ownership_test
{
namespace controller = db::zone_script_string_ownership;
namespace journal = db::script_string_journal;
namespace lifecycle = db::zone_load;

using controller::ZoneScriptStringOwnershipController;
using controller::ZoneScriptStringOwnershipControllerTestAccess;
using controller::ZoneScriptStringOwnershipPhase;
using controller::ZoneScriptStringOwnershipStatus;
using controller::ZoneScriptStringStorageBindingPhase;
using RegistryCallbackPurpose =
    ZoneScriptStringOwnershipControllerTestAccess::RegistryCallbackPurpose;
using journal::ScriptStringJournal;
using journal::ScriptStringJournalEntry;
using journal::ScriptStringJournalEntryState;

constexpr std::size_t kMaxCalls = 16;

struct FakeBackend final
{
    std::array<script_string::AcquireResult, kMaxCalls> acquire{};
    std::size_t acquireCount = 0;
    std::size_t acquireCalls = 0;

    std::array<script_string::TransferStatus, kMaxCalls> transfer{};
    std::size_t transferCount = 0;
    std::size_t transferCalls = 0;
    std::array<std::uint32_t, kMaxCalls> transferredIds{};

    std::array<script_string::ReleaseStatus, kMaxCalls> ordinary{};
    std::size_t ordinaryCount = 0;
    std::size_t ordinaryCalls = 0;
    std::array<std::uint32_t, kMaxCalls> ordinaryIds{};

    std::array<script_string::ReleaseStatus, kMaxCalls> database{};
    std::size_t databaseCount = 0;
    std::size_t databaseCalls = 0;
    std::array<std::uint32_t, kMaxCalls> databaseIds{};
};

FakeBackend backend{};
int failures = 0;

void Check(const bool condition, const char *const text, const int line)
{
    if (condition)
        return;
    std::fprintf(
        stderr,
        "zone script-string ownership line %d: check failed: %s\n",
        line,
        text);
    ++failures;
}

#define OWNERSHIP_CHECK(expression) \
    ::ownership_test::Check((expression), #expression, __LINE__)

void ResetBackend() noexcept
{
    backend = {};
}

void PushAcquire(
    const script_string::AcquireStatus status,
    const std::uint32_t stringId) noexcept
{
    OWNERSHIP_CHECK(backend.acquireCount < backend.acquire.size());
    if (backend.acquireCount < backend.acquire.size())
        backend.acquire[backend.acquireCount++] = {status, stringId};
}

void PushTransfer(const script_string::TransferStatus status) noexcept
{
    OWNERSHIP_CHECK(backend.transferCount < backend.transfer.size());
    if (backend.transferCount < backend.transfer.size())
        backend.transfer[backend.transferCount++] = status;
}

void PushOrdinary(const script_string::ReleaseStatus status) noexcept
{
    OWNERSHIP_CHECK(backend.ordinaryCount < backend.ordinary.size());
    if (backend.ordinaryCount < backend.ordinary.size())
        backend.ordinary[backend.ordinaryCount++] = status;
}

void PushDatabase(const script_string::ReleaseStatus status) noexcept
{
    OWNERSHIP_CHECK(backend.databaseCount < backend.database.size());
    if (backend.databaseCount < backend.database.size())
        backend.database[backend.databaseCount++] = status;
}

struct Fixture final
{
    lifecycle::ZoneLoadContextSlot lifecycleSlot{};
    lifecycle::ZoneLoadContextKey key{};
    ScriptStringJournal stringJournal{};
    std::array<ScriptStringJournalEntry, 4> storage{};
    ZoneScriptStringOwnershipController ownership{};
    std::array<std::size_t, 5> registryCallbackPurposeCalls{};
    std::array<std::uint8_t, 5> registryCallbackLastWitness{};
    std::thread registryCallbackForeignThread{};
    std::atomic<bool> registryCallbackForeignReady{false};
    std::atomic<bool> registryCallbackForeignDone{false};
    bool registryCallbackForeignAccepted = true;
    std::uint32_t registryCallbackForeignSerial = UINT32_C(0x13572468);
    RegistryCallbackPurpose registryCallbackForeignPurpose =
        RegistryCallbackPurpose::Unloading;
    std::uint8_t registryCallbackForeignWitness = UINT8_C(0xA5);

    bool begin(const std::uint32_t expectedCount, const std::uint32_t slot)
    {
        if (lifecycle::TryInitializeZoneLoadContextSlot(
                &lifecycleSlot, slot)
                != lifecycle::ZoneLoadContextStatus::Success
            || lifecycle::TryClaimZoneLoadContext(&lifecycleSlot, &key)
                != lifecycle::ZoneLoadContextStatus::Success)
        {
            return false;
        }
        return controller::TryBeginZoneScriptStringOwnership(
                   &ownership,
                   &lifecycleSlot,
                   key,
                   &stringJournal,
                   storage.data(),
                   static_cast<std::uint32_t>(storage.size()),
                   expectedCount)
            == ZoneScriptStringOwnershipStatus::Success;
    }
};

void ObserveRegistryCallbackAuthorization(
    Fixture *const fixture,
    const RegistryCallbackPurpose expectedPurpose) noexcept
{
    OWNERSHIP_CHECK(fixture != nullptr);
    if (!fixture)
        return;

    const std::size_t purposeIndex =
        static_cast<std::size_t>(expectedPurpose);
    OWNERSHIP_CHECK(expectedPurpose != RegistryCallbackPurpose::None);
    OWNERSHIP_CHECK(
        purposeIndex < fixture->registryCallbackPurposeCalls.size());
    if (expectedPurpose == RegistryCallbackPurpose::None
        || purposeIndex >= fixture->registryCallbackPurposeCalls.size())
    {
        return;
    }
    const bool firstObservation =
        fixture->registryCallbackPurposeCalls[purposeIndex]++ == 0;

    std::uint32_t ordinarySerial = UINT32_C(0xA55A5AA5);
    OWNERSHIP_CHECK(
        !fixture->ownership.trySnapshotRegistryTransaction(
            fixture->key, &ordinarySerial));
    OWNERSHIP_CHECK(ordinarySerial == UINT32_C(0xA55A5AA5));

    std::uint32_t serial = 0;
    RegistryCallbackPurpose purpose = RegistryCallbackPurpose::None;
    std::uint8_t windowWitness = 0;
    OWNERSHIP_CHECK(
        ZoneScriptStringOwnershipControllerTestAccess::
            TrySnapshotRegistryCallbackTransaction(
                &fixture->ownership,
                fixture->key,
                &serial,
                &purpose,
                &windowWitness));
    OWNERSHIP_CHECK(serial != 0);
    OWNERSHIP_CHECK(purpose == expectedPurpose);
    OWNERSHIP_CHECK(windowWitness != 0);
    const ZoneScriptStringOwnershipPhase callbackPhase =
        fixture->ownership.phase();
    const std::uint8_t previousWindowWitness =
        fixture->registryCallbackLastWitness[purposeIndex];
    if (previousWindowWitness != 0)
    {
        OWNERSHIP_CHECK(previousWindowWitness != windowWitness);
        OWNERSHIP_CHECK(
            !ZoneScriptStringOwnershipControllerTestAccess::
                AuthenticatesRegistryCallbackTransaction(
                    &fixture->ownership,
                    fixture->key,
                    serial,
                    expectedPurpose,
                    previousWindowWitness));
    }
    fixture->registryCallbackLastWitness[purposeIndex] = windowWitness;
    OWNERSHIP_CHECK(
        ZoneScriptStringOwnershipControllerTestAccess::
            AuthenticatesRegistryCallbackTransaction(
                &fixture->ownership,
                fixture->key,
                serial,
                expectedPurpose,
                windowWitness));
    OWNERSHIP_CHECK(
        !fixture->ownership.authenticatesRegistryTransaction(
            fixture->key, serial));
    OWNERSHIP_CHECK(
        !ZoneScriptStringOwnershipControllerTestAccess::
            AuthenticatesRegistryCallbackTransaction(
                &fixture->ownership,
                fixture->key,
                serial,
                RegistryCallbackPurpose::None,
                windowWitness));
    OWNERSHIP_CHECK(
        !ZoneScriptStringOwnershipControllerTestAccess::
            AuthenticatesRegistryCallbackTransaction(
                &fixture->ownership,
                fixture->key,
                serial,
                expectedPurpose,
                0));

    if (!firstObservation)
        return;

    lifecycle::ZoneLoadContextKey wrongKey = fixture->key;
    ++wrongKey.generation;
    std::uint32_t rejectedSerial = UINT32_C(0x11223344);
    RegistryCallbackPurpose rejectedPurpose =
        RegistryCallbackPurpose::Unloading;
    std::uint8_t rejectedWindowWitness = UINT8_C(0x5A);
    OWNERSHIP_CHECK(
        !ZoneScriptStringOwnershipControllerTestAccess::
            TrySnapshotRegistryCallbackTransaction(
                &fixture->ownership,
                wrongKey,
                &rejectedSerial,
                &rejectedPurpose,
                &rejectedWindowWitness));
    OWNERSHIP_CHECK(rejectedSerial == UINT32_C(0x11223344));
    OWNERSHIP_CHECK(
        rejectedPurpose == RegistryCallbackPurpose::Unloading);
    OWNERSHIP_CHECK(rejectedWindowWitness == UINT8_C(0x5A));

    rejectedPurpose = RegistryCallbackPurpose::Unloading;
    OWNERSHIP_CHECK(
        !ZoneScriptStringOwnershipControllerTestAccess::
            TrySnapshotRegistryCallbackTransaction(
                &fixture->ownership,
                fixture->key,
                nullptr,
                &rejectedPurpose,
                &rejectedWindowWitness));
    OWNERSHIP_CHECK(
        rejectedPurpose == RegistryCallbackPurpose::Unloading);
    rejectedSerial = UINT32_C(0x55667788);
    OWNERSHIP_CHECK(
        !ZoneScriptStringOwnershipControllerTestAccess::
            TrySnapshotRegistryCallbackTransaction(
                &fixture->ownership,
                fixture->key,
                &rejectedSerial,
                nullptr,
                &rejectedWindowWitness));
    OWNERSHIP_CHECK(rejectedSerial == UINT32_C(0x55667788));
    rejectedSerial = UINT32_C(0x66778899);
    rejectedPurpose = RegistryCallbackPurpose::Unloading;
    OWNERSHIP_CHECK(
        !ZoneScriptStringOwnershipControllerTestAccess::
            TrySnapshotRegistryCallbackTransaction(
                &fixture->ownership,
                fixture->key,
                &rejectedSerial,
                &rejectedPurpose,
                nullptr));
    OWNERSHIP_CHECK(rejectedSerial == UINT32_C(0x66778899));
    OWNERSHIP_CHECK(
        rejectedPurpose == RegistryCallbackPurpose::Unloading);

    const std::uint32_t wrongSerial = serial == UINT32_MAX
        ? serial - 1
        : serial + 1;
    OWNERSHIP_CHECK(
        !ZoneScriptStringOwnershipControllerTestAccess::
            AuthenticatesRegistryCallbackTransaction(
                &fixture->ownership,
                fixture->key,
                wrongSerial,
                expectedPurpose,
                windowWitness));

    ZoneScriptStringOwnershipControllerTestAccess::SetReserved(
        &fixture->ownership,
        windowWitness,
        static_cast<std::uint8_t>(windowWitness - 1));
    rejectedSerial = UINT32_C(0x99AABBCC);
    rejectedPurpose = RegistryCallbackPurpose::Unloading;
    rejectedWindowWitness = UINT8_C(0x5A);
    OWNERSHIP_CHECK(
        !ZoneScriptStringOwnershipControllerTestAccess::
            TrySnapshotRegistryCallbackTransaction(
                &fixture->ownership,
                fixture->key,
                &rejectedSerial,
                &rejectedPurpose,
                &rejectedWindowWitness));
    OWNERSHIP_CHECK(rejectedSerial == UINT32_C(0x99AABBCC));
    OWNERSHIP_CHECK(
        rejectedPurpose == RegistryCallbackPurpose::Unloading);
    OWNERSHIP_CHECK(rejectedWindowWitness == UINT8_C(0x5A));

    // The begin primitive must poison rather than advance a torn or exhausted
    // witness. Test access repairs the deliberately poisoned phase afterward
    // so the same process can cover every real callback purpose without
    // releasing the fail-closed production serializer.
    OWNERSHIP_CHECK(
        !ZoneScriptStringOwnershipControllerTestAccess::
            TryBeginRegistryCallbackWindow(
                &fixture->ownership, callbackPhase));
    OWNERSHIP_CHECK(fixture->ownership.poisoned());
    ZoneScriptStringOwnershipControllerTestAccess::SetPhase(
        &fixture->ownership, callbackPhase);
    ZoneScriptStringOwnershipControllerTestAccess::SetReserved(
        &fixture->ownership, windowWitness, windowWitness);

    ZoneScriptStringOwnershipControllerTestAccess::SetReserved(
        &fixture->ownership, UINT8_MAX, UINT8_MAX);
    OWNERSHIP_CHECK(
        !ZoneScriptStringOwnershipControllerTestAccess::
            TryBeginRegistryCallbackWindow(
                &fixture->ownership, callbackPhase));
    OWNERSHIP_CHECK(fixture->ownership.poisoned());
    ZoneScriptStringOwnershipControllerTestAccess::SetPhase(
        &fixture->ownership, callbackPhase);
    ZoneScriptStringOwnershipControllerTestAccess::SetReserved(
        &fixture->ownership, windowWitness, windowWitness);

    ZoneScriptStringOwnershipControllerTestAccess::SetTransactionSerial(
        &fixture->ownership, wrongSerial);
    rejectedSerial = UINT32_C(0xDDBBCCAA);
    rejectedPurpose = RegistryCallbackPurpose::Unloading;
    rejectedWindowWitness = UINT8_C(0x5A);
    OWNERSHIP_CHECK(
        !ZoneScriptStringOwnershipControllerTestAccess::
            TrySnapshotRegistryCallbackTransaction(
                &fixture->ownership,
                fixture->key,
                &rejectedSerial,
                &rejectedPurpose,
                &rejectedWindowWitness));
    OWNERSHIP_CHECK(rejectedSerial == UINT32_C(0xDDBBCCAA));
    OWNERSHIP_CHECK(
        rejectedPurpose == RegistryCallbackPurpose::Unloading);
    OWNERSHIP_CHECK(rejectedWindowWitness == UINT8_C(0x5A));
    ZoneScriptStringOwnershipControllerTestAccess::SetTransactionSerial(
        &fixture->ownership, serial);

    // The transaction serializer intentionally blocks a foreign thread until
    // the callback owner releases it. Launch one attempt from admission, but
    // join only after the outer commit has finished; joining here would wait
    // on the very transaction retained by this callback.
    if (expectedPurpose == RegistryCallbackPurpose::Admitting)
    {
        fixture->registryCallbackForeignThread = std::thread([fixture]() {
            fixture->registryCallbackForeignReady.store(
                true, std::memory_order_release);
            fixture->registryCallbackForeignAccepted =
            ZoneScriptStringOwnershipControllerTestAccess::
                TrySnapshotRegistryCallbackTransaction(
                    &fixture->ownership,
                    fixture->key,
                    &fixture->registryCallbackForeignSerial,
                    &fixture->registryCallbackForeignPurpose,
                    &fixture->registryCallbackForeignWitness);
            fixture->registryCallbackForeignDone.store(
                true, std::memory_order_release);
        });
        while (!fixture->registryCallbackForeignReady.load(
            std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        OWNERSHIP_CHECK(
            !fixture->registryCallbackForeignDone.load(
                std::memory_order_acquire));
    }

    OWNERSHIP_CHECK(
        ZoneScriptStringOwnershipControllerTestAccess::
            AuthenticatesRegistryCallbackTransaction(
                &fixture->ownership,
                fixture->key,
                serial,
                expectedPurpose,
                windowWitness));
}

struct RollbackDriver final
{
    Fixture *fixture = nullptr;
    ZoneScriptStringOwnershipController *reentryController = nullptr;
    bool attemptBeginReentry = false;
    bool attemptedBeginReentry = false;
    ZoneScriptStringOwnershipStatus beginReentryStatus =
        ZoneScriptStringOwnershipStatus::Success;
    std::size_t ensureCalls = 0;
    bool retryFirstEnsure = false;
    bool retriedEnsure = false;
    lifecycle::ZoneLoadCleanupOperation retryOperation =
        lifecycle::ZoneLoadCleanupOperation::ReleaseSlot;
    bool retriedCleanup = false;
    std::array<lifecycle::ZoneLoadCleanupOperation, 16> operations{};
    std::size_t operationCount = 0;
};

lifecycle::ZoneLoadCleanupCallbackStatus PerformCleanup(
    void *context,
    lifecycle::ZoneLoadCleanupOperation operation) noexcept;

controller::ZoneScriptStringUnpublishStatus EnsureUnreachable(
    void *const context) noexcept
{
    auto &driver = *static_cast<RollbackDriver *>(context);
    ++driver.ensureCalls;
    if (driver.fixture)
    {
        const auto purpose = driver.fixture->ownership.phase()
                == ZoneScriptStringOwnershipPhase::UnpublishingCallback
            ? RegistryCallbackPurpose::Unpublishing
            : RegistryCallbackPurpose::Cleaning;
        ObserveRegistryCallbackAuthorization(driver.fixture, purpose);
    }
    if (driver.attemptBeginReentry && !driver.attemptedBeginReentry)
    {
        driver.attemptedBeginReentry = true;
        driver.beginReentryStatus =
            controller::TryBeginZoneScriptStringRollback(
                driver.reentryController,
                {&driver, EnsureUnreachable, PerformCleanup});
    }
    if (driver.retryFirstEnsure && !driver.retriedEnsure)
    {
        driver.retriedEnsure = true;
        return controller::ZoneScriptStringUnpublishStatus::Retry;
    }
    return controller::ZoneScriptStringUnpublishStatus::Success;
}

lifecycle::ZoneLoadCleanupCallbackStatus PerformCleanup(
    void *const context,
    const lifecycle::ZoneLoadCleanupOperation operation) noexcept
{
    auto &driver = *static_cast<RollbackDriver *>(context);
    if (driver.fixture)
    {
        ObserveRegistryCallbackAuthorization(
            driver.fixture, RegistryCallbackPurpose::Cleaning);
    }
    OWNERSHIP_CHECK(
        operation
        != lifecycle::ZoneLoadCleanupOperation::
            MakePartialAssetsAndStagedReferencesUnreachable);
    OWNERSHIP_CHECK(driver.operationCount < driver.operations.size());
    if (driver.operationCount < driver.operations.size())
        driver.operations[driver.operationCount++] = operation;
    if (operation == driver.retryOperation && !driver.retriedCleanup)
    {
        driver.retriedCleanup = true;
        return lifecycle::ZoneLoadCleanupCallbackStatus::Retry;
    }
    return lifecycle::ZoneLoadCleanupCallbackStatus::Success;
}

controller::ZoneScriptStringRollbackCallbacks MakeRollbackCallbacks(
    RollbackDriver *const driver) noexcept
{
    return {driver, EnsureUnreachable, PerformCleanup};
}

struct AdmissionProbe final
{
    Fixture *fixture = nullptr;
    bool called = false;
    ZoneScriptStringOwnershipStatus reentryStatus =
        ZoneScriptStringOwnershipStatus::Success;
};

void AdmitLive(void *const context) noexcept
{
    auto &probe = *static_cast<AdmissionProbe *>(context);
    probe.called = true;
    OWNERSHIP_CHECK(probe.fixture != nullptr);
    if (!probe.fixture)
        return;
    OWNERSHIP_CHECK(
        probe.fixture->ownership.phase()
        == ZoneScriptStringOwnershipPhase::Admitting);
    OWNERSHIP_CHECK(probe.fixture->ownership.serializerRetained());
    OWNERSHIP_CHECK(
        probe.fixture->lifecycleSlot.phase()
        == lifecycle::ZoneLoadContextPhase::Live);
    OWNERSHIP_CHECK(
        probe.fixture->stringJournal.phase()
        == journal::ScriptStringJournalPhase::Committed);

    ObserveRegistryCallbackAuthorization(
        probe.fixture, RegistryCallbackPurpose::Admitting);

    std::uint32_t transactionSerial = UINT32_C(0xA55AA55A);
    OWNERSHIP_CHECK(
        !probe.fixture->ownership.trySnapshotRegistryTransaction(
            probe.fixture->key, &transactionSerial));
    OWNERSHIP_CHECK(transactionSerial == UINT32_C(0xA55AA55A));

    std::uint32_t output = 0xABCDu;
    probe.reentryStatus = controller::TryStageZoneScriptString(
        &probe.fixture->ownership,
        {"reenter\0", 8, 1},
        &output);
    OWNERSHIP_CHECK(output == 0xABCDu);
}

void AdmitNoop(void *) noexcept
{
}

struct AdmissionCorruptionProbe final
{
    ZoneScriptStringOwnershipController *controller = nullptr;
    bool called = false;
};

void CorruptAdmissionWindow(void *const context) noexcept
{
    auto &probe = *static_cast<AdmissionCorruptionProbe *>(context);
    probe.called = true;
    OWNERSHIP_CHECK(probe.controller != nullptr);
    ZoneScriptStringOwnershipControllerTestAccess::SetReserved(
        probe.controller, 1, 2);
}

bool AuthenticatesFixtureStorage(
    const Fixture &fixture,
    const std::uint32_t expectedCount,
    const ZoneScriptStringStorageBindingPhase phase) noexcept
{
    return controller::AuthenticateZoneScriptStringOwnershipStorage(
        fixture.ownership,
        &fixture.lifecycleSlot,
        fixture.key,
        &fixture.stringJournal,
        fixture.storage.data(),
        static_cast<std::uint32_t>(fixture.storage.size()),
        expectedCount,
        phase);
}

bool PrepareEmptyFixture(Fixture &fixture, const std::uint32_t slot)
{
    if (!fixture.begin(0, slot))
        return false;
    return controller::TrySealZoneScriptStrings(&fixture.ownership)
                == ZoneScriptStringOwnershipStatus::Success
        && controller::TryBeginZoneScriptStringTransfer(&fixture.ownership)
                == ZoneScriptStringOwnershipStatus::Success
        && controller::TryTransferNextZoneScriptString(&fixture.ownership)
                == ZoneScriptStringOwnershipStatus::Success
        && controller::TryPrepareZoneScriptStringCommit(&fixture.ownership)
                == ZoneScriptStringOwnershipStatus::Success;
}

bool CommitEmptyFixture(Fixture &fixture, const std::uint32_t slot)
{
    return PrepareEmptyFixture(fixture, slot)
        && controller::TryCommitZoneScriptStringsAndAdmit(
               &fixture.ownership, {nullptr, AdmitNoop})
            == ZoneScriptStringOwnershipStatus::Success;
}

struct LiveUnloadDriver final
{
    Fixture *fixture = nullptr;
    bool attemptReentry = false;
    bool attemptedReentry = false;
    ZoneScriptStringOwnershipStatus reentryStatus =
        ZoneScriptStringOwnershipStatus::Success;
    lifecycle::ZoneLoadCleanupOperation retryOperation =
        lifecycle::ZoneLoadCleanupOperation::ReleaseSlot;
    bool retried = false;
    bool freedPhysicalMemory = false;
    bool observedContextAfterFree = false;
    std::array<lifecycle::ZoneLoadCleanupOperation, 12> operations{};
    std::size_t operationCount = 0;
};

lifecycle::ZoneLoadCleanupCallbackStatus PerformLiveUnload(
    void *const context,
    const lifecycle::ZoneLoadCleanupOperation operation) noexcept
{
    auto &driver = *static_cast<LiveUnloadDriver *>(context);
    if (driver.fixture)
    {
        ObserveRegistryCallbackAuthorization(
            driver.fixture, RegistryCallbackPurpose::Unloading);
    }
    OWNERSHIP_CHECK(driver.fixture != nullptr);
    OWNERSHIP_CHECK(driver.operationCount < driver.operations.size());
    if (driver.operationCount < driver.operations.size())
        driver.operations[driver.operationCount++] = operation;
    if (driver.freedPhysicalMemory)
        driver.observedContextAfterFree = true;
    if (operation
        == lifecycle::ZoneLoadCleanupOperation::FreePhysicalMemory)
    {
        driver.freedPhysicalMemory = true;
    }
    if (driver.attemptReentry && !driver.attemptedReentry && driver.fixture)
    {
        driver.attemptedReentry = true;
        driver.reentryStatus =
            controller::TryUnloadLiveZoneScriptStringOwnership(
                &driver.fixture->ownership,
                &driver.fixture->lifecycleSlot,
                driver.fixture->key,
                {&driver, PerformLiveUnload});
    }
    if (operation == driver.retryOperation && !driver.retried)
    {
        driver.retried = true;
        return lifecycle::ZoneLoadCleanupCallbackStatus::Retry;
    }
    return lifecycle::ZoneLoadCleanupCallbackStatus::Success;
}

void StageExpected(
    Fixture &fixture,
    const std::uint32_t expectedId,
    const int type = 3)
{
    std::uint32_t output = 0;
    OWNERSHIP_CHECK(
        controller::TryStageZoneScriptString(
            &fixture.ownership,
            {"source\0", 7, type},
            &output)
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(output == expectedId);
}

void TestCommittedAdmission()
{
    ResetBackend();
    Fixture fixture;
    OWNERSHIP_CHECK(fixture.begin(2, 3));
    OWNERSHIP_CHECK(fixture.ownership.serializerRetained());
    OWNERSHIP_CHECK(
        fixture.ownership.phase()
        == ZoneScriptStringOwnershipPhase::Staging);

    std::uint32_t transactionSerial = UINT32_C(0xA55AA55A);
    lifecycle::ZoneLoadContextKey wrongKey = fixture.key;
    ++wrongKey.generation;
    OWNERSHIP_CHECK(
        !fixture.ownership.trySnapshotRegistryTransaction(
            wrongKey, &transactionSerial));
    OWNERSHIP_CHECK(transactionSerial == UINT32_C(0xA55AA55A));
    OWNERSHIP_CHECK(
        !fixture.ownership.trySnapshotRegistryTransaction(
            fixture.key, nullptr));
    OWNERSHIP_CHECK(
        fixture.ownership.trySnapshotRegistryTransaction(
            fixture.key, &transactionSerial));
    OWNERSHIP_CHECK(transactionSerial != 0);
    OWNERSHIP_CHECK(
        fixture.ownership.authenticatesRegistryTransaction(
            fixture.key, transactionSerial));
    OWNERSHIP_CHECK(
        !fixture.ownership.authenticatesRegistryTransaction(
            fixture.key, transactionSerial + 1));

    PushAcquire(script_string::AcquireStatus::InvalidArgumentNoChange, 0);
    std::uint32_t rejectedOutput = 0xDEADBEEFu;
    OWNERSHIP_CHECK(
        controller::TryStageZoneScriptString(
            &fixture.ownership,
            {"bad\0", 4, 1},
            &rejectedOutput)
        == ZoneScriptStringOwnershipStatus::Rejected);
    OWNERSHIP_CHECK(rejectedOutput == 0xDEADBEEFu);

    PushAcquire(script_string::AcquireStatus::Acquired, 71);
    PushAcquire(script_string::AcquireStatus::Acquired, 72);
    StageExpected(fixture, 71);
    StageExpected(fixture, 72);
    OWNERSHIP_CHECK(
        controller::TrySealZoneScriptStrings(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(
        controller::TryBeginZoneScriptStringTransfer(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::Success);

    PushTransfer(script_string::TransferStatus::DatabaseUserClaimed);
    PushTransfer(script_string::TransferStatus::DuplicateReleased);
    OWNERSHIP_CHECK(
        controller::TryTransferNextZoneScriptString(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(
        controller::TryTransferNextZoneScriptString(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(
        fixture.ownership.phase()
        == ZoneScriptStringOwnershipPhase::Transferred);
    OWNERSHIP_CHECK(
        controller::TryPrepareZoneScriptStringCommit(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::Success);

    AdmissionProbe admission{&fixture};
    OWNERSHIP_CHECK(
        controller::TryCommitZoneScriptStringsAndAdmit(
            &fixture.ownership,
            {&admission, AdmitLive})
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(fixture.registryCallbackForeignThread.joinable());
    if (fixture.registryCallbackForeignThread.joinable())
        fixture.registryCallbackForeignThread.join();
    OWNERSHIP_CHECK(
        fixture.registryCallbackForeignDone.load(std::memory_order_acquire));
    OWNERSHIP_CHECK(!fixture.registryCallbackForeignAccepted);
    OWNERSHIP_CHECK(
        fixture.registryCallbackForeignSerial == UINT32_C(0x13572468));
    OWNERSHIP_CHECK(
        fixture.registryCallbackForeignPurpose
        == RegistryCallbackPurpose::Unloading);
    OWNERSHIP_CHECK(
        fixture.registryCallbackForeignWitness == UINT8_C(0xA5));
    OWNERSHIP_CHECK(admission.called);
    OWNERSHIP_CHECK(
        fixture.registryCallbackPurposeCalls[static_cast<std::size_t>(
            RegistryCallbackPurpose::Admitting)] == 1);
    OWNERSHIP_CHECK(
        admission.reentryStatus == ZoneScriptStringOwnershipStatus::Busy);
    OWNERSHIP_CHECK(
        fixture.ownership.phase() == ZoneScriptStringOwnershipPhase::Live);
    OWNERSHIP_CHECK(!fixture.ownership.serializerRetained());
    const std::uint32_t liveSentinel = UINT32_C(0x5AA55AA5);
    transactionSerial = liveSentinel;
    OWNERSHIP_CHECK(
        !fixture.ownership.trySnapshotRegistryTransaction(
            fixture.key, &transactionSerial));
    OWNERSHIP_CHECK(transactionSerial == liveSentinel);
    OWNERSHIP_CHECK(
        !fixture.ownership.authenticatesRegistryTransaction(
            fixture.key, 1));
    OWNERSHIP_CHECK(
        fixture.lifecycleSlot.phase() == lifecycle::ZoneLoadContextPhase::Live);
    OWNERSHIP_CHECK(
        fixture.stringJournal.phase()
        == journal::ScriptStringJournalPhase::Committed);
    OWNERSHIP_CHECK(fixture.stringJournal.storage() == nullptr);
    OWNERSHIP_CHECK(
        fixture.storage[0].state
        == ScriptStringJournalEntryState::DatabaseUserClaimed);
    OWNERSHIP_CHECK(
        fixture.storage[1].state
        == ScriptStringJournalEntryState::DuplicateReleased);
}

void TestPartialRollbackAndCleanupRetry()
{
    ResetBackend();
    Fixture fixture;
    OWNERSHIP_CHECK(fixture.begin(3, 4));
    PushAcquire(script_string::AcquireStatus::Acquired, 101);
    PushAcquire(script_string::AcquireStatus::Acquired, 202);
    PushAcquire(script_string::AcquireStatus::Acquired, 303);
    StageExpected(fixture, 101);
    StageExpected(fixture, 202);
    StageExpected(fixture, 303);
    OWNERSHIP_CHECK(
        controller::TrySealZoneScriptStrings(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(
        controller::TryBeginZoneScriptStringTransfer(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::Success);
    PushTransfer(script_string::TransferStatus::DatabaseUserClaimed);
    PushTransfer(script_string::TransferStatus::DuplicateReleased);
    OWNERSHIP_CHECK(
        controller::TryTransferNextZoneScriptString(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(
        controller::TryTransferNextZoneScriptString(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::Success);

    RollbackDriver driver{};
    driver.fixture = &fixture;
    driver.reentryController = &fixture.ownership;
    driver.attemptBeginReentry = true;
    driver.retryFirstEnsure = true;
    driver.retryOperation = lifecycle::ZoneLoadCleanupOperation::ReleaseGeometry;
    const controller::ZoneScriptStringRollbackCallbacks callbacks =
        MakeRollbackCallbacks(&driver);
    OWNERSHIP_CHECK(
        controller::TryBeginZoneScriptStringRollback(
            &fixture.ownership, callbacks)
        == ZoneScriptStringOwnershipStatus::Retry);
    OWNERSHIP_CHECK(
        fixture.ownership.phase()
        == ZoneScriptStringOwnershipPhase::Unpublishing);
    OWNERSHIP_CHECK(driver.attemptedBeginReentry);
    OWNERSHIP_CHECK(
        driver.beginReentryStatus == ZoneScriptStringOwnershipStatus::Busy);
    RollbackDriver swappedDriver{};
    OWNERSHIP_CHECK(
        controller::TryBeginZoneScriptStringRollback(
            &fixture.ownership,
            MakeRollbackCallbacks(&swappedDriver))
        == ZoneScriptStringOwnershipStatus::InvalidState);
    OWNERSHIP_CHECK(
        controller::TryBeginZoneScriptStringRollback(
            &fixture.ownership, callbacks)
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(driver.ensureCalls == 2);
    OWNERSHIP_CHECK(
        fixture.lifecycleSlot.phase()
        == lifecycle::ZoneLoadContextPhase::Abandoning);

    PushOrdinary(script_string::ReleaseStatus::Success);
    PushDatabase(script_string::ReleaseStatus::Success);
    OWNERSHIP_CHECK(
        controller::TryRollbackNextZoneScriptString(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(backend.ordinaryCalls == 1);
    OWNERSHIP_CHECK(backend.ordinaryIds[0] == 303);
    OWNERSHIP_CHECK(
        controller::TryRollbackNextZoneScriptString(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(backend.ordinaryCalls == 1);
    OWNERSHIP_CHECK(backend.databaseCalls == 0);
    OWNERSHIP_CHECK(
        controller::TryRollbackNextZoneScriptString(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(backend.databaseCalls == 1);
    OWNERSHIP_CHECK(backend.databaseIds[0] == 101);
    OWNERSHIP_CHECK(
        fixture.ownership.phase()
        == ZoneScriptStringOwnershipPhase::OwnershipRolledBack);
    OWNERSHIP_CHECK(fixture.stringJournal.storage() == nullptr);

    OWNERSHIP_CHECK(
        controller::TryFinishZoneScriptStringAbandonment(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::Retry);
    OWNERSHIP_CHECK(fixture.ownership.serializerRetained());
    OWNERSHIP_CHECK(driver.ensureCalls == 3);
    const std::size_t callsBeforeRetry = driver.operationCount;
    OWNERSHIP_CHECK(
        controller::TryFinishZoneScriptStringAbandonment(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(driver.operationCount > callsBeforeRetry);
    OWNERSHIP_CHECK(
        fixture.registryCallbackPurposeCalls[static_cast<std::size_t>(
            RegistryCallbackPurpose::Unpublishing)] == 2);
    OWNERSHIP_CHECK(
        fixture.registryCallbackPurposeCalls[static_cast<std::size_t>(
            RegistryCallbackPurpose::Cleaning)] > 0);
    OWNERSHIP_CHECK(
        fixture.ownership.phase()
        == ZoneScriptStringOwnershipPhase::Abandoned);
    OWNERSHIP_CHECK(!fixture.ownership.serializerRetained());
    OWNERSHIP_CHECK(
        fixture.lifecycleSlot.phase() == lifecycle::ZoneLoadContextPhase::Empty);
    OWNERSHIP_CHECK(
        fixture.lifecycleSlot.terminalKind()
        == lifecycle::ZoneLoadTerminalKind::Abandoned);
    OWNERSHIP_CHECK(
        controller::TryFinishZoneScriptStringAbandonment(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(
        controller::TryResetTerminalZoneScriptStringOwnership(
            &fixture.ownership,
            &fixture.lifecycleSlot,
            fixture.key,
            lifecycle::ZoneLoadTerminalKind::Abandoned)
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(
        fixture.ownership.phase() == ZoneScriptStringOwnershipPhase::Empty);
}

void TestBindingAndForeignThreadRejection()
{
    ResetBackend();
    Fixture fixture;
    OWNERSHIP_CHECK(fixture.begin(1, 5));
    PushAcquire(script_string::AcquireStatus::Acquired, 404);

    ScriptStringJournal swappedJournal{};
    ZoneScriptStringOwnershipControllerTestAccess::SetJournal(
        &fixture.ownership, &swappedJournal);
    std::uint32_t output = 0x55AAu;
    OWNERSHIP_CHECK(
        controller::TryStageZoneScriptString(
            &fixture.ownership, {"x\0", 2, 0}, &output)
        == ZoneScriptStringOwnershipStatus::InvalidState);
    OWNERSHIP_CHECK(output == 0x55AAu);
    ZoneScriptStringOwnershipControllerTestAccess::SetJournal(
        &fixture.ownership, &fixture.stringJournal);

    ScriptStringJournalEntry swappedStorage{};
    ZoneScriptStringOwnershipControllerTestAccess::SetStorage(
        &fixture.ownership, &swappedStorage);
    OWNERSHIP_CHECK(
        controller::TryStageZoneScriptString(
            &fixture.ownership, {"x\0", 2, 0}, &output)
        == ZoneScriptStringOwnershipStatus::InvalidState);
    ZoneScriptStringOwnershipControllerTestAccess::SetStorage(
        &fixture.ownership, fixture.storage.data());

    const lifecycle::ZoneLoadContextKey originalKey = fixture.key;
    ZoneScriptStringOwnershipControllerTestAccess::SetKey(
        &fixture.ownership,
        {fixture.key.generation + 1, fixture.key.slot, 0});
    OWNERSHIP_CHECK(
        controller::TryStageZoneScriptString(
            &fixture.ownership, {"x\0", 2, 0}, &output)
        == ZoneScriptStringOwnershipStatus::StaleKey);
    ZoneScriptStringOwnershipControllerTestAccess::SetKey(
        &fixture.ownership, originalKey);

    ZoneScriptStringOwnershipController secondController{};
    ScriptStringJournal secondJournal{};
    ScriptStringJournalEntry secondStorage{};
    OWNERSHIP_CHECK(
        controller::TryBeginZoneScriptStringOwnership(
            &secondController,
            &fixture.lifecycleSlot,
            fixture.key,
            &secondJournal,
            &secondStorage,
            1,
            1)
        == ZoneScriptStringOwnershipStatus::Busy);

    std::atomic<bool> foreignReady{false};
    std::atomic<bool> foreignDone{false};
    std::atomic<int> foreignStatus{
        static_cast<int>(ZoneScriptStringOwnershipStatus::Success)};
    std::uint32_t foreignOutput = 0x12345678u;
    std::thread foreign([&]() {
        foreignReady.store(true, std::memory_order_release);
        const ZoneScriptStringOwnershipStatus status =
            controller::TryStageZoneScriptString(
                &fixture.ownership,
                {"foreign\0", 8, 0},
                &foreignOutput);
        foreignStatus.store(static_cast<int>(status), std::memory_order_release);
        foreignDone.store(true, std::memory_order_release);
    });
    while (!foreignReady.load(std::memory_order_acquire))
        std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    OWNERSHIP_CHECK(!foreignDone.load(std::memory_order_acquire));

    RollbackDriver driver{};
    OWNERSHIP_CHECK(
        controller::TryBeginZoneScriptStringRollback(
            &fixture.ownership,
            MakeRollbackCallbacks(&driver))
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(
        controller::TryFinishZoneScriptStringAbandonment(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::Success);
    foreign.join();
    OWNERSHIP_CHECK(foreignDone.load(std::memory_order_acquire));
    OWNERSHIP_CHECK(
        foreignStatus.load(std::memory_order_acquire)
        == static_cast<int>(ZoneScriptStringOwnershipStatus::InvalidPhase));
    OWNERSHIP_CHECK(foreignOutput == 0x12345678u);
}

void TestBeginFailureReleasesSerializer()
{
    ResetBackend();
    lifecycle::ZoneLoadContextSlot slot{};
    lifecycle::ZoneLoadContextKey key{};
    OWNERSHIP_CHECK(
        lifecycle::TryInitializeZoneLoadContextSlot(&slot, 7)
        == lifecycle::ZoneLoadContextStatus::Success);
    OWNERSHIP_CHECK(
        lifecycle::TryClaimZoneLoadContext(&slot, &key)
        == lifecycle::ZoneLoadContextStatus::Success);

    ZoneScriptStringOwnershipController rejected{};
    ScriptStringJournal journalValue{};
    ScriptStringJournalEntry storage{};
    const lifecycle::ZoneLoadContextKey stale{
        key.generation + 1,
        key.slot,
        0};
    OWNERSHIP_CHECK(
        controller::TryBeginZoneScriptStringOwnership(
            &rejected,
            &slot,
            stale,
            &journalValue,
            &storage,
            1,
            1)
        == ZoneScriptStringOwnershipStatus::StaleKey);
    OWNERSHIP_CHECK(
        rejected.phase() == ZoneScriptStringOwnershipPhase::Empty);
    OWNERSHIP_CHECK(!rejected.serializerRetained());

    ZoneScriptStringOwnershipController accepted{};
    OWNERSHIP_CHECK(
        controller::TryBeginZoneScriptStringOwnership(
            &accepted,
            &slot,
            key,
            &journalValue,
            &storage,
            1,
            1)
        == ZoneScriptStringOwnershipStatus::Success);
    RollbackDriver driver{};
    OWNERSHIP_CHECK(
        controller::TryBeginZoneScriptStringRollback(
            &accepted,
            MakeRollbackCallbacks(&driver))
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(
        controller::TryFinishZoneScriptStringAbandonment(&accepted)
        == ZoneScriptStringOwnershipStatus::Success);
}

void TestDurableStorageIdentityAuthentication()
{
    ResetBackend();
    Fixture fixture;
    OWNERSHIP_CHECK(fixture.begin(0, 11));
    OWNERSHIP_CHECK(
        AuthenticatesFixtureStorage(
            fixture, 0, ZoneScriptStringStorageBindingPhase::Attached));
    OWNERSHIP_CHECK(
        !AuthenticatesFixtureStorage(
            fixture, 0, ZoneScriptStringStorageBindingPhase::Detached));

    ScriptStringJournal foreignJournal{};
    std::array<ScriptStringJournalEntry, 4> foreignStorage{};
    OWNERSHIP_CHECK(
        journal::TryInitializeScriptStringJournal(
            &foreignJournal,
            fixture.key,
            foreignStorage.data(),
            static_cast<std::uint32_t>(foreignStorage.size()),
            0)
        == journal::ScriptStringJournalStatus::Success);
    OWNERSHIP_CHECK(
        !controller::AuthenticateZoneScriptStringOwnershipStorage(
            fixture.ownership,
            &fixture.lifecycleSlot,
            fixture.key,
            &foreignJournal,
            foreignStorage.data(),
            static_cast<std::uint32_t>(foreignStorage.size()),
            0,
            ZoneScriptStringStorageBindingPhase::Attached));
    OWNERSHIP_CHECK(
        !controller::AuthenticateZoneScriptStringOwnershipStorage(
            fixture.ownership,
            &fixture.lifecycleSlot,
            {fixture.key.generation + 1, fixture.key.slot, 0},
            &fixture.stringJournal,
            fixture.storage.data(),
            static_cast<std::uint32_t>(fixture.storage.size()),
            0,
            ZoneScriptStringStorageBindingPhase::Attached));
    lifecycle::ZoneLoadContextSlot foreignLifecycle{};
    OWNERSHIP_CHECK(
        !controller::AuthenticateZoneScriptStringOwnershipStorage(
            fixture.ownership,
            &foreignLifecycle,
            fixture.key,
            &fixture.stringJournal,
            fixture.storage.data(),
            static_cast<std::uint32_t>(fixture.storage.size()),
            0,
            ZoneScriptStringStorageBindingPhase::Attached));
    OWNERSHIP_CHECK(
        !controller::AuthenticateZoneScriptStringOwnershipStorage(
            fixture.ownership,
            &fixture.lifecycleSlot,
            fixture.key,
            &fixture.stringJournal,
            fixture.storage.data() + 1,
            static_cast<std::uint32_t>(fixture.storage.size()),
            0,
            ZoneScriptStringStorageBindingPhase::Attached));
    OWNERSHIP_CHECK(
        !controller::AuthenticateZoneScriptStringOwnershipStorage(
            fixture.ownership,
            &fixture.lifecycleSlot,
            fixture.key,
            &fixture.stringJournal,
            fixture.storage.data(),
            static_cast<std::uint32_t>(fixture.storage.size()) - 1,
            0,
            ZoneScriptStringStorageBindingPhase::Attached));
    OWNERSHIP_CHECK(
        !controller::AuthenticateZoneScriptStringOwnershipStorage(
            fixture.ownership,
            &fixture.lifecycleSlot,
            fixture.key,
            &fixture.stringJournal,
            fixture.storage.data(),
            static_cast<std::uint32_t>(fixture.storage.size()),
            1,
            ZoneScriptStringStorageBindingPhase::Attached));
    OWNERSHIP_CHECK(
        !controller::AuthenticateZoneScriptStringOwnershipStorage(
            fixture.ownership,
            &fixture.lifecycleSlot,
            fixture.key,
            &fixture.stringJournal,
            fixture.storage.data(),
            static_cast<std::uint32_t>(fixture.storage.size()),
            0,
            static_cast<ZoneScriptStringStorageBindingPhase>(0xFF)));

    ZoneScriptStringOwnershipControllerTestAccess::SetPlacementStorage(
        &fixture.ownership,
        &foreignJournal,
        foreignStorage.data(),
        static_cast<std::uint32_t>(foreignStorage.size()),
        0);
    OWNERSHIP_CHECK(
        !fixture.ownership.canonicalForBinding(
            &fixture.lifecycleSlot, fixture.key));
    OWNERSHIP_CHECK(
        !AuthenticatesFixtureStorage(
            fixture, 0, ZoneScriptStringStorageBindingPhase::Attached));
    ZoneScriptStringOwnershipControllerTestAccess::SetPlacementStorage(
        &fixture.ownership,
        &fixture.stringJournal,
        fixture.storage.data(),
        static_cast<std::uint32_t>(fixture.storage.size()),
        0);
    OWNERSHIP_CHECK(
        fixture.ownership.canonicalForBinding(
            &fixture.lifecycleSlot, fixture.key));

    OWNERSHIP_CHECK(
        controller::TrySealZoneScriptStrings(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(
        controller::TryBeginZoneScriptStringTransfer(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(
        controller::TryTransferNextZoneScriptString(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(
        controller::TryPrepareZoneScriptStringCommit(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(
        controller::TryCommitZoneScriptStringsAndAdmit(
            &fixture.ownership, {nullptr, AdmitNoop})
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(
        AuthenticatesFixtureStorage(
            fixture, 0, ZoneScriptStringStorageBindingPhase::Detached));
    OWNERSHIP_CHECK(
        !AuthenticatesFixtureStorage(
            fixture, 0, ZoneScriptStringStorageBindingPhase::Attached));

    // Detached canonical state alone cannot distinguish a same-key placement
    // substitution. The exact outer expectation remains authoritative.
    ZoneScriptStringOwnershipControllerTestAccess::SetPlacementStorage(
        &fixture.ownership,
        &foreignJournal,
        foreignStorage.data(),
        static_cast<std::uint32_t>(foreignStorage.size()),
        0);
    OWNERSHIP_CHECK(
        fixture.ownership.canonicalForBinding(
            &fixture.lifecycleSlot, fixture.key));
    OWNERSHIP_CHECK(
        !AuthenticatesFixtureStorage(
            fixture, 0, ZoneScriptStringStorageBindingPhase::Detached));
    ZoneScriptStringOwnershipControllerTestAccess::SetPlacementStorage(
        &fixture.ownership,
        &fixture.stringJournal,
        fixture.storage.data(),
        static_cast<std::uint32_t>(fixture.storage.size()),
        0);

    LiveUnloadDriver unload{};
    unload.fixture = &fixture;
    OWNERSHIP_CHECK(
        controller::TryUnloadLiveZoneScriptStringOwnership(
            &fixture.ownership,
            &fixture.lifecycleSlot,
            fixture.key,
            {&unload, PerformLiveUnload})
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(
        fixture.ownership.phase()
        == ZoneScriptStringOwnershipPhase::Unloaded);
    OWNERSHIP_CHECK(
        AuthenticatesFixtureStorage(
            fixture, 0, ZoneScriptStringStorageBindingPhase::Detached));
    OWNERSHIP_CHECK(
        controller::TryResetTerminalZoneScriptStringOwnership(
            &fixture.ownership,
            &fixture.lifecycleSlot,
            fixture.key,
            lifecycle::ZoneLoadTerminalKind::Unloaded)
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(fixture.ownership.isEmptyCanonical());
    OWNERSHIP_CHECK(
        !AuthenticatesFixtureStorage(
            fixture, 0, ZoneScriptStringStorageBindingPhase::Detached));
}

void TestAbandonedReceiptAuthentication()
{
    ResetBackend();
    Fixture fixture;
    OWNERSHIP_CHECK(fixture.begin(0, 8));
    RollbackDriver driver{};
    OWNERSHIP_CHECK(
        controller::TryBeginZoneScriptStringRollback(
            &fixture.ownership,
            MakeRollbackCallbacks(&driver))
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(
        AuthenticatesFixtureStorage(
            fixture, 0, ZoneScriptStringStorageBindingPhase::Detached));
    OWNERSHIP_CHECK(
        controller::TryFinishZoneScriptStringAbandonment(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(
        AuthenticatesFixtureStorage(
            fixture, 0, ZoneScriptStringStorageBindingPhase::Detached));
    OWNERSHIP_CHECK(
        controller::TryFinishZoneScriptStringAbandonment(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::Success);

    ScriptStringJournal swappedJournal{};
    ZoneScriptStringOwnershipControllerTestAccess::SetJournal(
        &fixture.ownership, &swappedJournal);
    OWNERSHIP_CHECK(
        controller::TryFinishZoneScriptStringAbandonment(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::InvalidState);
    OWNERSHIP_CHECK(
        controller::TryResetTerminalZoneScriptStringOwnership(
            &fixture.ownership,
            &fixture.lifecycleSlot,
            fixture.key,
            lifecycle::ZoneLoadTerminalKind::Abandoned)
        == ZoneScriptStringOwnershipStatus::InvalidState);
    OWNERSHIP_CHECK(
        fixture.ownership.phase()
        == ZoneScriptStringOwnershipPhase::Abandoned);
    OWNERSHIP_CHECK(fixture.ownership.key() == fixture.key);
    ZoneScriptStringOwnershipControllerTestAccess::SetJournal(
        &fixture.ownership, nullptr);

    ZoneScriptStringOwnershipControllerTestAccess::SetStorage(
        &fixture.ownership, fixture.storage.data());
    OWNERSHIP_CHECK(
        controller::TryFinishZoneScriptStringAbandonment(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::InvalidState);
    OWNERSHIP_CHECK(
        controller::TryResetTerminalZoneScriptStringOwnership(
            &fixture.ownership,
            &fixture.lifecycleSlot,
            fixture.key,
            lifecycle::ZoneLoadTerminalKind::Abandoned)
        == ZoneScriptStringOwnershipStatus::InvalidState);
    OWNERSHIP_CHECK(
        fixture.ownership.phase()
        == ZoneScriptStringOwnershipPhase::Abandoned);
    ZoneScriptStringOwnershipControllerTestAccess::SetStorage(
        &fixture.ownership, nullptr);

    const lifecycle::ZoneLoadContextKey originalKey = fixture.key;
    ZoneScriptStringOwnershipControllerTestAccess::SetKey(
        &fixture.ownership,
        {originalKey.generation + 1, originalKey.slot, 0});
    OWNERSHIP_CHECK(
        controller::TryFinishZoneScriptStringAbandonment(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::StaleKey);
    OWNERSHIP_CHECK(
        controller::TryResetTerminalZoneScriptStringOwnership(
            &fixture.ownership,
            &fixture.lifecycleSlot,
            originalKey,
            lifecycle::ZoneLoadTerminalKind::Abandoned)
        == ZoneScriptStringOwnershipStatus::StaleKey);
    OWNERSHIP_CHECK(
        fixture.ownership.phase()
        == ZoneScriptStringOwnershipPhase::Abandoned);
    ZoneScriptStringOwnershipControllerTestAccess::SetKey(
        &fixture.ownership, originalKey);

    lifecycle::ZoneLoadContextKey nextKey{};
    OWNERSHIP_CHECK(
        lifecycle::TryClaimZoneLoadContext(
            &fixture.lifecycleSlot, &nextKey)
        == lifecycle::ZoneLoadContextStatus::Success);
    OWNERSHIP_CHECK(nextKey.generation != originalKey.generation);
    OWNERSHIP_CHECK(
        controller::TryFinishZoneScriptStringAbandonment(&fixture.ownership)
        == ZoneScriptStringOwnershipStatus::StaleKey);
    OWNERSHIP_CHECK(
        controller::TryResetTerminalZoneScriptStringOwnership(
            &fixture.ownership,
            &fixture.lifecycleSlot,
            originalKey,
            lifecycle::ZoneLoadTerminalKind::Abandoned)
        == ZoneScriptStringOwnershipStatus::StaleKey);
    OWNERSHIP_CHECK(
        fixture.ownership.phase()
        == ZoneScriptStringOwnershipPhase::Abandoned);
    OWNERSHIP_CHECK(fixture.ownership.key() == originalKey);
}

void TestLiveUnloadRetryBindingAndTerminalReset()
{
    ResetBackend();
    Fixture fixture;
    OWNERSHIP_CHECK(CommitEmptyFixture(fixture, 10));
    OWNERSHIP_CHECK(
        fixture.ownership.canonicalForBinding(
            &fixture.lifecycleSlot, fixture.key));

    LiveUnloadDriver driver{};
    driver.fixture = &fixture;
    driver.attemptReentry = true;
    driver.retryOperation =
        lifecycle::ZoneLoadCleanupOperation::ReleaseGeometry;
    const lifecycle::ZoneLoadCleanupCallbacks callbacks{
        &driver,
        PerformLiveUnload};
    OWNERSHIP_CHECK(
        controller::TryUnloadLiveZoneScriptStringOwnership(
            &fixture.ownership,
            &fixture.lifecycleSlot,
            fixture.key,
            callbacks)
        == ZoneScriptStringOwnershipStatus::Retry);
    OWNERSHIP_CHECK(driver.attemptedReentry);
    OWNERSHIP_CHECK(
        driver.reentryStatus == ZoneScriptStringOwnershipStatus::Busy);
    OWNERSHIP_CHECK(
        fixture.ownership.phase()
        == ZoneScriptStringOwnershipPhase::Unloading);
    OWNERSHIP_CHECK(fixture.ownership.serializerRetained());
    OWNERSHIP_CHECK(
        fixture.lifecycleSlot.phase()
        == lifecycle::ZoneLoadContextPhase::Abandoning);
    OWNERSHIP_CHECK(
        fixture.lifecycleSlot.terminalKind()
        == lifecycle::ZoneLoadTerminalKind::Unloaded);
    OWNERSHIP_CHECK(
        fixture.ownership.canonicalForBinding(
            &fixture.lifecycleSlot, fixture.key));

    LiveUnloadDriver swapped{};
    swapped.fixture = &fixture;
    const std::size_t operationCount = driver.operationCount;
    OWNERSHIP_CHECK(
        controller::TryUnloadLiveZoneScriptStringOwnership(
            &fixture.ownership,
            &fixture.lifecycleSlot,
            fixture.key,
            {&swapped, PerformLiveUnload})
        == ZoneScriptStringOwnershipStatus::InvalidState);
    OWNERSHIP_CHECK(driver.operationCount == operationCount);
    OWNERSHIP_CHECK(fixture.ownership.serializerRetained());

    OWNERSHIP_CHECK(
        controller::TryUnloadLiveZoneScriptStringOwnership(
            &fixture.ownership,
            &fixture.lifecycleSlot,
            fixture.key,
            callbacks)
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(
        fixture.ownership.phase()
        == ZoneScriptStringOwnershipPhase::Unloaded);
    OWNERSHIP_CHECK(!fixture.ownership.serializerRetained());
    OWNERSHIP_CHECK(
        fixture.lifecycleSlot.phase()
        == lifecycle::ZoneLoadContextPhase::Empty);
    OWNERSHIP_CHECK(
        fixture.lifecycleSlot.terminalKind()
        == lifecycle::ZoneLoadTerminalKind::Unloaded);
    OWNERSHIP_CHECK(driver.freedPhysicalMemory);
    OWNERSHIP_CHECK(driver.observedContextAfterFree);
    OWNERSHIP_CHECK(
        fixture.registryCallbackPurposeCalls[static_cast<std::size_t>(
            RegistryCallbackPurpose::Unloading)] > 0);
    OWNERSHIP_CHECK(
        fixture.ownership.canonicalForBinding(
            &fixture.lifecycleSlot, fixture.key));
    OWNERSHIP_CHECK(
        controller::TryUnloadLiveZoneScriptStringOwnership(
            &fixture.ownership,
            &fixture.lifecycleSlot,
            fixture.key,
            {})
        == ZoneScriptStringOwnershipStatus::Success);

    const lifecycle::ZoneLoadContextKey stale{
        fixture.key.generation + 1,
        fixture.key.slot,
        0};
    OWNERSHIP_CHECK(
        controller::TryResetTerminalZoneScriptStringOwnership(
            &fixture.ownership,
            &fixture.lifecycleSlot,
            stale,
            lifecycle::ZoneLoadTerminalKind::Unloaded)
        == ZoneScriptStringOwnershipStatus::StaleKey);
    OWNERSHIP_CHECK(
        controller::TryResetTerminalZoneScriptStringOwnership(
            &fixture.ownership,
            &fixture.lifecycleSlot,
            fixture.key,
            lifecycle::ZoneLoadTerminalKind::Abandoned)
        == ZoneScriptStringOwnershipStatus::InvalidPhase);
    OWNERSHIP_CHECK(
        controller::TryResetTerminalZoneScriptStringOwnership(
            &fixture.ownership,
            &fixture.lifecycleSlot,
            fixture.key,
            lifecycle::ZoneLoadTerminalKind::Unloaded)
        == ZoneScriptStringOwnershipStatus::Success);
    OWNERSHIP_CHECK(fixture.ownership.isEmptyCanonical());
    OWNERSHIP_CHECK(
        controller::TryResetTerminalZoneScriptStringOwnership(
            &fixture.ownership,
            &fixture.lifecycleSlot,
            fixture.key,
            lifecycle::ZoneLoadTerminalKind::Unloaded)
        == ZoneScriptStringOwnershipStatus::Success);

    const lifecycle::ZoneLoadContextKey oldKey = fixture.key;
    lifecycle::ZoneLoadContextKey nextKey{};
    OWNERSHIP_CHECK(
        lifecycle::TryClaimZoneLoadContext(
            &fixture.lifecycleSlot, &nextKey)
        == lifecycle::ZoneLoadContextStatus::Success);
    OWNERSHIP_CHECK(nextKey.generation == oldKey.generation + 1);
    OWNERSHIP_CHECK(
        controller::TryResetTerminalZoneScriptStringOwnership(
            &fixture.ownership,
            &fixture.lifecycleSlot,
            oldKey,
            lifecycle::ZoneLoadTerminalKind::Unloaded)
        == ZoneScriptStringOwnershipStatus::StaleKey);
}

void TestAdmissionPostconditionFailsClosed()
{
    ResetBackend();
    Fixture fixture;
    OWNERSHIP_CHECK(PrepareEmptyFixture(fixture, 11));

    AdmissionCorruptionProbe corruption{&fixture.ownership};
    OWNERSHIP_CHECK(
        controller::TryCommitZoneScriptStringsAndAdmit(
            &fixture.ownership,
            {&corruption, CorruptAdmissionWindow})
        == ZoneScriptStringOwnershipStatus::UnsafeFailure);
    OWNERSHIP_CHECK(corruption.called);
    OWNERSHIP_CHECK(fixture.ownership.poisoned());
    OWNERSHIP_CHECK(fixture.ownership.serializerRetained());
    OWNERSHIP_CHECK(
        fixture.ownership.phase()
        == ZoneScriptStringOwnershipPhase::UnsafeFailure);
    OWNERSHIP_CHECK(
        fixture.lifecycleSlot.phase()
        == lifecycle::ZoneLoadContextPhase::Live);
    OWNERSHIP_CHECK(
        fixture.stringJournal.phase()
        == journal::ScriptStringJournalPhase::Committed);
}
} // namespace ownership_test

namespace script_string
{
AcquireResult TryAcquireOrdinaryStringOfSize(
    const char *,
    const std::uint32_t,
    const int) noexcept
{
    using namespace ownership_test;
    const std::size_t call = backend.acquireCalls++;
    if (call >= backend.acquireCount)
        return {AcquireStatus::UnsafeFailure, 0};
    return backend.acquire[call];
}

TransferStatus TryTransferOrdinaryToDatabaseUser(
    const std::uint32_t stringId) noexcept
{
    using namespace ownership_test;
    const std::size_t call = backend.transferCalls++;
    if (call >= backend.transferredIds.size())
        return TransferStatus::UnsafeFailure;
    backend.transferredIds[call] = stringId;
    return call < backend.transferCount
        ? backend.transfer[call]
        : TransferStatus::UnsafeFailure;
}

ReleaseStatus TryRemoveOrdinaryReference(
    const std::uint32_t stringId) noexcept
{
    using namespace ownership_test;
    const std::size_t call = backend.ordinaryCalls++;
    if (call >= backend.ordinaryIds.size())
        return ReleaseStatus::UnsafeFailure;
    backend.ordinaryIds[call] = stringId;
    return call < backend.ordinaryCount
        ? backend.ordinary[call]
        : ReleaseStatus::UnsafeFailure;
}

ReleaseStatus TryRemoveDatabaseUserReference(
    const std::uint32_t stringId) noexcept
{
    using namespace ownership_test;
    const std::size_t call = backend.databaseCalls++;
    if (call >= backend.databaseIds.size())
        return ReleaseStatus::UnsafeFailure;
    backend.databaseIds[call] = stringId;
    return call < backend.databaseCount
        ? backend.database[call]
        : ReleaseStatus::UnsafeFailure;
}
} // namespace script_string

int main()
{
    using namespace ownership_test;
    TestCommittedAdmission();
    TestPartialRollbackAndCleanupRetry();
    TestBindingAndForeignThreadRejection();
    TestBeginFailureReleasesSerializer();
    TestDurableStorageIdentityAuthentication();
    TestAbandonedReceiptAuthentication();
    TestLiveUnloadRetryBindingAndTerminalReset();
    // This fail-closed case intentionally retains the process serializer and
    // must remain last.
    TestAdmissionPostconditionFailsClosed();
    if (failures != 0)
    {
        std::fprintf(
            stderr,
            "zone script-string ownership tests failed: %d\n",
            failures);
        return 1;
    }
    return 0;
}
