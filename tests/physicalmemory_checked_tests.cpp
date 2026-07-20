#include <universal/physicalmemory_checked.h>
#include <universal/physicalmemory_runtime.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace
{
using physical_memory::AllocationReceipt;
using physical_memory::AllocationScopeStatus;
using physical_memory::TryBegin;
using physical_memory::TryEnd;
using physical_memory::TryFree;
using pmem_runtime::AllocationReceiptPhase;

int g_failures;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            std::fprintf(                                                       \
                stderr,                                                         \
                "CHECK failed at %s:%d: %s\n",                                 \
                __FILE__,                                                       \
                __LINE__,                                                       \
                #condition);                                                    \
            ++g_failures;                                                       \
        }                                                                       \
    } while (false)

template <typename T>
using ObjectBytes = std::array<std::byte, sizeof(T)>;

template <typename T>
[[nodiscard]] ObjectBytes<T> Snapshot(const T &value)
{
    ObjectBytes<T> result{};
    std::memcpy(result.data(), &value, sizeof(value));
    return result;
}

template <typename T>
[[nodiscard]] bool MatchesSnapshot(
    const T &value,
    const ObjectBytes<T> &snapshot)
{
    return std::memcmp(&value, snapshot.data(), sizeof(value)) == 0;
}

template <typename T>
void RestoreAdversarialBytes(T &value, const ObjectBytes<T> &bytes)
{
    // This deliberately models an out-of-contract bytewise authority copy.
    // AllocationReceipt is non-trivially-copyable; the checked self/witness
    // representation still rejects the common copied/corrupted forms.
    std::memcpy(static_cast<void *>(&value), bytes.data(), sizeof(value));
}

struct Fixture final
{
    std::array<std::uint8_t, 1024> storage{};
    PhysicalMemory memory{};

    Fixture()
    {
        memory.buf = storage.data();
        memory.prim[0].pos = 0;
        memory.prim[1].pos = static_cast<std::uint32_t>(storage.size());
    }
};

template <typename Mutator>
void ExpectMalformedBegin(Mutator mutate)
{
    static char name;
    Fixture fixture;
    AllocationReceipt receipt;
    mutate(fixture.memory);
    const auto memoryBefore = Snapshot(fixture.memory);
    const auto receiptBefore = Snapshot(receipt);
    CHECK(TryBegin(&fixture.memory, 0, &name, &receipt)
        == AllocationScopeStatus::MalformedState);
    CHECK(MatchesSnapshot(fixture.memory, memoryBefore));
    CHECK(MatchesSnapshot(receipt, receiptBefore));
}

void TestApiShapeAndArguments()
{
    static_assert(!std::is_copy_constructible_v<AllocationReceipt>);
    static_assert(!std::is_copy_assignable_v<AllocationReceipt>);
    static_assert(!std::is_move_constructible_v<AllocationReceipt>);
    static_assert(!std::is_move_assignable_v<AllocationReceipt>);
    static_assert(!std::is_trivially_copyable_v<AllocationReceipt>);
    static_assert(noexcept(TryBegin(
        static_cast<PhysicalMemory *>(nullptr),
        0,
        static_cast<const char *>(nullptr),
        static_cast<AllocationReceipt *>(nullptr))));
    static_assert(noexcept(TryEnd(nullptr)));
    static_assert(noexcept(TryFree(nullptr)));

    static char name;
    Fixture fixture;
    AllocationReceipt receipt;
    const auto initialMemory = Snapshot(fixture.memory);
    const auto initialReceipt = Snapshot(receipt);

    CHECK(TryBegin(nullptr, 0, &name, &receipt)
        == AllocationScopeStatus::InvalidArgument);
    CHECK(TryBegin(&fixture.memory, 0, nullptr, &receipt)
        == AllocationScopeStatus::InvalidArgument);
    CHECK(TryBegin(&fixture.memory, 0, &name, nullptr)
        == AllocationScopeStatus::InvalidArgument);
    CHECK(TryBegin(&fixture.memory, 2, &name, &receipt)
        == AllocationScopeStatus::InvalidAllocationType);
    CHECK(TryEnd(nullptr) == AllocationScopeStatus::InvalidArgument);
    CHECK(TryFree(nullptr) == AllocationScopeStatus::InvalidArgument);
    CHECK(TryEnd(&receipt) == AllocationScopeStatus::WrongPhase);
    CHECK(TryFree(&receipt) == AllocationScopeStatus::WrongPhase);
    CHECK(MatchesSnapshot(fixture.memory, initialMemory));
    CHECK(MatchesSnapshot(receipt, initialReceipt));

    fixture.memory.buf = nullptr;
    const auto malformedMemory = Snapshot(fixture.memory);
    CHECK(TryBegin(&fixture.memory, 0, &name, &receipt)
        == AllocationScopeStatus::MalformedState);
    CHECK(MatchesSnapshot(fixture.memory, malformedMemory));
    CHECK(MatchesSnapshot(receipt, initialReceipt));
}

void TestLowAndHighLifecycle()
{
    static char lowName;
    static char highName;
    Fixture fixture;
    AllocationReceipt low;
    AllocationReceipt high;

    CHECK(TryBegin(&fixture.memory, 0, &lowName, &low)
        == AllocationScopeStatus::Success);
    CHECK(fixture.memory.prim[0].allocName == &lowName);
    CHECK(fixture.memory.prim[0].allocListCount == 1);
    CHECK(fixture.memory.prim[0].allocList[0].name == &lowName);
    CHECK(fixture.memory.prim[0].allocList[0].pos == 0);
    CHECK(TryBegin(&fixture.memory, 0, &lowName, &low)
        == AllocationScopeStatus::ReceiptInUse);
    CHECK(TryFree(&low) == AllocationScopeStatus::WrongPhase);

    fixture.memory.prim[0].pos = 128;
    CHECK(TryEnd(&low) == AllocationScopeStatus::Success);
    CHECK(fixture.memory.prim[0].allocName == nullptr);
    const auto endedLow = Snapshot(fixture.memory);
    CHECK(TryEnd(&low) == AllocationScopeStatus::AlreadyComplete);
    CHECK(MatchesSnapshot(fixture.memory, endedLow));
    CHECK(TryBegin(&fixture.memory, 0, &lowName, &low)
        == AllocationScopeStatus::ReceiptInUse);
    CHECK(TryFree(&low) == AllocationScopeStatus::Success);
    CHECK(fixture.memory.prim[0].pos == 0);
    CHECK(fixture.memory.prim[0].allocListCount == 0);
    CHECK(fixture.memory.prim[0].allocList[0].name == nullptr);
    const auto freedLow = Snapshot(fixture.memory);
    CHECK(TryFree(&low) == AllocationScopeStatus::AlreadyComplete);
    CHECK(TryEnd(&low) == AllocationScopeStatus::AlreadyComplete);
    CHECK(MatchesSnapshot(fixture.memory, freedLow));

    CHECK(TryBegin(&fixture.memory, 1, &highName, &high)
        == AllocationScopeStatus::Success);
    CHECK(fixture.memory.prim[1].allocList[0].pos == 1024);
    fixture.memory.prim[1].pos = 896;
    CHECK(TryEnd(&high) == AllocationScopeStatus::Success);
    CHECK(TryFree(&high) == AllocationScopeStatus::Success);
    CHECK(fixture.memory.prim[1].pos == 1024);
    CHECK(fixture.memory.prim[1].allocListCount == 0);
}

void TestNamesAreIdentityOnly()
{
    Fixture fixture;
    AllocationReceipt receipt;
    const auto invalidButStableIdentity =
        reinterpret_cast<const char *>(std::uintptr_t{1});

    CHECK(TryBegin(
        &fixture.memory, 0, invalidButStableIdentity, &receipt)
        == AllocationScopeStatus::Success);
    fixture.memory.prim[0].pos = 1;
    CHECK(TryEnd(&receipt) == AllocationScopeStatus::Success);
    CHECK(TryFree(&receipt) == AllocationScopeStatus::Success);
}

void TestMiddleHolesAndLowTailCollapse()
{
    static char names[3];
    Fixture fixture;
    AllocationReceipt first;
    AllocationReceipt middle;
    AllocationReceipt tail;

    CHECK(TryBegin(&fixture.memory, 0, &names[0], &first)
        == AllocationScopeStatus::Success);
    fixture.memory.prim[0].pos = 100;
    CHECK(TryEnd(&first) == AllocationScopeStatus::Success);
    CHECK(TryBegin(&fixture.memory, 0, &names[1], &middle)
        == AllocationScopeStatus::Success);
    fixture.memory.prim[0].pos = 200;
    CHECK(TryEnd(&middle) == AllocationScopeStatus::Success);
    CHECK(TryBegin(&fixture.memory, 0, &names[2], &tail)
        == AllocationScopeStatus::Success);
    fixture.memory.prim[0].pos = 300;
    CHECK(TryEnd(&tail) == AllocationScopeStatus::Success);

    CHECK(TryFree(&middle) == AllocationScopeStatus::Success);
    CHECK(fixture.memory.prim[0].allocListCount == 3);
    CHECK(fixture.memory.prim[0].allocList[1].name == nullptr);
    CHECK(fixture.memory.prim[0].pos == 300);

    CHECK(TryFree(&tail) == AllocationScopeStatus::Success);
    CHECK(fixture.memory.prim[0].allocListCount == 1);
    CHECK(fixture.memory.prim[0].pos == 100);
    CHECK(fixture.memory.prim[0].allocList[1].name == nullptr);
    CHECK(fixture.memory.prim[0].allocList[1].pos == 100);
    CHECK(fixture.memory.prim[0].allocList[2].name == nullptr);
    CHECK(fixture.memory.prim[0].allocList[2].pos == 200);

    CHECK(TryFree(&first) == AllocationScopeStatus::Success);
    CHECK(fixture.memory.prim[0].allocListCount == 0);
    CHECK(fixture.memory.prim[0].pos == 0);
}

void TestMiddleHolesAndHighTailCollapse()
{
    static char names[3];
    Fixture fixture;
    AllocationReceipt first;
    AllocationReceipt middle;
    AllocationReceipt tail;

    CHECK(TryBegin(&fixture.memory, 1, &names[0], &first)
        == AllocationScopeStatus::Success);
    fixture.memory.prim[1].pos = 900;
    CHECK(TryEnd(&first) == AllocationScopeStatus::Success);
    CHECK(TryBegin(&fixture.memory, 1, &names[1], &middle)
        == AllocationScopeStatus::Success);
    fixture.memory.prim[1].pos = 800;
    CHECK(TryEnd(&middle) == AllocationScopeStatus::Success);
    CHECK(TryBegin(&fixture.memory, 1, &names[2], &tail)
        == AllocationScopeStatus::Success);
    fixture.memory.prim[1].pos = 700;
    CHECK(TryEnd(&tail) == AllocationScopeStatus::Success);

    CHECK(TryFree(&middle) == AllocationScopeStatus::Success);
    CHECK(fixture.memory.prim[1].allocListCount == 3);
    CHECK(fixture.memory.prim[1].pos == 700);
    CHECK(TryFree(&tail) == AllocationScopeStatus::Success);
    CHECK(fixture.memory.prim[1].allocListCount == 1);
    CHECK(fixture.memory.prim[1].pos == 900);
    CHECK(TryFree(&first) == AllocationScopeStatus::Success);
    CHECK(fixture.memory.prim[1].allocListCount == 0);
    CHECK(fixture.memory.prim[1].pos == 1024);
}

void TestBusySelectedPrimAndActiveOtherPrim()
{
    static char names[4];
    Fixture fixture;
    AllocationReceipt low;
    AllocationReceipt oldHigh;
    AllocationReceipt newHigh;

    fixture.memory.prim[1].allocName = &names[3];
    fixture.memory.prim[1].allocListCount = 1;
    fixture.memory.prim[1].allocList[0] = {&names[3], 1024};
    fixture.memory.prim[1].pos = 900;
    CHECK(TryBegin(&fixture.memory, 0, &names[0], &low)
        == AllocationScopeStatus::Success);
    fixture.memory.prim[0].pos = 100;
    CHECK(TryEnd(&low) == AllocationScopeStatus::Success);
    CHECK(TryFree(&low) == AllocationScopeStatus::Success);

    fixture.memory.prim[1] = {};
    fixture.memory.prim[1].pos = 1024;
    CHECK(TryBegin(&fixture.memory, 1, &names[1], &oldHigh)
        == AllocationScopeStatus::Success);
    fixture.memory.prim[1].pos = 900;
    AllocationReceipt rejected;
    const auto beforeBusy = Snapshot(fixture.memory);
    const auto rejectedBefore = Snapshot(rejected);
    CHECK(TryBegin(&fixture.memory, 1, &names[2], &rejected)
        == AllocationScopeStatus::Busy);
    CHECK(MatchesSnapshot(fixture.memory, beforeBusy));
    CHECK(MatchesSnapshot(rejected, rejectedBefore));
    CHECK(TryEnd(&oldHigh) == AllocationScopeStatus::Success);

    CHECK(TryBegin(&fixture.memory, 1, &names[2], &newHigh)
        == AllocationScopeStatus::Success);
    fixture.memory.prim[1].pos = 800;
    fixture.memory.prim[1].allocList[0].pos = 1000;
    const auto mismatchedWhileActive = Snapshot(fixture.memory);
    CHECK(TryFree(&oldHigh) == AllocationScopeStatus::ReceiptMismatch);
    CHECK(MatchesSnapshot(fixture.memory, mismatchedWhileActive));
    fixture.memory.prim[1].allocList[0].pos = 1024;
    const auto activeState = Snapshot(fixture.memory);
    CHECK(TryFree(&oldHigh) == AllocationScopeStatus::Busy);
    CHECK(MatchesSnapshot(fixture.memory, activeState));
    CHECK(TryEnd(&newHigh) == AllocationScopeStatus::Success);
    CHECK(TryFree(&oldHigh) == AllocationScopeStatus::Success);
    CHECK(fixture.memory.prim[1].allocListCount == 2);
    CHECK(fixture.memory.prim[1].allocList[0].name == nullptr);
    CHECK(TryFree(&newHigh) == AllocationScopeStatus::Success);
    CHECK(fixture.memory.prim[1].allocListCount == 0);
    CHECK(fixture.memory.prim[1].pos == 1024);
}

void TestMalformedTopologyRejection()
{
    static char names[4];

    ExpectMalformedBegin([](PhysicalMemory &memory) {
        memory.prim[0].pos = 900;
        memory.prim[1].pos = 800;
    });
    ExpectMalformedBegin([](PhysicalMemory &memory) {
        memory.prim[0].pos = 1;
    });
    ExpectMalformedBegin([](PhysicalMemory &memory) {
        memory.prim[0].allocListCount = 33;
    });
    ExpectMalformedBegin([](PhysicalMemory &memory) {
        memory.prim[1].allocListCount = 33;
    });
    ExpectMalformedBegin([&](PhysicalMemory &memory) {
        memory.prim[0].allocName = &names[0];
    });
    ExpectMalformedBegin([&](PhysicalMemory &memory) {
        memory.prim[1].allocName = &names[0];
    });
    ExpectMalformedBegin([&](PhysicalMemory &memory) {
        memory.prim[0].allocListCount = 1;
        memory.prim[0].allocList[0] = {nullptr, 0};
    });
    ExpectMalformedBegin([&](PhysicalMemory &memory) {
        memory.prim[0].allocListCount = 1;
        memory.prim[0].allocList[0] = {&names[0], 0};
        memory.prim[0].allocName = &names[1];
    });
    ExpectMalformedBegin([&](PhysicalMemory &memory) {
        memory.prim[0].pos = 100;
        memory.prim[0].allocListCount = 2;
        memory.prim[0].allocList[0] = {&names[0], 90};
        memory.prim[0].allocList[1] = {&names[1], 80};
    });
    ExpectMalformedBegin([&](PhysicalMemory &memory) {
        memory.prim[0].pos = 100;
        memory.prim[0].allocListCount = 1;
        memory.prim[0].allocList[0] = {&names[0], 101};
    });
    ExpectMalformedBegin([&](PhysicalMemory &memory) {
        memory.prim[0].pos = 2;
        memory.prim[0].allocListCount = 1;
        memory.prim[0].allocList[0] = {&names[0], 1};
    });
    ExpectMalformedBegin([&](PhysicalMemory &memory) {
        memory.prim[1].pos = 700;
        memory.prim[1].allocListCount = 2;
        memory.prim[1].allocList[0] = {&names[0], 800};
        memory.prim[1].allocList[1] = {&names[1], 900};
    });
    ExpectMalformedBegin([&](PhysicalMemory &memory) {
        memory.prim[1].pos = 700;
        memory.prim[1].allocListCount = 1;
        memory.prim[1].allocList[0] = {&names[0], 699};
    });
    ExpectMalformedBegin([&](PhysicalMemory &memory) {
        memory.prim[0].allocList[31].name = &names[0];
    });
    ExpectMalformedBegin([&](PhysicalMemory &memory) {
        memory.prim[1].allocList[31].name = &names[0];
    });

    Fixture validHoles;
    validHoles.memory.prim[0].pos = 30;
    validHoles.memory.prim[0].allocListCount = 3;
    validHoles.memory.prim[0].allocList[0] = {&names[0], 0};
    validHoles.memory.prim[0].allocList[1] = {nullptr, 10};
    validHoles.memory.prim[0].allocList[2] = {&names[2], 20};
    validHoles.memory.prim[0].allocList[31].pos = UINT32_MAX;
    validHoles.memory.prim[1].allocList[31].pos = UINT32_MAX;
    AllocationReceipt accepted;
    CHECK(TryBegin(&validHoles.memory, 0, &names[3], &accepted)
        == AllocationScopeStatus::Success);
    CHECK(TryEnd(&accepted) == AllocationScopeStatus::Success);
    CHECK(TryFree(&accepted) == AllocationScopeStatus::Success);

    Fixture validHighHoles;
    validHighHoles.memory.prim[1].pos = 700;
    validHighHoles.memory.prim[1].allocListCount = 3;
    validHighHoles.memory.prim[1].allocList[0] = {&names[0], 1024};
    validHighHoles.memory.prim[1].allocList[1] = {nullptr, 900};
    validHighHoles.memory.prim[1].allocList[2] = {&names[2], 800};
    validHighHoles.memory.prim[0].allocList[31].pos = UINT32_MAX;
    validHighHoles.memory.prim[1].allocList[31].pos = UINT32_MAX;
    AllocationReceipt acceptedHigh;
    CHECK(TryBegin(&validHighHoles.memory, 1, &names[3], &acceptedHigh)
        == AllocationScopeStatus::Success);
    CHECK(TryEnd(&acceptedHigh) == AllocationScopeStatus::Success);
    CHECK(TryFree(&acceptedHigh) == AllocationScopeStatus::Success);
}

void TestCapacityFailureAtomicity()
{
    std::array<char, 33> names{};
    Fixture fixture;
    PhysicalMemoryPrim &prim = fixture.memory.prim[0];
    prim.pos = 64;
    prim.allocListCount = 32;
    for (std::uint32_t index = 0; index < 32; ++index)
        prim.allocList[index] = {&names[index], index * 2};

    AllocationReceipt receipt;
    const auto memoryBefore = Snapshot(fixture.memory);
    const auto receiptBefore = Snapshot(receipt);
    CHECK(TryBegin(&fixture.memory, 0, &names[32], &receipt)
        == AllocationScopeStatus::CapacityExceeded);
    CHECK(MatchesSnapshot(fixture.memory, memoryBefore));
    CHECK(MatchesSnapshot(receipt, receiptBefore));
}

void TestReceiptAuthenticationAndIdentity()
{
    char originalName[] = "scope";
    char equalTextDifferentIdentity[] = "scope";
    static char replacement;
    Fixture fixture;
    AllocationReceipt receipt;

    CHECK(TryBegin(&fixture.memory, 0, originalName, &receipt)
        == AllocationScopeStatus::Success);
    fixture.memory.prim[0].pos = 64;

    AllocationReceipt relocated;
    RestoreAdversarialBytes(relocated, Snapshot(receipt));
    const auto beforeRelocatedEnd = Snapshot(fixture.memory);
    CHECK(TryEnd(&relocated) == AllocationScopeStatus::ReceiptMismatch);
    CHECK(TryBegin(&fixture.memory, 1, &replacement, &relocated)
        == AllocationScopeStatus::ReceiptMismatch);
    CHECK(MatchesSnapshot(fixture.memory, beforeRelocatedEnd));

    fixture.memory.prim[0].allocList[0].name = equalTextDifferentIdentity;
    fixture.memory.prim[0].allocName = equalTextDifferentIdentity;
    const auto wrongName = Snapshot(fixture.memory);
    CHECK(TryEnd(&receipt) == AllocationScopeStatus::ReceiptMismatch);
    CHECK(MatchesSnapshot(fixture.memory, wrongName));
    fixture.memory.prim[0].allocList[0].name = originalName;
    fixture.memory.prim[0].allocName = originalName;

    fixture.memory.prim[0].allocList[1] = {&replacement, 32};
    fixture.memory.prim[0].allocListCount = 2;
    fixture.memory.prim[0].allocName = &replacement;
    const auto wrongTail = Snapshot(fixture.memory);
    CHECK(TryEnd(&receipt) == AllocationScopeStatus::ReceiptMismatch);
    CHECK(MatchesSnapshot(fixture.memory, wrongTail));
    fixture.memory.prim[0].allocList[1].name = nullptr;
    fixture.memory.prim[0].allocListCount = 1;
    fixture.memory.prim[0].allocName = originalName;

    CHECK(TryEnd(&receipt) == AllocationScopeStatus::Success);
    fixture.memory.prim[0].allocList[0].name = equalTextDifferentIdentity;
    const auto wrongFreeName = Snapshot(fixture.memory);
    CHECK(TryFree(&receipt) == AllocationScopeStatus::ReceiptMismatch);
    CHECK(MatchesSnapshot(fixture.memory, wrongFreeName));
    fixture.memory.prim[0].allocList[0].name = originalName;
    CHECK(TryFree(&receipt) == AllocationScopeStatus::Success);
}

void TestReceiptPhaseWitnessAndTerminalCanonicality()
{
    static char name;
    Fixture fixture;
    AllocationReceipt receipt;
    const auto pristine = Snapshot(receipt);
    CHECK(TryBegin(&fixture.memory, 0, &name, &receipt)
        == AllocationScopeStatus::Success);
    const auto begun = Snapshot(receipt);
    fixture.memory.prim[0].pos = 32;
    CHECK(TryEnd(&receipt) == AllocationScopeStatus::Success);
    const auto ended = Snapshot(receipt);
    CHECK(TryFree(&receipt) == AllocationScopeStatus::Success);
    const auto freed = Snapshot(receipt);

    std::array<std::size_t, sizeof(AllocationReceipt)> phaseBytes{};
    std::size_t phaseByteCount = 0;
    for (std::size_t index = 0; index < ended.size(); ++index)
    {
        if (ended[index] != freed[index])
        {
            phaseBytes[phaseByteCount] = index;
            ++phaseByteCount;
        }
    }
    // Phase, phase witness, and both private terminal-tag bytes change at the
    // successful Ended-to-Freed transition.
    CHECK(phaseByteCount == 4);

    if (phaseByteCount == 4)
    {
        for (std::size_t changed = 0; changed < phaseByteCount; ++changed)
        {
            const std::size_t changedIndex = phaseBytes[changed];
            auto corruptWitness = ended;
            corruptWitness[changedIndex] = freed[changedIndex];
            RestoreAdversarialBytes(receipt, corruptWitness);
            CHECK(TryEnd(&receipt) == AllocationScopeStatus::ReceiptMismatch);
            CHECK(TryFree(&receipt) == AllocationScopeStatus::ReceiptMismatch);

            auto corruptFreed = freed;
            corruptFreed[changedIndex] ^= std::byte{1};
            RestoreAdversarialBytes(receipt, corruptFreed);
            CHECK(TryEnd(&receipt) == AllocationScopeStatus::ReceiptMismatch);
            CHECK(TryFree(&receipt) == AllocationScopeStatus::ReceiptMismatch);
        }

        auto terminalPristine = pristine;
        for (std::size_t changed = 0; changed < phaseByteCount; ++changed)
        {
            terminalPristine[phaseBytes[changed]] =
                freed[phaseBytes[changed]];
        }
        RestoreAdversarialBytes(receipt, terminalPristine);
        CHECK(TryEnd(&receipt) == AllocationScopeStatus::ReceiptMismatch);
        CHECK(TryFree(&receipt) == AllocationScopeStatus::ReceiptMismatch);
    }

    RestoreAdversarialBytes(receipt, begun);
    const auto emptyOwner = Snapshot(fixture.memory);
    CHECK(TryEnd(&receipt) == AllocationScopeStatus::ReceiptMismatch);
    CHECK(MatchesSnapshot(fixture.memory, emptyOwner));
}

void TestFreedReceiptSurvivesSameIndexReuse()
{
    static char names[4];
    Fixture fixture;
    AllocationReceipt oldLow;
    AllocationReceipt replacementLow;
    AllocationReceipt oldHigh;
    AllocationReceipt replacementHigh;

    const auto exercise = [&fixture](
                              const std::uint32_t allocType,
                              char *const oldName,
                              char *const replacementName,
                              AllocationReceipt &oldReceipt,
                              AllocationReceipt &replacementReceipt) {
        PhysicalMemoryPrim &prim = fixture.memory.prim[allocType];
        const std::uint32_t initialPosition = prim.pos;
        const std::uint32_t movedPosition = allocType == 0
            ? initialPosition + 64u
            : initialPosition - 64u;

        CHECK(TryBegin(
            &fixture.memory, allocType, oldName, &oldReceipt)
            == AllocationScopeStatus::Success);
        CHECK(prim.allocListCount == 1);
        CHECK(prim.allocList[0].name == oldName);
        prim.pos = movedPosition;
        CHECK(TryEnd(&oldReceipt) == AllocationScopeStatus::Success);
        CHECK(TryFree(&oldReceipt) == AllocationScopeStatus::Success);
        CHECK(prim.allocListCount == 0);
        CHECK(prim.pos == initialPosition);
        CHECK(pmem_runtime::detail::AuthenticateAllocationReceiptNoLock(
            oldReceipt,
            fixture.memory,
            allocType,
            0,
            oldName,
            AllocationReceiptPhase::Freed));
        CHECK(!pmem_runtime::detail::AuthenticateAllocationReceiptNoLock(
            oldReceipt,
            fixture.memory,
            1u - allocType,
            0,
            oldName,
            AllocationReceiptPhase::Freed));

        // A count-zero high prim may validly have a lower retained top (for
        // example after a persistent high allocation outside this scope).
        // This makes high index-zero reuse start at a different position.
        const std::uint32_t replacementStart = allocType == 0
            ? initialPosition
            : initialPosition - 32u;
        prim.pos = replacementStart;
        CHECK(pmem_runtime::detail::AuthenticateAllocationReceiptNoLock(
            oldReceipt,
            fixture.memory,
            allocType,
            0,
            oldName,
            AllocationReceiptPhase::Freed));

        CHECK(TryBegin(
            &fixture.memory,
            allocType,
            replacementName,
            &replacementReceipt)
            == AllocationScopeStatus::Success);
        CHECK(prim.allocListCount == 1);
        CHECK(prim.allocList[0].name == replacementName);
        CHECK(prim.allocList[0].pos == replacementStart);
        CHECK(pmem_runtime::detail::AuthenticateAllocationReceiptNoLock(
            oldReceipt,
            fixture.memory,
            allocType,
            0,
            oldName,
            AllocationReceiptPhase::Freed));
        CHECK(pmem_runtime::detail::AuthenticateAllocationReceiptNoLock(
            replacementReceipt,
            fixture.memory,
            allocType,
            0,
            replacementName,
            AllocationReceiptPhase::Begun));
        CHECK(!pmem_runtime::detail::AuthenticateAllocationReceiptNoLock(
            oldReceipt,
            fixture.memory,
            allocType,
            0,
            replacementName,
            AllocationReceiptPhase::Freed));
        const auto replacementBegun = Snapshot(fixture.memory);
        CHECK(TryEnd(&oldReceipt) == AllocationScopeStatus::AlreadyComplete);
        CHECK(TryFree(&oldReceipt) == AllocationScopeStatus::AlreadyComplete);
        CHECK(MatchesSnapshot(fixture.memory, replacementBegun));

        prim.pos = allocType == 0
            ? replacementStart + 64u
            : replacementStart - 64u;
        CHECK(TryEnd(&replacementReceipt) == AllocationScopeStatus::Success);
        CHECK(pmem_runtime::detail::AuthenticateAllocationReceiptNoLock(
            oldReceipt,
            fixture.memory,
            allocType,
            0,
            oldName,
            AllocationReceiptPhase::Freed));
        CHECK(pmem_runtime::detail::AuthenticateAllocationReceiptNoLock(
            replacementReceipt,
            fixture.memory,
            allocType,
            0,
            replacementName,
            AllocationReceiptPhase::Ended));
        const auto replacementEnded = Snapshot(fixture.memory);
        CHECK(TryEnd(&oldReceipt) == AllocationScopeStatus::AlreadyComplete);
        CHECK(TryFree(&oldReceipt) == AllocationScopeStatus::AlreadyComplete);
        CHECK(MatchesSnapshot(fixture.memory, replacementEnded));
        CHECK(TryFree(&replacementReceipt) == AllocationScopeStatus::Success);
        CHECK(pmem_runtime::detail::AuthenticateAllocationReceiptNoLock(
            oldReceipt,
            fixture.memory,
            allocType,
            0,
            oldName,
            AllocationReceiptPhase::Freed));
        CHECK(pmem_runtime::detail::AuthenticateAllocationReceiptNoLock(
            replacementReceipt,
            fixture.memory,
            allocType,
            0,
            replacementName,
            AllocationReceiptPhase::Freed));
        CHECK(TryEnd(&oldReceipt) == AllocationScopeStatus::AlreadyComplete);
        CHECK(TryFree(&oldReceipt) == AllocationScopeStatus::AlreadyComplete);
        CHECK(prim.allocListCount == 0);
        CHECK(prim.pos == replacementStart);
        prim.pos = initialPosition;
    };

    exercise(0, &names[0], &names[1], oldLow, replacementLow);
    exercise(1, &names[2], &names[3], oldHigh, replacementHigh);
}

void TestBothPrimsRevalidatedBeforeEndAndFree()
{
    static char names[3];
    Fixture fixture;
    AllocationReceipt receipt;
    CHECK(TryBegin(&fixture.memory, 0, &names[0], &receipt)
        == AllocationScopeStatus::Success);
    fixture.memory.prim[0].pos = 100;

    fixture.memory.prim[1].allocList[31].name = &names[1];
    const auto malformedEnd = Snapshot(fixture.memory);
    CHECK(TryEnd(&receipt) == AllocationScopeStatus::MalformedState);
    CHECK(MatchesSnapshot(fixture.memory, malformedEnd));
    fixture.memory.prim[1].allocList[31].name = nullptr;
    CHECK(TryEnd(&receipt) == AllocationScopeStatus::Success);

    fixture.memory.prim[1].allocName = &names[2];
    const auto malformedFree = Snapshot(fixture.memory);
    CHECK(TryFree(&receipt) == AllocationScopeStatus::MalformedState);
    CHECK(MatchesSnapshot(fixture.memory, malformedFree));
    fixture.memory.prim[1].allocName = nullptr;
    CHECK(TryFree(&receipt) == AllocationScopeStatus::Success);
}
} // namespace

int main()
{
    TestApiShapeAndArguments();
    TestLowAndHighLifecycle();
    TestNamesAreIdentityOnly();
    TestMiddleHolesAndLowTailCollapse();
    TestMiddleHolesAndHighTailCollapse();
    TestBusySelectedPrimAndActiveOtherPrim();
    TestMalformedTopologyRejection();
    TestCapacityFailureAtomicity();
    TestReceiptAuthenticationAndIdentity();
    TestReceiptPhaseWitnessAndTerminalCanonicality();
    TestFreedReceiptSurvivesSameIndexReuse();
    TestBothPrimsRevalidatedBeforeEndAndFree();

    if (g_failures != 0)
        std::fprintf(stderr, "%d checked PMem test(s) failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
