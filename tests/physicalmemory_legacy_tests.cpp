#include <universal/physicalmemory.h>

#include <qcommon/com_error.h>
#include <qcommon/sys_sync.h>

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace
{
using PhysicalMemoryStateAccess =
    PhysicalMemoryGlobalStateTestAccess;

int g_failures;
int g_assertReports;
int g_vaCalls;
int g_unexpectedServiceCalls;
int g_lastAssertLine;
int g_lastAssertType;
thread_local int g_legacyLockDepth;
char g_lastAssertFile[96];
char g_lastAssertFormat[256];

void Check(const bool condition, const char *const expression, const int line)
{
    if (condition)
        return;
    std::fprintf(stderr, "line %d: check failed: %s\n", line, expression);
    ++g_failures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

bool SameAllocationState(
    const PhysicalMemoryAllocation &lhs,
    const PhysicalMemoryAllocation &rhs)
{
    return lhs.name == rhs.name && lhs.pos == rhs.pos;
}

bool SamePrimState(
    const PhysicalMemoryPrim &lhs,
    const PhysicalMemoryPrim &rhs)
{
    if (lhs.allocName != rhs.allocName
        || lhs.allocListCount != rhs.allocListCount
        || lhs.pos != rhs.pos)
    {
        return false;
    }
    for (std::uint32_t index = 0; index < MAX_PHYSICAL_ALLOCATIONS; ++index)
    {
        if (!SameAllocationState(lhs.allocList[index], rhs.allocList[index]))
            return false;
    }
    return true;
}

bool SamePhysicalMemoryState(
    const PhysicalMemory &lhs,
    const PhysicalMemory &rhs)
{
    return lhs.buf == rhs.buf
        && SamePrimState(lhs.prim[0], rhs.prim[0])
        && SamePrimState(lhs.prim[1], rhs.prim[1]);
}

PhysicalMemoryPrim CapturePrimState(const PhysicalMemoryPrim &prim)
{
    return prim;
}

using GlobalStateImage = PhysicalMemoryStateAccess::Snapshot;

bool SameOwnedNames(
    const GlobalStateImage &left,
    const GlobalStateImage &right)
{
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
                 byte < PhysicalMemoryStateAccess::OWNED_NAME_CAPACITY;
                 ++byte)
            {
                if (leftName.text[byte] != rightName.text[byte])
                    return false;
            }
        }
    }
    return true;
}

GlobalStateImage CaptureGlobalState()
{
    return PhysicalMemoryStateAccess::Capture();
}

bool MatchesGlobalState(const GlobalStateImage &expected)
{
    const auto current = CaptureGlobalState();
    return SamePhysicalMemoryState(current.memory, expected.memory)
        && SameOwnedNames(current, expected)
        && current.overAllocatedSize == expected.overAllocatedSize
        && current.retainedBase == expected.retainedBase
        && current.retainedSize == expected.retainedSize
        && current.initializationPhase == expected.initializationPhase
        && current.runtimeReserved[0] == expected.runtimeReserved[0]
        && current.runtimeReserved[1] == expected.runtimeReserved[1]
        && current.runtimeReserved[2] == expected.runtimeReserved[2]
        && current.initializationWitness == expected.initializationWitness;
}

void ResetReports()
{
    g_assertReports = 0;
    g_vaCalls = 0;
    g_lastAssertLine = 0;
    g_lastAssertType = 0;
    g_lastAssertFile[0] = '\0';
    g_lastAssertFormat[0] = '\0';
}

void CheckHoleReport(const char *const expected)
{
    CHECK(g_vaCalls == 0);
    CHECK(g_assertReports == 1);
    CHECK(g_lastAssertLine == 411);
    CHECK(g_lastAssertType == 0);
    CHECK(std::strcmp(
        g_lastAssertFile,
        ".\\universal\\physicalmemory.cpp") == 0);
    CHECK(std::strcmp(g_lastAssertFormat, expected) == 0);
}

void CheckRejected(const int expectedLine)
{
    CHECK(g_vaCalls == 0);
    CHECK(g_assertReports == 1);
    CHECK(g_lastAssertLine == expectedLine);
    CHECK(g_lastAssertType == 0);
    CHECK(std::strcmp(
        g_lastAssertFile,
        ".\\universal\\physicalmemory.cpp") == 0);
}

void CheckRejectedUnchanged(
    const PhysicalMemoryPrim &prim,
    const PhysicalMemoryPrim &before,
    const int expectedLine)
{
    CheckRejected(expectedLine);
    CHECK(SamePrimState(prim, before));
}

void CheckGlobalRejectedUnchanged(
    const GlobalStateImage &before,
    const int expectedLine)
{
    CheckRejected(expectedLine);
    CHECK(MatchesGlobalState(before));
}

void BeginAndEnd(
    PhysicalMemoryPrim &prim,
    const char *const name,
    const std::uint32_t endPosition)
{
    const std::uint32_t index = prim.allocListCount;
    const std::uint32_t startPosition = prim.pos;
    PMem_BeginAllocInPrim(&prim, name);
    CHECK(prim.allocName == name);
    CHECK(prim.allocListCount == index + 1);
    CHECK(prim.allocList[index].name == name);
    CHECK(prim.allocList[index].pos == startPosition);
    prim.pos = endPosition;
    PMem_EndAllocInPrim(&prim, name);
    CHECK(prim.allocName == nullptr);
    CHECK(prim.allocListCount == index + 1);
}

void TestNativeLayout()
{
    static_assert(std::is_standard_layout_v<PhysicalMemoryAllocation>);
    static_assert(std::is_standard_layout_v<PhysicalMemoryPrim>);
    static_assert(std::is_standard_layout_v<PhysicalMemory>);
    static_assert(std::extent_v<decltype(PhysicalMemoryPrim::allocList)> == 32);
    static_assert(std::extent_v<decltype(PhysicalMemory::prim)> == 2);

#if UINTPTR_MAX == UINT32_MAX
    CHECK(sizeof(PhysicalMemoryAllocation) == 0x8);
    CHECK(offsetof(PhysicalMemoryAllocation, pos) == 0x4);
    CHECK(sizeof(PhysicalMemoryPrim) == 0x10C);
    CHECK(offsetof(PhysicalMemoryPrim, allocList) == 0xC);
    CHECK(sizeof(PhysicalMemory) == 0x21C);
    CHECK(offsetof(PhysicalMemory, prim) == 0x4);
#elif UINTPTR_MAX == UINT64_MAX
    CHECK(sizeof(PhysicalMemoryAllocation) == 0x10);
    CHECK(offsetof(PhysicalMemoryAllocation, pos) == 0x8);
    CHECK(sizeof(PhysicalMemoryPrim) == 0x210);
    CHECK(offsetof(PhysicalMemoryPrim, allocList) == 0x10);
    CHECK(sizeof(PhysicalMemory) == 0x428);
    CHECK(offsetof(PhysicalMemory, prim) == 0x8);
#endif
}

void TestGlobalStateAccessCopiesByValue()
{
    static_assert(!std::is_constructible_v<PhysicalMemoryStateAccess>);
    static_assert(!std::is_reference_v<
        decltype(PhysicalMemoryStateAccess::Capture())>);

    std::array<std::uint8_t, 64> backing{};
    const auto original = PhysicalMemoryStateAccess::Capture();
    PhysicalMemoryStateAccess::Snapshot installed{};
    installed.memory.buf = backing.data();
    installed.memory.prim[0].pos = 7;
    installed.memory.prim[1].pos = 61;
    installed.overAllocatedSize = 23;
    installed = PhysicalMemoryStateAccess::MakeCanonicalReady(
        installed.memory,
        static_cast<std::uint32_t>(backing.size()),
        installed.overAllocatedSize);
    PhysicalMemoryStateAccess::Install(installed);

    auto detached = PhysicalMemoryStateAccess::Capture();
    CHECK(detached.memory.buf == backing.data());
    CHECK(detached.memory.prim[0].pos == 7);
    CHECK(detached.memory.prim[1].pos == 61);
    CHECK(detached.overAllocatedSize == 23);
    CHECK(detached.retainedBase == backing.data());
    CHECK(detached.retainedSize == backing.size());
    detached.memory = {};
    detached.overAllocatedSize = -1;

    const auto retained = PhysicalMemoryStateAccess::Capture();
    CHECK(retained.memory.buf == backing.data());
    CHECK(retained.memory.prim[0].pos == 7);
    CHECK(retained.memory.prim[1].pos == 61);
    CHECK(retained.overAllocatedSize == 23);
    CHECK(retained.retainedBase == backing.data());
    CHECK(retained.retainedSize == backing.size());
    PhysicalMemoryStateAccess::Install(original);
}

void TestWrapperAllocationTypeFailureAtomicity()
{
    char name[] = "wrapper";
    char lowName[] = "wrapper-low";
    char highName[] = "wrapper-high";
    std::array<std::uint8_t, 128> backing{};
    const auto original = PhysicalMemoryStateAccess::Capture();
    PhysicalMemoryStateAccess::Snapshot installed{};
    installed.memory.buf = backing.data();
    installed.memory.prim[0].pos = 0;
    installed.memory.prim[1].pos =
        static_cast<std::uint32_t>(backing.size());
    installed.overAllocatedSize = 37;
    installed = PhysicalMemoryStateAccess::MakeCanonicalReady(
        installed.memory,
        static_cast<std::uint32_t>(backing.size()),
        installed.overAllocatedSize);
    PhysicalMemoryStateAccess::Install(installed);
    const auto before = CaptureGlobalState();

    constexpr std::array<std::uint32_t, 2> invalidTypes{
        2,
        UINT32_MAX,
    };
    for (const std::uint32_t allocType : invalidTypes)
    {
        ResetReports();
        PMem_BeginAlloc(name, allocType);
        CheckGlobalRejectedUnchanged(before, 350);

        ResetReports();
        PMem_EndAlloc(name, allocType);
        CheckGlobalRejectedUnchanged(before, 378);

        ResetReports();
        PMem_Free(name, allocType);
        CheckGlobalRejectedUnchanged(before, 454);
    }

    const std::array<const char *, 2> validNames{
        lowName,
        highName,
    };
    for (std::uint32_t allocType = 0; allocType < validNames.size(); ++allocType)
    {
        const char *const validName = validNames[allocType];
        const auto beforeBegin = PhysicalMemoryStateAccess::Capture();
        const std::uint32_t initialPosition =
            beforeBegin.memory.prim[allocType].pos;
        ResetReports();
        PMem_BeginAlloc(validName, allocType);
        const auto begun = PhysicalMemoryStateAccess::Capture();
        CHECK(g_assertReports == 0);
        CHECK(begun.memory.prim[allocType].allocName == validName);
        CHECK(begun.memory.prim[allocType].allocListCount == 1);
        CHECK(begun.memory.prim[allocType].allocList[0].name == validName);
        CHECK(begun.ownedNames[allocType][0].identity
            == reinterpret_cast<std::uintptr_t>(validName));
        CHECK(std::strcmp(begun.ownedNames[allocType][0].text, validName) == 0);
        CHECK(begun.memory.prim[allocType].allocList[0].pos == initialPosition);
        CHECK(begun.overAllocatedSize == 37);

        PMem_EndAlloc(validName, allocType);
        const auto ended = PhysicalMemoryStateAccess::Capture();
        CHECK(g_assertReports == 0);
        CHECK(ended.memory.prim[allocType].allocName == nullptr);
        CHECK(ended.memory.prim[allocType].allocListCount == 1);
        CHECK(ended.overAllocatedSize == 37);

        PMem_Free(validName, allocType);
        const auto freed = PhysicalMemoryStateAccess::Capture();
        CHECK(g_assertReports == 0);
        CHECK(freed.memory.prim[allocType].allocName == nullptr);
        CHECK(freed.memory.prim[allocType].allocListCount == 0);
        CHECK(freed.memory.prim[allocType].pos == initialPosition);
        CHECK(freed.overAllocatedSize == 37);
    }

    PhysicalMemoryStateAccess::Install(original);
}

void TestBeginFailureAtomicity()
{
    char name[] = "begin";
    char active[] = "active";

    ResetReports();
    PMem_BeginAllocInPrim(nullptr, name);
    CheckRejected(330);

    PhysicalMemoryPrim nullName{};
    nullName.pos = 9;
    const auto nullNameBefore = CapturePrimState(nullName);
    ResetReports();
    PMem_BeginAllocInPrim(&nullName, nullptr);
    CheckRejectedUnchanged(nullName, nullNameBefore, 331);

    PhysicalMemoryPrim busy{};
    busy.allocName = active;
    busy.allocListCount = 1;
    busy.pos = 32;
    busy.allocList[0] = {active, 0};
    const auto busyBefore = CapturePrimState(busy);
    ResetReports();
    PMem_BeginAllocInPrim(&busy, name);
    CheckRejectedUnchanged(busy, busyBefore, 332);

    PhysicalMemoryPrim full{};
    full.allocListCount = 32;
    full.pos = 64;
    const auto fullBefore = CapturePrimState(full);
    ResetReports();
    PMem_BeginAllocInPrim(&full, name);
    CheckRejectedUnchanged(full, fullBefore, 333);

    PhysicalMemoryPrim overCapacity{};
    overCapacity.allocListCount = 33;
    overCapacity.pos = 64;
    const auto overCapacityBefore = CapturePrimState(overCapacity);
    ResetReports();
    PMem_BeginAllocInPrim(&overCapacity, name);
    CheckRejectedUnchanged(overCapacity, overCapacityBefore, 333);

    PhysicalMemoryPrim maximumCount{};
    maximumCount.allocListCount = UINT32_MAX;
    maximumCount.pos = 64;
    const auto maximumCountBefore = CapturePrimState(maximumCount);
    ResetReports();
    PMem_BeginAllocInPrim(&maximumCount, name);
    CheckRejectedUnchanged(maximumCount, maximumCountBefore, 333);

    PhysicalMemoryPrim lastSlot{};
    lastSlot.allocListCount = 31;
    lastSlot.pos = 73;
    ResetReports();
    PMem_BeginAllocInPrim(&lastSlot, name);
    CHECK(g_assertReports == 0);
    CHECK(g_vaCalls == 0);
    CHECK(lastSlot.allocName == name);
    CHECK(lastSlot.allocListCount == 32);
    CHECK(lastSlot.allocList[31].name == name);
    CHECK(lastSlot.allocList[31].pos == 73);
}

void TestEndFailureAtomicity()
{
    char expected[] = "expected";
    char other[] = "other";

    ResetReports();
    PMem_EndAllocInPrim(nullptr, expected);
    CheckRejected(361);

    PhysicalMemoryPrim nullName{};
    nullName.allocName = expected;
    nullName.allocListCount = 1;
    nullName.allocList[0] = {expected, 0};
    const auto nullNameBefore = CapturePrimState(nullName);
    ResetReports();
    PMem_EndAllocInPrim(&nullName, nullptr);
    CheckRejectedUnchanged(nullName, nullNameBefore, 362);

    PhysicalMemoryPrim wrongActive{};
    wrongActive.allocName = expected;
    wrongActive.allocListCount = 1;
    wrongActive.pos = 64;
    wrongActive.allocList[0] = {expected, 0};
    const auto wrongActiveBefore = CapturePrimState(wrongActive);
    ResetReports();
    PMem_EndAllocInPrim(&wrongActive, other);
    CheckRejectedUnchanged(wrongActive, wrongActiveBefore, 364);

    PhysicalMemoryPrim empty{};
    empty.allocName = expected;
    const auto emptyBefore = CapturePrimState(empty);
    ResetReports();
    PMem_EndAllocInPrim(&empty, expected);
    CheckRejectedUnchanged(empty, emptyBefore, 368);

    PhysicalMemoryPrim overCapacity{};
    overCapacity.allocName = expected;
    overCapacity.allocListCount = 33;
    const auto overCapacityBefore = CapturePrimState(overCapacity);
    ResetReports();
    PMem_EndAllocInPrim(&overCapacity, expected);
    CheckRejectedUnchanged(overCapacity, overCapacityBefore, 369);

    PhysicalMemoryPrim wrongTail{};
    wrongTail.allocName = expected;
    wrongTail.allocListCount = 2;
    wrongTail.pos = 64;
    wrongTail.allocList[0] = {expected, 0};
    wrongTail.allocList[1] = {other, 32};
    const auto wrongTailBefore = CapturePrimState(wrongTail);
    ResetReports();
    PMem_EndAllocInPrim(&wrongTail, expected);
    CheckRejectedUnchanged(wrongTail, wrongTailBefore, 370);
}

void TestFreeIndexFailureAtomicity()
{
    char name[] = "entry";
    char active[] = "active";

    ResetReports();
    PMem_FreeIndex(nullptr, 0);
    CheckRejected(393);

    PhysicalMemoryPrim busy{};
    busy.allocName = active;
    busy.allocListCount = 1;
    busy.allocList[0] = {name, 0};
    const auto busyBefore = CapturePrimState(busy);
    ResetReports();
    PMem_FreeIndex(&busy, 0);
    CheckRejectedUnchanged(busy, busyBefore, 394);

    PhysicalMemoryPrim empty{};
    const auto emptyBefore = CapturePrimState(empty);
    ResetReports();
    PMem_FreeIndex(&empty, 0);
    CheckRejectedUnchanged(empty, emptyBefore, 424);

    PhysicalMemoryPrim overCapacity{};
    overCapacity.allocListCount = 33;
    overCapacity.allocList[0] = {name, 0};
    const auto overCapacityBefore = CapturePrimState(overCapacity);
    ResetReports();
    PMem_FreeIndex(&overCapacity, 0);
    CheckRejectedUnchanged(overCapacity, overCapacityBefore, 395);

    PhysicalMemoryPrim invalidIndex{};
    invalidIndex.allocListCount = 1;
    invalidIndex.allocList[0] = {name, 0};
    const auto invalidIndexBefore = CapturePrimState(invalidIndex);
    ResetReports();
    PMem_FreeIndex(&invalidIndex, 1);
    CheckRejectedUnchanged(invalidIndex, invalidIndexBefore, 408);

    ResetReports();
    PMem_FreeIndex(&invalidIndex, UINT32_MAX);
    CheckRejectedUnchanged(invalidIndex, invalidIndexBefore, 408);

    PhysicalMemoryPrim nullEntry{};
    nullEntry.allocListCount = 1;
    nullEntry.allocList[0] = {nullptr, 17};
    const auto nullEntryBefore = CapturePrimState(nullEntry);
    ResetReports();
    PMem_FreeIndex(&nullEntry, 0);
    CheckRejectedUnchanged(nullEntry, nullEntryBefore, 400);

    PhysicalMemoryPrim identityOnlyTail{};
    identityOnlyTail.allocListCount = 1;
    identityOnlyTail.pos = 23;
    identityOnlyTail.allocList[0] = {
        reinterpret_cast<const char *>(static_cast<std::uintptr_t>(1)),
        11,
    };
    ResetReports();
    PMem_FreeIndex(&identityOnlyTail, 0);
    CHECK(g_assertReports == 0);
    CHECK(identityOnlyTail.allocListCount == 0);
    CHECK(identityOnlyTail.pos == 11);
}

void TestFreeInPrimFailureAtomicity()
{
    char name[] = "free";
    char missing[] = "missing";

    ResetReports();
    PMem_FreeInPrim(nullptr, name);
    CheckRejected(437);

    PhysicalMemoryPrim nullName{};
    nullName.allocListCount = 1;
    nullName.pos = 32;
    nullName.allocList[0] = {name, 0};
    const auto nullNameBefore = CapturePrimState(nullName);
    ResetReports();
    PMem_FreeInPrim(&nullName, nullptr);
    CheckRejectedUnchanged(nullName, nullNameBefore, 438);

    PhysicalMemoryPrim overCapacity{};
    overCapacity.allocListCount = 33;
    overCapacity.allocList[0] = {name, 0};
    const auto overCapacityBefore = CapturePrimState(overCapacity);
    ResetReports();
    PMem_FreeInPrim(&overCapacity, name);
    CheckRejectedUnchanged(overCapacity, overCapacityBefore, 439);

    PhysicalMemoryPrim maximumCount{};
    maximumCount.allocListCount = UINT32_MAX;
    maximumCount.allocList[0] = {name, 0};
    const auto maximumCountBefore = CapturePrimState(maximumCount);
    ResetReports();
    PMem_FreeInPrim(&maximumCount, name);
    CheckRejectedUnchanged(maximumCount, maximumCountBefore, 439);

    PhysicalMemoryPrim full{};
    full.allocListCount = 32;
    for (std::uint32_t index = 0; index < full.allocListCount; ++index)
        full.allocList[index] = {name, index};
    const auto fullBefore = CapturePrimState(full);
    ResetReports();
    PMem_FreeInPrim(&full, missing);
    CHECK(g_assertReports == 0);
    CHECK(g_vaCalls == 0);
    CHECK(SamePrimState(full, fullBefore));

    PhysicalMemoryPrim valid{};
    valid.pos = 96;
    ResetReports();
    BeginAndEnd(valid, name, 160);
    PMem_FreeInPrim(&valid, name);
    CHECK(g_assertReports == 0);
    CHECK(g_vaCalls == 0);
    CHECK(valid.allocName == nullptr);
    CHECK(valid.allocListCount == 0);
    CHECK(valid.pos == 96);
}

void TestLowPrimTailHoleCollapse()
{
    char first[] = "low-first";
    char middle[] = "low-%n-%s-middle";
    char tail[] = "low-tail";
    char missing[] = "low-missing";
    PhysicalMemoryPrim prim{};

    ResetReports();
    BeginAndEnd(prim, first, 64);
    BeginAndEnd(prim, middle, 160);
    BeginAndEnd(prim, tail, 240);
    CHECK(g_assertReports == 0);
    CHECK(g_vaCalls == 0);

    const auto beforeMissing = CapturePrimState(prim);
    PMem_FreeInPrim(&prim, missing);
    CHECK(SamePrimState(prim, beforeMissing));
    CHECK(g_assertReports == 0);

    ResetReports();
    PMem_FreeIndex(&prim, 1);
    CheckHoleReport("freeing 'low-%n-%s-middle' caused a memory hole\n");
    CHECK(prim.allocListCount == 3);
    CHECK(prim.pos == 240);
    CHECK(prim.allocList[0].name == first);
    CHECK(prim.allocList[1].name == nullptr);
    CHECK(prim.allocList[2].name == tail);

    ResetReports();
    PMem_FreeIndex(&prim, 2);
    CHECK(g_assertReports == 0);
    CHECK(g_vaCalls == 0);
    CHECK(prim.allocListCount == 1);
    CHECK(prim.pos == 64);
    CHECK(prim.allocList[0].name == first);

    PMem_FreeIndex(&prim, 0);
    CHECK(g_assertReports == 0);
    CHECK(g_vaCalls == 0);
    CHECK(prim.allocListCount == 0);
    CHECK(prim.pos == 0);

    std::array<char, 96> longName{};
    longName.fill('q');
    longName.back() = '\0';
    char longTail[] = "long-tail";
    PhysicalMemoryPrim longPrim{};
    BeginAndEnd(longPrim, longName.data(), 1);
    BeginAndEnd(longPrim, longTail, 2);
    char expectedLongReport[160]{};
    std::snprintf(
        expectedLongReport,
        sizeof(expectedLongReport),
        "freeing '%s' caused a memory hole\n",
        longName.data());
    ResetReports();
    PMem_FreeIndex(&longPrim, 0);
    CheckHoleReport(expectedLongReport);
}

void TestHighPrimRetainsInitialAllocation()
{
    char initName[] = "$init";
    char middle[] = "high-middle";
    char tail[] = "high-tail";
    PhysicalMemoryPrim prim{};
    prim.pos = 1024;

    ResetReports();
    BeginAndEnd(prim, initName, 900);
    BeginAndEnd(prim, middle, 800);
    BeginAndEnd(prim, tail, 700);
    CHECK(g_assertReports == 0);
    CHECK(g_vaCalls == 0);

    ResetReports();
    PMem_FreeIndex(&prim, 1);
    CheckHoleReport("freeing 'high-middle' caused a memory hole\n");
    CHECK(prim.allocListCount == 3);
    CHECK(prim.pos == 700);

    ResetReports();
    PMem_FreeIndex(&prim, 2);
    CHECK(g_assertReports == 0);
    CHECK(g_vaCalls == 0);
    CHECK(prim.allocListCount == 1);
    CHECK(prim.pos == 900);
    CHECK(prim.allocList[0].name == initName);
    CHECK(prim.allocList[0].pos == 1024);

    PMem_FreeIndex(&prim, 0);
    CHECK(g_assertReports == 0);
    CHECK(g_vaCalls == 0);
    CHECK(prim.allocListCount == 0);
    CHECK(prim.pos == 1024);
}
} // namespace

void MyAssertHandler(
    const char *filename,
    const int line,
    const int type,
    const char *format,
    ...)
{
    CHECK(g_legacyLockDepth == 0);
    ++g_assertReports;
    g_lastAssertLine = line;
    g_lastAssertType = type;
    std::snprintf(
        g_lastAssertFile,
        sizeof(g_lastAssertFile),
        "%s",
        filename ? filename : "<null>");
    if (!format)
    {
        std::snprintf(
            g_lastAssertFormat,
            sizeof(g_lastAssertFormat),
            "%s",
            "<null>");
        return;
    }
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(
        g_lastAssertFormat,
        sizeof(g_lastAssertFormat),
        format,
        arguments);
    va_end(arguments);
}

char *KISAK_CDECL va(const char *const format, ...)
{
    static char buffer[256];
    ++g_vaCalls;
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    return buffer;
}

void KISAK_CDECL Com_Printf(const int, const char *, ...)
{
    CHECK(g_legacyLockDepth == 0);
    ++g_unexpectedServiceCalls;
}

void KISAK_CDECL Com_Error(const errorParm_t, const char *, ...)
{
    CHECK(g_legacyLockDepth == 0);
    ++g_unexpectedServiceCalls;
}

double KISAK_CDECL ConvertToMB(const int bytes)
{
    CHECK(g_legacyLockDepth == 0);
    ++g_unexpectedServiceCalls;
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

void *KISAK_CDECL Sys_VirtualMemoryReserve(const std::size_t)
{
    CHECK(g_legacyLockDepth == 0);
    ++g_unexpectedServiceCalls;
    return nullptr;
}

bool KISAK_CDECL Sys_VirtualMemoryCommit(void *, const std::size_t)
{
    CHECK(g_legacyLockDepth == 0);
    ++g_unexpectedServiceCalls;
    return false;
}

bool KISAK_CDECL Sys_VirtualMemoryRelease(void *)
{
    CHECK(g_legacyLockDepth == 0);
    ++g_unexpectedServiceCalls;
    return false;
}

void KISAK_CDECL Sys_OutOfMemErrorInternal(const char *, const int)
{
    CHECK(g_legacyLockDepth == 0);
    ++g_unexpectedServiceCalls;
}

void KISAK_CDECL Sys_EnterCriticalSection(const int criticalSection)
{
    CHECK(criticalSection == CRITSECT_PHYSICAL_MEMORY);
    ++g_legacyLockDepth;
    CHECK(g_legacyLockDepth == 1);
}

void KISAK_CDECL Sys_LeaveCriticalSection(const int criticalSection)
{
    CHECK(criticalSection == CRITSECT_PHYSICAL_MEMORY);
    CHECK(g_legacyLockDepth == 1);
    --g_legacyLockDepth;
}

int main()
{
    TestNativeLayout();
    TestGlobalStateAccessCopiesByValue();
    TestWrapperAllocationTypeFailureAtomicity();
    TestBeginFailureAtomicity();
    TestEndFailureAtomicity();
    TestFreeIndexFailureAtomicity();
    TestFreeInPrimFailureAtomicity();
    TestLowPrimTailHoleCollapse();
    TestHighPrimRetainsInitialAllocation();
    CHECK(g_legacyLockDepth == 0);
    CHECK(g_unexpectedServiceCalls == 0);

    if (g_failures != 0)
        std::fprintf(stderr, "%d legacy PMem test(s) failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
