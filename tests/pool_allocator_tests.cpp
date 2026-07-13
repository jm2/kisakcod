#include <universal/pool_allocator.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>

namespace
{
std::size_t g_assertionCount = 0;
int g_failureCount = 0;

bool Check(const bool condition, const char *const message)
{
    if (condition)
        return true;

    std::fprintf(stderr, "FAIL: %s\n", message);
    ++g_failureCount;
    return false;
}

bool CheckAsserted(
    const std::size_t assertionCountBefore,
    const char *const message)
{
    return Check(g_assertionCount > assertionCountBefore, message);
}

constexpr std::size_t kSlotCount = 5;
constexpr std::size_t kSlotSize = sizeof(void *) + 5;
constexpr std::size_t kPrefixSize = 17;
constexpr std::size_t kSuffixSize = 19;
constexpr unsigned char kInitialByte = 0xA5u;

struct PoolFixture
{
    static constexpr std::size_t kPoolBytes = kSlotCount * kSlotSize;

    alignas(void *)
        std::array<unsigned char,
            kPrefixSize + kPoolBytes + kSuffixSize> bytes{};
    poolstorage_t storage{};
    pooldata_t state{};

    PoolFixture()
        : storage{bytes.data() + kPrefixSize, kSlotSize, kSlotCount}
    {
        bytes.fill(kInitialByte);
    }

    void *Slot(const std::size_t index) noexcept
    {
        return static_cast<void *>(
            static_cast<unsigned char *>(storage.base)
            + index * storage.itemSize);
    }

    const void *Slot(const std::size_t index) const noexcept
    {
        return static_cast<const void *>(
            static_cast<const unsigned char *>(storage.base)
            + index * storage.itemSize);
    }

    bool Initialize() noexcept
    {
        return Pool_Init(storage, &state);
    }
};

struct PoolSnapshot
{
    void *firstFree;
    int activeCount;
    std::array<unsigned char,
        kPrefixSize + PoolFixture::kPoolBytes + kSuffixSize> bytes;
};

PoolSnapshot Capture(const PoolFixture &fixture)
{
    return {fixture.state.firstFree, fixture.state.activeCount, fixture.bytes};
}

bool CheckUnchanged(
    const PoolFixture &fixture,
    const PoolSnapshot &snapshot,
    const char *const message)
{
    return Check(
        fixture.state.firstFree == snapshot.firstFree
            && fixture.state.activeCount == snapshot.activeCount
            && fixture.bytes == snapshot.bytes,
        message);
}

void StoreNext(void *const slot, void *const next) noexcept
{
    std::memcpy(slot, &next, sizeof(next));
}

void TestLayoutAndStorageFactory()
{
    static_assert(std::is_standard_layout_v<pooldata_t>);
    static_assert(offsetof(pooldata_t, firstFree) == 0);
    static_assert(offsetof(pooldata_t, activeCount) == sizeof(void *));
    static_assert(
        sizeof(pooldata_t) == (sizeof(void *) == 4 ? 8u : 16u));
    static_assert(noexcept(Pool_Init(poolstorage_t{}, nullptr)));
    static_assert(noexcept(Pool_Alloc(poolstorage_t{}, nullptr)));
    static_assert(noexcept(Pool_Free(poolstorage_t{}, nullptr, nullptr)));
    static_assert(noexcept(Pool_GetFreeCount(poolstorage_t{}, nullptr)));
    static_assert(noexcept(Pool_NextFree(poolstorage_t{}, nullptr)));
    static_assert(noexcept(Pool_GetSlotIndex(poolstorage_t{}, nullptr)));

    // The former output-pointer API let a caller alias query output with pool
    // storage or metadata. Keep those signatures uncallable: results must be
    // returned by value and carry their own validity bit.
    static_assert(std::is_nothrow_invocable_r_v<
        poolcountresult_t,
        decltype(Pool_GetFreeCount),
        poolstorage_t,
        const pooldata_t *>);
    static_assert(!std::is_invocable_v<
        decltype(Pool_GetFreeCount),
        poolstorage_t,
        const pooldata_t *,
        std::size_t *>);
    static_assert(std::is_nothrow_invocable_r_v<
        poolnextresult_t,
        decltype(Pool_NextFree),
        poolstorage_t,
        const void *>);
    static_assert(!std::is_invocable_v<
        decltype(Pool_NextFree),
        poolstorage_t,
        const void *,
        void **>);
    static_assert(std::is_nothrow_invocable_r_v<
        poolindexresult_t,
        decltype(Pool_GetSlotIndex),
        poolstorage_t,
        const void *>);
    static_assert(!std::is_invocable_v<
        decltype(Pool_GetSlotIndex),
        poolstorage_t,
        const void *,
        std::size_t *>);

    struct alignas(16) StaticSlot
    {
        std::array<unsigned char, 32> payload{};
    };

    StaticSlot slots[3]{};
    const poolstorage_t storage = Pool_StorageFor(slots);
    Check(storage.base == static_cast<void *>(slots),
        "Pool_StorageFor returned the wrong base");
    Check(storage.itemSize == sizeof(StaticSlot),
        "Pool_StorageFor returned the wrong stride");
    Check(storage.itemCount == 3,
        "Pool_StorageFor returned the wrong item count");

    pooldata_t state{};
    Check(Pool_Init(storage, &state),
        "Pool_StorageFor descriptor did not initialize");
    const poolcountresult_t freeCount = Pool_GetFreeCount(storage, &state);
    Check(freeCount.valid && freeCount.count == 3,
        "Pool_StorageFor descriptor reported the wrong free count");
}

void TestInitializationAndTraversal()
{
    PoolFixture fixture;
    Check(
        reinterpret_cast<std::uintptr_t>(fixture.storage.base)
                % alignof(void *)
            != 0,
        "test fixture must exercise an unaligned pool base");
    Check(fixture.storage.itemSize % alignof(void *) != 0,
        "test fixture must exercise an unaligned pool stride");
    Check(fixture.Initialize(),
        "valid unaligned pool initialization failed");
    Check(fixture.state.firstFree == fixture.Slot(0),
        "pool initialization published the wrong head");
    Check(fixture.state.activeCount == 0,
        "pool initialization published a nonzero active count");

    const poolcountresult_t freeCount =
        Pool_GetFreeCount(fixture.storage, &fixture.state);
    Check(freeCount.valid && freeCount.count == kSlotCount,
        "initialized pool reported the wrong free count");

    for (std::size_t index = 0; index < kSlotCount; ++index)
    {
        const void *const expectedNext = index + 1 < kSlotCount
            ? fixture.Slot(index + 1)
            : nullptr;
        const poolnextresult_t next =
            Pool_NextFree(fixture.storage, fixture.Slot(index));
        Check(next.valid && next.next == expectedNext,
            "initialized pool link did not retain its full pointer value");

        const poolindexresult_t slotIndex =
            Pool_GetSlotIndex(fixture.storage, fixture.Slot(index));
        Check(slotIndex.valid && slotIndex.index == index,
            "exact pool member mapped to the wrong slot index");

        const auto *const slot = static_cast<const unsigned char *>(
            fixture.Slot(index));
        for (std::size_t byte = sizeof(void *);
             byte < fixture.storage.itemSize;
             ++byte)
        {
            Check(slot[byte] == kInitialByte,
                "Pool_Init overwrote bytes beyond the freelist link");
        }
    }

    for (std::size_t byte = 0; byte < kPrefixSize; ++byte)
    {
        Check(fixture.bytes[byte] == kInitialByte,
            "Pool_Init overwrote the prefix guard");
    }
    for (std::size_t byte = kPrefixSize + PoolFixture::kPoolBytes;
         byte < fixture.bytes.size();
         ++byte)
    {
        Check(fixture.bytes[byte] == kInitialByte,
            "Pool_Init overwrote the suffix guard");
    }
}

void TestAllocationFreeAndLifo()
{
    PoolFixture fixture;
    Check(fixture.Initialize(), "allocation fixture failed to initialize");

    std::array<void *, kSlotCount> allocated{};
    for (std::size_t index = 0; index < kSlotCount; ++index)
    {
        allocated[index] = Pool_Alloc(fixture.storage, &fixture.state);
        Check(allocated[index] == fixture.Slot(index),
            "Pool_Alloc did not consume the ascending initialized chain");
        Check(fixture.state.activeCount == static_cast<int>(index + 1),
            "Pool_Alloc updated activeCount incorrectly");

        const poolcountresult_t freeCount =
            Pool_GetFreeCount(fixture.storage, &fixture.state);
        Check(freeCount.valid
                && freeCount.count == kSlotCount - index - 1,
            "Pool_Alloc updated the free count incorrectly");
    }

    const PoolSnapshot exhausted = Capture(fixture);
    const std::size_t assertionsBeforeExhaustion = g_assertionCount;
    Check(Pool_Alloc(fixture.storage, &fixture.state) == nullptr,
        "exhausted pool returned an item");
    Check(g_assertionCount == assertionsBeforeExhaustion,
        "ordinary pool exhaustion raised an assertion");
    CheckUnchanged(fixture, exhausted,
        "ordinary pool exhaustion mutated allocator state");

    std::memset(allocated[1], 0x5A, fixture.storage.itemSize);
    Check(Pool_Free(fixture.storage, &fixture.state, allocated[1]),
        "first valid free failed");
    const poolnextresult_t firstNext =
        Pool_NextFree(fixture.storage, allocated[1]);
    Check(firstNext.valid && firstNext.next == nullptr,
        "first valid free did not store a full-width null link");
    const auto *const firstFreedBytes =
        static_cast<const unsigned char *>(allocated[1]);
    for (std::size_t byte = sizeof(void *);
         byte < fixture.storage.itemSize;
         ++byte)
    {
        Check(firstFreedBytes[byte] == 0x5Au,
            "Pool_Free overwrote payload bytes after the link");
    }

    std::memset(allocated[3], 0x3C, fixture.storage.itemSize);
    Check(Pool_Free(fixture.storage, &fixture.state, allocated[3]),
        "second valid free failed");
    const poolnextresult_t secondNext =
        Pool_NextFree(fixture.storage, allocated[3]);
    Check(secondNext.valid && secondNext.next == allocated[1],
        "second valid free did not link to the previous head");
    const auto *const secondFreedBytes =
        static_cast<const unsigned char *>(allocated[3]);
    for (std::size_t byte = sizeof(void *);
         byte < fixture.storage.itemSize;
         ++byte)
    {
        Check(secondFreedBytes[byte] == 0x3Cu,
            "second Pool_Free overwrote payload bytes after the link");
    }

    Check(Pool_Alloc(fixture.storage, &fixture.state) == allocated[3],
        "freelist did not return the newest free first");
    Check(Pool_Alloc(fixture.storage, &fixture.state) == allocated[1],
        "freelist did not return the older free second");

    for (std::size_t index = 0; index < kSlotCount; ++index)
    {
        std::memset(
            allocated[index],
            static_cast<int>(0x20u + index),
            fixture.storage.itemSize);
        Check(Pool_Free(
                  fixture.storage, &fixture.state, allocated[index]),
            "valid refill free failed");
    }
    Check(fixture.state.activeCount == 0,
        "freeing every item did not clear activeCount");

    for (std::size_t index = 0; index < kSlotCount; ++index)
    {
        const std::size_t expectedIndex = kSlotCount - index - 1;
        Check(Pool_Alloc(fixture.storage, &fixture.state)
                == allocated[expectedIndex],
            "refilled pool did not preserve LIFO order");
    }
}

void TestInvalidInitialization()
{
    PoolFixture fixture;
    const auto originalBytes = fixture.bytes;

    const auto rejectDescriptor = [&](const poolstorage_t storage,
                                      const char *const description) {
        fixture.state.firstFree = fixture.Slot(2);
        fixture.state.activeCount = 3;
        fixture.bytes = originalBytes;
        const std::size_t assertionsBefore = g_assertionCount;
        Check(!Pool_Init(storage, &fixture.state), description);
        CheckAsserted(assertionsBefore,
            "invalid Pool_Init descriptor did not assert");
        Check(fixture.state.firstFree == nullptr
                && fixture.state.activeCount == 0,
            "invalid Pool_Init did not fail closed");
        Check(fixture.bytes == originalBytes,
            "invalid Pool_Init modified backing storage");
    };

    rejectDescriptor(
        {nullptr, kSlotSize, kSlotCount},
        "Pool_Init accepted a null base");
    rejectDescriptor(
        {fixture.storage.base, 0, kSlotCount},
        "Pool_Init accepted a zero stride");
    rejectDescriptor(
        {fixture.storage.base, sizeof(void *) - 1, kSlotCount},
        "Pool_Init accepted a sub-pointer stride");
    rejectDescriptor(
        {fixture.storage.base, kSlotSize, 0},
        "Pool_Init accepted a zero item count");
    rejectDescriptor(
        {fixture.storage.base, kSlotSize, 1},
        "Pool_Init accepted a one-item pool");

    const std::size_t tooManyItems =
        static_cast<std::size_t>((std::numeric_limits<int>::max)()) + 1u;
    rejectDescriptor(
        {fixture.storage.base, kSlotSize, tooManyItems},
        "Pool_Init accepted a count that cannot fit activeCount");

    const std::size_t overflowingStride =
        (std::numeric_limits<std::size_t>::max)() / 2u + 1u;
    rejectDescriptor(
        {fixture.storage.base, overflowingStride, 2},
        "Pool_Init accepted a size multiplication overflow");

    const std::uintptr_t overflowingBase =
        (std::numeric_limits<std::uintptr_t>::max)()
        - sizeof(void *) + 1u;
    rejectDescriptor(
        {reinterpret_cast<void *>(overflowingBase), sizeof(void *), 2},
        "Pool_Init accepted an address-range overflow");

    pooldata_t overlappingStorage[2]{};
    overlappingStorage[0].firstFree = &overlappingStorage[1];
    overlappingStorage[0].activeCount = 1;
    std::array<unsigned char, sizeof(overlappingStorage)> overlapSnapshot{};
    std::memcpy(
        overlapSnapshot.data(), overlappingStorage, overlapSnapshot.size());
    const std::size_t assertionsBeforeOverlap = g_assertionCount;
    Check(!Pool_Init(
              Pool_StorageFor(overlappingStorage),
              &overlappingStorage[0]),
        "Pool_Init accepted metadata inside its backing storage");
    CheckAsserted(assertionsBeforeOverlap,
        "overlapping Pool_Init metadata did not assert");
    Check(std::memcmp(
              overlappingStorage,
              overlapSnapshot.data(),
              overlapSnapshot.size())
            == 0,
        "overlapping Pool_Init modified backing storage or metadata");

    fixture.bytes = originalBytes;
    const std::size_t assertionsBeforeNullState = g_assertionCount;
    Check(!Pool_Init(fixture.storage, nullptr),
        "Pool_Init accepted a null state");
    CheckAsserted(assertionsBeforeNullState,
        "null Pool_Init state did not assert");
    Check(fixture.bytes == originalBytes,
        "null Pool_Init state modified backing storage");
}

void TestInvalidArgumentsAndMembership()
{
    PoolFixture fixture;
    Check(fixture.Initialize(),
        "invalid-operation fixture failed to initialize");
    void *const allocated = Pool_Alloc(fixture.storage, &fixture.state);
    Check(allocated == fixture.Slot(0),
        "invalid-operation fixture did not allocate its first slot");

    alignas(void *) std::array<unsigned char, sizeof(void *)> foreign{};
    void *const interior = static_cast<void *>(
        static_cast<unsigned char *>(fixture.storage.base) + 1);
    void *const onePast = static_cast<void *>(
        static_cast<unsigned char *>(fixture.storage.base)
        + fixture.storage.itemSize * fixture.storage.itemCount);
    const poolstorage_t invalidStorage{
        fixture.storage.base, sizeof(void *) - 1, kSlotCount};

    const auto rejectWithoutMutation = [&](const auto &operation,
                                           const char *const rejected,
                                           const char *const asserted,
                                           const char *const unchanged) {
        const PoolSnapshot snapshot = Capture(fixture);
        const std::size_t assertionsBefore = g_assertionCount;
        Check(operation(), rejected);
        CheckAsserted(assertionsBefore, asserted);
        CheckUnchanged(fixture, snapshot, unchanged);
    };

    rejectWithoutMutation(
        [&]() { return Pool_Alloc(fixture.storage, nullptr) == nullptr; },
        "Pool_Alloc accepted a null state",
        "null Pool_Alloc state did not assert",
        "null Pool_Alloc state mutated storage");
    rejectWithoutMutation(
        [&]() { return Pool_Alloc(invalidStorage, &fixture.state) == nullptr; },
        "Pool_Alloc accepted an invalid descriptor",
        "invalid Pool_Alloc descriptor did not assert",
        "invalid Pool_Alloc descriptor mutated state");
    rejectWithoutMutation(
        [&]() {
            return !Pool_Free(invalidStorage, &fixture.state, allocated);
        },
        "Pool_Free accepted an invalid descriptor",
        "invalid Pool_Free descriptor did not assert",
        "invalid Pool_Free descriptor mutated state");
    rejectWithoutMutation(
        [&]() { return !Pool_Free(fixture.storage, nullptr, allocated); },
        "Pool_Free accepted a null state",
        "null Pool_Free state did not assert",
        "null Pool_Free state mutated storage");
    rejectWithoutMutation(
        [&]() { return !Pool_Free(fixture.storage, &fixture.state, nullptr); },
        "Pool_Free accepted a null item",
        "null Pool_Free item did not assert",
        "null Pool_Free item mutated state");
    rejectWithoutMutation(
        [&]() { return !Pool_Free(fixture.storage, &fixture.state, interior); },
        "Pool_Free accepted an interior pointer",
        "interior Pool_Free pointer did not assert",
        "interior Pool_Free pointer mutated state");
    rejectWithoutMutation(
        [&]() {
            return !Pool_Free(
                fixture.storage, &fixture.state, foreign.data());
        },
        "Pool_Free accepted a foreign pointer",
        "foreign Pool_Free pointer did not assert",
        "foreign Pool_Free pointer mutated state");
    rejectWithoutMutation(
        [&]() { return !Pool_Free(fixture.storage, &fixture.state, onePast); },
        "Pool_Free accepted the one-past pointer",
        "one-past Pool_Free pointer did not assert",
        "one-past Pool_Free pointer mutated state");

    rejectWithoutMutation(
        [&]() {
            const poolcountresult_t result =
                Pool_GetFreeCount(fixture.storage, nullptr);
            return !result.valid && result.count == 0;
        },
        "Pool_GetFreeCount accepted a null state",
        "null Pool_GetFreeCount state did not assert",
        "null Pool_GetFreeCount state mutated storage");
    rejectWithoutMutation(
        [&]() {
            const poolcountresult_t result =
                Pool_GetFreeCount(invalidStorage, &fixture.state);
            return !result.valid && result.count == 0;
        },
        "Pool_GetFreeCount accepted an invalid descriptor",
        "invalid Pool_GetFreeCount descriptor did not assert",
        "invalid Pool_GetFreeCount descriptor mutated state");

    rejectWithoutMutation(
        [&]() {
            const poolnextresult_t result =
                Pool_NextFree(fixture.storage, nullptr);
            return !result.valid && result.next == nullptr;
        },
        "Pool_NextFree accepted a null node",
        "null Pool_NextFree node did not assert",
        "null Pool_NextFree node mutated state");
    rejectWithoutMutation(
        [&]() {
            const poolnextresult_t result =
                Pool_NextFree(fixture.storage, interior);
            return !result.valid && result.next == nullptr;
        },
        "Pool_NextFree accepted an interior node",
        "interior Pool_NextFree node did not assert",
        "interior Pool_NextFree node mutated state");
    rejectWithoutMutation(
        [&]() {
            const poolnextresult_t result =
                Pool_NextFree(fixture.storage, foreign.data());
            return !result.valid && result.next == nullptr;
        },
        "Pool_NextFree accepted a foreign node",
        "foreign Pool_NextFree node did not assert",
        "foreign Pool_NextFree node mutated state");
    rejectWithoutMutation(
        [&]() {
            const poolnextresult_t result =
                Pool_NextFree(invalidStorage, fixture.Slot(1));
            return !result.valid && result.next == nullptr;
        },
        "Pool_NextFree accepted an invalid descriptor",
        "invalid Pool_NextFree descriptor did not assert",
        "invalid Pool_NextFree descriptor mutated state");

    rejectWithoutMutation(
        [&]() {
            const poolindexresult_t result =
                Pool_GetSlotIndex(fixture.storage, nullptr);
            return !result.valid && result.index == 0;
        },
        "Pool_GetSlotIndex accepted a null item",
        "null Pool_GetSlotIndex item did not assert",
        "null Pool_GetSlotIndex item mutated state");
    rejectWithoutMutation(
        [&]() {
            const poolindexresult_t result =
                Pool_GetSlotIndex(fixture.storage, interior);
            return !result.valid && result.index == 0;
        },
        "Pool_GetSlotIndex accepted an interior item",
        "interior Pool_GetSlotIndex item did not assert",
        "interior Pool_GetSlotIndex item mutated state");
    rejectWithoutMutation(
        [&]() {
            const poolindexresult_t result =
                Pool_GetSlotIndex(fixture.storage, foreign.data());
            return !result.valid && result.index == 0;
        },
        "Pool_GetSlotIndex accepted a foreign item",
        "foreign Pool_GetSlotIndex item did not assert",
        "foreign Pool_GetSlotIndex item mutated state");
    rejectWithoutMutation(
        [&]() {
            const poolindexresult_t result =
                Pool_GetSlotIndex(fixture.storage, onePast);
            return !result.valid && result.index == 0;
        },
        "Pool_GetSlotIndex accepted the one-past item",
        "one-past Pool_GetSlotIndex item did not assert",
        "one-past Pool_GetSlotIndex item mutated state");
    rejectWithoutMutation(
        [&]() {
            const poolindexresult_t result =
                Pool_GetSlotIndex(invalidStorage, fixture.Slot(1));
            return !result.valid && result.index == 0;
        },
        "Pool_GetSlotIndex accepted an invalid descriptor",
        "invalid Pool_GetSlotIndex descriptor did not assert",
        "invalid Pool_GetSlotIndex descriptor mutated state");
}

void TestDuplicateFreeAndCounterBounds()
{
    PoolFixture fixture;
    Check(fixture.Initialize(),
        "duplicate-free fixture failed to initialize");
    void *const first = Pool_Alloc(fixture.storage, &fixture.state);
    void *const second = Pool_Alloc(fixture.storage, &fixture.state);
    void *const third = Pool_Alloc(fixture.storage, &fixture.state);
    Check(first && second && third,
        "duplicate-free fixture failed to allocate three slots");
    Check(Pool_Free(fixture.storage, &fixture.state, first),
        "duplicate-free fixture failed its first free");
    Check(Pool_Free(fixture.storage, &fixture.state, second),
        "duplicate-free fixture failed its second free");

    PoolSnapshot snapshot = Capture(fixture);
    std::size_t assertionsBefore = g_assertionCount;
    Check(!Pool_Free(fixture.storage, &fixture.state, second),
        "Pool_Free accepted a duplicate freelist head");
    CheckAsserted(assertionsBefore,
        "duplicate head free did not assert");
    CheckUnchanged(fixture, snapshot,
        "duplicate head free mutated allocator state");

    snapshot = Capture(fixture);
    assertionsBefore = g_assertionCount;
    Check(!Pool_Free(fixture.storage, &fixture.state, first),
        "Pool_Free accepted a duplicate deeper in the freelist");
    CheckAsserted(assertionsBefore,
        "deep duplicate free did not assert");
    CheckUnchanged(fixture, snapshot,
        "deep duplicate free mutated allocator state");

    PoolFixture zeroActive;
    Check(zeroActive.Initialize(),
        "zero-active fixture failed to initialize");
    snapshot = Capture(zeroActive);
    assertionsBefore = g_assertionCount;
    Check(!Pool_Free(zeroActive.storage, &zeroActive.state, zeroActive.Slot(0)),
        "Pool_Free underflowed activeCount");
    CheckAsserted(assertionsBefore,
        "activeCount underflow did not assert");
    CheckUnchanged(zeroActive, snapshot,
        "activeCount underflow mutated allocator state");

    PoolFixture negativeActive;
    Check(negativeActive.Initialize(),
        "negative-active fixture failed to initialize");
    negativeActive.state.activeCount = -1;
    snapshot = Capture(negativeActive);
    assertionsBefore = g_assertionCount;
    Check(Pool_Alloc(negativeActive.storage, &negativeActive.state) == nullptr,
        "Pool_Alloc accepted a negative activeCount");
    CheckAsserted(assertionsBefore,
        "negative activeCount did not assert");
    CheckUnchanged(negativeActive, snapshot,
        "negative activeCount mutated allocator state");

    PoolFixture excessiveActive;
    Check(excessiveActive.Initialize(),
        "excessive-active fixture failed to initialize");
    excessiveActive.state.activeCount =
        static_cast<int>(excessiveActive.storage.itemCount);
    snapshot = Capture(excessiveActive);
    assertionsBefore = g_assertionCount;
    Check(Pool_Alloc(excessiveActive.storage, &excessiveActive.state) == nullptr,
        "Pool_Alloc accepted a nonempty head at maximum activeCount");
    CheckAsserted(assertionsBefore,
        "excessive activeCount did not assert");
    CheckUnchanged(excessiveActive, snapshot,
        "excessive activeCount mutated allocator state");
}

void ExpectCorruptStateRejected(
    PoolFixture &fixture,
    void *const allocated,
    const char *const description)
{
    PoolSnapshot snapshot = Capture(fixture);
    std::size_t assertionsBefore = g_assertionCount;
    const poolcountresult_t freeCount =
        Pool_GetFreeCount(fixture.storage, &fixture.state);
    Check(!freeCount.valid && freeCount.count == 0, description);
    CheckAsserted(assertionsBefore,
        "corrupt free-count query did not assert");
    if (!CheckUnchanged(fixture, snapshot,
            "corrupt free-count query mutated allocator state"))
    {
        return;
    }

    snapshot = Capture(fixture);
    assertionsBefore = g_assertionCount;
    Check(Pool_Alloc(fixture.storage, &fixture.state) == nullptr,
        "Pool_Alloc accepted a corrupt freelist");
    CheckAsserted(assertionsBefore,
        "corrupt Pool_Alloc did not assert");
    if (!CheckUnchanged(fixture, snapshot,
            "corrupt Pool_Alloc mutated allocator state"))
    {
        return;
    }

    snapshot = Capture(fixture);
    assertionsBefore = g_assertionCount;
    Check(!Pool_Free(fixture.storage, &fixture.state, allocated),
        "Pool_Free accepted a corrupt freelist");
    CheckAsserted(assertionsBefore,
        "corrupt Pool_Free did not assert");
    CheckUnchanged(fixture, snapshot,
        "corrupt Pool_Free mutated allocator state");
}

void PrepareOneAllocated(PoolFixture &fixture, void **const allocated)
{
    Check(fixture.Initialize(), "corruption fixture failed to initialize");
    *allocated = Pool_Alloc(fixture.storage, &fixture.state);
    Check(*allocated == fixture.Slot(0),
        "corruption fixture failed to allocate slot zero");
}

void TestCorruptChains()
{
    alignas(void *) std::array<unsigned char, sizeof(void *)> foreign{};

    {
        PoolFixture fixture;
        void *allocated = nullptr;
        PrepareOneAllocated(fixture, &allocated);
        fixture.state.firstFree = foreign.data();
        ExpectCorruptStateRejected(fixture, allocated,
            "Pool_GetFreeCount accepted a foreign freelist head");
    }
    {
        PoolFixture fixture;
        void *allocated = nullptr;
        PrepareOneAllocated(fixture, &allocated);
        fixture.state.firstFree = static_cast<void *>(
            static_cast<unsigned char *>(fixture.storage.base) + 1);
        ExpectCorruptStateRejected(fixture, allocated,
            "Pool_GetFreeCount accepted an interior freelist head");
    }
    {
        PoolFixture fixture;
        void *allocated = nullptr;
        PrepareOneAllocated(fixture, &allocated);
        fixture.state.firstFree = nullptr;
        ExpectCorruptStateRejected(fixture, allocated,
            "Pool_GetFreeCount accepted a prematurely empty chain");
    }
    {
        PoolFixture fixture;
        void *allocated = nullptr;
        PrepareOneAllocated(fixture, &allocated);
        StoreNext(fixture.Slot(1), foreign.data());
        ExpectCorruptStateRejected(fixture, allocated,
            "Pool_GetFreeCount accepted a foreign next link");

        const std::size_t assertionsBefore = g_assertionCount;
        const poolnextresult_t next =
            Pool_NextFree(fixture.storage, fixture.Slot(1));
        Check(!next.valid && next.next == nullptr,
            "Pool_NextFree accepted a foreign next link");
        CheckAsserted(assertionsBefore,
            "foreign Pool_NextFree link did not assert");
    }
    {
        PoolFixture fixture;
        void *allocated = nullptr;
        PrepareOneAllocated(fixture, &allocated);
        void *const interior = static_cast<void *>(
            static_cast<unsigned char *>(fixture.Slot(2)) + 1);
        StoreNext(fixture.Slot(1), interior);
        ExpectCorruptStateRejected(fixture, allocated,
            "Pool_GetFreeCount accepted an interior next link");

        const std::size_t assertionsBefore = g_assertionCount;
        const poolnextresult_t next =
            Pool_NextFree(fixture.storage, fixture.Slot(1));
        Check(!next.valid && next.next == nullptr,
            "Pool_NextFree accepted an interior next link");
        CheckAsserted(assertionsBefore,
            "interior Pool_NextFree link did not assert");
    }
    {
        PoolFixture fixture;
        void *allocated = nullptr;
        PrepareOneAllocated(fixture, &allocated);
        StoreNext(fixture.Slot(1), fixture.Slot(1));
        ExpectCorruptStateRejected(fixture, allocated,
            "Pool_GetFreeCount accepted a self-cycle");
    }
    {
        PoolFixture fixture;
        void *allocated = nullptr;
        PrepareOneAllocated(fixture, &allocated);
        StoreNext(fixture.Slot(1), fixture.Slot(2));
        StoreNext(fixture.Slot(2), fixture.Slot(1));
        ExpectCorruptStateRejected(fixture, allocated,
            "Pool_GetFreeCount accepted a two-node cycle");
    }
    {
        PoolFixture fixture;
        void *allocated = nullptr;
        PrepareOneAllocated(fixture, &allocated);
        StoreNext(fixture.Slot(2), nullptr);
        ExpectCorruptStateRejected(fixture, allocated,
            "Pool_GetFreeCount accepted a short acyclic chain");
    }
    {
        PoolFixture fixture;
        void *allocated = nullptr;
        PrepareOneAllocated(fixture, &allocated);
        StoreNext(fixture.Slot(4), fixture.Slot(0));
        StoreNext(fixture.Slot(0), nullptr);
        ExpectCorruptStateRejected(fixture, allocated,
            "Pool_GetFreeCount accepted a long acyclic chain");
    }
    {
        PoolFixture fixture;
        void *allocated = nullptr;
        PrepareOneAllocated(fixture, &allocated);
        fixture.state.activeCount = -1;
        ExpectCorruptStateRejected(fixture, allocated,
            "Pool_GetFreeCount accepted a negative activeCount");
    }
    {
        PoolFixture fixture;
        void *allocated = nullptr;
        PrepareOneAllocated(fixture, &allocated);
        fixture.state.activeCount =
            static_cast<int>(fixture.storage.itemCount) + 1;
        ExpectCorruptStateRejected(fixture, allocated,
            "Pool_GetFreeCount accepted activeCount above capacity");
    }
}
} // namespace

void MyAssertHandler(const char *, int, int, const char *, ...)
{
    ++g_assertionCount;
}

int main()
{
    TestLayoutAndStorageFactory();
    TestInitializationAndTraversal();
    TestAllocationFreeAndLifo();
    TestInvalidInitialization();
    TestInvalidArgumentsAndMembership();
    TestDuplicateFreeAndCounterBounds();
    TestCorruptChains();

    if (g_failureCount != 0)
    {
        std::fprintf(stderr, "%d pool allocator tests failed\n", g_failureCount);
        return 1;
    }

    std::puts("native pointer pool allocator tests passed");
    return 0;
}
