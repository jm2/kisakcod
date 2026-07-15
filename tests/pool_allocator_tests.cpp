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

constexpr std::size_t kSlotCount = 9;
constexpr std::size_t kSlotSize = sizeof(void *) + 5;
constexpr std::size_t kPrefixSize = 17;
constexpr std::size_t kSuffixSize = 19;
constexpr unsigned char kInitialByte = 0xA5u;
constexpr poolslotstate_t kUninitializedSlot = 0xCCCCCCCCu;

struct PoolFixture
{
    static constexpr std::size_t kPoolBytes = kSlotCount * kSlotSize;

    alignas(void *)
        std::array<unsigned char,
            kPrefixSize + kPoolBytes + kSuffixSize> bytes{};
    poolslotstate_t slotState[kSlotCount]{};
    poolcontrol_t control;
    poolstorage_t storage;
    pooldata_t data{};

    PoolFixture()
        : control(Pool_ControlFor(slotState)),
          storage(Pool_StorageFor(
              bytes.data() + kPrefixSize,
              kSlotSize,
              kSlotCount,
              control))
    {
        bytes.fill(kInitialByte);
        for (std::size_t index = 0; index < kSlotCount; ++index)
            slotState[index] = kUninitializedSlot;
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
        return Pool_Init(storage, &data);
    }
};

struct PoolSnapshot
{
    void *firstFree;
    int retailActiveCount;
    poolcontrol_t control;
    std::array<poolslotstate_t, kSlotCount> slotState;
    std::array<unsigned char,
        kPrefixSize + PoolFixture::kPoolBytes + kSuffixSize> bytes;
};

bool ControlsEqual(
    const poolcontrol_t &left,
    const poolcontrol_t &right) noexcept
{
    return left.slotState == right.slotState
        && left.slotStateCount == right.slotStateCount
        && left.boundBase == right.boundBase
        && left.boundData == right.boundData
        && left.boundStride == right.boundStride
        && left.boundCount == right.boundCount
        && left.headIndex == right.headIndex
        && left.activeCount == right.activeCount
        && left.initMagic == right.initMagic;
}

PoolSnapshot Capture(const PoolFixture &fixture)
{
    PoolSnapshot snapshot{
        fixture.data.firstFree,
        fixture.data.activeCount,
        fixture.control,
        {},
        fixture.bytes,
    };
    for (std::size_t index = 0; index < kSlotCount; ++index)
        snapshot.slotState[index] = fixture.slotState[index];
    return snapshot;
}

bool CheckUnchanged(
    const PoolFixture &fixture,
    const PoolSnapshot &snapshot,
    const char *const message)
{
    bool slotsEqual = true;
    for (std::size_t index = 0; index < kSlotCount; ++index)
        slotsEqual = slotsEqual
            && fixture.slotState[index] == snapshot.slotState[index];

    return Check(
        fixture.data.firstFree == snapshot.firstFree
            && fixture.data.activeCount == snapshot.retailActiveCount
            && ControlsEqual(fixture.control, snapshot.control)
            && slotsEqual
            && fixture.bytes == snapshot.bytes,
        message);
}

bool RawLinkEquals(const void *const slot, void *const expected) noexcept
{
    return std::memcmp(slot, &expected, sizeof(expected)) == 0;
}

void StoreRawLink(void *const slot, void *const next) noexcept
{
    std::memcpy(slot, &next, sizeof(next));
}

std::array<unsigned char,
    kPrefixSize + PoolFixture::kPoolBytes + kSuffixSize>
ExpectedAfterLinkWrite(
    const std::array<unsigned char,
        kPrefixSize + PoolFixture::kPoolBytes + kSuffixSize> &before,
    const std::size_t slotIndex,
    void *const next)
{
    auto expected = before;
    std::memcpy(
        expected.data() + kPrefixSize + slotIndex * kSlotSize,
        &next,
        sizeof(next));
    return expected;
}

void SetWrappingStorageBase(poolstorage_t &storage)
{
    const std::uintptr_t base =
        (std::numeric_limits<std::uintptr_t>::max)()
        - sizeof(void *) + 1u;
    storage.base = reinterpret_cast<void *>(base);
    storage.itemSize = sizeof(void *);
    storage.itemCount = 2;
}

template <typename Operation>
void ExpectRejectedWithoutMutation(
    PoolFixture &fixture,
    const Operation &operation,
    const char *const rejectedMessage,
    const char *const assertionMessage,
    const char *const mutationMessage)
{
    const PoolSnapshot snapshot = Capture(fixture);
    const std::size_t assertionsBefore = g_assertionCount;
    Check(operation(), rejectedMessage);
    CheckAsserted(assertionsBefore, assertionMessage);
    CheckUnchanged(fixture, snapshot, mutationMessage);
}

void CheckInitializedBinding(const PoolFixture &fixture)
{
    Check(fixture.control.slotState == fixture.slotState,
        "initialization changed the sidecar pointer");
    Check(fixture.control.slotStateCount == kSlotCount,
        "initialization changed the sidecar count");
    Check(fixture.control.boundBase == fixture.storage.base,
        "initialization bound the wrong pool base");
    Check(fixture.control.boundData == &fixture.data,
        "initialization bound the wrong retail metadata");
    Check(fixture.control.boundStride == fixture.storage.itemSize,
        "initialization bound the wrong pool stride");
    Check(fixture.control.boundCount == fixture.storage.itemCount,
        "initialization bound the wrong slot count");
    Check(fixture.control.headIndex == 0,
        "initialization published the wrong shadow head");
    Check(fixture.control.activeCount == 0,
        "initialization published a nonzero shadow active count");
    Check(fixture.control.initMagic != 0,
        "initialization did not publish its validity marker");
}

void TestLayoutAndFactories()
{
    static_assert(std::is_standard_layout<pooldata_t>::value, "pooldata ABI");
    static_assert(offsetof(pooldata_t, firstFree) == 0, "pooldata firstFree");
    static_assert(
        offsetof(pooldata_t, activeCount) == sizeof(void *),
        "pooldata activeCount");
    static_assert(
        sizeof(pooldata_t) == (sizeof(void *) == 4 ? 8u : 16u),
        "pooldata native size");
    static_assert(std::is_same<poolslotstate_t, std::uint32_t>::value,
        "slot states must have a fixed width");
    static_assert(POOL_SLOT_END == UINT32_MAX - 1u, "end sentinel value");
    static_assert(POOL_SLOT_ALLOCATED == UINT32_MAX,
        "allocated sentinel value");
    static_assert(POOL_SLOT_END != POOL_SLOT_ALLOCATED,
        "slot sentinels must be distinct");

    static_assert(noexcept(Pool_Init(poolstorage_t{}, nullptr)), "init noexcept");
    static_assert(noexcept(Pool_Invalidate(poolstorage_t{}, nullptr)),
        "invalidate noexcept");
    static_assert(noexcept(Pool_Alloc(poolstorage_t{}, nullptr)),
        "alloc noexcept");
    static_assert(noexcept(Pool_Free(poolstorage_t{}, nullptr, nullptr)),
        "free noexcept");
    static_assert(noexcept(Pool_TryAllocNoReport(poolstorage_t{}, nullptr)),
        "silent alloc noexcept");
    static_assert(noexcept(
        Pool_TryFreeNoReport(poolstorage_t{}, nullptr, nullptr)),
        "silent free noexcept");
    static_assert(noexcept(Pool_TryValidateAllocatedNoReport(
        poolstorage_t{}, nullptr, nullptr)),
        "silent allocated validation noexcept");
    static_assert(noexcept(
        Pool_TryValidateFullNoReport(poolstorage_t{}, nullptr)),
        "silent full validation noexcept");
    static_assert(noexcept(Pool_ValidateFull(poolstorage_t{}, nullptr)),
        "full validation noexcept");
    static_assert(noexcept(Pool_GetFreeCount(poolstorage_t{}, nullptr)),
        "count noexcept");
    static_assert(noexcept(Pool_NextFree(poolstorage_t{}, nullptr)),
        "next noexcept");
    static_assert(noexcept(Pool_GetSlotIndex(poolstorage_t{}, nullptr)),
        "index noexcept");

    static_assert(std::is_nothrow_invocable_r<
        poolcountresult_t,
        decltype(Pool_GetFreeCount),
        poolstorage_t,
        const pooldata_t *>::value,
        "free count signature");
    static_assert(!std::is_invocable<
        decltype(Pool_GetFreeCount),
        poolstorage_t,
        const pooldata_t *,
        std::size_t *>::value,
        "legacy free-count output must stay unavailable");
    static_assert(!std::is_invocable<
        decltype(Pool_NextFree),
        poolstorage_t,
        const void *,
        void **>::value,
        "legacy next output must stay unavailable");
    static_assert(!std::is_invocable<
        decltype(Pool_GetSlotIndex),
        poolstorage_t,
        const void *,
        std::size_t *>::value,
        "legacy index output must stay unavailable");

    struct alignas(16) StaticSlot
    {
        unsigned char payload[32];
    };

    StaticSlot slots[3]{};
    poolslotstate_t states[3]{};
    poolcontrol_t control = Pool_ControlFor(states);
    const poolstorage_t storage = Pool_StorageFor(slots, control);
    Check(storage.base == static_cast<void *>(slots),
        "Pool_StorageFor returned the wrong base");
    Check(storage.itemSize == sizeof(StaticSlot),
        "Pool_StorageFor returned the wrong stride");
    Check(storage.itemCount == 3,
        "Pool_StorageFor returned the wrong item count");
    Check(storage.control == &control,
        "Pool_StorageFor returned the wrong control object");
    Check(control.slotState == states && control.slotStateCount == 3,
        "Pool_ControlFor returned the wrong sidecar extent");

    pooldata_t data{};
    Check(Pool_Init(storage, &data),
        "typed factories did not initialize a pool");
    Check(Pool_ValidateFull(storage, &data),
        "typed-factory pool did not pass full validation");
    const poolcountresult_t count = Pool_GetFreeCount(storage, &data);
    Check(count.valid && count.count == 3,
        "typed-factory pool reported the wrong free count");
}

void TestInitializationAndTraversal()
{
    PoolFixture fixture;
    const auto originalBytes = fixture.bytes;
    Check(
        reinterpret_cast<std::uintptr_t>(fixture.storage.base)
                % alignof(void *)
            != 0,
        "fixture must exercise an unaligned pool base");
    Check(fixture.storage.itemSize % alignof(void *) != 0,
        "fixture must exercise an unaligned pool stride");
    Check(fixture.Initialize(), "valid unaligned pool initialization failed");
    Check(fixture.data.firstFree == fixture.Slot(0),
        "initialization published the wrong retail head");
    Check(fixture.data.activeCount == 0,
        "initialization published a nonzero retail active count");
    CheckInitializedBinding(fixture);

    for (std::size_t index = 0; index < kSlotCount; ++index)
    {
        const poolslotstate_t expected = index + 1 < kSlotCount
            ? static_cast<poolslotstate_t>(index + 1)
            : POOL_SLOT_END;
        Check(fixture.slotState[index] == expected,
            "Pool_Init built the wrong shadow chain");

        const void *const expectedNext = index + 1 < kSlotCount
            ? fixture.Slot(index + 1)
            : nullptr;
        Check(RawLinkEquals(
                  fixture.Slot(index),
                  const_cast<void *>(expectedNext)),
            "Pool_Init wrote the wrong compatibility link bytes");
        const poolnextresult_t next =
            Pool_NextFree(fixture.storage, fixture.Slot(index));
        Check(next.valid && next.next == expectedNext,
            "Pool_NextFree did not follow the shadow chain");

        const poolindexresult_t slot =
            Pool_GetSlotIndex(fixture.storage, fixture.Slot(index));
        Check(slot.valid && slot.index == index,
            "exact pool member mapped to the wrong slot index");

        const auto *const bytes = static_cast<const unsigned char *>(
            fixture.Slot(index));
        for (std::size_t byte = sizeof(void *);
             byte < fixture.storage.itemSize;
             ++byte)
        {
            Check(bytes[byte] == kInitialByte,
                "Pool_Init overwrote payload beyond the compatibility link");
        }
    }

    for (std::size_t byte = 0; byte < kPrefixSize; ++byte)
        Check(fixture.bytes[byte] == originalBytes[byte],
            "Pool_Init overwrote the prefix guard");
    for (std::size_t byte = kPrefixSize + PoolFixture::kPoolBytes;
         byte < fixture.bytes.size();
         ++byte)
    {
        Check(fixture.bytes[byte] == originalBytes[byte],
            "Pool_Init overwrote the suffix guard");
    }

    const poolcountresult_t count =
        Pool_GetFreeCount(fixture.storage, &fixture.data);
    Check(count.valid && count.count == kSlotCount,
        "initialized pool reported the wrong free count");
    Check(Pool_ValidateFull(fixture.storage, &fixture.data),
        "initialized pool failed full validation");
}

void TestAllocationFreeAndLifo()
{
    PoolFixture fixture;
    Check(fixture.Initialize(), "allocation fixture failed to initialize");
    const auto originalBytes = fixture.bytes;

    std::array<void *, kSlotCount> allocated{};
    for (std::size_t index = 0; index < kSlotCount; ++index)
    {
        allocated[index] = Pool_Alloc(fixture.storage, &fixture.data);
        Check(allocated[index] == fixture.Slot(index),
            "Pool_Alloc did not consume the ascending shadow chain");
        Check(fixture.slotState[index] == POOL_SLOT_ALLOCATED,
            "Pool_Alloc did not mark exactly the returned slot allocated");
        const poolslotstate_t expectedHead = index + 1 < kSlotCount
            ? static_cast<poolslotstate_t>(index + 1)
            : POOL_SLOT_END;
        Check(fixture.control.headIndex == expectedHead,
            "Pool_Alloc updated the shadow head incorrectly");
        Check(fixture.control.activeCount == static_cast<int>(index + 1)
                && fixture.data.activeCount == static_cast<int>(index + 1),
            "Pool_Alloc did not update both active counts");
        Check(fixture.data.firstFree
                == (index + 1 < kSlotCount
                        ? fixture.Slot(index + 1)
                        : nullptr),
            "Pool_Alloc did not update the retail head mirror");
        Check(fixture.bytes == originalBytes,
            "Pool_Alloc modified object payload bytes");

        const poolcountresult_t count =
            Pool_GetFreeCount(fixture.storage, &fixture.data);
        Check(count.valid && count.count == kSlotCount - index - 1,
            "Pool_Alloc updated the free count incorrectly");
        Check(Pool_ValidateFull(fixture.storage, &fixture.data),
            "Pool_Alloc produced a state that failed full validation");
    }

    const PoolSnapshot exhausted = Capture(fixture);
    const std::size_t assertionsBefore = g_assertionCount;
    Check(Pool_Alloc(fixture.storage, &fixture.data) == nullptr,
        "exhausted pool returned an item");
    Check(g_assertionCount == assertionsBefore,
        "ordinary exhaustion raised an assertion");
    CheckUnchanged(fixture, exhausted,
        "ordinary exhaustion mutated allocator state");

    std::memset(allocated[1], 0x5A, fixture.storage.itemSize);
    const auto bytesBeforeFirstFree = fixture.bytes;
    void *const nullNext = nullptr;
    Check(Pool_Free(fixture.storage, &fixture.data, allocated[1]),
        "first valid free failed");
    Check(fixture.slotState[1] == POOL_SLOT_END,
        "first free did not terminate the shadow list");
    Check(fixture.control.headIndex == 1
            && fixture.data.firstFree == allocated[1],
        "first free published the wrong head");
    Check(fixture.bytes
            == ExpectedAfterLinkWrite(bytesBeforeFirstFree, 1, nullNext),
        "Pool_Free modified bytes beyond its compatibility link");

    std::memset(allocated[3], 0x3C, fixture.storage.itemSize);
    const auto bytesBeforeSecondFree = fixture.bytes;
    Check(Pool_Free(fixture.storage, &fixture.data, allocated[3]),
        "second valid free failed");
    Check(fixture.slotState[3] == 1,
        "second free did not link to the previous shadow head");
    Check(fixture.bytes
            == ExpectedAfterLinkWrite(bytesBeforeSecondFree, 3, allocated[1]),
        "second Pool_Free modified bytes beyond its compatibility link");
    const poolnextresult_t next =
        Pool_NextFree(fixture.storage, allocated[3]);
    Check(next.valid && next.next == allocated[1],
        "Pool_NextFree did not use the authoritative shadow edge");

    Check(Pool_Alloc(fixture.storage, &fixture.data) == allocated[3],
        "free list did not return the newest free first");
    Check(Pool_Alloc(fixture.storage, &fixture.data) == allocated[1],
        "free list did not return the older free second");

    for (std::size_t index = 0; index < kSlotCount; ++index)
    {
        std::memset(
            allocated[index],
            static_cast<int>(0x20u + index),
            fixture.storage.itemSize);
        const auto bytesBeforeFree = fixture.bytes;
        void *const previousHead = fixture.data.firstFree;
        Check(Pool_Free(fixture.storage, &fixture.data, allocated[index]),
            "valid refill free failed");
        Check(fixture.bytes
                == ExpectedAfterLinkWrite(
                    bytesBeforeFree, index, previousHead),
            "refill free modified bytes beyond its compatibility link");
    }
    Check(fixture.control.activeCount == 0 && fixture.data.activeCount == 0,
        "freeing every item did not clear both active counts");
    Check(Pool_ValidateFull(fixture.storage, &fixture.data),
        "refilled pool failed full validation");

    for (std::size_t index = 0; index < kSlotCount; ++index)
    {
        const std::size_t expected = kSlotCount - index - 1;
        Check(Pool_Alloc(fixture.storage, &fixture.data) == allocated[expected],
            "refilled pool did not preserve LIFO order");
    }
}

void TestLargePool()
{
    constexpr std::size_t count = 257;
    struct LargeSlot
    {
        unsigned char payload[sizeof(void *) + 3];
    };

    LargeSlot slots[count]{};
    poolslotstate_t states[count]{};
    poolcontrol_t control = Pool_ControlFor(states);
    const poolstorage_t storage = Pool_StorageFor(slots, control);
    pooldata_t data{};

    Check(Pool_Init(storage, &data), "large pool failed to initialize");
    for (std::size_t index = 0; index < count; ++index)
    {
        Check(Pool_Alloc(storage, &data) == &slots[index],
            "large pool allocation lost index precision");
    }
    Check(control.headIndex == POOL_SLOT_END,
        "large exhausted pool retained a head index");
    for (std::size_t index = 0; index < count; index += 3)
    {
        Check(Pool_Free(storage, &data, &slots[index]),
            "large pool valid free failed");
    }
    Check(Pool_ValidateFull(storage, &data),
        "large pool failed full validation");
}

void TestInvalidInitialization()
{
    const auto rejectDescriptor = [](const char *const description,
                                      const auto &mutateStorage) {
        PoolFixture fixture;
        poolstorage_t storage = fixture.storage;
        mutateStorage(storage);
        const PoolSnapshot snapshot = Capture(fixture);
        const std::size_t assertionsBefore = g_assertionCount;
        Check(!Pool_Init(storage, &fixture.data), description);
        CheckAsserted(assertionsBefore,
            "invalid Pool_Init descriptor did not assert");
        CheckUnchanged(fixture, snapshot,
            "invalid Pool_Init descriptor mutated state");
    };

    rejectDescriptor("Pool_Init accepted a null base",
        [](poolstorage_t &storage) { storage.base = nullptr; });
    rejectDescriptor("Pool_Init accepted a zero stride",
        [](poolstorage_t &storage) { storage.itemSize = 0; });
    rejectDescriptor("Pool_Init accepted a sub-pointer stride",
        [](poolstorage_t &storage) { storage.itemSize = sizeof(void *) - 1; });
    rejectDescriptor("Pool_Init accepted a zero item count",
        [](poolstorage_t &storage) { storage.itemCount = 0; });
    rejectDescriptor("Pool_Init accepted a one-item pool",
        [](poolstorage_t &storage) { storage.itemCount = 1; });
    rejectDescriptor("Pool_Init accepted a null control",
        [](poolstorage_t &storage) { storage.control = nullptr; });
    rejectDescriptor("Pool_Init accepted a count above INT_MAX",
        [](poolstorage_t &storage) {
            storage.itemCount =
                static_cast<std::size_t>((std::numeric_limits<int>::max)())
                + 1u;
        });
    rejectDescriptor("Pool_Init accepted extent multiplication overflow",
        [](poolstorage_t &storage) {
            storage.itemSize =
                (std::numeric_limits<std::size_t>::max)() / 2u + 1u;
            storage.itemCount = 2;
        });
    rejectDescriptor("Pool_Init accepted an address-range overflow",
        [](poolstorage_t &storage) { SetWrappingStorageBase(storage); });

    {
        PoolFixture fixture;
        fixture.control.slotState = nullptr;
        const PoolSnapshot snapshot = Capture(fixture);
        const std::size_t assertionsBefore = g_assertionCount;
        Check(!fixture.Initialize(), "Pool_Init accepted a null sidecar");
        CheckAsserted(assertionsBefore, "null sidecar did not assert");
        CheckUnchanged(fixture, snapshot, "null sidecar mutated state");
    }
    {
        PoolFixture fixture;
        --fixture.control.slotStateCount;
        const PoolSnapshot snapshot = Capture(fixture);
        const std::size_t assertionsBefore = g_assertionCount;
        Check(!fixture.Initialize(), "Pool_Init accepted a short sidecar");
        CheckAsserted(assertionsBefore, "short sidecar did not assert");
        CheckUnchanged(fixture, snapshot, "short sidecar mutated state");
    }
    {
        PoolFixture fixture;
        ++fixture.control.slotStateCount;
        const PoolSnapshot snapshot = Capture(fixture);
        const std::size_t assertionsBefore = g_assertionCount;
        Check(!fixture.Initialize(), "Pool_Init accepted a long sidecar");
        CheckAsserted(assertionsBefore, "long sidecar did not assert");
        CheckUnchanged(fixture, snapshot, "long sidecar mutated state");
    }
    {
        PoolFixture fixture;
        alignas(poolslotstate_t)
            unsigned char misaligned[sizeof(poolslotstate_t) * kSlotCount + 1]{};
        fixture.control.slotState = reinterpret_cast<poolslotstate_t *>(
            misaligned + 1);
        const auto bytesBefore = misaligned;
        const PoolSnapshot snapshot = Capture(fixture);
        const std::size_t assertionsBefore = g_assertionCount;
        Check(!fixture.Initialize(), "Pool_Init accepted a misaligned sidecar");
        CheckAsserted(assertionsBefore, "misaligned sidecar did not assert");
        CheckUnchanged(fixture, snapshot, "misaligned sidecar mutated pool state");
        Check(std::memcmp(misaligned, bytesBefore, sizeof(misaligned)) == 0,
            "misaligned sidecar storage was modified");
    }
    {
        PoolFixture fixture;
        const std::uintptr_t stateAddress =
            (std::numeric_limits<std::uintptr_t>::max)()
            - sizeof(poolslotstate_t) + 1u;
        fixture.control.slotState =
            reinterpret_cast<poolslotstate_t *>(stateAddress);
        const PoolSnapshot snapshot = Capture(fixture);
        const std::size_t assertionsBefore = g_assertionCount;
        Check(!fixture.Initialize(),
            "Pool_Init accepted a wrapping sidecar range");
        CheckAsserted(assertionsBefore, "wrapping sidecar did not assert");
        CheckUnchanged(fixture, snapshot, "wrapping sidecar mutated state");
    }

    {
        PoolFixture fixture;
        fixture.control.slotState = reinterpret_cast<poolslotstate_t *>(
            fixture.storage.base);
        const PoolSnapshot snapshot = Capture(fixture);
        const std::size_t assertionsBefore = g_assertionCount;
        Check(!fixture.Initialize(),
            "Pool_Init accepted sidecar storage inside pool payload");
        CheckAsserted(assertionsBefore,
            "payload-overlapping sidecar did not assert");
        CheckUnchanged(fixture, snapshot,
            "payload-overlapping sidecar mutated state");
    }
    {
        struct Slot
        {
            unsigned char payload[sizeof(void *)];
        } slots[2]{};
        pooldata_t data{};
        poolslotstate_t realStates[2]{};
        poolcontrol_t control = Pool_ControlFor(realStates);
        control.slotState = reinterpret_cast<poolslotstate_t *>(&data);
        const poolstorage_t storage = Pool_StorageFor(slots, control);
        const pooldata_t dataBefore = data;
        const poolcontrol_t controlBefore = control;
        Check(!Pool_Init(storage, &data),
            "Pool_Init accepted a sidecar overlapping pooldata");
        Check(data.firstFree == dataBefore.firstFree
                && data.activeCount == dataBefore.activeCount
                && ControlsEqual(control, controlBefore),
            "pooldata-overlapping sidecar mutated metadata");
    }
    {
        struct Slot
        {
            unsigned char payload[sizeof(void *)];
        } slots[2]{};
        poolslotstate_t states[2]{};
        poolcontrol_t control = Pool_ControlFor(states);
        control.slotState = reinterpret_cast<poolslotstate_t *>(
            &control.boundBase);
        pooldata_t data{};
        const poolstorage_t storage = Pool_StorageFor(slots, control);
        const poolcontrol_t controlBefore = control;
        Check(!Pool_Init(storage, &data),
            "Pool_Init accepted a sidecar overlapping its control object");
        Check(ControlsEqual(control, controlBefore),
            "control-overlapping sidecar mutated control storage");
    }
    {
        poolcontrol_t controls[2]{};
        poolslotstate_t states[2]{};
        controls[0] = Pool_ControlFor(states);
        pooldata_t data{};
        const poolstorage_t storage = Pool_StorageFor(controls, controls[0]);
        const poolcontrol_t controlBefore = controls[0];
        Check(!Pool_Init(storage, &data),
            "Pool_Init accepted control storage inside pool payload");
        Check(ControlsEqual(controls[0], controlBefore),
            "payload-overlapping control was modified");
    }
    {
        pooldata_t poolObjects[2]{};
        poolslotstate_t states[2]{};
        poolcontrol_t control = Pool_ControlFor(states);
        const poolstorage_t storage = Pool_StorageFor(poolObjects, control);
        const pooldata_t before = poolObjects[0];
        Check(!Pool_Init(storage, &poolObjects[0]),
            "Pool_Init accepted pooldata inside pool payload");
        Check(poolObjects[0].firstFree == before.firstFree
                && poolObjects[0].activeCount == before.activeCount,
            "payload-overlapping pooldata was modified");
    }
    {
        struct Slot
        {
            unsigned char payload[sizeof(void *)];
        } slots[2]{};
        poolslotstate_t states[2]{};
        poolcontrol_t control = Pool_ControlFor(states);
        pooldata_t *const overlappingData = reinterpret_cast<pooldata_t *>(
            &control.boundBase);
        const poolstorage_t storage = Pool_StorageFor(slots, control);
        const poolcontrol_t before = control;
        Check(!Pool_Init(storage, overlappingData),
            "Pool_Init accepted pooldata overlapping its control object");
        Check(ControlsEqual(control, before),
            "control-overlapping pooldata modified control storage");
    }
}

void TestInvalidArgumentsAndBinding()
{
    PoolFixture fixture;
    Check(fixture.Initialize(), "invalid-operation fixture did not initialize");
    void *const allocated = Pool_Alloc(fixture.storage, &fixture.data);
    Check(allocated == fixture.Slot(0),
        "invalid-operation fixture did not allocate slot zero");

    alignas(void *) unsigned char foreign[sizeof(void *)]{};
    void *const interior = static_cast<void *>(
        static_cast<unsigned char *>(fixture.storage.base) + 1);
    void *const onePast = static_cast<void *>(
        static_cast<unsigned char *>(fixture.storage.base)
        + fixture.storage.itemSize * fixture.storage.itemCount);

    ExpectRejectedWithoutMutation(
        fixture,
        [&fixture]() { return Pool_Alloc(fixture.storage, nullptr) == nullptr; },
        "Pool_Alloc accepted null pooldata",
        "null Pool_Alloc pooldata did not assert",
        "null Pool_Alloc pooldata mutated state");
    ExpectRejectedWithoutMutation(
        fixture,
        [&fixture, allocated]() {
            return !Pool_Free(fixture.storage, nullptr, allocated);
        },
        "Pool_Free accepted null pooldata",
        "null Pool_Free pooldata did not assert",
        "null Pool_Free pooldata mutated state");
    ExpectRejectedWithoutMutation(
        fixture,
        [&fixture]() {
            const poolcountresult_t result =
                Pool_GetFreeCount(fixture.storage, nullptr);
            return !result.valid && result.count == 0;
        },
        "Pool_GetFreeCount accepted null pooldata",
        "null count pooldata did not assert",
        "null count pooldata mutated state");

    const auto rejectFreeItem = [&fixture](void *const item,
                                          const char *const description) {
        ExpectRejectedWithoutMutation(
            fixture,
            [&fixture, item]() {
                return !Pool_Free(fixture.storage, &fixture.data, item);
            },
            description,
            "invalid Pool_Free item did not assert",
            "invalid Pool_Free item mutated state");
    };
    rejectFreeItem(nullptr, "Pool_Free accepted null item");
    rejectFreeItem(interior, "Pool_Free accepted an interior pointer");
    rejectFreeItem(foreign, "Pool_Free accepted a foreign pointer");
    rejectFreeItem(onePast, "Pool_Free accepted a one-past pointer");

    const auto rejectIndex = [&fixture](const void *const item,
                                       const char *const description) {
        ExpectRejectedWithoutMutation(
            fixture,
            [&fixture, item]() {
                const poolindexresult_t result =
                    Pool_GetSlotIndex(fixture.storage, item);
                return !result.valid && result.index == 0;
            },
            description,
            "invalid slot-index query did not assert",
            "invalid slot-index query mutated state");
    };
    rejectIndex(nullptr, "Pool_GetSlotIndex accepted null item");
    rejectIndex(interior, "Pool_GetSlotIndex accepted an interior pointer");
    rejectIndex(foreign, "Pool_GetSlotIndex accepted a foreign pointer");
    rejectIndex(onePast, "Pool_GetSlotIndex accepted a one-past pointer");

    ExpectRejectedWithoutMutation(
        fixture,
        [&fixture, allocated]() {
            const poolnextresult_t result =
                Pool_NextFree(fixture.storage, allocated);
            return !result.valid && result.next == nullptr;
        },
        "Pool_NextFree accepted an allocated slot",
        "allocated Pool_NextFree slot did not assert",
        "allocated Pool_NextFree slot mutated state");

    poolstorage_t invalidStorage = fixture.storage;
    invalidStorage.itemSize = sizeof(void *) - 1;
    ExpectRejectedWithoutMutation(
        fixture,
        [&fixture, invalidStorage]() {
            return Pool_Alloc(invalidStorage, &fixture.data) == nullptr;
        },
        "Pool_Alloc accepted an invalid descriptor",
        "invalid Pool_Alloc descriptor did not assert",
        "invalid Pool_Alloc descriptor mutated state");

    poolstorage_t wrongBinding = fixture.storage;
    ++wrongBinding.itemSize;
    ExpectRejectedWithoutMutation(
        fixture,
        [&fixture, wrongBinding]() {
            return Pool_Alloc(wrongBinding, &fixture.data) == nullptr;
        },
        "Pool_Alloc accepted a descriptor with the wrong binding",
        "wrong descriptor binding did not assert",
        "wrong descriptor binding mutated state");

    pooldata_t otherData{};
    ExpectRejectedWithoutMutation(
        fixture,
        [&fixture, &otherData]() {
            return Pool_Alloc(fixture.storage, &otherData) == nullptr;
        },
        "Pool_Alloc accepted different retail metadata",
        "wrong pooldata binding did not assert",
        "wrong pooldata binding mutated state");
}

void PrepareOneAllocated(PoolFixture &fixture);

void TestDuplicateFreeAndLocalCorruption()
{
    PoolFixture fixture;
    Check(fixture.Initialize(), "duplicate-free fixture did not initialize");
    void *const first = Pool_Alloc(fixture.storage, &fixture.data);
    void *const second = Pool_Alloc(fixture.storage, &fixture.data);
    void *const third = Pool_Alloc(fixture.storage, &fixture.data);
    Check(first && second && third,
        "duplicate-free fixture did not allocate three slots");
    Check(Pool_Free(fixture.storage, &fixture.data, first),
        "duplicate-free fixture failed its first free");
    Check(Pool_Free(fixture.storage, &fixture.data, second),
        "duplicate-free fixture failed its second free");

    ExpectRejectedWithoutMutation(
        fixture,
        [&fixture, second]() {
            return !Pool_Free(fixture.storage, &fixture.data, second);
        },
        "Pool_Free accepted a duplicate shadow head",
        "duplicate shadow-head free did not assert",
        "duplicate shadow-head free mutated state");
    ExpectRejectedWithoutMutation(
        fixture,
        [&fixture, first]() {
            return !Pool_Free(fixture.storage, &fixture.data, first);
        },
        "Pool_Free accepted a duplicate deep free",
        "duplicate deep free did not assert",
        "duplicate deep free mutated state");

    const auto expectLocallyRejected = [](PoolFixture &corrupt,
                                          void *const allocatedItem,
                                          const char *const description) {
        ExpectRejectedWithoutMutation(
            corrupt,
            [&corrupt]() {
                return Pool_Alloc(corrupt.storage, &corrupt.data) == nullptr;
            },
            description,
            "locally corrupt Pool_Alloc did not assert",
            "locally corrupt Pool_Alloc mutated state");
        ExpectRejectedWithoutMutation(
            corrupt,
            [&corrupt, allocatedItem]() {
                return !Pool_Free(
                    corrupt.storage, &corrupt.data, allocatedItem);
            },
            "Pool_Free accepted locally corrupt state",
            "locally corrupt Pool_Free did not assert",
            "locally corrupt Pool_Free mutated state");
        ExpectRejectedWithoutMutation(
            corrupt,
            [&corrupt]() {
                const poolcountresult_t result =
                    Pool_GetFreeCount(corrupt.storage, &corrupt.data);
                return !result.valid && result.count == 0;
            },
            "Pool_GetFreeCount accepted locally corrupt state",
            "locally corrupt count did not assert",
            "locally corrupt count mutated state");
        ExpectRejectedWithoutMutation(
            corrupt,
            [&corrupt]() {
                return !Pool_ValidateFull(corrupt.storage, &corrupt.data);
            },
            "Pool_ValidateFull accepted locally corrupt state",
            "locally corrupt full validation did not assert",
            "locally corrupt full validation mutated state");
    };

    {
        PoolFixture corrupt;
        PrepareOneAllocated(corrupt);
        corrupt.data.firstFree = corrupt.Slot(2);
        expectLocallyRejected(corrupt, corrupt.Slot(0),
            "Pool_Alloc accepted a mismatched retail head");
    }
    {
        PoolFixture corrupt;
        PrepareOneAllocated(corrupt);
        ++corrupt.data.activeCount;
        expectLocallyRejected(corrupt, corrupt.Slot(0),
            "Pool_Alloc accepted mismatched active counts");
    }
    {
        PoolFixture corrupt;
        PrepareOneAllocated(corrupt);
        corrupt.control.headIndex = static_cast<poolslotstate_t>(kSlotCount);
        expectLocallyRejected(corrupt, corrupt.Slot(0),
            "Pool_Alloc accepted an out-of-range shadow head");
    }
    {
        PoolFixture corrupt;
        PrepareOneAllocated(corrupt);
        corrupt.slotState[1] = POOL_SLOT_ALLOCATED;
        expectLocallyRejected(corrupt, corrupt.Slot(0),
            "Pool_Alloc accepted an allocated shadow head");
    }
    {
        PoolFixture corrupt;
        PrepareOneAllocated(corrupt);
        corrupt.slotState[1] = static_cast<poolslotstate_t>(kSlotCount);
        expectLocallyRejected(corrupt, corrupt.Slot(0),
            "Pool_Alloc accepted an out-of-range next index");
    }
    {
        PoolFixture corrupt;
        PrepareOneAllocated(corrupt);
        corrupt.slotState[1] = 1;
        expectLocallyRejected(corrupt, corrupt.Slot(0),
            "Pool_Alloc accepted a head self-cycle");
    }
    {
        PoolFixture corrupt;
        PrepareOneAllocated(corrupt);
        corrupt.control.activeCount = -1;
        expectLocallyRejected(corrupt, corrupt.Slot(0),
            "Pool_Alloc accepted a negative shadow active count");
    }
    {
        PoolFixture corrupt;
        PrepareOneAllocated(corrupt);
        corrupt.control.initMagic ^= 1u;
        expectLocallyRejected(corrupt, corrupt.Slot(0),
            "Pool_Alloc accepted a corrupt initialization marker");
    }
    {
        PoolFixture corrupt;
        PrepareOneAllocated(corrupt);
        ++corrupt.control.boundStride;
        expectLocallyRejected(corrupt, corrupt.Slot(0),
            "Pool_Alloc accepted a corrupt bound stride");
    }
    {
        PoolFixture corrupt;
        PrepareOneAllocated(corrupt);
        --corrupt.control.slotStateCount;
        expectLocallyRejected(corrupt, corrupt.Slot(0),
            "Pool_Alloc accepted a corrupt sidecar count");
    }
}

void ExpectDormantCorruptionRejectedByFullValidation(
    PoolFixture &fixture,
    const char *const description)
{
    PoolSnapshot snapshot = Capture(fixture);
    std::size_t assertionsBefore = g_assertionCount;
    const poolcountresult_t count =
        Pool_GetFreeCount(fixture.storage, &fixture.data);
    Check(count.valid && count.count == kSlotCount - 1,
        "O(1) free-count query inspected dormant tail state");
    Check(g_assertionCount == assertionsBefore,
        "O(1) free-count query asserted on dormant tail state");
    CheckUnchanged(fixture, snapshot,
        "O(1) free-count query mutated dormant corruption");

    snapshot = Capture(fixture);
    assertionsBefore = g_assertionCount;
    Check(!Pool_ValidateFull(fixture.storage, &fixture.data), description);
    CheckAsserted(assertionsBefore,
        "dormant corruption full validation did not assert");
    CheckUnchanged(fixture, snapshot,
        "full validation mutated dormant corruption");
}

void PrepareOneAllocated(PoolFixture &fixture)
{
    Check(fixture.Initialize(), "deep-corruption fixture did not initialize");
    Check(Pool_Alloc(fixture.storage, &fixture.data) == fixture.Slot(0),
        "deep-corruption fixture did not allocate slot zero");
}

void TestDormantCorruptionAndFullValidation()
{
    {
        PoolFixture fixture;
        PrepareOneAllocated(fixture);
        fixture.slotState[6] = 6;
        StoreRawLink(fixture.Slot(6), fixture.Slot(6));
        ExpectRejectedWithoutMutation(
            fixture,
            [&fixture]() {
                const poolnextresult_t result =
                    Pool_NextFree(fixture.storage, fixture.Slot(6));
                return !result.valid && result.next == nullptr;
            },
            "Pool_NextFree accepted a deep self-link",
            "deep Pool_NextFree self-link did not assert",
            "deep Pool_NextFree self-link mutated state");
        ExpectDormantCorruptionRejectedByFullValidation(
            fixture, "Pool_ValidateFull accepted a dormant self-cycle");
    }
    {
        PoolFixture fixture;
        PrepareOneAllocated(fixture);
        fixture.slotState[6] = 5;
        ExpectDormantCorruptionRejectedByFullValidation(
            fixture, "Pool_ValidateFull accepted a dormant two-node cycle");
    }
    {
        PoolFixture fixture;
        PrepareOneAllocated(fixture);
        fixture.slotState[6] = POOL_SLOT_END;
        ExpectDormantCorruptionRejectedByFullValidation(
            fixture, "Pool_ValidateFull accepted a short shadow chain");
    }
    {
        PoolFixture fixture;
        PrepareOneAllocated(fixture);
        fixture.slotState[6] = POOL_SLOT_ALLOCATED;
        ExpectDormantCorruptionRejectedByFullValidation(
            fixture, "Pool_ValidateFull accepted a false allocated tail slot");
    }
    {
        PoolFixture fixture;
        PrepareOneAllocated(fixture);
        fixture.slotState[6] = static_cast<poolslotstate_t>(kSlotCount);
        ExpectDormantCorruptionRejectedByFullValidation(
            fixture, "Pool_ValidateFull accepted an out-of-range tail edge");
    }

    // A valid O(1) mutation may proceed while corruption is beyond the current
    // head. It must remain locally coherent, and the explicit boundary scan
    // must still diagnose the dormant fault.
    PoolFixture fixture;
    PrepareOneAllocated(fixture);
    fixture.slotState[7] = 7;
    const std::size_t assertionsBeforeAlloc = g_assertionCount;
    Check(Pool_Alloc(fixture.storage, &fixture.data) == fixture.Slot(1),
        "Pool_Alloc walked and rejected a dormant tail fault");
    Check(g_assertionCount == assertionsBeforeAlloc,
        "Pool_Alloc asserted on a dormant tail fault");
    Check(fixture.slotState[1] == POOL_SLOT_ALLOCATED
            && fixture.control.headIndex == 2
            && fixture.control.activeCount == 2
            && fixture.data.activeCount == 2,
        "Pool_Alloc was not locally coherent around dormant corruption");

    const std::size_t assertionsBeforeFree = g_assertionCount;
    Check(Pool_Free(fixture.storage, &fixture.data, fixture.Slot(0)),
        "Pool_Free walked and rejected a dormant tail fault");
    Check(g_assertionCount == assertionsBeforeFree,
        "Pool_Free asserted on a dormant tail fault");
    Check(fixture.slotState[0] == 2
            && fixture.control.headIndex == 0
            && fixture.control.activeCount == 1
            && fixture.data.activeCount == 1,
        "Pool_Free was not locally coherent around dormant corruption");

    const PoolSnapshot snapshot = Capture(fixture);
    const std::size_t assertionsBeforeFull = g_assertionCount;
    Check(!Pool_ValidateFull(fixture.storage, &fixture.data),
        "Pool_ValidateFull missed corruption after local operations");
    CheckAsserted(assertionsBeforeFull,
        "post-operation dormant corruption did not assert");
    CheckUnchanged(fixture, snapshot,
        "post-operation full validation mutated state");
}

void TestInvalidation()
{
    PoolFixture fixture;
    Check(fixture.Initialize(), "invalidation fixture did not initialize");
    Check(Pool_Alloc(fixture.storage, &fixture.data) == fixture.Slot(0),
        "invalidation fixture did not allocate slot zero");
    fixture.slotState[7] = 7;
    const auto bytesBefore = fixture.bytes;
    poolslotstate_t *const statePointer = fixture.control.slotState;
    const std::size_t stateCount = fixture.control.slotStateCount;

    Check(Pool_Invalidate(fixture.storage, &fixture.data),
        "Pool_Invalidate rejected a bound pool with corrupt dormant state");
    Check(fixture.data.firstFree == nullptr && fixture.data.activeCount == 0,
        "Pool_Invalidate did not retire retail metadata");
    Check(fixture.control.slotState == statePointer
            && fixture.control.slotStateCount == stateCount,
        "Pool_Invalidate discarded reusable sidecar configuration");
    Check(fixture.control.boundBase == nullptr
            && fixture.control.boundData == nullptr
            && fixture.control.boundStride == 0
            && fixture.control.boundCount == 0
            && fixture.control.headIndex == POOL_SLOT_END
            && fixture.control.activeCount == 0
            && fixture.control.initMagic == 0,
        "Pool_Invalidate did not clear the published binding");
    Check(fixture.bytes == bytesBefore,
        "Pool_Invalidate modified object payload");

    ExpectRejectedWithoutMutation(
        fixture,
        [&fixture]() {
            return Pool_Alloc(fixture.storage, &fixture.data) == nullptr;
        },
        "Pool_Alloc accepted an invalidated pool",
        "invalidated Pool_Alloc did not assert",
        "invalidated Pool_Alloc mutated state");
    ExpectRejectedWithoutMutation(
        fixture,
        [&fixture]() {
            return !Pool_ValidateFull(fixture.storage, &fixture.data);
        },
        "Pool_ValidateFull accepted an invalidated pool",
        "invalidated full validation did not assert",
        "invalidated full validation mutated state");

    Check(fixture.Initialize(), "invalidated pool could not be reinitialized");
    Check(Pool_ValidateFull(fixture.storage, &fixture.data),
        "reinitialized pool failed full validation");

    poolstorage_t wrongStorage = fixture.storage;
    ++wrongStorage.itemSize;
    ExpectRejectedWithoutMutation(
        fixture,
        [&fixture, wrongStorage]() {
            return !Pool_Invalidate(wrongStorage, &fixture.data);
        },
        "Pool_Invalidate accepted the wrong bound descriptor",
        "wrong-binding Pool_Invalidate did not assert",
        "wrong-binding Pool_Invalidate mutated state");
}

void TestNoReportMutationPrimitives()
{
    PoolFixture fixture;
    Check(fixture.Initialize(), "silent-operation fixture did not initialize");
    const std::size_t assertionsBefore = g_assertionCount;

    Check(Pool_TryValidateFullNoReport(fixture.storage, &fixture.data)
            == poolmutationstatus_t::Success,
        "silent full validation rejected a valid pool");
    Check(Pool_TryValidateAllocatedNoReport(
              fixture.storage, &fixture.data, fixture.Slot(0))
            == poolmutationstatus_t::InvalidState,
        "silent allocated validation accepted a free slot");

    std::array<void *, kSlotCount> allocated{};
    for (std::size_t index = 0; index < kSlotCount; ++index)
    {
        const poolallocresult_t result =
            Pool_TryAllocNoReport(fixture.storage, &fixture.data);
        allocated[index] = result.item;
        Check(result.status == poolmutationstatus_t::Success
                && result.item == fixture.Slot(index),
            "silent allocation returned the wrong result");
        Check(Pool_TryValidateAllocatedNoReport(
                  fixture.storage, &fixture.data, result.item)
                == poolmutationstatus_t::Success,
            "silent allocated validation rejected a live slot");
    }

    const PoolSnapshot exhausted = Capture(fixture);
    const poolallocresult_t unavailable =
        Pool_TryAllocNoReport(fixture.storage, &fixture.data);
    Check(unavailable.status == poolmutationstatus_t::Unavailable
            && unavailable.item == nullptr,
        "silent exhaustion did not return Unavailable");
    CheckUnchanged(fixture, exhausted,
        "silent exhaustion mutated allocator state");

    alignas(void *) unsigned char foreign[sizeof(void *)]{};
    const PoolSnapshot beforeInvalidFree = Capture(fixture);
    Check(Pool_TryFreeNoReport(
              fixture.storage, &fixture.data, foreign)
            == poolmutationstatus_t::InvalidState,
        "silent free accepted a foreign pointer");
    CheckUnchanged(fixture, beforeInvalidFree,
        "rejected silent free mutated allocator state");

    Check(Pool_TryFreeNoReport(
              fixture.storage, &fixture.data, allocated[3])
            == poolmutationstatus_t::Success,
        "silent free rejected an allocated slot");
    Check(Pool_TryValidateAllocatedNoReport(
              fixture.storage, &fixture.data, allocated[3])
            == poolmutationstatus_t::InvalidState,
        "silent allocated validation accepted a freed slot");

    PoolFixture corrupt;
    PrepareOneAllocated(corrupt);
    corrupt.slotState[7] = 7;
    const PoolSnapshot corruptSnapshot = Capture(corrupt);
    Check(Pool_TryValidateFullNoReport(corrupt.storage, &corrupt.data)
            == poolmutationstatus_t::InvalidState,
        "silent full validation accepted dormant corruption");
    CheckUnchanged(corrupt, corruptSnapshot,
        "silent full validation mutated corrupt state");
    Check(g_assertionCount == assertionsBefore,
        "a no-report pool primitive raised an assertion");
}
} // namespace

void MyAssertHandler(const char *, int, int, const char *, ...)
{
    ++g_assertionCount;
}

int main()
{
    TestLayoutAndFactories();
    TestInitializationAndTraversal();
    TestAllocationFreeAndLifo();
    TestLargePool();
    TestInvalidInitialization();
    TestInvalidArgumentsAndBinding();
    TestDuplicateFreeAndLocalCorruption();
    TestDormantCorruptionAndFullValidation();
    TestInvalidation();
    TestNoReportMutationPrimitives();

    if (g_failureCount != 0)
    {
        std::fprintf(stderr, "%d pool allocator tests failed\n", g_failureCount);
        return 1;
    }

    std::puts("external-shadow pool allocator tests passed");
    return 0;
}
