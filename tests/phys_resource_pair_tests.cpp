#include <physics/phys_resource_pair.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <type_traits>
#include <utility>

namespace allocation = physics::allocation;

static_assert(std::is_standard_layout_v<allocation::ResourcePairCallbacks>);
static_assert(std::is_trivially_copyable_v<
              allocation::ResourcePairCallbacks>);
static_assert(std::is_standard_layout_v<allocation::ResourcePairResult>);
static_assert(std::is_trivially_copyable_v<
              allocation::ResourcePairResult>);
static_assert(noexcept(allocation::TryCreateResourcePair(
    std::declval<const allocation::ResourcePairCallbacks &>(), true)));
static_assert(std::is_same_v<
              decltype(static_cast<bool>(
                  std::declval<allocation::ResourcePairResult>())),
              bool>);

namespace
{
enum class Event : std::uint8_t
{
    CreatePrimary,
    CreateCompanion,
    DestroyPrimary,
};

struct FakePool
{
    std::size_t freeCount = 0;
    std::size_t activeCount = 0;
};

struct FakeResource
{
    std::uint32_t tag = 0;
};

struct FakeContext
{
    FakePool primaryPool{};
    FakePool companionPool{};
    FakeResource primaryResource{0x5052494Du};
    FakeResource companionResource{0x434F4D50u};
    std::array<Event, 16> events{};
    std::size_t eventCount = 0;
    std::size_t primaryLinkCount = 0;
    std::size_t companionLinkCount = 0;
    std::size_t primaryDestroyCount = 0;
    bool companionSharesPrimaryPool = false;
    bool refusePrimaryDestroy = false;
    bool callbackContractViolated = false;
    bool eventOverflow = false;

    FakePool &PoolForCompanion() noexcept
    {
        return companionSharesPrimaryPool
            ? primaryPool
            : companionPool;
    }

    void Record(const Event event) noexcept
    {
        if (eventCount == events.size())
        {
            eventOverflow = true;
            return;
        }
        events[eventCount++] = event;
    }

    void ClearEvents() noexcept
    {
        eventCount = 0;
        eventOverflow = false;
    }
};

int failures = 0;

void Expect(const bool condition, const char *const message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void ExpectStatus(
    const allocation::ResourcePairResult &result,
    const allocation::ResourcePairStatus expected,
    const char *const message)
{
    if (result.status != expected)
    {
        std::fprintf(
            stderr,
            "FAIL: %s (got %u, expected %u)\n",
            message,
            static_cast<unsigned>(result.status),
            static_cast<unsigned>(expected));
        ++failures;
    }
}

void ExpectEvents(
    const FakeContext &context,
    const std::initializer_list<Event> expected,
    const char *const message)
{
    bool matches = !context.eventOverflow
        && context.eventCount == expected.size();
    std::size_t index = 0;
    for (const Event event : expected)
    {
        if (index >= context.eventCount
            || context.events[index] != event)
        {
            matches = false;
        }
        ++index;
    }
    Expect(matches, message);
}

allocation::ResourceHandle CreatePrimary(void *const opaque) noexcept
{
    auto *const context = static_cast<FakeContext *>(opaque);
    context->Record(Event::CreatePrimary);
    if (context->primaryPool.freeCount == 0)
        return nullptr;

    --context->primaryPool.freeCount;
    ++context->primaryPool.activeCount;
    ++context->primaryLinkCount;
    return &context->primaryResource;
}

allocation::ResourceHandle CreateCompanion(
    void *const opaque,
    allocation::ResourceHandle const primary) noexcept
{
    auto *const context = static_cast<FakeContext *>(opaque);
    context->Record(Event::CreateCompanion);
    if (primary != &context->primaryResource)
        context->callbackContractViolated = true;

    FakePool &pool = context->PoolForCompanion();
    if (pool.freeCount == 0)
        return nullptr;

    --pool.freeCount;
    ++pool.activeCount;
    ++context->companionLinkCount;
    return &context->companionResource;
}

bool DestroyPrimary(
    void *const opaque,
    allocation::ResourceHandle const primary) noexcept
{
    auto *const context = static_cast<FakeContext *>(opaque);
    context->Record(Event::DestroyPrimary);
    ++context->primaryDestroyCount;
    if (primary != &context->primaryResource
        || context->primaryPool.activeCount == 0
        || context->primaryLinkCount == 0)
    {
        context->callbackContractViolated = true;
        return false;
    }

    if (context->refusePrimaryDestroy)
        return false;

    --context->primaryPool.activeCount;
    ++context->primaryPool.freeCount;
    --context->primaryLinkCount;
    return true;
}

allocation::ResourcePairCallbacks CallbacksFor(
    FakeContext *const context) noexcept
{
    allocation::ResourcePairCallbacks callbacks{};
    callbacks.context = context;
    callbacks.createPrimary = CreatePrimary;
    callbacks.createCompanion = CreateCompanion;
    callbacks.destroyPrimary = DestroyPrimary;
    return callbacks;
}

void ReleaseSuccessfulPrimary(
    FakeContext *const context,
    allocation::ResourceHandle const primary) noexcept
{
    if (!context || primary != &context->primaryResource
        || context->primaryPool.activeCount == 0
        || context->primaryLinkCount == 0)
    {
        if (context)
            context->callbackContractViolated = true;
        return;
    }
    --context->primaryPool.activeCount;
    ++context->primaryPool.freeCount;
    --context->primaryLinkCount;
}

void ReleaseSuccessfulCompanion(
    FakeContext *const context,
    allocation::ResourceHandle const companion) noexcept
{
    if (!context || companion != &context->companionResource
        || context->companionLinkCount == 0)
    {
        if (context)
            context->callbackContractViolated = true;
        return;
    }
    FakePool &pool = context->PoolForCompanion();
    if (pool.activeCount == 0)
    {
        context->callbackContractViolated = true;
        return;
    }
    --pool.activeCount;
    ++pool.freeCount;
    --context->companionLinkCount;
}

void TestInvalidCallbacksAreRejectedBeforeAllocation()
{
    allocation::ResourcePairCallbacks callbacks{};
    allocation::ResourcePairResult result =
        allocation::TryCreateResourcePair(callbacks, true);
    ExpectStatus(
        result,
        allocation::ResourcePairStatus::InvalidCallbacks,
        "empty callback set is rejected");
    Expect(!result && !result.primary && !result.companion,
           "invalid callback result owns no resources");

    FakeContext context{};
    context.primaryPool.freeCount = 1;
    callbacks.context = &context;
    callbacks.createPrimary = CreatePrimary;
    result = allocation::TryCreateResourcePair(callbacks, true);
    ExpectStatus(
        result,
        allocation::ResourcePairStatus::InvalidCallbacks,
        "missing required companion callbacks are rejected");
    ExpectEvents(context, {},
                 "callback validation happens before primary creation");

    callbacks.createCompanion = CreateCompanion;
    result = allocation::TryCreateResourcePair(callbacks, true);
    ExpectStatus(
        result,
        allocation::ResourcePairStatus::InvalidCallbacks,
        "missing primary destroy callback is rejected");
    ExpectEvents(context, {},
                 "missing rollback callback causes no allocation");
    Expect(context.primaryPool.freeCount == 1
               && context.primaryPool.activeCount == 0,
           "invalid callbacks leave counters unchanged");
}

void TestBodyAllocationFailureIsStable()
{
    FakeContext context{};
    context.companionPool.freeCount = 1;
    const allocation::ResourcePairCallbacks callbacks =
        CallbacksFor(&context);

    for (std::size_t attempt = 0; attempt < 2; ++attempt)
    {
        context.ClearEvents();
        const allocation::ResourcePairResult result =
            allocation::TryCreateResourcePair(callbacks, true);
        ExpectStatus(
            result,
            allocation::ResourcePairStatus::PrimaryUnavailable,
            "body-pool exhaustion reports primary failure");
        Expect(!result && !result.primary && !result.companion,
               "body allocation failure returns no ownership");
        ExpectEvents(
            context,
            {Event::CreatePrimary},
            "body failure does not allocate user data or destroy a null body");
        Expect(context.primaryPool.freeCount == 0
                   && context.primaryPool.activeCount == 0
                   && context.companionPool.freeCount == 1
                   && context.companionPool.activeCount == 0
                   && context.primaryLinkCount == 0
                   && context.companionLinkCount == 0
                   && context.primaryDestroyCount == 0,
               "body failure preserves all counters");
    }
}

void TestUserDataFailureRollsBackBody()
{
    FakeContext context{};
    context.primaryPool.freeCount = 1;
    const allocation::ResourcePairCallbacks callbacks =
        CallbacksFor(&context);

    for (std::size_t attempt = 0; attempt < 2; ++attempt)
    {
        context.ClearEvents();
        const allocation::ResourcePairResult result =
            allocation::TryCreateResourcePair(callbacks, true);
        ExpectStatus(
            result,
            allocation::ResourcePairStatus::CompanionUnavailable,
            "user-data exhaustion reports companion failure");
        Expect(!result && !result.primary && !result.companion,
               "user-data failure returns no body ownership");
        ExpectEvents(
            context,
            {Event::CreatePrimary,
             Event::CreateCompanion,
             Event::DestroyPrimary},
            "user-data failure destroys the body after allocation fails");
        Expect(context.primaryPool.freeCount == 1
                   && context.primaryPool.activeCount == 0
                   && context.companionPool.freeCount == 0
                   && context.companionPool.activeCount == 0
                   && context.primaryLinkCount == 0
                   && context.companionLinkCount == 0
                   && context.primaryDestroyCount == attempt + 1,
               "user-data failure restores body and world counters exactly");
        Expect(!context.callbackContractViolated,
               "user-data rollback observes the callback contract");
    }
}

void TestPrimaryGeomFailureIsStable()
{
    FakeContext context{};
    context.companionSharesPrimaryPool = true;
    const allocation::ResourcePairCallbacks callbacks =
        CallbacksFor(&context);

    for (std::size_t attempt = 0; attempt < 2; ++attempt)
    {
        context.ClearEvents();
        const allocation::ResourcePairResult result =
            allocation::TryCreateResourcePair(callbacks, true);
        ExpectStatus(
            result,
            allocation::ResourcePairStatus::PrimaryUnavailable,
            "geom-pool exhaustion reports primary failure");
        ExpectEvents(
            context,
            {Event::CreatePrimary},
            "primary geom failure never attempts a transform");
        Expect(context.primaryPool.freeCount == 0
                   && context.primaryPool.activeCount == 0
                   && context.primaryLinkCount == 0
                   && context.companionLinkCount == 0
                   && context.primaryDestroyCount == 0,
               "primary geom failure preserves geom counters");
    }
}

void TestTransformFailureRollsBackPrimaryGeom()
{
    FakeContext context{};
    context.primaryPool.freeCount = 1;
    context.companionSharesPrimaryPool = true;
    const allocation::ResourcePairCallbacks callbacks =
        CallbacksFor(&context);

    for (std::size_t attempt = 0; attempt < 2; ++attempt)
    {
        context.ClearEvents();
        const allocation::ResourcePairResult result =
            allocation::TryCreateResourcePair(callbacks, true);
        ExpectStatus(
            result,
            allocation::ResourcePairStatus::CompanionUnavailable,
            "transform exhaustion reports companion failure");
        Expect(!result && !result.primary && !result.companion,
               "transform failure returns no geom ownership");
        ExpectEvents(
            context,
            {Event::CreatePrimary,
             Event::CreateCompanion,
             Event::DestroyPrimary},
            "transform failure unlinks and destroys the primary geom");
        Expect(context.primaryPool.freeCount == 1
                   && context.primaryPool.activeCount == 0
                   && context.primaryLinkCount == 0
                   && context.companionLinkCount == 0
                   && context.primaryDestroyCount == attempt + 1,
               "transform failure restores the shared geom pool exactly");
        Expect(!context.callbackContractViolated,
               "transform rollback observes the callback contract");
    }
}

void TestCompanionFailureRetainsPrimaryWhenRollbackIsRefused()
{
    FakeContext context{};
    context.primaryPool.freeCount = 1;
    context.refusePrimaryDestroy = true;
    const allocation::ResourcePairCallbacks callbacks =
        CallbacksFor(&context);

    const allocation::ResourcePairResult result =
        allocation::TryCreateResourcePair(callbacks, true);
    ExpectStatus(
        result,
        allocation::ResourcePairStatus::PrimaryCleanupFailed,
        "refused companion-failure rollback reports cleanup failure");
    Expect(!result
               && result.primary == &context.primaryResource
               && !result.companion,
           "cleanup failure preserves primary ownership for the caller");
    ExpectEvents(
        context,
        {Event::CreatePrimary,
         Event::CreateCompanion,
         Event::DestroyPrimary},
        "cleanup refusal follows allocation and rollback order");
    Expect(context.primaryPool.freeCount == 0
               && context.primaryPool.activeCount == 1
               && context.companionPool.freeCount == 0
               && context.companionPool.activeCount == 0
               && context.primaryLinkCount == 1
               && context.companionLinkCount == 0
               && context.primaryDestroyCount == 1,
           "cleanup refusal leaves the primary allocation linked and active");
    Expect(!context.callbackContractViolated,
           "cleanup refusal observes the callback contract");

    context.refusePrimaryDestroy = false;
    Expect(callbacks.destroyPrimary(callbacks.context, result.primary),
           "caller can clean up the retained primary ownership");
    ExpectEvents(
        context,
        {Event::CreatePrimary,
         Event::CreateCompanion,
         Event::DestroyPrimary,
         Event::DestroyPrimary},
        "caller cleanup occurs after the refused automatic rollback");
    Expect(context.primaryPool.freeCount == 1
               && context.primaryPool.activeCount == 0
               && context.primaryLinkCount == 0
               && context.primaryDestroyCount == 2,
           "caller cleanup restores the retained primary counters");
    Expect(!context.callbackContractViolated,
           "caller cleanup observes the retained ownership contract");
}

void TestBodyAndUserDataSuccessTransfersOwnership()
{
    FakeContext context{};
    context.primaryPool.freeCount = 1;
    context.companionPool.freeCount = 1;
    const allocation::ResourcePairCallbacks callbacks =
        CallbacksFor(&context);

    for (std::size_t attempt = 0; attempt < 2; ++attempt)
    {
        context.ClearEvents();
        const allocation::ResourcePairResult result =
            allocation::TryCreateResourcePair(callbacks, true);
        ExpectStatus(
            result,
            allocation::ResourcePairStatus::Success,
            "body and user-data allocation succeeds");
        Expect(result
                   && result.primary == &context.primaryResource
                   && result.companion == &context.companionResource,
               "successful body pair returns both owned resources");
        ExpectEvents(
            context,
            {Event::CreatePrimary, Event::CreateCompanion},
            "success does not invoke rollback");
        Expect(context.primaryPool.freeCount == 0
                   && context.primaryPool.activeCount == 1
                   && context.companionPool.freeCount == 0
                   && context.companionPool.activeCount == 1
                   && context.primaryLinkCount == 1
                   && context.companionLinkCount == 1
                   && context.primaryDestroyCount == 0,
               "successful body pair transfers both allocations");

        ReleaseSuccessfulCompanion(&context, result.companion);
        ReleaseSuccessfulPrimary(&context, result.primary);
        Expect(context.primaryPool.freeCount == 1
                   && context.primaryPool.activeCount == 0
                   && context.companionPool.freeCount == 1
                   && context.companionPool.activeCount == 0
                   && context.primaryLinkCount == 0
                   && context.companionLinkCount == 0,
               "caller teardown restores successful body-pair counters");
        Expect(!context.callbackContractViolated,
               "successful body pair observes the callback contract");
    }
}

void TestOrientedGeomSuccessTransfersBothPoolSlots()
{
    FakeContext context{};
    context.primaryPool.freeCount = 2;
    context.companionSharesPrimaryPool = true;
    const allocation::ResourcePairCallbacks callbacks =
        CallbacksFor(&context);

    for (std::size_t attempt = 0; attempt < 2; ++attempt)
    {
        context.ClearEvents();
        const allocation::ResourcePairResult result =
            allocation::TryCreateResourcePair(callbacks, true);
        ExpectStatus(
            result,
            allocation::ResourcePairStatus::Success,
            "oriented geom and transform allocation succeeds");
        Expect(result.primary == &context.primaryResource
                   && result.companion == &context.companionResource,
               "oriented geom success returns primary and transform");
        ExpectEvents(
            context,
            {Event::CreatePrimary, Event::CreateCompanion},
            "oriented geom success has deterministic allocation order");
        Expect(context.primaryPool.freeCount == 0
                   && context.primaryPool.activeCount == 2
                   && context.primaryLinkCount == 1
                   && context.companionLinkCount == 1
                   && context.primaryDestroyCount == 0,
               "oriented geom success transfers two shared-pool slots");

        ReleaseSuccessfulCompanion(&context, result.companion);
        ReleaseSuccessfulPrimary(&context, result.primary);
        Expect(context.primaryPool.freeCount == 2
                   && context.primaryPool.activeCount == 0
                   && context.primaryLinkCount == 0
                   && context.companionLinkCount == 0,
               "caller teardown restores successful oriented geom counters");
        Expect(!context.callbackContractViolated,
               "oriented geom success observes the callback contract");
    }
}

void TestUnorientedGeomSkipsCompanionCallbacks()
{
    FakeContext context{};
    context.primaryPool.freeCount = 1;
    context.companionSharesPrimaryPool = true;
    const allocation::ResourcePairCallbacks callbacks =
        CallbacksFor(&context);

    for (std::size_t attempt = 0; attempt < 2; ++attempt)
    {
        context.ClearEvents();
        const allocation::ResourcePairResult result =
            allocation::TryCreateResourcePair(callbacks, false);
        ExpectStatus(
            result,
            allocation::ResourcePairStatus::Success,
            "unoriented geom allocation succeeds without a transform");
        Expect(result && result.primary == &context.primaryResource
                   && !result.companion,
               "unoriented geom returns only the primary resource");
        ExpectEvents(
            context,
            {Event::CreatePrimary},
            "unoriented geom never invokes companion callbacks");
        Expect(context.primaryPool.freeCount == 0
                   && context.primaryPool.activeCount == 1
                   && context.primaryLinkCount == 1
                   && context.companionLinkCount == 0
                   && context.primaryDestroyCount == 0,
               "unoriented geom transfers one shared-pool slot");

        ReleaseSuccessfulPrimary(&context, result.primary);
        Expect(context.primaryPool.freeCount == 1
                   && context.primaryPool.activeCount == 0
                   && context.primaryLinkCount == 0,
               "caller teardown restores the unoriented geom counter");
        Expect(!context.callbackContractViolated,
               "unoriented geom success observes the callback contract");
    }
}
} // namespace

int main()
{
    TestInvalidCallbacksAreRejectedBeforeAllocation();
    TestBodyAllocationFailureIsStable();
    TestUserDataFailureRollsBackBody();
    TestPrimaryGeomFailureIsStable();
    TestTransformFailureRollsBackPrimaryGeom();
    TestCompanionFailureRetainsPrimaryWhenRollbackIsRefused();
    TestBodyAndUserDataSuccessTransfersOwnership();
    TestOrientedGeomSuccessTransfersBothPoolSlots();
    TestUnorientedGeomSkipsCompanionCallbacks();

    if (failures != 0)
    {
        std::fprintf(
            stderr,
            "%d physics resource-pair test(s) failed\n",
            failures);
        return 1;
    }

    std::puts("physics resource-pair tests passed");
    return 0;
}
