#include <EffectsCore/fx_fastfile_native_arena.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

namespace
{
namespace fastfile = fx::fastfile;

using Arena = fastfile::FxFastFileNativeArena;
using Transaction = fastfile::FxFastFileNativeArenaTransaction;
using Status = fastfile::FxFastFileNativeArenaStatus;

int failures = 0;

void Check(
    const bool condition,
    const char *const expression,
    const int line)
{
    if (condition)
        return;
    std::fprintf(stderr, "line %d: check failed: %s\n", line, expression);
    ++failures;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

constexpr std::size_t kStorageBytes = 4096;

struct alignas(fastfile::kFxFastFileNativeArenaStorageAlignment) Storage final
{
    std::uint8_t bytes[kStorageBytes];
};

struct Fixture final
{
    Arena arena{};
    Storage storage{};

    Fixture()
    {
        std::memset(storage.bytes, 0xA5, sizeof(storage.bytes));
        CHECK(arena.TryBind(storage.bytes, sizeof(storage.bytes), 7)
              == Status::Success);
    }
};

void TestBindValidation()
{
    Arena arena;
    Storage storage;
    CHECK(!arena.bound());
    CHECK(arena.TryBind(nullptr, kStorageBytes, 1)
          == Status::InvalidArgument);
    CHECK(arena.TryBind(storage.bytes, 0, 1) == Status::InvalidArgument);
    CHECK(arena.TryBind(storage.bytes, kStorageBytes, 0)
          == Status::InvalidArgument);
    CHECK(arena.TryBind(storage.bytes + 1, kStorageBytes - 1, 1)
          == Status::MisalignedStorage);
    CHECK(!arena.bound());
    CHECK(arena.TryUnbind() == Status::InvalidPhase);

    CHECK(arena.TryBind(storage.bytes, kStorageBytes, 41) == Status::Success);
    CHECK(arena.bound());
    CHECK(arena.zoneIdentity() == 41);
    CHECK(arena.capacity() == kStorageBytes);
    CHECK(arena.usedBytes() == 0);
    CHECK(arena.committedBytes() == 0);
    CHECK(arena.openTransactionDepth() == 0);
    CHECK(arena.TryUnbind() == Status::Success);
    CHECK(!arena.bound());
    CHECK(arena.zoneIdentity() == 0);
    CHECK(arena.capacity() == 0);
}

void TestUnboundOperationsFail()
{
    Arena arena;
    Transaction transaction;
    void *storage = nullptr;
    CHECK(arena.TryBeginTransaction(&transaction) == Status::InvalidPhase);
    CHECK(arena.TryReserve(transaction, 8, 8, &storage)
          == Status::InvalidPhase);
    CHECK(arena.TryCommit(transaction) == Status::InvalidPhase);
    CHECK(arena.TryAbandon(transaction) == Status::InvalidPhase);
    CHECK(storage == nullptr);
}

void TestReserveCommit()
{
    Fixture fixture;
    Transaction transaction;
    CHECK(fixture.arena.TryBeginTransaction(&transaction) == Status::Success);
    CHECK(static_cast<bool>(transaction));
    CHECK(fixture.arena.openTransactionDepth() == 1);

    void *first = nullptr;
    CHECK(fixture.arena.TryReserve(transaction, 24, 8, &first)
          == Status::Success);
    CHECK(first == fixture.storage.bytes);
    CHECK(fixture.arena.usedBytes() == 24);
    for (std::size_t index = 0; index < 24; ++index)
        CHECK(static_cast<std::uint8_t *>(first)[index] == 0);

    // Alignment padding is zero-filled and accounted.
    void *second = nullptr;
    CHECK(fixture.arena.TryReserve(transaction, 3, 1, &second)
          == Status::Success);
    CHECK(second == fixture.storage.bytes + 24);
    void *third = nullptr;
    CHECK(fixture.arena.TryReserve(transaction, 8, 16, &third)
          == Status::Success);
    CHECK(third == fixture.storage.bytes + 32);
    CHECK(fixture.storage.bytes[27] == 0);
    CHECK(fixture.arena.usedBytes() == 40);
    CHECK(fixture.arena.committedBytes() == 0);

    CHECK(fixture.arena.TryCommit(transaction) == Status::Success);
    CHECK(fixture.arena.openTransactionDepth() == 0);
    CHECK(fixture.arena.committedBytes() == 40);
    CHECK(fixture.arena.usedBytes() == 40);

    // A dead token cannot act again.
    CHECK(fixture.arena.TryCommit(transaction) == Status::InvalidTransaction);
    CHECK(fixture.arena.TryAbandon(transaction)
          == Status::InvalidTransaction);
    void *stale = nullptr;
    CHECK(fixture.arena.TryReserve(transaction, 8, 8, &stale)
          == Status::InvalidTransaction);
    CHECK(stale == nullptr);
}

void TestReserveValidation()
{
    Fixture fixture;
    Transaction transaction;
    CHECK(fixture.arena.TryBeginTransaction(&transaction) == Status::Success);

    void *storage = nullptr;
    CHECK(fixture.arena.TryReserve(transaction, 0, 8, &storage)
          == Status::InvalidArgument);
    CHECK(fixture.arena.TryReserve(transaction, 8, 0, &storage)
          == Status::InvalidArgument);
    CHECK(fixture.arena.TryReserve(transaction, 8, 24, &storage)
          == Status::InvalidArgument);
    CHECK(fixture.arena.TryReserve(transaction, 8, 8, nullptr)
          == Status::InvalidArgument);
    CHECK(fixture.arena.TryReserve(transaction, kStorageBytes + 1, 1, &storage)
          == Status::InsufficientCapacity);
    CHECK(storage == nullptr);
    CHECK(fixture.arena.usedBytes() == 0);

    // Exact capacity succeeds.
    CHECK(fixture.arena.TryReserve(transaction, kStorageBytes, 1, &storage)
          == Status::Success);
    CHECK(fixture.arena.usedBytes() == kStorageBytes);
    void *overflow = nullptr;
    CHECK(fixture.arena.TryReserve(transaction, 1, 1, &overflow)
          == Status::InsufficientCapacity);
    CHECK(overflow == nullptr);
    CHECK(fixture.arena.TryAbandon(transaction) == Status::Success);
    CHECK(fixture.arena.usedBytes() == 0);
}

void TestAbandonReclaimsAndRezeroes()
{
    Fixture fixture;
    Transaction transaction;
    CHECK(fixture.arena.TryBeginTransaction(&transaction) == Status::Success);
    void *storage = nullptr;
    CHECK(fixture.arena.TryReserve(transaction, 64, 8, &storage)
          == Status::Success);
    std::memset(storage, 0xEE, 64);
    CHECK(fixture.arena.TryAbandon(transaction) == Status::Success);
    CHECK(fixture.arena.usedBytes() == 0);
    CHECK(fixture.arena.openTransactionDepth() == 0);
    for (std::size_t index = 0; index < 64; ++index)
        CHECK(fixture.storage.bytes[index] == 0);

    // Reclaimed bytes are reissued zero-filled.
    Transaction retry;
    CHECK(fixture.arena.TryBeginTransaction(&retry) == Status::Success);
    void *again = nullptr;
    CHECK(fixture.arena.TryReserve(retry, 64, 8, &again) == Status::Success);
    CHECK(again == fixture.storage.bytes);
    CHECK(fixture.arena.TryCommit(retry) == Status::Success);
}

void TestNestedTransactions()
{
    Fixture fixture;
    Transaction outer;
    CHECK(fixture.arena.TryBeginTransaction(&outer) == Status::Success);
    void *outerStorage = nullptr;
    CHECK(fixture.arena.TryReserve(outer, 16, 8, &outerStorage)
          == Status::Success);

    Transaction inner;
    CHECK(fixture.arena.TryBeginTransaction(&inner) == Status::Success);
    CHECK(fixture.arena.openTransactionDepth() == 2);

    // The depth limit is exact.
    Transaction third;
    CHECK(fixture.arena.TryBeginTransaction(&third)
          == Status::TransactionLimit);
    CHECK(!static_cast<bool>(third));

    // Only the innermost open transaction may operate.
    void *rejected = nullptr;
    CHECK(fixture.arena.TryReserve(outer, 8, 8, &rejected)
          == Status::InvalidTransaction);
    CHECK(fixture.arena.TryCommit(outer) == Status::InvalidTransaction);
    CHECK(fixture.arena.TryAbandon(outer) == Status::InvalidTransaction);
    CHECK(rejected == nullptr);

    void *innerStorage = nullptr;
    CHECK(fixture.arena.TryReserve(inner, 32, 8, &innerStorage)
          == Status::Success);
    CHECK(innerStorage == fixture.storage.bytes + 16);
    CHECK(fixture.arena.TryCommit(inner) == Status::Success);
    CHECK(fixture.arena.committedBytes() == 48);

    // Abandoning the outer transaction must not reclaim the committed inner
    // region: its bytes are already published.
    std::memset(innerStorage, 0xBB, 32);
    CHECK(fixture.arena.TryAbandon(outer) == Status::Success);
    CHECK(fixture.arena.usedBytes() == 48);
    CHECK(fixture.arena.committedBytes() == 48);
    for (std::size_t index = 16; index < 48; ++index)
        CHECK(fixture.storage.bytes[index] == 0xBB);
    CHECK(fixture.arena.openTransactionDepth() == 0);
}

void TestAbandonAboveCommittedWatermark()
{
    Fixture fixture;
    Transaction outer;
    CHECK(fixture.arena.TryBeginTransaction(&outer) == Status::Success);

    Transaction inner;
    CHECK(fixture.arena.TryBeginTransaction(&inner) == Status::Success);
    void *innerStorage = nullptr;
    CHECK(fixture.arena.TryReserve(inner, 16, 8, &innerStorage)
          == Status::Success);
    CHECK(fixture.arena.TryCommit(inner) == Status::Success);

    // Outer bytes reserved above the watermark are reclaimed on abandon.
    void *outerStorage = nullptr;
    CHECK(fixture.arena.TryReserve(outer, 16, 8, &outerStorage)
          == Status::Success);
    std::memset(outerStorage, 0xCC, 16);
    CHECK(fixture.arena.usedBytes() == 32);
    CHECK(fixture.arena.TryAbandon(outer) == Status::Success);
    CHECK(fixture.arena.usedBytes() == 16);
    CHECK(fixture.arena.committedBytes() == 16);
    for (std::size_t index = 16; index < 32; ++index)
        CHECK(fixture.storage.bytes[index] == 0);
}

void TestForeignAndStaleTransactions()
{
    Fixture fixture;
    Storage otherStorage;
    Arena other;
    CHECK(other.TryBind(otherStorage.bytes, sizeof(otherStorage.bytes), 9)
          == Status::Success);

    Transaction foreign;
    CHECK(other.TryBeginTransaction(&foreign) == Status::Success);

    Transaction transaction;
    CHECK(fixture.arena.TryBeginTransaction(&transaction) == Status::Success);

    void *storage = nullptr;
    CHECK(fixture.arena.TryReserve(foreign, 8, 8, &storage)
          == Status::InvalidTransaction);
    CHECK(fixture.arena.TryCommit(foreign) == Status::InvalidTransaction);
    CHECK(fixture.arena.TryAbandon(foreign) == Status::InvalidTransaction);
    CHECK(storage == nullptr);

    // A default token is never valid.
    Transaction defaulted;
    CHECK(fixture.arena.TryReserve(defaulted, 8, 8, &storage)
          == Status::InvalidTransaction);

    CHECK(fixture.arena.TryAbandon(transaction) == Status::Success);
    CHECK(other.TryAbandon(foreign) == Status::Success);
}

void TestBindWhileOpenFails()
{
    Fixture fixture;
    Transaction transaction;
    CHECK(fixture.arena.TryBeginTransaction(&transaction) == Status::Success);
    Storage replacement;
    CHECK(fixture.arena.TryBind(
              replacement.bytes, sizeof(replacement.bytes), 11)
          == Status::InvalidPhase);
    CHECK(fixture.arena.TryUnbind() == Status::InvalidPhase);
    CHECK(fixture.arena.zoneIdentity() == 7);
    CHECK(fixture.arena.TryCommit(transaction) == Status::Success);

    // Rebinding after close resets accounting for the new zone.
    CHECK(fixture.arena.TryBind(
              replacement.bytes, sizeof(replacement.bytes), 11)
          == Status::Success);
    CHECK(fixture.arena.zoneIdentity() == 11);
    CHECK(fixture.arena.usedBytes() == 0);
    CHECK(fixture.arena.committedBytes() == 0);
}

void TestSequentialTransactionsRatchet()
{
    Fixture fixture;
    for (std::uint32_t round = 0; round < 8; ++round)
    {
        Transaction transaction;
        CHECK(fixture.arena.TryBeginTransaction(&transaction)
              == Status::Success);
        void *storage = nullptr;
        CHECK(fixture.arena.TryReserve(transaction, 40, 8, &storage)
              == Status::Success);
        CHECK(storage == fixture.storage.bytes + 40u * round);
        CHECK(fixture.arena.TryCommit(transaction) == Status::Success);
        CHECK(fixture.arena.committedBytes() == 40u * (round + 1u));
    }
}

void TestHighAlignmentUsesAbsoluteAddress()
{
    Fixture fixture;
    Transaction transaction;
    CHECK(fixture.arena.TryBeginTransaction(&transaction) == Status::Success);
    void *odd = nullptr;
    CHECK(fixture.arena.TryReserve(transaction, 1, 1, &odd)
          == Status::Success);
    void *wide = nullptr;
    CHECK(fixture.arena.TryReserve(transaction, 64, 64, &wide)
          == Status::Success);
    CHECK(reinterpret_cast<std::uintptr_t>(wide) % 64 == 0);
    CHECK(fixture.arena.TryAbandon(transaction) == Status::Success);
}
} // namespace

int main()
{
    TestBindValidation();
    TestUnboundOperationsFail();
    TestReserveCommit();
    TestReserveValidation();
    TestAbandonReclaimsAndRezeroes();
    TestNestedTransactions();
    TestAbandonAboveCommittedWatermark();
    TestForeignAndStaleTransactions();
    TestBindWhileOpenFails();
    TestSequentialTransactionsRatchet();
    TestHighAlignmentUsesAbsoluteAddress();

    if (failures != 0)
    {
        std::fprintf(stderr, "%d arena check(s) failed\n", failures);
        return 1;
    }
    std::puts("fx fast-file native arena tests passed");
    return 0;
}
