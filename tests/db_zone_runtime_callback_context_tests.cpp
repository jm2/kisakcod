#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

#define KISAK_DB_ZONE_RUNTIME_CALLBACK_CONTEXT_TESTING 1
#include <database/db_zone_runtime_callback_context.h>

namespace
{
pmem_runtime::StorageIsolationStatus g_isolationStatus =
    pmem_runtime::StorageIsolationStatus::Uninitialized;
const void *g_lastIsolationStorage = nullptr;
std::size_t g_lastIsolationSize = 0;
std::uint32_t g_isolationCalls = 0;

[[noreturn]] void Fail(const char *expression, const int line)
{
    std::cerr << "CHECK failed at line " << line << ": "
              << expression << '\n';
    std::exit(1);
}

#define CHECK(expression) \
    do \
    { \
        if (!(expression)) \
            Fail(#expression, __LINE__); \
    } while (false)

using db::zone_load::ZoneLoadContextKey;
using db::zone_runtime::ZoneRuntimeCallbackContext;
using db::zone_runtime::ZoneRuntimeCallbackContextBindResult;
using db::zone_runtime::ZoneRuntimeCallbackContextPhase;
using db::zone_runtime::ZoneRuntimeCallbackContextSnapshot;
using db::zone_runtime::ZoneRuntimeCallbackContextStatus;
using Access = db::zone_runtime::ZoneRuntimeCallbackContextTestAccess;

std::array<const ZoneRuntimeCallbackContext *, 33> g_contexts{};

constexpr ZoneLoadContextKey Key(
    const std::uint32_t slot,
    const std::uint64_t generation,
    const std::uint32_t reserved = 0) noexcept
{
    return ZoneLoadContextKey{generation, slot, reserved};
}

void ExpectLastFullSpan(const ZoneRuntimeCallbackContext *const context)
{
    CHECK(g_lastIsolationStorage == context);
    CHECK(g_lastIsolationSize == sizeof(*context));
}

void ExpectBindFailure(
    const ZoneRuntimeCallbackContextBindResult &result,
    const ZoneRuntimeCallbackContextStatus expectedStatus)
{
    CHECK(result.context == nullptr);
    CHECK(result.status == expectedStatus);
    CHECK(result.reserved[0] == 0);
    CHECK(result.reserved[1] == 0);
    CHECK(result.reserved[2] == 0);
    CHECK(!result);
}

void ExpectResolveFailure(
    const ZoneRuntimeCallbackContextBindResult &result,
    const ZoneRuntimeCallbackContextStatus expectedStatus)
{
    ExpectBindFailure(result, expectedStatus);
}

void ExpectSnapshotFailure(
    const ZoneRuntimeCallbackContextSnapshot &snapshot,
    const ZoneRuntimeCallbackContextStatus expectedStatus)
{
    CHECK(snapshot.key == ZoneLoadContextKey{});
    CHECK(snapshot.physicalSlot == db::zone_load::kInvalidZoneLoadSlot);
    CHECK(snapshot.phase == ZoneRuntimeCallbackContextPhase::Unbound);
    CHECK(snapshot.status == expectedStatus);
    CHECK(snapshot.reserved[0] == 0);
    CHECK(snapshot.reserved[1] == 0);
    CHECK(!snapshot);
}

void TestCompileSurfaceAndValueContracts()
{
    static_assert(sizeof(ZoneRuntimeCallbackContext) == 0x28u);
    static_assert(alignof(ZoneRuntimeCallbackContext) == 0x8u);
    static_assert(std::is_standard_layout_v<ZoneRuntimeCallbackContext>);
    static_assert(!std::is_copy_constructible_v<ZoneRuntimeCallbackContext>);
    static_assert(!std::is_copy_assignable_v<ZoneRuntimeCallbackContext>);
    static_assert(!std::is_move_constructible_v<ZoneRuntimeCallbackContext>);
    static_assert(!std::is_move_assignable_v<ZoneRuntimeCallbackContext>);
    static_assert(std::is_same_v<
        decltype(ZoneRuntimeCallbackContextBindResult::context),
        const ZoneRuntimeCallbackContext *>);
    static_assert(sizeof(ZoneRuntimeCallbackContextBindResult)
        == (sizeof(void *) == 4 ? 0x8u : 0x10u));
    static_assert(sizeof(ZoneRuntimeCallbackContextSnapshot) == 0x18u);
    static_assert(std::is_standard_layout_v<
        ZoneRuntimeCallbackContextBindResult>);
    static_assert(std::is_trivially_copyable_v<
        ZoneRuntimeCallbackContextBindResult>);
    static_assert(std::is_standard_layout_v<
        ZoneRuntimeCallbackContextSnapshot>);
    static_assert(std::is_trivially_copyable_v<
        ZoneRuntimeCallbackContextSnapshot>);
    static_assert(noexcept(Access::TryClassifyStorage(nullptr)));
    static_assert(noexcept(Access::TryBind(1u, Key(1, 1))));
    static_assert(noexcept(Access::TryResolve(1u, Key(1, 1))));
    static_assert(noexcept(Access::TryAdvance(
        nullptr, Key(1, 1), ZoneRuntimeCallbackContextPhase::Terminal)));
    static_assert(noexcept(Access::TryAuthenticate(
        nullptr, Key(1, 1), ZoneRuntimeCallbackContextPhase::Bound)));
    static_assert(noexcept(Access::TryCapture(nullptr, Key(1, 1))));
    static_assert(noexcept(Access::SpanIsSeparated(nullptr, 0, 0)));

    constexpr ZoneRuntimeCallbackContextSnapshot bound{
        Key(1, 1),
        1u,
        ZoneRuntimeCallbackContextPhase::Bound,
        ZoneRuntimeCallbackContextStatus::Success,
        {0, 0}};
    static_assert(static_cast<bool>(bound));
    constexpr ZoneRuntimeCallbackContextSnapshot terminal{
        Key(1, 1),
        1u,
        ZoneRuntimeCallbackContextPhase::Terminal,
        ZoneRuntimeCallbackContextStatus::Success,
        {0, 0}};
    static_assert(static_cast<bool>(terminal));
    constexpr ZoneRuntimeCallbackContextSnapshot unbound{
        Key(1, 1),
        1u,
        ZoneRuntimeCallbackContextPhase::Unbound,
        ZoneRuntimeCallbackContextStatus::Success,
        {0, 0}};
    static_assert(!static_cast<bool>(unbound));
    constexpr ZoneRuntimeCallbackContextSnapshot reserved{
        Key(1, 1),
        1u,
        ZoneRuntimeCallbackContextPhase::Reserved,
        ZoneRuntimeCallbackContextStatus::Success,
        {0, 0}};
    static_assert(!static_cast<bool>(reserved));
}

void TestWholeBankSpanSeparation()
{
    g_isolationStatus =
        static_cast<pmem_runtime::StorageIsolationStatus>(0xFFu);
    const std::uint32_t isolationCalls = g_isolationCalls;
    const std::uintptr_t bankBegin =
        reinterpret_cast<std::uintptr_t>(g_contexts.front());
    const std::uintptr_t bankEnd =
        reinterpret_cast<std::uintptr_t>(g_contexts.back())
        + sizeof(ZoneRuntimeCallbackContext);

    CHECK(!Access::SpanIsSeparated(nullptr, 1u, 1u));
    CHECK(!Access::SpanIsSeparated(g_contexts.front(), 0u, 1u));
    CHECK(!Access::SpanIsSeparated(g_contexts.front(), 1u, 0u));
    CHECK(!Access::SpanIsSeparated(
        g_contexts.front(), sizeof(ZoneRuntimeCallbackContext), 1u));
    CHECK(!Access::SpanIsSeparated(
        g_contexts.back(), sizeof(ZoneRuntimeCallbackContext), 1u));
    CHECK(!Access::SpanIsSeparated(
        g_contexts[10], sizeof(ZoneRuntimeCallbackContext), 1u));
    CHECK(!Access::SpanIsSeparated(
        reinterpret_cast<const void *>(
            reinterpret_cast<std::uintptr_t>(g_contexts[10]) + 1u),
        1u,
        1u));
    CHECK(!Access::SpanIsSeparated(
        g_contexts.front(),
        sizeof(ZoneRuntimeCallbackContext) * g_contexts.size(),
        alignof(ZoneRuntimeCallbackContext)));
    CHECK(!Access::SpanIsSeparated(
        reinterpret_cast<const void *>(bankBegin - 1u), 2u, 1u));
    CHECK(!Access::SpanIsSeparated(
        reinterpret_cast<const void *>(bankEnd - 1u), 2u, 1u));

    CHECK(Access::SpanIsSeparated(
        reinterpret_cast<const void *>(bankBegin - 8u), 8u, 1u));
    CHECK(Access::SpanIsSeparated(
        reinterpret_cast<const void *>(bankEnd), 1u, 1u));
    CHECK(!Access::SpanIsSeparated(
        reinterpret_cast<const void *>(bankEnd + 1u), 1u, 2u));

    const std::uintptr_t alignedToThree =
        bankEnd + ((3u - bankEnd % 3u) % 3u);
    CHECK(Access::SpanIsSeparated(
        reinterpret_cast<const void *>(alignedToThree), 1u, 3u));

    const std::uintptr_t maximum =
        (std::numeric_limits<std::uintptr_t>::max)();
    CHECK(!Access::SpanIsSeparated(
        reinterpret_cast<const void *>(maximum - 3u), 8u, 1u));

    alignas(16) std::uint8_t outside[16]{};
    CHECK(Access::SpanIsSeparated(outside, sizeof(outside), 16u));
    CHECK(g_isolationCalls == isolationCalls);
}

void TestAllPhysicalSlotsAreStableAdjacentAndCanonical()
{
    g_isolationStatus = pmem_runtime::StorageIsolationStatus::Uninitialized;
    for (std::uint32_t slot = 0; slot < g_contexts.size(); ++slot)
    {
        g_contexts[slot] = Access::ContextForPhysicalSlot(slot);
        CHECK(g_contexts[slot]);
        CHECK(Access::TryClassifyStorage(g_contexts[slot])
            == pmem_runtime::StorageIsolationStatus::Uninitialized);
        ExpectLastFullSpan(g_contexts[slot]);
    }
    CHECK(Access::ContextForPhysicalSlot(33u) == nullptr);

    for (std::size_t slot = 1; slot < g_contexts.size(); ++slot)
    {
        const std::uintptr_t previous =
            reinterpret_cast<std::uintptr_t>(g_contexts[slot - 1]);
        const std::uintptr_t current =
            reinterpret_cast<std::uintptr_t>(g_contexts[slot]);
        CHECK(current - previous == sizeof(ZoneRuntimeCallbackContext));
    }

    ExpectSnapshotFailure(
        Access::TryCapture(g_contexts[0], Key(0, 1)),
        ZoneRuntimeCallbackContextStatus::InvalidPhase);
    CHECK(Access::TryAuthenticate(
        g_contexts[0], Key(0, 1), ZoneRuntimeCallbackContextPhase::Bound)
        == ZoneRuntimeCallbackContextStatus::InvalidPhase);
    ExpectBindFailure(
        Access::TryBind(0u, Key(0, 1)),
        ZoneRuntimeCallbackContextStatus::InvalidSlot);
}

void TestInvalidPointersKeysPhasesAndUnboundPaths()
{
    g_isolationStatus = pmem_runtime::StorageIsolationStatus::Success;
    ZoneRuntimeCallbackContext fabricated{};
    const auto *const interior =
        reinterpret_cast<const ZoneRuntimeCallbackContext *>(
            reinterpret_cast<std::uintptr_t>(g_contexts[10]) + 1u);
    const auto *const onePast = g_contexts[32] + 1;
    const std::uint32_t calls = g_isolationCalls;

    CHECK(Access::TryClassifyStorage(nullptr)
        == pmem_runtime::StorageIsolationStatus::InvalidArgument);
    CHECK(Access::TryClassifyStorage(&fabricated)
        == pmem_runtime::StorageIsolationStatus::InvalidArgument);
    CHECK(Access::TryClassifyStorage(interior)
        == pmem_runtime::StorageIsolationStatus::InvalidArgument);
    CHECK(Access::TryClassifyStorage(onePast)
        == pmem_runtime::StorageIsolationStatus::InvalidArgument);
    CHECK(g_isolationCalls == calls);

    CHECK(Access::TryAuthenticate(
        &fabricated, Key(1, 1), ZoneRuntimeCallbackContextPhase::Bound)
        == ZoneRuntimeCallbackContextStatus::InvalidArgument);
    CHECK(Access::TryAdvance(
        interior, Key(10, 1), ZoneRuntimeCallbackContextPhase::Terminal)
        == ZoneRuntimeCallbackContextStatus::InvalidArgument);
    ExpectSnapshotFailure(
        Access::TryCapture(onePast, Key(32, 1)),
        ZoneRuntimeCallbackContextStatus::InvalidArgument);

    ExpectBindFailure(
        Access::TryBind(33u, Key(33, 1)),
        ZoneRuntimeCallbackContextStatus::InvalidSlot);
    ExpectBindFailure(
        Access::TryBind(1u, {}),
        ZoneRuntimeCallbackContextStatus::InvalidKey);
    ExpectBindFailure(
        Access::TryBind(1u, Key(1, 1, 1)),
        ZoneRuntimeCallbackContextStatus::InvalidKey);
    ExpectBindFailure(
        Access::TryBind(1u, Key(2, 1)),
        ZoneRuntimeCallbackContextStatus::StaleKey);

    const ZoneRuntimeCallbackContext *const unbound = g_contexts[5];
    CHECK(Access::TryAuthenticate(
        unbound, {}, ZoneRuntimeCallbackContextPhase::Bound)
        == ZoneRuntimeCallbackContextStatus::InvalidKey);
    CHECK(Access::TryAuthenticate(
        unbound, Key(5, 1, 1), ZoneRuntimeCallbackContextPhase::Bound)
        == ZoneRuntimeCallbackContextStatus::InvalidKey);
    CHECK(Access::TryAuthenticate(
        unbound, Key(6, 1), ZoneRuntimeCallbackContextPhase::Bound)
        == ZoneRuntimeCallbackContextStatus::StaleKey);
    CHECK(Access::TryAdvance(
        unbound, Key(6, 1), ZoneRuntimeCallbackContextPhase::Terminal)
        == ZoneRuntimeCallbackContextStatus::StaleKey);
    ExpectSnapshotFailure(
        Access::TryCapture(unbound, Key(6, 1)),
        ZoneRuntimeCallbackContextStatus::StaleKey);
    CHECK(Access::TryAuthenticate(
        unbound, Key(5, 1), ZoneRuntimeCallbackContextPhase::Unbound)
        == ZoneRuntimeCallbackContextStatus::InvalidPhase);
    CHECK(Access::TryAuthenticate(
        unbound,
        Key(5, 1),
        static_cast<ZoneRuntimeCallbackContextPhase>(0xFFu))
        == ZoneRuntimeCallbackContextStatus::InvalidPhase);
    CHECK(Access::TryAdvance(
        unbound, Key(5, 1), ZoneRuntimeCallbackContextPhase::Terminal)
        == ZoneRuntimeCallbackContextStatus::InvalidPhase);
    CHECK(Access::TryAdvance(
        unbound, Key(5, 1), ZoneRuntimeCallbackContextPhase::Reserved)
        == ZoneRuntimeCallbackContextStatus::InvalidPhase);
    CHECK(Access::TryAdvance(
        unbound,
        Key(5, 1),
        static_cast<ZoneRuntimeCallbackContextPhase>(0xFFu))
        == ZoneRuntimeCallbackContextStatus::InvalidPhase);
    ExpectSnapshotFailure(
        Access::TryCapture(unbound, Key(5, 1)),
        ZoneRuntimeCallbackContextStatus::InvalidPhase);
    ExpectSnapshotFailure(
        Access::TryCapture(unbound, {}),
        ZoneRuntimeCallbackContextStatus::InvalidKey);
    ExpectSnapshotFailure(
        Access::TryCapture(unbound, Key(5, 1, 1)),
        ZoneRuntimeCallbackContextStatus::InvalidKey);
}

void TestEveryStorageStatusAndExactMemberMapping()
{
    const ZoneRuntimeCallbackContext *const context = g_contexts[2];
    const ZoneLoadContextKey key = Key(2, 1);

    g_isolationStatus = pmem_runtime::StorageIsolationStatus::Busy;
    CHECK(Access::TryClassifyStorage(context) == g_isolationStatus);
    ExpectBindFailure(
        Access::TryBind(2u, key), ZoneRuntimeCallbackContextStatus::Busy);

    for (const pmem_runtime::StorageIsolationStatus status : {
             pmem_runtime::StorageIsolationStatus::InvalidArgument,
             pmem_runtime::StorageIsolationStatus::ProtectedStorageOverlap,
             pmem_runtime::StorageIsolationStatus::Poisoned,
             pmem_runtime::StorageIsolationStatus::CorruptState,
             static_cast<pmem_runtime::StorageIsolationStatus>(0xFFu)})
    {
        g_isolationStatus = status;
        CHECK(Access::TryClassifyStorage(context) == status);
        ExpectLastFullSpan(context);
        ExpectBindFailure(
            Access::TryBind(2u, key),
            ZoneRuntimeCallbackContextStatus::UnsafeFailure);
    }

    g_isolationStatus = pmem_runtime::StorageIsolationStatus::Uninitialized;
    const ZoneRuntimeCallbackContextBindResult bound =
        Access::TryBind(2u, key);
    CHECK(bound);
    CHECK(bound.context == context);
    ExpectLastFullSpan(context);

    g_isolationStatus = pmem_runtime::StorageIsolationStatus::Success;
    CHECK(Access::TryClassifyStorage(context) == g_isolationStatus);
    CHECK(Access::TryAuthenticate(
        context, key, ZoneRuntimeCallbackContextPhase::Bound)
        == ZoneRuntimeCallbackContextStatus::Success);
}

void TestResolveRetainedContextWithoutFailureDisclosure()
{
    g_isolationStatus = pmem_runtime::StorageIsolationStatus::Success;
    const ZoneRuntimeCallbackContext *const context = g_contexts[6];
    const ZoneLoadContextKey key = Key(6, 41);

    ExpectResolveFailure(
        ZoneRuntimeCallbackContextBindResult{},
        ZoneRuntimeCallbackContextStatus::InvalidArgument);

    const std::uint32_t callsBeforeRejectedInputs = g_isolationCalls;
    ExpectResolveFailure(
        Access::TryResolve(0u, Key(0, 1)),
        ZoneRuntimeCallbackContextStatus::InvalidSlot);
    ExpectResolveFailure(
        Access::TryResolve(33u, Key(33, 1)),
        ZoneRuntimeCallbackContextStatus::InvalidSlot);
    ExpectResolveFailure(
        Access::TryResolve(6u, {}),
        ZoneRuntimeCallbackContextStatus::InvalidKey);
    ExpectResolveFailure(
        Access::TryResolve(6u, Key(6, 41, 1)),
        ZoneRuntimeCallbackContextStatus::InvalidKey);
    ExpectResolveFailure(
        Access::TryResolve(6u, Key(7, 41)),
        ZoneRuntimeCallbackContextStatus::StaleKey);
    CHECK(g_isolationCalls == callsBeforeRejectedInputs);

    ExpectResolveFailure(
        Access::TryResolve(6u, key),
        ZoneRuntimeCallbackContextStatus::InvalidPhase);
    ExpectLastFullSpan(context);

    const ZoneRuntimeCallbackContextBindResult bound =
        Access::TryBind(6u, key);
    CHECK(bound);
    CHECK(bound.context == context);

    g_isolationStatus = pmem_runtime::StorageIsolationStatus::Uninitialized;
    const ZoneRuntimeCallbackContextBindResult resolvedBound =
        Access::TryResolve(6u, key);
    CHECK(resolvedBound);
    CHECK(resolvedBound.context == context);
    const ZoneRuntimeCallbackContextSnapshot boundState =
        Access::TryCapture(context, key);
    CHECK(boundState);
    CHECK(boundState.phase == ZoneRuntimeCallbackContextPhase::Bound);

    g_isolationStatus = pmem_runtime::StorageIsolationStatus::Success;
    CHECK(Access::TryAdvance(
        context, key, ZoneRuntimeCallbackContextPhase::Terminal)
        == ZoneRuntimeCallbackContextStatus::Success);
    const ZoneRuntimeCallbackContextBindResult resolvedTerminal =
        Access::TryResolve(6u, key);
    CHECK(resolvedTerminal);
    CHECK(resolvedTerminal.context == context);
    const ZoneRuntimeCallbackContextSnapshot terminalState =
        Access::TryCapture(context, key);
    CHECK(terminalState);
    CHECK(terminalState.phase == ZoneRuntimeCallbackContextPhase::Terminal);

    ExpectResolveFailure(
        Access::TryResolve(6u, Key(6, 40)),
        ZoneRuntimeCallbackContextStatus::StaleKey);

    g_isolationStatus = pmem_runtime::StorageIsolationStatus::Busy;
    ExpectResolveFailure(
        Access::TryResolve(6u, key),
        ZoneRuntimeCallbackContextStatus::Busy);

    for (const pmem_runtime::StorageIsolationStatus status : {
             pmem_runtime::StorageIsolationStatus::InvalidArgument,
             pmem_runtime::StorageIsolationStatus::ProtectedStorageOverlap,
             pmem_runtime::StorageIsolationStatus::Poisoned,
             pmem_runtime::StorageIsolationStatus::CorruptState,
             static_cast<pmem_runtime::StorageIsolationStatus>(0xFFu)})
    {
        g_isolationStatus = status;
        ExpectResolveFailure(
            Access::TryResolve(6u, key),
            ZoneRuntimeCallbackContextStatus::UnsafeFailure);
        ExpectLastFullSpan(context);
    }

    g_isolationStatus = pmem_runtime::StorageIsolationStatus::Success;
    const ZoneRuntimeCallbackContextSnapshot afterRejectedResolves =
        Access::TryCapture(context, key);
    CHECK(afterRejectedResolves);
    CHECK(afterRejectedResolves.key == key);
    CHECK(afterRejectedResolves.phase
        == ZoneRuntimeCallbackContextPhase::Terminal);

    Access::CorruptWitness(context);
    ExpectResolveFailure(
        Access::TryResolve(6u, key),
        ZoneRuntimeCallbackContextStatus::UnsafeFailure);
    Access::Restore(
        context, 6u, key, ZoneRuntimeCallbackContextPhase::Terminal);

    const ZoneLoadContextKey successor = Key(6, 42);
    CHECK(Access::TryBind(6u, successor));
    ExpectResolveFailure(
        Access::TryResolve(6u, key),
        ZoneRuntimeCallbackContextStatus::StaleKey);
    const ZoneRuntimeCallbackContextBindResult resolvedSuccessor =
        Access::TryResolve(6u, successor);
    CHECK(resolvedSuccessor);
    CHECK(resolvedSuccessor.context == context);

    Access::Restore(
        context, 6u, {}, ZoneRuntimeCallbackContextPhase::Unbound);
}

void TestKeyedLifecycleSuccessorAndNoStaleDisclosure()
{
    g_isolationStatus = pmem_runtime::StorageIsolationStatus::Success;
    const ZoneLoadContextKey firstKey = Key(1, 7);
    const auto first = Access::TryBind(1u, firstKey);
    CHECK(first);
    CHECK(first.context == g_contexts[1]);
    CHECK(Access::TryBind(1u, firstKey).context == first.context);

    const auto bound = Access::TryCapture(first.context, firstKey);
    CHECK(bound);
    CHECK(bound.key == firstKey);
    CHECK(bound.physicalSlot == 1u);
    CHECK(bound.phase == ZoneRuntimeCallbackContextPhase::Bound);
    CHECK(Access::TryAuthenticate(
        first.context, firstKey, ZoneRuntimeCallbackContextPhase::Bound)
        == ZoneRuntimeCallbackContextStatus::Success);
    CHECK(Access::TryAuthenticate(
        first.context, Key(2, 7), ZoneRuntimeCallbackContextPhase::Bound)
        == ZoneRuntimeCallbackContextStatus::StaleKey);
    CHECK(Access::TryAuthenticate(
        first.context, Key(1, 8), ZoneRuntimeCallbackContextPhase::Bound)
        == ZoneRuntimeCallbackContextStatus::StaleKey);
    ExpectSnapshotFailure(
        Access::TryCapture(first.context, Key(2, 7)),
        ZoneRuntimeCallbackContextStatus::StaleKey);

    CHECK(Access::TryAdvance(
        first.context, firstKey, ZoneRuntimeCallbackContextPhase::Bound)
        == ZoneRuntimeCallbackContextStatus::Success);
    CHECK(Access::TryAdvance(
        first.context, Key(2, 7), ZoneRuntimeCallbackContextPhase::Terminal)
        == ZoneRuntimeCallbackContextStatus::StaleKey);
    CHECK(Access::TryAdvance(
        first.context, firstKey, ZoneRuntimeCallbackContextPhase::Terminal)
        == ZoneRuntimeCallbackContextStatus::Success);
    CHECK(Access::TryAdvance(
        first.context, firstKey, ZoneRuntimeCallbackContextPhase::Terminal)
        == ZoneRuntimeCallbackContextStatus::Success);

    const auto terminal = Access::TryCapture(first.context, firstKey);
    CHECK(terminal);
    CHECK(terminal.phase == ZoneRuntimeCallbackContextPhase::Terminal);
    ExpectBindFailure(
        Access::TryBind(1u, firstKey),
        ZoneRuntimeCallbackContextStatus::InvalidPhase);
    ExpectBindFailure(
        Access::TryBind(1u, Key(1, 6)),
        ZoneRuntimeCallbackContextStatus::StaleKey);
    ExpectBindFailure(
        Access::TryBind(1u, Key(1, 9)),
        ZoneRuntimeCallbackContextStatus::StaleKey);

    const ZoneLoadContextKey successor = Key(1, 8);
    const auto second = Access::TryBind(1u, successor);
    CHECK(second);
    CHECK(second.context == first.context);
    ExpectSnapshotFailure(
        Access::TryCapture(second.context, firstKey),
        ZoneRuntimeCallbackContextStatus::StaleKey);
    const auto current = Access::TryCapture(second.context, successor);
    CHECK(current);
    CHECK(current.key == successor);

    const ZoneLoadContextKey maximumKey =
        Key(1, (std::numeric_limits<std::uint64_t>::max)());
    Access::Restore(
        second.context,
        1u,
        maximumKey,
        ZoneRuntimeCallbackContextPhase::Terminal);
    ExpectBindFailure(
        Access::TryBind(1u, maximumKey),
        ZoneRuntimeCallbackContextStatus::GenerationExhausted);
    ExpectBindFailure(
        Access::TryBind(1u, Key(1, 9)),
        ZoneRuntimeCallbackContextStatus::GenerationExhausted);
    CHECK(Access::TryAuthenticate(
        second.context, maximumKey, ZoneRuntimeCallbackContextPhase::Terminal)
        == ZoneRuntimeCallbackContextStatus::Success);
}

void TestPrivateRepresentationAndStoreIndexFailClosed()
{
    g_isolationStatus = pmem_runtime::StorageIsolationStatus::Success;
    const ZoneLoadContextKey key = Key(3, 19);
    const auto bound = Access::TryBind(3u, key);
    CHECK(bound);

    Access::CorruptSelf(bound.context);
    CHECK(Access::TryClassifyStorage(bound.context)
        == pmem_runtime::StorageIsolationStatus::Success);
    ExpectBindFailure(
        Access::TryBind(3u, key),
        ZoneRuntimeCallbackContextStatus::UnsafeFailure);
    CHECK(Access::TryAuthenticate(
        bound.context, key, ZoneRuntimeCallbackContextPhase::Bound)
        == ZoneRuntimeCallbackContextStatus::UnsafeFailure);
    ExpectSnapshotFailure(
        Access::TryCapture(bound.context, key),
        ZoneRuntimeCallbackContextStatus::UnsafeFailure);
    Access::Restore(
        bound.context, 3u, key, ZoneRuntimeCallbackContextPhase::Bound);

    Access::CorruptWitness(bound.context);
    CHECK(Access::TryAdvance(
        bound.context, key, ZoneRuntimeCallbackContextPhase::Terminal)
        == ZoneRuntimeCallbackContextStatus::UnsafeFailure);
    Access::Restore(
        bound.context, 3u, key, ZoneRuntimeCallbackContextPhase::Bound);

    Access::CorruptReserved(bound.context);
    CHECK(Access::TryAuthenticate(
        bound.context, key, ZoneRuntimeCallbackContextPhase::Bound)
        == ZoneRuntimeCallbackContextStatus::UnsafeFailure);
    Access::Restore(
        bound.context, 3u, key, ZoneRuntimeCallbackContextPhase::Bound);

#if !KISAK_ARCH_64BIT
    Access::CorruptPointerAlignmentPadding(bound.context);
    ExpectSnapshotFailure(
        Access::TryCapture(bound.context, key),
        ZoneRuntimeCallbackContextStatus::UnsafeFailure);
    Access::Restore(
        bound.context, 3u, key, ZoneRuntimeCallbackContextPhase::Bound);
#endif

    const ZoneRuntimeCallbackContext *const wrongIndex = g_contexts[4];
    const ZoneLoadContextKey wrongIndexKey = Key(5, 23);
    Access::Restore(
        wrongIndex,
        5u,
        wrongIndexKey,
        ZoneRuntimeCallbackContextPhase::Bound);
    CHECK(Access::TryClassifyStorage(wrongIndex)
        == pmem_runtime::StorageIsolationStatus::Success);
    CHECK(Access::TryAuthenticate(
        wrongIndex,
        wrongIndexKey,
        ZoneRuntimeCallbackContextPhase::Bound)
        == ZoneRuntimeCallbackContextStatus::UnsafeFailure);
    ExpectSnapshotFailure(
        Access::TryCapture(wrongIndex, wrongIndexKey),
        ZoneRuntimeCallbackContextStatus::UnsafeFailure);
    Access::Restore(wrongIndex, 4u, {}, ZoneRuntimeCallbackContextPhase::Unbound);

    const ZoneRuntimeCallbackContext *const reserved = g_contexts[0];
    Access::Restore(
        reserved,
        1u,
        Key(1, 1),
        ZoneRuntimeCallbackContextPhase::Bound);
    ExpectSnapshotFailure(
        Access::TryCapture(reserved, Key(1, 1)),
        ZoneRuntimeCallbackContextStatus::UnsafeFailure);
    Access::Restore(
        reserved, 0u, {}, ZoneRuntimeCallbackContextPhase::Reserved);
    ExpectSnapshotFailure(
        Access::TryCapture(reserved, Key(0, 1)),
        ZoneRuntimeCallbackContextStatus::InvalidPhase);
}
} // namespace

namespace pmem_runtime
{
StorageIsolationStatus KISAK_CDECL TryClassifyStorageIsolation(
    const void *const storage,
    const std::size_t size) noexcept
{
    g_lastIsolationStorage = storage;
    g_lastIsolationSize = size;
    ++g_isolationCalls;
    return g_isolationStatus;
}
} // namespace pmem_runtime

int main()
{
    TestCompileSurfaceAndValueContracts();
    TestAllPhysicalSlotsAreStableAdjacentAndCanonical();
    TestWholeBankSpanSeparation();
    TestInvalidPointersKeysPhasesAndUnboundPaths();
    TestEveryStorageStatusAndExactMemberMapping();
    TestResolveRetainedContextWithoutFailureDisclosure();
    TestKeyedLifecycleSuccessorAndNoStaleDisclosure();
    TestPrivateRepresentationAndStoreIndexFailClosed();
    return 0;
}
