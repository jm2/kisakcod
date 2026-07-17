#include <database/db_script_string_adapter.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <script/scr_string_transaction.h>

namespace adapter_test
{
namespace adapter = db::script_string_adapter;
namespace journal = db::script_string_journal;
namespace transaction = db::script_string_transaction;

using journal::ScriptStringJournal;
using journal::ScriptStringJournalEntry;
using journal::ScriptStringJournalEntryState;
using journal::ScriptStringJournalPhase;
using journal::ScriptStringJournalStatus;
using transaction::ScriptStringTransactionToken;

constexpr std::size_t kMaxFakeCalls = 16;
constexpr std::size_t kMaxCapturedSourceBytes = 32;

struct CapturedSource final
{
    std::array<char, kMaxCapturedSourceBytes> bytes{};
    std::uint32_t byteCount = 0;
    int type = 0;
    bool wasNull = false;
};

struct FakeScriptStringBackend final
{
    std::array<script_string::AcquireResult, kMaxFakeCalls> acquireResults{};
    std::size_t acquireResultCount = 0;
    std::size_t acquireCallCount = 0;
    std::array<CapturedSource, kMaxFakeCalls> capturedSources{};

    std::array<script_string::TransferStatus, kMaxFakeCalls> transferResults{};
    std::size_t transferResultCount = 0;
    std::size_t transferCallCount = 0;
    std::array<std::uint32_t, kMaxFakeCalls> transferredIds{};

    std::array<script_string::ReleaseStatus, kMaxFakeCalls> ordinaryResults{};
    std::size_t ordinaryResultCount = 0;
    std::size_t ordinaryCallCount = 0;
    std::array<std::uint32_t, kMaxFakeCalls> ordinaryIds{};

    std::array<script_string::ReleaseStatus, kMaxFakeCalls> databaseResults{};
    std::size_t databaseResultCount = 0;
    std::size_t databaseCallCount = 0;
    std::array<std::uint32_t, kMaxFakeCalls> databaseIds{};
};

FakeScriptStringBackend backend{};
int failures = 0;

void Check(
    const bool condition,
    const char *const expression,
    const int line)
{
    if (condition)
        return;
    std::fprintf(
        stderr,
        "db script-string adapter line %d: check failed: %s\n",
        line,
        expression);
    ++failures;
}

#define ADAPTER_CHECK(expression) \
    ::adapter_test::Check((expression), #expression, __LINE__)

void ResetBackend() noexcept
{
    backend = {};
}

void PushAcquireResult(
    const script_string::AcquireStatus status,
    const std::uint32_t stringId) noexcept
{
    ADAPTER_CHECK(backend.acquireResultCount < kMaxFakeCalls);
    if (backend.acquireResultCount < kMaxFakeCalls)
    {
        backend.acquireResults[backend.acquireResultCount++] = {
            status,
            stringId};
    }
}

void PushTransferResult(
    const script_string::TransferStatus status) noexcept
{
    ADAPTER_CHECK(backend.transferResultCount < kMaxFakeCalls);
    if (backend.transferResultCount < kMaxFakeCalls)
        backend.transferResults[backend.transferResultCount++] = status;
}

void PushOrdinaryResult(
    const script_string::ReleaseStatus status) noexcept
{
    ADAPTER_CHECK(backend.ordinaryResultCount < kMaxFakeCalls);
    if (backend.ordinaryResultCount < kMaxFakeCalls)
        backend.ordinaryResults[backend.ordinaryResultCount++] = status;
}

void PushDatabaseResult(
    const script_string::ReleaseStatus status) noexcept
{
    ADAPTER_CHECK(backend.databaseResultCount < kMaxFakeCalls);
    if (backend.databaseResultCount < kMaxFakeCalls)
        backend.databaseResults[backend.databaseResultCount++] = status;
}

constexpr db::zone_load::ZoneLoadContextKey MakeKey(
    const std::uint64_t generation,
    const std::uint32_t slot) noexcept
{
    return {generation, slot, 0};
}

void InitializeJournal(
    ScriptStringJournal *const value,
    const db::zone_load::ZoneLoadContextKey &key,
    ScriptStringJournalEntry *const storage,
    const std::uint32_t capacity,
    const std::uint32_t expectedCount)
{
    ADAPTER_CHECK(
        journal::TryInitializeScriptStringJournal(
            value, key, storage, capacity, expectedCount)
        == ScriptStringJournalStatus::Success);
}

void TestInactiveTokenIsRejected()
{
    ResetBackend();
    const db::zone_load::ZoneLoadContextKey key = MakeKey(1, 2);
    const adapter::ScriptStringSourceView source{"x", 1, 7};
    ScriptStringTransactionToken inactive;

    std::uint32_t output = 0xA5A5A5A5u;
    ADAPTER_CHECK(
        adapter::TryStageScriptStringFromSource(
            nullptr, key, inactive, source, &output)
        == ScriptStringJournalStatus::InvalidState);
    ADAPTER_CHECK(output == 0xA5A5A5A5u);
    ADAPTER_CHECK(
        adapter::TryTransferNextScriptStringToDatabaseUser(
            nullptr, key, inactive)
        == ScriptStringJournalStatus::InvalidState);
    ADAPTER_CHECK(
        adapter::TryRollbackNextScriptStringOwnership(
            nullptr, key, inactive)
        == ScriptStringJournalStatus::InvalidState);
    ADAPTER_CHECK(backend.acquireCallCount == 0);
    ADAPTER_CHECK(backend.transferCallCount == 0);
    ADAPTER_CHECK(backend.ordinaryCallCount == 0);
    ADAPTER_CHECK(backend.databaseCallCount == 0);
}

void TestSourceForwardingAndRepeatedAcquisitions(
    const ScriptStringTransactionToken &token)
{
    ResetBackend();
    const db::zone_load::ZoneLoadContextKey key = MakeKey(11, 3);
    std::array<ScriptStringJournalEntry, 2> storage{};
    ScriptStringJournal value{};
    InitializeJournal(
        &value,
        key,
        storage.data(),
        static_cast<std::uint32_t>(storage.size()),
        static_cast<std::uint32_t>(storage.size()));

    constexpr std::array<char, 4> sourceBytes{{'a', '\0', 'b', '\0'}};
    const adapter::ScriptStringSourceView source{
        sourceBytes.data(),
        static_cast<std::uint32_t>(sourceBytes.size()),
        37};
    PushAcquireResult(script_string::AcquireStatus::Acquired, 77);
    PushAcquireResult(script_string::AcquireStatus::Acquired, 77);

    std::uint32_t firstOutput = 0;
    ADAPTER_CHECK(
        adapter::TryStageScriptStringFromSource(
            &value, key, token, source, &firstOutput)
        == ScriptStringJournalStatus::Success);
    ADAPTER_CHECK(firstOutput == 77);
    std::uint32_t secondOutput = 1234;
    ADAPTER_CHECK(
        adapter::TryStageScriptStringFromSource(
            &value, key, token, source, &secondOutput)
        == ScriptStringJournalStatus::Success);
    ADAPTER_CHECK(secondOutput == 77);
    ADAPTER_CHECK(backend.acquireCallCount == 2);
    for (std::size_t call = 0; call < backend.acquireCallCount; ++call)
    {
        ADAPTER_CHECK(!backend.capturedSources[call].wasNull);
        ADAPTER_CHECK(
            backend.capturedSources[call].byteCount == sourceBytes.size());
        ADAPTER_CHECK(backend.capturedSources[call].type == source.type);
        for (std::size_t byte = 0; byte < sourceBytes.size(); ++byte)
        {
            ADAPTER_CHECK(
                backend.capturedSources[call].bytes[byte]
                == sourceBytes[byte]);
        }
    }
    ADAPTER_CHECK(value.entryCount() == 2);
    ADAPTER_CHECK(storage[0].stringId == 77);
    ADAPTER_CHECK(storage[1].stringId == 77);
    ADAPTER_CHECK(
        storage[0].state
        == ScriptStringJournalEntryState::OrdinaryStaged);
    ADAPTER_CHECK(
        storage[1].state
        == ScriptStringJournalEntryState::OrdinaryStaged);

    ADAPTER_CHECK(
        journal::TryBeginScriptStringRollback(&value, key)
        == ScriptStringJournalStatus::Success);
    PushOrdinaryResult(script_string::ReleaseStatus::Success);
    PushOrdinaryResult(script_string::ReleaseStatus::Success);
    ADAPTER_CHECK(
        adapter::TryRollbackNextScriptStringOwnership(
            &value, key, token)
        == ScriptStringJournalStatus::Success);
    ADAPTER_CHECK(
        adapter::TryRollbackNextScriptStringOwnership(
            &value, key, token)
        == ScriptStringJournalStatus::Success);
    ADAPTER_CHECK(value.phase() == ScriptStringJournalPhase::RolledBack);
    ADAPTER_CHECK(backend.ordinaryCallCount == 2);
    ADAPTER_CHECK(backend.ordinaryIds[0] == 77);
    ADAPTER_CHECK(backend.ordinaryIds[1] == 77);
}

void TestRejectedAcquireStatuses(
    const ScriptStringTransactionToken &token)
{
    ResetBackend();
    const db::zone_load::ZoneLoadContextKey key = MakeKey(12, 4);
    ScriptStringJournalEntry storage{};
    ScriptStringJournal value{};
    InitializeJournal(&value, key, &storage, 1, 1);
    const adapter::ScriptStringSourceView source{"ok\0", 3, 5};

    PushAcquireResult(
        script_string::AcquireStatus::InvalidArgumentNoChange, 0);
    PushAcquireResult(script_string::AcquireStatus::CapacityNoChange, 0);
    PushAcquireResult(
        script_string::AcquireStatus::RefCountExhaustedNoChange, 0);
    PushAcquireResult(script_string::AcquireStatus::Acquired, 91);

    for (int attempt = 0; attempt < 3; ++attempt)
    {
        std::uint32_t output = 0xDEADBEEFu;
        ADAPTER_CHECK(
            adapter::TryStageScriptStringFromSource(
                &value, key, token, source, &output)
            == ScriptStringJournalStatus::Rejected);
        ADAPTER_CHECK(output == 0xDEADBEEFu);
        ADAPTER_CHECK(value.entryCount() == 0);
        ADAPTER_CHECK(!value.poisoned());
    }
    std::uint32_t output = 0;
    ADAPTER_CHECK(
        adapter::TryStageScriptStringFromSource(
            &value, key, token, source, &output)
        == ScriptStringJournalStatus::Success);
    ADAPTER_CHECK(output == 91);
    ADAPTER_CHECK(storage.stringId == 91);

    ADAPTER_CHECK(
        journal::TryBeginScriptStringRollback(&value, key)
        == ScriptStringJournalStatus::Success);
    PushOrdinaryResult(script_string::ReleaseStatus::Success);
    ADAPTER_CHECK(
        adapter::TryRollbackNextScriptStringOwnership(
            &value, key, token)
        == ScriptStringJournalStatus::Success);
}

void TestInvalidAcquiredIdPoisons(
    const ScriptStringTransactionToken &token,
    const std::uint32_t invalidId,
    const std::uint64_t generation)
{
    ResetBackend();
    const db::zone_load::ZoneLoadContextKey key = MakeKey(generation, 5);
    ScriptStringJournalEntry storage{};
    ScriptStringJournal value{};
    InitializeJournal(&value, key, &storage, 1, 1);
    PushAcquireResult(script_string::AcquireStatus::Acquired, invalidId);

    ADAPTER_CHECK(
        adapter::TryStageScriptStringFromSource(
            &value,
            key,
            token,
            adapter::ScriptStringSourceView{"bad\0", 4, 0})
        == ScriptStringJournalStatus::UnsafeFailure);
    ADAPTER_CHECK(value.poisoned());
    ADAPTER_CHECK(value.entryCount() == 0);
    ADAPTER_CHECK(storage.stringId == 0);
}

void TestUnsafeAcquirePoisons(
    const ScriptStringTransactionToken &token)
{
    ResetBackend();
    const db::zone_load::ZoneLoadContextKey key = MakeKey(15, 5);
    ScriptStringJournalEntry storage{};
    ScriptStringJournal value{};
    InitializeJournal(&value, key, &storage, 1, 1);
    PushAcquireResult(script_string::AcquireStatus::UnsafeFailure, 0);

    ADAPTER_CHECK(
        adapter::TryStageScriptStringFromSource(
            &value,
            key,
            token,
            adapter::ScriptStringSourceView{"unsafe\0", 7, 1})
        == ScriptStringJournalStatus::UnsafeFailure);
    ADAPTER_CHECK(value.poisoned());
    ADAPTER_CHECK(value.entryCount() == 0);
    ADAPTER_CHECK(backend.acquireCallCount == 1);
}

void TestPartialTransferRollsBackExactOwnership(
    const ScriptStringTransactionToken &token)
{
    ResetBackend();
    const db::zone_load::ZoneLoadContextKey key = MakeKey(21, 6);
    std::array<ScriptStringJournalEntry, 3> storage{};
    ScriptStringJournal value{};
    InitializeJournal(
        &value,
        key,
        storage.data(),
        static_cast<std::uint32_t>(storage.size()),
        static_cast<std::uint32_t>(storage.size()));
    PushAcquireResult(script_string::AcquireStatus::Acquired, 101);
    PushAcquireResult(script_string::AcquireStatus::Acquired, 202);
    PushAcquireResult(script_string::AcquireStatus::Acquired, 303);
    const adapter::ScriptStringSourceView source{"owned\0", 6, 1};
    for (std::size_t entry = 0; entry < storage.size(); ++entry)
    {
        ADAPTER_CHECK(
            adapter::TryStageScriptStringFromSource(
                &value, key, token, source)
            == ScriptStringJournalStatus::Success);
    }
    ADAPTER_CHECK(
        journal::TrySealScriptStringJournal(&value, key)
        == ScriptStringJournalStatus::Success);
    ADAPTER_CHECK(
        journal::TryBeginScriptStringTransfer(&value, key)
        == ScriptStringJournalStatus::Success);
    PushTransferResult(
        script_string::TransferStatus::DatabaseUserClaimed);
    PushTransferResult(script_string::TransferStatus::DuplicateReleased);
    ADAPTER_CHECK(
        adapter::TryTransferNextScriptStringToDatabaseUser(
            &value, key, token)
        == ScriptStringJournalStatus::Success);
    ADAPTER_CHECK(
        adapter::TryTransferNextScriptStringToDatabaseUser(
            &value, key, token)
        == ScriptStringJournalStatus::Success);
    ADAPTER_CHECK(value.transferCursor() == 2);
    ADAPTER_CHECK(
        storage[0].state
        == ScriptStringJournalEntryState::DatabaseUserClaimed);
    ADAPTER_CHECK(
        storage[1].state
        == ScriptStringJournalEntryState::DuplicateReleased);
    ADAPTER_CHECK(
        storage[2].state
        == ScriptStringJournalEntryState::OrdinaryStaged);

    ADAPTER_CHECK(
        journal::TryBeginScriptStringRollback(&value, key)
        == ScriptStringJournalStatus::Success);
    PushOrdinaryResult(script_string::ReleaseStatus::Success);
    PushDatabaseResult(script_string::ReleaseStatus::Success);
    ADAPTER_CHECK(
        adapter::TryRollbackNextScriptStringOwnership(
            &value, key, token)
        == ScriptStringJournalStatus::Success);
    ADAPTER_CHECK(backend.ordinaryCallCount == 1);
    ADAPTER_CHECK(backend.ordinaryIds[0] == 303);
    ADAPTER_CHECK(
        adapter::TryRollbackNextScriptStringOwnership(
            &value, key, token)
        == ScriptStringJournalStatus::Success);
    ADAPTER_CHECK(backend.ordinaryCallCount == 1);
    ADAPTER_CHECK(backend.databaseCallCount == 0);
    ADAPTER_CHECK(
        adapter::TryRollbackNextScriptStringOwnership(
            &value, key, token)
        == ScriptStringJournalStatus::Success);
    ADAPTER_CHECK(value.phase() == ScriptStringJournalPhase::RolledBack);
    ADAPTER_CHECK(backend.databaseCallCount == 1);
    ADAPTER_CHECK(backend.databaseIds[0] == 101);
    for (const ScriptStringJournalEntry &entry : storage)
    {
        ADAPTER_CHECK(entry.state == ScriptStringJournalEntryState::Released);
    }
}

void TestOwnershipMismatchPoisons(
    const ScriptStringTransactionToken &token)
{
    {
        ResetBackend();
        const db::zone_load::ZoneLoadContextKey key = MakeKey(31, 7);
        ScriptStringJournalEntry storage{};
        ScriptStringJournal value{};
        InitializeJournal(&value, key, &storage, 1, 1);
        PushAcquireResult(script_string::AcquireStatus::Acquired, 401);
        ADAPTER_CHECK(
            adapter::TryStageScriptStringFromSource(
                &value,
                key,
                token,
                adapter::ScriptStringSourceView{"x\0", 2, 0})
            == ScriptStringJournalStatus::Success);
        ADAPTER_CHECK(
            journal::TrySealScriptStringJournal(&value, key)
            == ScriptStringJournalStatus::Success);
        ADAPTER_CHECK(
            journal::TryBeginScriptStringTransfer(&value, key)
            == ScriptStringJournalStatus::Success);
        PushTransferResult(
            script_string::TransferStatus::OwnershipMismatchNoChange);
        ADAPTER_CHECK(
            adapter::TryTransferNextScriptStringToDatabaseUser(
                &value, key, token)
            == ScriptStringJournalStatus::UnsafeFailure);
        ADAPTER_CHECK(value.poisoned());
        ADAPTER_CHECK(value.transferCursor() == 0);
    }

    {
        ResetBackend();
        const db::zone_load::ZoneLoadContextKey key = MakeKey(32, 8);
        ScriptStringJournalEntry storage{};
        ScriptStringJournal value{};
        InitializeJournal(&value, key, &storage, 1, 1);
        PushAcquireResult(script_string::AcquireStatus::Acquired, 402);
        ADAPTER_CHECK(
            adapter::TryStageScriptStringFromSource(
                &value,
                key,
                token,
                adapter::ScriptStringSourceView{"y\0", 2, 0})
            == ScriptStringJournalStatus::Success);
        ADAPTER_CHECK(
            journal::TryBeginScriptStringRollback(&value, key)
            == ScriptStringJournalStatus::Success);
        PushOrdinaryResult(
            script_string::ReleaseStatus::OwnershipMismatchNoChange);
        ADAPTER_CHECK(
            adapter::TryRollbackNextScriptStringOwnership(
                &value, key, token)
            == ScriptStringJournalStatus::UnsafeFailure);
        ADAPTER_CHECK(value.poisoned());
        ADAPTER_CHECK(value.rollbackCursor() == 1);
    }

    {
        ResetBackend();
        const db::zone_load::ZoneLoadContextKey key = MakeKey(33, 9);
        ScriptStringJournalEntry storage{};
        ScriptStringJournal value{};
        InitializeJournal(&value, key, &storage, 1, 1);
        PushAcquireResult(script_string::AcquireStatus::Acquired, 403);
        ADAPTER_CHECK(
            adapter::TryStageScriptStringFromSource(
                &value,
                key,
                token,
                adapter::ScriptStringSourceView{"z\0", 2, 0})
            == ScriptStringJournalStatus::Success);
        ADAPTER_CHECK(
            journal::TrySealScriptStringJournal(&value, key)
            == ScriptStringJournalStatus::Success);
        ADAPTER_CHECK(
            journal::TryBeginScriptStringTransfer(&value, key)
            == ScriptStringJournalStatus::Success);
        PushTransferResult(
            script_string::TransferStatus::DatabaseUserClaimed);
        ADAPTER_CHECK(
            adapter::TryTransferNextScriptStringToDatabaseUser(
                &value, key, token)
            == ScriptStringJournalStatus::Success);
        ADAPTER_CHECK(
            journal::TryBeginScriptStringRollback(&value, key)
            == ScriptStringJournalStatus::Success);
        PushDatabaseResult(
            script_string::ReleaseStatus::OwnershipMismatchNoChange);
        ADAPTER_CHECK(
            adapter::TryRollbackNextScriptStringOwnership(
                &value, key, token)
            == ScriptStringJournalStatus::UnsafeFailure);
        ADAPTER_CHECK(value.poisoned());
        ADAPTER_CHECK(value.rollbackCursor() == 1);
    }
}

} // namespace adapter_test

namespace script_string
{
AcquireResult TryAcquireOrdinaryStringOfSize(
    const char *const bytes,
    const std::uint32_t byteCount,
    const int type) noexcept
{
    using namespace adapter_test;
    const std::size_t call = backend.acquireCallCount++;
    if (call >= kMaxFakeCalls)
        return {AcquireStatus::UnsafeFailure, 0};

    CapturedSource &captured = backend.capturedSources[call];
    captured.byteCount = byteCount;
    captured.type = type;
    captured.wasNull = bytes == nullptr;
    if (bytes)
    {
        const std::size_t copyCount = byteCount < captured.bytes.size()
            ? static_cast<std::size_t>(byteCount)
            : captured.bytes.size();
        for (std::size_t byte = 0; byte < copyCount; ++byte)
            captured.bytes[byte] = bytes[byte];
    }

    if (call >= backend.acquireResultCount)
        return {AcquireStatus::UnsafeFailure, 0};
    return backend.acquireResults[call];
}

TransferStatus TryTransferOrdinaryToDatabaseUser(
    const std::uint32_t stringId) noexcept
{
    using namespace adapter_test;
    const std::size_t call = backend.transferCallCount++;
    if (call >= kMaxFakeCalls)
        return TransferStatus::UnsafeFailure;
    backend.transferredIds[call] = stringId;
    if (call >= backend.transferResultCount)
        return TransferStatus::UnsafeFailure;
    return backend.transferResults[call];
}

ReleaseStatus TryRemoveOrdinaryReference(
    const std::uint32_t stringId) noexcept
{
    using namespace adapter_test;
    const std::size_t call = backend.ordinaryCallCount++;
    if (call >= kMaxFakeCalls)
        return ReleaseStatus::UnsafeFailure;
    backend.ordinaryIds[call] = stringId;
    if (call >= backend.ordinaryResultCount)
        return ReleaseStatus::UnsafeFailure;
    return backend.ordinaryResults[call];
}

ReleaseStatus TryRemoveDatabaseUserReference(
    const std::uint32_t stringId) noexcept
{
    using namespace adapter_test;
    const std::size_t call = backend.databaseCallCount++;
    if (call >= kMaxFakeCalls)
        return ReleaseStatus::UnsafeFailure;
    backend.databaseIds[call] = stringId;
    if (call >= backend.databaseResultCount)
        return ReleaseStatus::UnsafeFailure;
    return backend.databaseResults[call];
}
} // namespace script_string

bool RunDatabaseScriptStringAdapterTests()
{
    using namespace adapter_test;

    failures = 0;
    TestInactiveTokenIsRejected();

    ScriptStringTransactionToken token;
    ADAPTER_CHECK(
        transaction::TryBeginScriptStringTransaction(&token)
        == transaction::ScriptStringTransactionStatus::Success);
    if (token.active())
    {
        ScriptStringTransactionToken inactive;
        const db::zone_load::ZoneLoadContextKey key = MakeKey(2, 2);
        const adapter::ScriptStringSourceView source{"x", 1, 0};
        ADAPTER_CHECK(
            adapter::TryStageScriptStringFromSource(
                nullptr, key, inactive, source)
            == ScriptStringJournalStatus::InvalidState);

        TestSourceForwardingAndRepeatedAcquisitions(token);
        TestRejectedAcquireStatuses(token);
        TestInvalidAcquiredIdPoisons(token, 0, 13);
        TestInvalidAcquiredIdPoisons(
            token, script_string::kCurrentRuntimeStringLimit, 14);
        TestUnsafeAcquirePoisons(token);
        TestPartialTransferRollsBackExactOwnership(token);
        TestOwnershipMismatchPoisons(token);

        ADAPTER_CHECK(
            transaction::FinishScriptStringTransaction(&token)
            == transaction::ScriptStringTransactionStatus::Success);
    }

    ResetBackend();
    const db::zone_load::ZoneLoadContextKey key = MakeKey(40, 10);
    ADAPTER_CHECK(
        adapter::TryTransferNextScriptStringToDatabaseUser(
            nullptr, key, token)
        == ScriptStringJournalStatus::InvalidState);
    ADAPTER_CHECK(backend.transferCallCount == 0);

    ADAPTER_CHECK(
        transaction::TryBeginScriptStringTransaction(&token)
        == transaction::ScriptStringTransactionStatus::Success);
    if (token.active())
    {
        ResetBackend();
        const db::zone_load::ZoneLoadContextKey reusedKey = MakeKey(41, 11);
        ScriptStringJournalEntry storage{};
        ScriptStringJournal value{};
        InitializeJournal(&value, reusedKey, &storage, 1, 1);
        PushAcquireResult(script_string::AcquireStatus::Acquired, 511);
        ADAPTER_CHECK(
            adapter::TryStageScriptStringFromSource(
                &value,
                reusedKey,
                token,
                adapter::ScriptStringSourceView{"reuse\0", 6, 2})
            == ScriptStringJournalStatus::Success);
        ADAPTER_CHECK(
            journal::TryBeginScriptStringRollback(&value, reusedKey)
            == ScriptStringJournalStatus::Success);
        PushOrdinaryResult(script_string::ReleaseStatus::Success);
        ADAPTER_CHECK(
            adapter::TryRollbackNextScriptStringOwnership(
                &value, reusedKey, token)
            == ScriptStringJournalStatus::Success);
        ADAPTER_CHECK(
            transaction::FinishScriptStringTransaction(&token)
            == transaction::ScriptStringTransactionStatus::Success);
    }

    return failures == 0;
}
