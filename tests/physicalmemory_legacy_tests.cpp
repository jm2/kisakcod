#include <universal/physicalmemory.h>

#include <qcommon/com_error.h>

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

extern PhysicalMemory g_mem;

namespace
{
int g_failures;
int g_assertReports;
int g_vaCalls;
int g_unexpectedServiceCalls;
int g_lastAssertLine;
int g_lastAssertType;
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

template <typename T>
std::array<std::byte, sizeof(T)> Snapshot(const T &value)
{
    std::array<std::byte, sizeof(T)> bytes{};
    std::memcpy(bytes.data(), &value, sizeof(value));
    return bytes;
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
    const std::array<std::byte, sizeof(PhysicalMemoryPrim)> &before,
    const int expectedLine)
{
    CheckRejected(expectedLine);
    CHECK(Snapshot(prim) == before);
}

void CheckGlobalRejectedUnchanged(
    const std::array<std::byte, sizeof(PhysicalMemory)> &before,
    const int expectedLine)
{
    CheckRejected(expectedLine);
    CHECK(Snapshot(g_mem) == before);
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

void TestWrapperAllocationTypeFailureAtomicity()
{
    char name[] = "wrapper";
    char lowName[] = "wrapper-low";
    char highName[] = "wrapper-high";
    std::array<std::uint8_t, 128> backing{};
    const PhysicalMemory original = g_mem;
    g_mem = {};
    g_mem.buf = backing.data();
    g_mem.prim[0].pos = 17;
    g_mem.prim[1].pos = 111;
    const auto before = Snapshot(g_mem);

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
        const std::uint32_t initialPosition = g_mem.prim[allocType].pos;
        ResetReports();
        PMem_BeginAlloc(validName, allocType);
        CHECK(g_assertReports == 0);
        CHECK(g_mem.prim[allocType].allocName == validName);
        CHECK(g_mem.prim[allocType].allocListCount == 1);
        CHECK(g_mem.prim[allocType].allocList[0].name == validName);
        CHECK(g_mem.prim[allocType].allocList[0].pos == initialPosition);

        PMem_EndAlloc(validName, allocType);
        CHECK(g_assertReports == 0);
        CHECK(g_mem.prim[allocType].allocName == nullptr);
        CHECK(g_mem.prim[allocType].allocListCount == 1);

        PMem_Free(validName, allocType);
        CHECK(g_assertReports == 0);
        CHECK(g_mem.prim[allocType].allocName == nullptr);
        CHECK(g_mem.prim[allocType].allocListCount == 0);
        CHECK(g_mem.prim[allocType].pos == initialPosition);
    }

    g_mem = original;
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
    const auto nullNameBefore = Snapshot(nullName);
    ResetReports();
    PMem_BeginAllocInPrim(&nullName, nullptr);
    CheckRejectedUnchanged(nullName, nullNameBefore, 331);

    PhysicalMemoryPrim busy{};
    busy.allocName = active;
    busy.allocListCount = 1;
    busy.pos = 32;
    busy.allocList[0] = {active, 0};
    const auto busyBefore = Snapshot(busy);
    ResetReports();
    PMem_BeginAllocInPrim(&busy, name);
    CheckRejectedUnchanged(busy, busyBefore, 332);

    PhysicalMemoryPrim full{};
    full.allocListCount = 32;
    full.pos = 64;
    const auto fullBefore = Snapshot(full);
    ResetReports();
    PMem_BeginAllocInPrim(&full, name);
    CheckRejectedUnchanged(full, fullBefore, 333);

    PhysicalMemoryPrim overCapacity{};
    overCapacity.allocListCount = 33;
    overCapacity.pos = 64;
    const auto overCapacityBefore = Snapshot(overCapacity);
    ResetReports();
    PMem_BeginAllocInPrim(&overCapacity, name);
    CheckRejectedUnchanged(overCapacity, overCapacityBefore, 333);

    PhysicalMemoryPrim maximumCount{};
    maximumCount.allocListCount = UINT32_MAX;
    maximumCount.pos = 64;
    const auto maximumCountBefore = Snapshot(maximumCount);
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
    const auto nullNameBefore = Snapshot(nullName);
    ResetReports();
    PMem_EndAllocInPrim(&nullName, nullptr);
    CheckRejectedUnchanged(nullName, nullNameBefore, 362);

    PhysicalMemoryPrim wrongActive{};
    wrongActive.allocName = expected;
    wrongActive.allocListCount = 1;
    wrongActive.pos = 64;
    wrongActive.allocList[0] = {expected, 0};
    const auto wrongActiveBefore = Snapshot(wrongActive);
    ResetReports();
    PMem_EndAllocInPrim(&wrongActive, other);
    CheckRejectedUnchanged(wrongActive, wrongActiveBefore, 364);

    PhysicalMemoryPrim empty{};
    empty.allocName = expected;
    const auto emptyBefore = Snapshot(empty);
    ResetReports();
    PMem_EndAllocInPrim(&empty, expected);
    CheckRejectedUnchanged(empty, emptyBefore, 368);

    PhysicalMemoryPrim overCapacity{};
    overCapacity.allocName = expected;
    overCapacity.allocListCount = 33;
    const auto overCapacityBefore = Snapshot(overCapacity);
    ResetReports();
    PMem_EndAllocInPrim(&overCapacity, expected);
    CheckRejectedUnchanged(overCapacity, overCapacityBefore, 369);

    PhysicalMemoryPrim wrongTail{};
    wrongTail.allocName = expected;
    wrongTail.allocListCount = 2;
    wrongTail.pos = 64;
    wrongTail.allocList[0] = {expected, 0};
    wrongTail.allocList[1] = {other, 32};
    const auto wrongTailBefore = Snapshot(wrongTail);
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
    const auto busyBefore = Snapshot(busy);
    ResetReports();
    PMem_FreeIndex(&busy, 0);
    CheckRejectedUnchanged(busy, busyBefore, 394);

    PhysicalMemoryPrim empty{};
    const auto emptyBefore = Snapshot(empty);
    ResetReports();
    PMem_FreeIndex(&empty, 0);
    CheckRejectedUnchanged(empty, emptyBefore, 424);

    PhysicalMemoryPrim overCapacity{};
    overCapacity.allocListCount = 33;
    overCapacity.allocList[0] = {name, 0};
    const auto overCapacityBefore = Snapshot(overCapacity);
    ResetReports();
    PMem_FreeIndex(&overCapacity, 0);
    CheckRejectedUnchanged(overCapacity, overCapacityBefore, 395);

    PhysicalMemoryPrim invalidIndex{};
    invalidIndex.allocListCount = 1;
    invalidIndex.allocList[0] = {name, 0};
    const auto invalidIndexBefore = Snapshot(invalidIndex);
    ResetReports();
    PMem_FreeIndex(&invalidIndex, 1);
    CheckRejectedUnchanged(invalidIndex, invalidIndexBefore, 408);

    ResetReports();
    PMem_FreeIndex(&invalidIndex, UINT32_MAX);
    CheckRejectedUnchanged(invalidIndex, invalidIndexBefore, 408);

    PhysicalMemoryPrim nullEntry{};
    nullEntry.allocListCount = 1;
    nullEntry.allocList[0] = {nullptr, 17};
    const auto nullEntryBefore = Snapshot(nullEntry);
    ResetReports();
    PMem_FreeIndex(&nullEntry, 0);
    CheckRejectedUnchanged(nullEntry, nullEntryBefore, 400);
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
    const auto nullNameBefore = Snapshot(nullName);
    ResetReports();
    PMem_FreeInPrim(&nullName, nullptr);
    CheckRejectedUnchanged(nullName, nullNameBefore, 438);

    PhysicalMemoryPrim overCapacity{};
    overCapacity.allocListCount = 33;
    overCapacity.allocList[0] = {name, 0};
    const auto overCapacityBefore = Snapshot(overCapacity);
    ResetReports();
    PMem_FreeInPrim(&overCapacity, name);
    CheckRejectedUnchanged(overCapacity, overCapacityBefore, 439);

    PhysicalMemoryPrim maximumCount{};
    maximumCount.allocListCount = UINT32_MAX;
    maximumCount.allocList[0] = {name, 0};
    const auto maximumCountBefore = Snapshot(maximumCount);
    ResetReports();
    PMem_FreeInPrim(&maximumCount, name);
    CheckRejectedUnchanged(maximumCount, maximumCountBefore, 439);

    PhysicalMemoryPrim full{};
    full.allocListCount = 32;
    for (std::uint32_t index = 0; index < full.allocListCount; ++index)
        full.allocList[index] = {name, index};
    const auto fullBefore = Snapshot(full);
    ResetReports();
    PMem_FreeInPrim(&full, missing);
    CHECK(g_assertReports == 0);
    CHECK(g_vaCalls == 0);
    CHECK(Snapshot(full) == fullBefore);

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

    const auto beforeMissing = Snapshot(prim);
    PMem_FreeInPrim(&prim, missing);
    CHECK(Snapshot(prim) == beforeMissing);
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
    ++g_unexpectedServiceCalls;
}

void KISAK_CDECL Com_Error(const errorParm_t, const char *, ...)
{
    ++g_unexpectedServiceCalls;
}

double KISAK_CDECL ConvertToMB(const int bytes)
{
    ++g_unexpectedServiceCalls;
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

void *KISAK_CDECL Sys_VirtualMemoryReserve(const std::size_t)
{
    ++g_unexpectedServiceCalls;
    return nullptr;
}

bool KISAK_CDECL Sys_VirtualMemoryCommit(void *, const std::size_t)
{
    ++g_unexpectedServiceCalls;
    return false;
}

bool KISAK_CDECL Sys_VirtualMemoryRelease(void *)
{
    ++g_unexpectedServiceCalls;
    return false;
}

void KISAK_CDECL Sys_OutOfMemErrorInternal(const char *, const int)
{
    ++g_unexpectedServiceCalls;
}

int main()
{
    TestNativeLayout();
    TestWrapperAllocationTypeFailureAtomicity();
    TestBeginFailureAtomicity();
    TestEndFailureAtomicity();
    TestFreeIndexFailureAtomicity();
    TestFreeInPrimFailureAtomicity();
    TestLowPrimTailHoleCollapse();
    TestHighPrimRetainsInitialAllocation();
    CHECK(g_unexpectedServiceCalls == 0);

    if (g_failures != 0)
        std::fprintf(stderr, "%d legacy PMem test(s) failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
