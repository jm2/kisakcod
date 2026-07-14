#define KISAK_FX_PHYSICS_SIDECAR_TESTING 1
#include <EffectsCore/fx_physics_sidecar.h>
#include <EffectsCore/fx_pool.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

struct dxBody
{
    std::uint32_t id;
};

static_assert(sizeof(FxElem) == 0x28);
static_assert(offsetof(FxElem, physObjId) == 0x18);
static_assert(std::is_same_v<decltype(FxElem::physObjId), std::int32_t>);
static_assert(!std::is_copy_constructible_v<fx::physics::BodySidecar>);
static_assert(!std::is_copy_assignable_v<fx::physics::BodySidecar>);
static_assert(!std::is_move_constructible_v<fx::physics::BodySidecar>);
static_assert(!std::is_move_assignable_v<fx::physics::BodySidecar>);
static_assert(!std::is_trivially_copyable_v<fx::physics::BodySidecar>);
static_assert(std::is_same_v<
              decltype(fx::physics::Bind(
                  std::declval<fx::physics::BodySidecar *>(),
                  std::declval<std::size_t>(),
                  std::declval<dxBody *>())),
              fx::physics::TokenResult>);
static_assert(std::is_same_v<
              decltype(fx::physics::Resolve(
                  std::declval<const fx::physics::BodySidecar *>(),
                  std::declval<std::size_t>(),
                  std::declval<fx::physics::BodyToken>())),
              fx::physics::BodyResult>);
static_assert(std::is_same_v<
              decltype(fx::physics::Take(
                  std::declval<fx::physics::BodySidecar *>(),
                  std::declval<std::size_t>(),
                  std::declval<fx::physics::BodyToken>())),
              fx::physics::BodyResult>);
static_assert(std::is_same_v<
              decltype(fx::physics::TakeFirst(
                  std::declval<fx::physics::BodySidecar *>())),
              fx::physics::IndexedBodyResult>);
static_assert(noexcept(fx::physics::Validate(
    std::declval<const fx::physics::BodySidecar *>())));
static_assert(std::is_same_v<
              decltype(fx::physics::ValidateVacantOwner(
                  std::declval<const fx::physics::BodySidecar *>(),
                  std::declval<std::size_t>())),
              fx::physics::SidecarStatus>);
static_assert(noexcept(fx::physics::ValidateVacantOwner(
    std::declval<const fx::physics::BodySidecar *>(),
    std::declval<std::size_t>())));
static_assert(noexcept(fx::physics::Bind(
    std::declval<fx::physics::BodySidecar *>(),
    std::declval<std::size_t>(),
    std::declval<dxBody *>())));

namespace
{
using fx::physics::BodyResult;
using fx::physics::BodySidecar;
using fx::physics::BodySlot;
using fx::physics::BodyToken;
using fx::physics::IndexedBodyResult;
using fx::physics::SidecarStatus;
using fx::physics::TokenResult;
using fx::physics::TransactionRole;

int Fail(const char *const message)
{
    std::fprintf(stderr, "FX physics sidecar test failed: %s\n", message);
    return 1;
}

bool Initialize(BodySidecar *const sidecar)
{
    fx::physics::SidecarTestAccess::DisableDestructionCheck(sidecar);
    return fx::physics::ResetEmpty(sidecar) == SidecarStatus::Success
        && sidecar->IsInitialized()
        && sidecar->ActiveCount() == 0u
        && fx::physics::Validate(sidecar) == SidecarStatus::Success;
}

template <typename... Sidecars>
void AllowUndrainedFixture(Sidecars *const... sidecars)
{
    (fx::physics::SidecarTestAccess::DisableDestructionCheck(sidecars), ...);
}

bool ResolvesTo(
    const BodySidecar *const sidecar,
    const std::size_t ownerIndex,
    const BodyToken token,
    dxBody *const expectedBody)
{
    const BodyResult result =
        fx::physics::Resolve(sidecar, ownerIndex, token);
    return result.status == SidecarStatus::Success
        && result.body == expectedBody
        && static_cast<bool>(result);
}

bool IsNotBound(
    const BodySidecar *const sidecar,
    const std::size_t ownerIndex,
    const BodyToken token)
{
    const BodyResult result =
        fx::physics::Resolve(sidecar, ownerIndex, token);
    return result.status == SidecarStatus::NotBound
        && result.body == nullptr
        && !static_cast<bool>(result);
}

bool SameSlot(const BodySlot &first, const BodySlot &second)
{
    return first.body == second.body
        && first.generation == second.generation;
}

struct PhysicsPoolItem
{
    std::int32_t physObjId;
    std::uint32_t marker;
};

static_assert(std::is_trivially_copyable_v<PhysicsPoolItem>);
static_assert(FxPoolSlotLayoutIsCompatible<PhysicsPoolItem>());

bool SamePoolItem(
    const PhysicsPoolItem &first,
    const PhysicsPoolItem &second)
{
    return first.physObjId == second.physObjId
        && first.marker == second.marker;
}

bool TestInitializationAndLegacyTokenBits()
{
    BodySidecar sidecar{};
    dxBody body{1u};
    if (fx::physics::Validate(&sidecar) != SidecarStatus::Uninitialized
        || fx::physics::Bind(&sidecar, 0u, &body).status
            != SidecarStatus::Uninitialized
        || !Initialize(&sidecar))
    {
        return false;
    }

    constexpr std::array<BodyToken, 5> tokens{
        0u,
        1u,
        0x7FFFFFFFu,
        0x80000000u,
        0xFFFFFFFFu,
    };
    for (const BodyToken token : tokens)
    {
        if (fx::physics::TokenFromLegacyField(
                fx::physics::TokenToLegacyField(token)) != token)
        {
            return false;
        }
    }
    return fx::physics::NextGeneration(0u) == 1u
        && fx::physics::NextGeneration(0xFFFFFFFFu) == 1u
        && fx::physics::NextRevision(0u) == 1u
        && fx::physics::NextRevision(
               (std::numeric_limits<std::uint64_t>::max)()) == 1u;
}

bool TestVacantOwnerValidation()
{
    constexpr std::size_t owner = 23u;
    BodySidecar sidecar{};
    dxBody body{1u};

    if (fx::physics::ValidateVacantOwner(nullptr, owner)
            != SidecarStatus::InvalidArgument
        || fx::physics::ValidateVacantOwner(&sidecar, owner)
            != SidecarStatus::Uninitialized
        || fx::physics::ValidateVacantOwner(&sidecar, MAX_ELEMS)
            != SidecarStatus::Uninitialized
        || !Initialize(&sidecar)
        || fx::physics::ValidateVacantOwner(&sidecar, MAX_ELEMS)
            != SidecarStatus::OwnerOutOfRange)
    {
        return false;
    }

    // A second empty reset advances every generation away from zero. The
    // owner remains vacant and must still pass the O(1) admission check.
    if (fx::physics::ResetEmpty(&sidecar) != SidecarStatus::Success)
        return false;
    const BodyToken resetGeneration =
        fx::physics::SidecarTestAccess::GetSlot(&sidecar, owner)
            .generation;
    if (resetGeneration == fx::physics::INVALID_BODY_TOKEN
        || fx::physics::ValidateVacantOwner(&sidecar, owner)
            != SidecarStatus::Success)
    {
        return false;
    }

    fx::physics::SidecarTestAccess::SetActiveCount(
        &sidecar, fx::physics::BODY_LIMIT + 1u);
    if (fx::physics::ValidateVacantOwner(&sidecar, owner)
            != SidecarStatus::ActiveCountCorrupt)
    {
        return false;
    }
    fx::physics::SidecarTestAccess::SetActiveCount(
        &sidecar, fx::physics::BODY_LIMIT);
    if (fx::physics::ValidateVacantOwner(&sidecar, owner)
            != SidecarStatus::Success)
    {
        return false;
    }
    fx::physics::SidecarTestAccess::SetActiveCount(&sidecar, 0u);

    const TokenResult bound = fx::physics::Bind(&sidecar, owner, &body);
    if (!bound
        || fx::physics::ValidateVacantOwner(&sidecar, owner)
            != SidecarStatus::AlreadyBound)
    {
        return false;
    }

    const BodyResult released =
        fx::physics::Take(&sidecar, owner, bound.token);
    const BodyToken releasedGeneration =
        fx::physics::SidecarTestAccess::GetSlot(&sidecar, owner)
            .generation;
    return released.status == SidecarStatus::Success
        && released.body == &body
        && releasedGeneration
            == fx::physics::NextGeneration(bound.token)
        && releasedGeneration != fx::physics::INVALID_BODY_TOKEN
        && fx::physics::ValidateVacantOwner(&sidecar, owner)
            == SidecarStatus::Success;
}

bool TestPoolFreeAndSidecarPublicationTransaction()
{
    constexpr std::size_t poolLimit = 2u;
    constexpr std::uint32_t firstMarker = 0xC001D00Du;
    constexpr std::uint32_t secondMarker = 0x5A1EC0DEu;
    FxPool<PhysicsPoolItem> pool[poolLimit]{};
    pool[0].nextFree = 1;
    pool[1].nextFree = -1;
    alignas(4) volatile std::int32_t firstFree = 0;
    alignas(4) volatile std::int32_t activeCount = 0;
    FxPoolAllocationState<poolLimit> allocationState{};
    FxPoolResetAllocationState(&allocationState);

    BodySidecar sidecar{};
    dxBody firstBody{1u};
    dxBody secondBody{2u};
    if (!Initialize(&sidecar))
        return false;

    FxPoolMutationStatus poolStatus =
        FxPoolMutationStatus::InvalidArgument;
    FxPool<PhysicsPoolItem> *const allocated =
        FxPoolAllocateLocked<PhysicsPoolItem, poolLimit>(
            &firstFree,
            pool,
            &activeCount,
            &allocationState,
            &poolStatus);
    std::int32_t ownerIndex = -1;
    if (allocated != &pool[0]
        || poolStatus != FxPoolMutationStatus::Success
        || !FxPoolItemIndex<PhysicsPoolItem, poolLimit>(
            pool, &allocated->item, &ownerIndex)
        || ownerIndex != 0)
    {
        return false;
    }

    const TokenResult firstBind = fx::physics::Bind(
        &sidecar, static_cast<std::size_t>(ownerIndex), &firstBody);
    if (!firstBind)
        return false;

    // Model a stale/corrupt legacy token. The failed Take must veto pool-slot
    // publication without changing either ownership domain.
    const BodyToken staleToken =
        fx::physics::NextGeneration(firstBind.token);
    allocated->item = {
        fx::physics::TokenToLegacyField(staleToken),
        firstMarker,
    };
    const PhysicsPoolItem rejectedItem = allocated->item;
    const std::int32_t rejectedFirstFree = firstFree;
    const std::int32_t rejectedActiveCount =
        Sys_AtomicLoad(&activeCount);
    const FxPoolAllocationState<poolLimit> rejectedAllocationState =
        allocationState;
    const BodySlot rejectedSidecarSlot =
        fx::physics::SidecarTestAccess::GetSlot(
            &sidecar, static_cast<std::size_t>(ownerIndex));
    const std::size_t rejectedSidecarCount = sidecar.ActiveCount();
    const std::uint64_t rejectedSidecarRevision =
        fx::physics::SidecarTestAccess::GetRevision(&sidecar);
    BodyResult rejectedTake{};
    bool rejectedCallbackSawAllocatedSlot = false;
    poolStatus = FxPoolFreeLocked<PhysicsPoolItem, poolLimit>(
        &allocated->item,
        &firstFree,
        pool,
        &activeCount,
        &allocationState,
        [&]() noexcept -> bool {
            rejectedCallbackSawAllocatedSlot =
                firstFree == rejectedFirstFree
                && Sys_AtomicLoad(&activeCount)
                    == rejectedActiveCount
                && FxPoolAllocationStateIsAllocated(
                    allocationState,
                    static_cast<std::size_t>(ownerIndex))
                && firstFree != ownerIndex
                && SamePoolItem(allocated->item, rejectedItem);
            rejectedTake = fx::physics::Take(
                &sidecar,
                static_cast<std::size_t>(ownerIndex),
                fx::physics::TokenFromLegacyField(
                    allocated->item.physObjId));
            return static_cast<bool>(rejectedTake);
        });
    if (poolStatus != FxPoolMutationStatus::BeforePublishRejected
        || !rejectedCallbackSawAllocatedSlot
        || rejectedTake.status != SidecarStatus::StaleToken
        || rejectedTake.body != nullptr
        || firstFree != rejectedFirstFree
        || Sys_AtomicLoad(&activeCount) != rejectedActiveCount
        || allocationState.allocatedWords
            != rejectedAllocationState.allocatedWords
        || allocationState.allocatedCount
            != rejectedAllocationState.allocatedCount
        || allocationState.initialized
            != rejectedAllocationState.initialized
        || !SamePoolItem(allocated->item, rejectedItem)
        || sidecar.ActiveCount() != rejectedSidecarCount
        || fx::physics::SidecarTestAccess::GetRevision(&sidecar)
            != rejectedSidecarRevision
        || !SameSlot(
            rejectedSidecarSlot,
            fx::physics::SidecarTestAccess::GetSlot(
                &sidecar, static_cast<std::size_t>(ownerIndex)))
        || !ResolvesTo(
            &sidecar,
            static_cast<std::size_t>(ownerIndex),
            firstBind.token,
            &firstBody))
    {
        return false;
    }

    // With the matching token, Take executes while the pool item is still
    // allocated. FxPoolFreeLocked publishes the slot only after it succeeds.
    allocated->item.physObjId =
        fx::physics::TokenToLegacyField(firstBind.token);
    const PhysicsPoolItem acceptedItem = allocated->item;
    BodyResult acceptedTake{};
    bool acceptedCallbackSawAllocatedSlot = false;
    poolStatus = FxPoolFreeLocked<PhysicsPoolItem, poolLimit>(
        &allocated->item,
        &firstFree,
        pool,
        &activeCount,
        &allocationState,
        [&]() noexcept -> bool {
            acceptedCallbackSawAllocatedSlot =
                firstFree == rejectedFirstFree
                && Sys_AtomicLoad(&activeCount)
                    == rejectedActiveCount
                && FxPoolAllocationStateIsAllocated(
                    allocationState,
                    static_cast<std::size_t>(ownerIndex))
                && firstFree != ownerIndex
                && SamePoolItem(allocated->item, acceptedItem);
            acceptedTake = fx::physics::Take(
                &sidecar,
                static_cast<std::size_t>(ownerIndex),
                fx::physics::TokenFromLegacyField(
                    allocated->item.physObjId));
            return static_cast<bool>(acceptedTake);
        });
    const std::uintptr_t firstBodyAddress =
        reinterpret_cast<std::uintptr_t>(&firstBody);
    if (poolStatus != FxPoolMutationStatus::Success
        || !acceptedCallbackSawAllocatedSlot
        || acceptedTake.status != SidecarStatus::Success
        || acceptedTake.body != &firstBody
        || reinterpret_cast<std::uintptr_t>(acceptedTake.body)
            != firstBodyAddress
        || firstFree != ownerIndex
        || Sys_AtomicLoad(&activeCount) != 0
        || allocationState.allocatedCount != 0u
        || FxPoolAllocationStateIsAllocated(
            allocationState, static_cast<std::size_t>(ownerIndex))
        || pool[ownerIndex].nextFree != rejectedFirstFree
        || sidecar.ActiveCount() != 0u)
    {
        return false;
    }

    FxPool<PhysicsPoolItem> *const reallocated =
        FxPoolAllocateLocked<PhysicsPoolItem, poolLimit>(
            &firstFree,
            pool,
            &activeCount,
            &allocationState,
            &poolStatus);
    std::int32_t reboundOwnerIndex = -1;
    if (reallocated != allocated
        || poolStatus != FxPoolMutationStatus::Success
        || !FxPoolItemIndex<PhysicsPoolItem, poolLimit>(
            pool, &reallocated->item, &reboundOwnerIndex)
        || reboundOwnerIndex != ownerIndex)
    {
        return false;
    }

    const TokenResult secondBind = fx::physics::Bind(
        &sidecar,
        static_cast<std::size_t>(reboundOwnerIndex),
        &secondBody);
    if (!secondBind)
        return false;
    reallocated->item = {
        fx::physics::TokenToLegacyField(secondBind.token),
        secondMarker,
    };
    const BodyResult staleResolve = fx::physics::Resolve(
        &sidecar,
        static_cast<std::size_t>(reboundOwnerIndex),
        firstBind.token);
    if (secondBind.token == firstBind.token
        || staleResolve.status != SidecarStatus::StaleToken
        || staleResolve.body != nullptr
        || !ResolvesTo(
            &sidecar,
            static_cast<std::size_t>(reboundOwnerIndex),
            secondBind.token,
            &secondBody))
    {
        return false;
    }

    const BodyResult finalTake = fx::physics::Take(
        &sidecar,
        static_cast<std::size_t>(reboundOwnerIndex),
        secondBind.token);
    return finalTake.status == SidecarStatus::Success
        && finalTake.body == &secondBody
        && FxPoolFreeLocked<PhysicsPoolItem, poolLimit>(
            &reallocated->item,
            &firstFree,
            pool,
            &activeCount,
            &allocationState)
            == FxPoolMutationStatus::Success;
}

bool TestReturnValueBindResolveTakeAndRecycle()
{
    BodySidecar sidecar{};
    if (!Initialize(&sidecar))
        return false;

    dxBody first{1u};
    dxBody second{2u};
    const TokenResult firstBind =
        fx::physics::Bind(&sidecar, 17u, &first);
    if (firstBind.status != SidecarStatus::Success
        || firstBind.token == fx::physics::INVALID_BODY_TOKEN
        || !static_cast<bool>(firstBind)
        || sidecar.ActiveCount() != 1u
        || !ResolvesTo(&sidecar, 17u, firstBind.token, &first))
    {
        return false;
    }

    const BodyResult wrongOwner =
        fx::physics::Resolve(&sidecar, 18u, firstBind.token);
    const BodyResult zeroToken =
        fx::physics::Resolve(&sidecar, 17u, 0u);
    const BodyResult staleResolve = fx::physics::Resolve(
        &sidecar,
        17u,
        fx::physics::NextGeneration(firstBind.token));
    if (wrongOwner.status != SidecarStatus::NotBound
        || wrongOwner.body != nullptr
        || zeroToken.status != SidecarStatus::ZeroToken
        || zeroToken.body != nullptr
        || staleResolve.status != SidecarStatus::StaleToken
        || staleResolve.body != nullptr
        || fx::physics::Bind(&sidecar, 18u, &first).status
            != SidecarStatus::DuplicateBody
        || fx::physics::Bind(&sidecar, 17u, &second).status
            != SidecarStatus::AlreadyBound)
    {
        return false;
    }

    const BodyToken staleToken =
        fx::physics::NextGeneration(firstBind.token);
    const BodyResult staleTake =
        fx::physics::Take(&sidecar, 17u, staleToken);
    if (staleTake.status != SidecarStatus::StaleToken
        || staleTake.body != nullptr
        || sidecar.ActiveCount() != 1u)
    {
        return false;
    }

    const BodyResult taken =
        fx::physics::Take(&sidecar, 17u, firstBind.token);
    const BodyToken generationAfterTake =
        fx::physics::SidecarTestAccess::GetSlot(&sidecar, 17u)
            .generation;
    if (taken.status != SidecarStatus::Success
        || taken.body != &first
        || !static_cast<bool>(taken)
        || sidecar.ActiveCount() != 0u
        || generationAfterTake
            != fx::physics::NextGeneration(firstBind.token))
    {
        return false;
    }

    const TokenResult secondBind =
        fx::physics::Bind(&sidecar, 17u, &second);
    return secondBind.status == SidecarStatus::Success
        && secondBind.token
            == fx::physics::NextGeneration(generationAfterTake)
        && secondBind.token != firstBind.token
        && fx::physics::Resolve(&sidecar, 17u, firstBind.token).status
            == SidecarStatus::StaleToken
        && ResolvesTo(&sidecar, 17u, secondBind.token, &second);
}

bool TestLifecycleDrainAndFinalize()
{
    // Declare bodies first so the checked registry is finalized and destroyed
    // before its caller-owned native bodies leave scope.
    dxBody first{1u};
    dxBody second{2u};
    BodySidecar sidecar{};
    if (fx::physics::ResetEmpty(&sidecar) != SidecarStatus::Success)
        return false;
    const TokenResult firstBind =
        fx::physics::Bind(&sidecar, 7u, &first);
    const TokenResult secondBind =
        fx::physics::Bind(&sidecar, 19u, &second);
    if (!firstBind || !secondBind)
        return false;

    const IndexedBodyResult firstTaken =
        fx::physics::TakeFirst(&sidecar);
    const IndexedBodyResult secondTaken =
        fx::physics::TakeFirst(&sidecar);
    const IndexedBodyResult empty =
        fx::physics::TakeFirst(&sidecar);
    return firstTaken.status == SidecarStatus::Success
        && firstTaken.body == &first
        && firstTaken.ownerIndex == 7u
        && firstTaken.token == firstBind.token
        && secondTaken.status == SidecarStatus::Success
        && secondTaken.body == &second
        && secondTaken.ownerIndex == 19u
        && secondTaken.token == secondBind.token
        && empty.status == SidecarStatus::NotBound
        && empty.body == nullptr
        && empty.ownerIndex == MAX_ELEMS
        && empty.token == fx::physics::INVALID_BODY_TOKEN
        && sidecar.ActiveCount() == 0u
        && fx::physics::ResetEmpty(&sidecar) == SidecarStatus::Success
        && fx::physics::SidecarTestAccess::GetTransactionRole(&sidecar)
            == TransactionRole::None;
}

bool TestCapacityAndUniqueOwnership()
{
    BodySidecar sidecar{};
    if (!Initialize(&sidecar))
        return false;

    std::array<dxBody, fx::physics::BODY_LIMIT + 1u> bodies{};
    for (std::size_t index = 0; index < fx::physics::BODY_LIMIT; ++index)
    {
        bodies[index].id = static_cast<std::uint32_t>(index + 1u);
        const TokenResult result =
            fx::physics::Bind(&sidecar, index, &bodies[index]);
        if (result.status != SidecarStatus::Success || result.token == 0u)
            return false;
    }

    const TokenResult overflow = fx::physics::Bind(
        &sidecar, fx::physics::BODY_LIMIT, &bodies.back());
    return sidecar.ActiveCount() == fx::physics::BODY_LIMIT
        && fx::physics::Validate(&sidecar) == SidecarStatus::Success
        && overflow.status == SidecarStatus::CapacityExceeded
        && overflow.token == fx::physics::INVALID_BODY_TOKEN;
}

bool TestGenerationWrapAndResetInvalidation()
{
    BodySidecar sidecar{};
    if (!Initialize(&sidecar))
        return false;

    constexpr std::size_t owner = 31u;
    fx::physics::SidecarTestAccess::SetSlot(
        &sidecar,
        owner,
        nullptr,
        (std::numeric_limits<BodyToken>::max)());
    dxBody body{1u};
    const TokenResult wrapped = fx::physics::Bind(&sidecar, owner, &body);
    if (wrapped.status != SidecarStatus::Success || wrapped.token != 1u
        || fx::physics::ResetEmpty(&sidecar) != SidecarStatus::NotEmpty
        || !ResolvesTo(&sidecar, owner, wrapped.token, &body))
    {
        return false;
    }

    const BodyResult taken =
        fx::physics::Take(&sidecar, owner, wrapped.token);
    const BodyToken generationAfterTake =
        fx::physics::SidecarTestAccess::GetSlot(&sidecar, owner)
            .generation;
    if (taken.status != SidecarStatus::Success || taken.body != &body
        || generationAfterTake != 2u
        || fx::physics::ResetEmpty(&sidecar) != SidecarStatus::Success)
    {
        return false;
    }

    const BodyToken generationAfterReset =
        fx::physics::SidecarTestAccess::GetSlot(&sidecar, owner)
            .generation;
    dxBody replacement{2u};
    const TokenResult rebound =
        fx::physics::Bind(&sidecar, owner, &replacement);
    return generationAfterReset == 3u
        && rebound.status == SidecarStatus::Success
        && rebound.token == 4u
        && fx::physics::Resolve(&sidecar, owner, wrapped.token).status
            == SidecarStatus::StaleToken
        && ResolvesTo(&sidecar, owner, rebound.token, &replacement);
}

bool TestSemanticValidation()
{
    BodySidecar sidecar{};
    if (!Initialize(&sidecar))
        return false;
    dxBody first{1u};
    dxBody second{2u};
    const TokenResult firstBind = fx::physics::Bind(&sidecar, 2u, &first);
    const TokenResult secondBind = fx::physics::Bind(&sidecar, 9u, &second);
    if (!firstBind || !secondBind)
        return false;

    std::array<BodyToken, MAX_ELEMS> expected{};
    expected[2] = firstBind.token;
    expected[9] = secondBind.token;
    if (fx::physics::ValidateSemanticOwnership(&sidecar, expected)
            != SidecarStatus::Success)
    {
        return false;
    }
    expected[2] = fx::physics::NextGeneration(firstBind.token);
    if (fx::physics::ValidateSemanticOwnership(&sidecar, expected)
            != SidecarStatus::OwnershipMismatch)
    {
        return false;
    }
    expected[2] = firstBind.token;
    expected[10] = 1u;
    return fx::physics::ValidateSemanticOwnership(&sidecar, expected)
        == SidecarStatus::OwnershipMismatch;
}

bool TestCorruptBindAndTakeAreNonMutating()
{
    BodySidecar countCorrupt{};
    if (!Initialize(&countCorrupt))
        return false;
    dxBody first{1u};
    dxBody second{2u};
    const TokenResult firstBind =
        fx::physics::Bind(&countCorrupt, 2u, &first);
    if (!firstBind)
        return false;
    fx::physics::SidecarTestAccess::SetActiveCount(&countCorrupt, 2u);
    const BodySlot countSlotBefore =
        fx::physics::SidecarTestAccess::GetSlot(&countCorrupt, 2u);
    const BodySlot emptySlotBefore =
        fx::physics::SidecarTestAccess::GetSlot(&countCorrupt, 3u);

    const TokenResult rejectedBind =
        fx::physics::Bind(&countCorrupt, 3u, &second);
    const BodyResult rejectedTake =
        fx::physics::Take(&countCorrupt, 2u, firstBind.token);
    if (rejectedBind.status != SidecarStatus::ActiveCountCorrupt
        || rejectedBind.token != 0u
        || rejectedTake.status != SidecarStatus::ActiveCountCorrupt
        || rejectedTake.body != nullptr
        || countCorrupt.ActiveCount() != 2u
        || !SameSlot(
            countSlotBefore,
            fx::physics::SidecarTestAccess::GetSlot(&countCorrupt, 2u))
        || !SameSlot(
            emptySlotBefore,
            fx::physics::SidecarTestAccess::GetSlot(&countCorrupt, 3u)))
    {
        return false;
    }

    BodySidecar duplicate{};
    if (!Initialize(&duplicate))
        return false;
    dxBody duplicateBody{3u};
    dxBody unrelatedBody{4u};
    const TokenResult duplicateBind =
        fx::physics::Bind(&duplicate, 4u, &duplicateBody);
    if (!duplicateBind)
        return false;
    fx::physics::SidecarTestAccess::SetSlot(
        &duplicate, 5u, &duplicateBody, duplicateBind.token);
    fx::physics::SidecarTestAccess::SetActiveCount(&duplicate, 2u);
    const BodySlot firstDuplicateBefore =
        fx::physics::SidecarTestAccess::GetSlot(&duplicate, 4u);
    const BodySlot secondDuplicateBefore =
        fx::physics::SidecarTestAccess::GetSlot(&duplicate, 5u);

    const TokenResult duplicateRejectedBind =
        fx::physics::Bind(&duplicate, 6u, &unrelatedBody);
    const BodyResult duplicateRejectedTake =
        fx::physics::Take(&duplicate, 4u, duplicateBind.token);
    return fx::physics::Validate(&duplicate) == SidecarStatus::DuplicateBody
        && duplicateRejectedBind.status == SidecarStatus::DuplicateBody
        && duplicateRejectedBind.token == 0u
        && duplicateRejectedTake.status == SidecarStatus::DuplicateBody
        && duplicateRejectedTake.body == nullptr
        && duplicate.ActiveCount() == 2u
        && SameSlot(
            firstDuplicateBefore,
            fx::physics::SidecarTestAccess::GetSlot(&duplicate, 4u))
        && SameSlot(
            secondDuplicateBefore,
            fx::physics::SidecarTestAccess::GetSlot(&duplicate, 5u));
}

bool TestCorruptTransactionRoleIsRejected()
{
    BodySidecar sidecar{};
    BodySidecar peer{};
    if (!Initialize(&sidecar) || !Initialize(&peer))
        return false;

    fx::physics::SidecarTestAccess::SetTransactionState(
        &sidecar,
        static_cast<TransactionRole>(0xFFu),
        &peer,
        fx::physics::SidecarTestAccess::GetRevision(&peer));
    dxBody body{1u};
    const TokenResult rejected =
        fx::physics::Bind(&sidecar, 0u, &body);
    if (fx::physics::Validate(&sidecar)
            != SidecarStatus::TransactionProvenanceMismatch
        || rejected.status
            != SidecarStatus::TransactionProvenanceMismatch
        || rejected.token != fx::physics::INVALID_BODY_TOKEN
        || sidecar.ActiveCount() != 0u)
    {
        return false;
    }

    fx::physics::SidecarTestAccess::SetTransactionState(
        &sidecar, TransactionRole::None, nullptr, 0);
    return fx::physics::Validate(&sidecar) == SidecarStatus::Success;
}

bool TestReplacementPublicationAndRollbackEveryOwner()
{
    BodySidecar live{};
    BodySidecar staged{};
    BodySidecar rollback{};
    BodySidecar discarded{};
    AllowUndrainedFixture(&staged, &rollback, &discarded);
    if (!Initialize(&live))
        return false;

    dxBody oldFirst{1u};
    dxBody oldSecond{2u};
    const TokenResult oldFirstBind =
        fx::physics::Bind(&live, 1u, &oldFirst);
    const TokenResult oldSecondBind =
        fx::physics::Bind(&live, 3u, &oldSecond);
    if (!oldFirstBind || !oldSecondBind
        || fx::physics::PrepareReplacement(&live, &staged)
            != SidecarStatus::Success)
    {
        return false;
    }

    dxBody newFirst{3u};
    dxBody newSecond{4u};
    const TokenResult newFirstBind =
        fx::physics::Bind(&staged, 1u, &newFirst);
    const TokenResult newSecondBind =
        fx::physics::Bind(&staged, 8u, &newSecond);
    if (!newFirstBind || !newSecondBind
        || newFirstBind.token
            != fx::physics::NextGeneration(oldFirstBind.token)
        || fx::physics::ValidateReplacementRelation(&live, &staged)
            != SidecarStatus::Success
        || fx::physics::PublishReplacement(&live, &staged, &rollback)
            != SidecarStatus::Success)
    {
        return false;
    }

    if (live.ActiveCount() != 2u || rollback.ActiveCount() != 2u
        || staged.ActiveCount() != 0u
        || !ResolvesTo(&live, 1u, newFirstBind.token, &newFirst)
        || !ResolvesTo(&live, 8u, newSecondBind.token, &newSecond)
        || !IsNotBound(&live, 3u, oldSecondBind.token)
        || !ResolvesTo(&rollback, 1u, oldFirstBind.token, &oldFirst)
        || !ResolvesTo(&rollback, 3u, oldSecondBind.token, &oldSecond)
        || !IsNotBound(&rollback, 8u, newSecondBind.token)
        || !IsNotBound(&staged, 1u, newFirstBind.token)
        || !IsNotBound(&staged, 8u, newSecondBind.token)
        || fx::physics::ValidateReplacementRelation(&rollback, &live)
            != SidecarStatus::Success
        || fx::physics::RollbackReplacement(
               &live, &rollback, &discarded)
            != SidecarStatus::Success)
    {
        return false;
    }

    return live.ActiveCount() == 2u
        && rollback.ActiveCount() == 0u
        && discarded.ActiveCount() == 2u
        && fx::physics::Validate(&live) == SidecarStatus::Success
        && fx::physics::Validate(&rollback) == SidecarStatus::Success
        && fx::physics::Validate(&discarded) == SidecarStatus::Success
        && ResolvesTo(&live, 1u, oldFirstBind.token, &oldFirst)
        && ResolvesTo(&live, 3u, oldSecondBind.token, &oldSecond)
        && IsNotBound(&live, 8u, newSecondBind.token)
        && ResolvesTo(
            &discarded, 1u, newFirstBind.token, &newFirst)
        && ResolvesTo(
            &discarded, 8u, newSecondBind.token, &newSecond)
        && IsNotBound(&discarded, 3u, oldSecondBind.token)
        && IsNotBound(&rollback, 1u, oldFirstBind.token)
        && IsNotBound(&rollback, 3u, oldSecondBind.token);
}

bool TestSameTokenIndependentReplacementRejected()
{
    BodySidecar live{};
    BodySidecar independent{};
    BodySidecar rollback{};
    if (!Initialize(&live) || !Initialize(&independent))
        return false;

    dxBody oldBody{1u};
    dxBody independentBody{2u};
    const TokenResult oldBind = fx::physics::Bind(&live, 4u, &oldBody);
    const TokenResult independentBind =
        fx::physics::Bind(&independent, 4u, &independentBody);
    if (!oldBind || !independentBind
        || oldBind.token != independentBind.token
        || fx::physics::ValidateReplacementRelation(&live, &independent)
            != SidecarStatus::GenerationRelationMismatch
        || fx::physics::PublishReplacement(
               &live, &independent, &rollback)
            != SidecarStatus::TransactionProvenanceMismatch)
    {
        return false;
    }
    return ResolvesTo(&live, 4u, oldBind.token, &oldBody)
        && ResolvesTo(
            &independent,
            4u,
            independentBind.token,
            &independentBody)
        && rollback.ActiveCount() == 0u;
}

bool TestPreparedSourceProvenanceAndReplayAreRejected()
{
    BodySidecar source{};
    BodySidecar unrelated{};
    BodySidecar staged{};
    BodySidecar rollback{};
    BodySidecar spareRollback{};
    if (!Initialize(&source) || !Initialize(&unrelated)
        || fx::physics::PrepareReplacement(&source, &staged)
            != SidecarStatus::Success)
    {
        return false;
    }

    dxBody replacementBody{1u};
    const TokenResult replacement =
        fx::physics::Bind(&staged, 4u, &replacementBody);
    if (!replacement
        || fx::physics::ValidateReplacementRelation(
               &unrelated, &staged) != SidecarStatus::Success
        || fx::physics::PublishReplacement(
               &unrelated, &staged, &spareRollback)
            != SidecarStatus::TransactionProvenanceMismatch
        || source.ActiveCount() != 0u
        || unrelated.ActiveCount() != 0u
        || !ResolvesTo(
            &staged, 4u, replacement.token, &replacementBody))
    {
        return false;
    }

    if (fx::physics::PublishReplacement(&source, &staged, &rollback)
            != SidecarStatus::Success
        || !ResolvesTo(
            &source, 4u, replacement.token, &replacementBody)
        || rollback.ActiveCount() != 0u
        || fx::physics::PublishReplacement(
               &source, &staged, &spareRollback)
            != SidecarStatus::TransactionProvenanceMismatch
        || fx::physics::PrepareReplacement(&unrelated, &rollback)
            != SidecarStatus::DestinationNotVacant)
    {
        return false;
    }

    // An empty rollback still carries transaction authority. ResetEmpty is
    // the explicit no-body commit/finalization path that consumes it.
    return fx::physics::ResetEmpty(&rollback) == SidecarStatus::Success
        && fx::physics::ValidateVacantDestination(&rollback)
            == SidecarStatus::Success
        && fx::physics::SidecarTestAccess::GetTransactionRole(&staged)
            == TransactionRole::None
        && fx::physics::SidecarTestAccess::GetTransactionRole(&rollback)
            == TransactionRole::None;
}

bool TestReconstructedSourceRejectsStalePreparation()
{
    dxBody replacementBody{1u};
    alignas(BodySidecar) std::byte sourceStorage[sizeof(BodySidecar)]{};
    BodySidecar staged{};
    BodySidecar rollback{};
    AllowUndrainedFixture(&staged, &rollback);

    BodySidecar *const original =
        ::new (static_cast<void *>(sourceStorage)) BodySidecar{};
    fx::physics::SidecarTestAccess::DisableDestructionCheck(original);
    const bool originalInitialized =
        fx::physics::ResetEmpty(original) == SidecarStatus::Success;
    const std::uint64_t originalLifetime =
        fx::physics::SidecarTestAccess::GetLifetimeNonce(original);
    const std::uint64_t originalRevision =
        fx::physics::SidecarTestAccess::GetRevision(original);
    const SidecarStatus preparedStatus =
        fx::physics::PrepareReplacement(original, &staged);
    const TokenResult stagedBind =
        fx::physics::Bind(&staged, 4u, &replacementBody);
    original->~BodySidecar();

    BodySidecar *const reconstructed =
        ::new (static_cast<void *>(sourceStorage)) BodySidecar{};
    fx::physics::SidecarTestAccess::DisableDestructionCheck(reconstructed);
    const bool reconstructedInitialized =
        fx::physics::ResetEmpty(reconstructed) == SidecarStatus::Success;
    const std::uint64_t reconstructedLifetime =
        fx::physics::SidecarTestAccess::GetLifetimeNonce(reconstructed);
    const std::uint64_t reconstructedRevision =
        fx::physics::SidecarTestAccess::GetRevision(reconstructed);
    const SidecarStatus relationStatus =
        fx::physics::ValidateReplacementRelation(reconstructed, &staged);
    const SidecarStatus publishStatus =
        fx::physics::PublishReplacement(
            reconstructed, &staged, &rollback);
    const bool remainedUnchanged = reconstructed->ActiveCount() == 0u
        && stagedBind
        && ResolvesTo(
            &staged, 4u, stagedBind.token, &replacementBody)
        && rollback.ActiveCount() == 0u;
    reconstructed->~BodySidecar();

    return originalInitialized && reconstructedInitialized
        && originalLifetime != 0u && reconstructedLifetime != 0u
        && originalLifetime != reconstructedLifetime
        && originalRevision == reconstructedRevision
        && preparedStatus == SidecarStatus::Success
        && stagedBind.status == SidecarStatus::Success
        && relationStatus == SidecarStatus::Success
        && publishStatus == SidecarStatus::TransactionProvenanceMismatch
        && remainedUnchanged;
}

bool TestChangedLiveAfterPrepareRejected()
{
    BodySidecar live{};
    BodySidecar staged{};
    BodySidecar rollback{};
    AllowUndrainedFixture(&staged);
    if (!Initialize(&live))
        return false;

    dxBody oldBody{1u};
    dxBody newBody{2u};
    const TokenResult oldBind = fx::physics::Bind(&live, 6u, &oldBody);
    if (!oldBind
        || fx::physics::PrepareReplacement(&live, &staged)
            != SidecarStatus::Success)
    {
        return false;
    }
    const TokenResult stagedBind =
        fx::physics::Bind(&staged, 6u, &newBody);
    const BodyResult removed =
        fx::physics::Take(&live, 6u, oldBind.token);
    if (!stagedBind || removed.status != SidecarStatus::Success
        || removed.body != &oldBody
        || fx::physics::PublishReplacement(&live, &staged, &rollback)
            != SidecarStatus::TransactionProvenanceMismatch)
    {
        return false;
    }

    const BodyToken liveGeneration =
        fx::physics::SidecarTestAccess::GetSlot(&live, 6u).generation;
    return live.ActiveCount() == 0u
        && liveGeneration == fx::physics::NextGeneration(oldBind.token)
        && IsNotBound(&live, 6u, oldBind.token)
        && ResolvesTo(&staged, 6u, stagedBind.token, &newBody)
        && rollback.ActiveCount() == 0u;
}

bool TestBindAndResetInvalidatePreparedSource()
{
    BodySidecar bindLive{};
    BodySidecar bindStaged{};
    BodySidecar bindRollback{};
    AllowUndrainedFixture(&bindStaged);
    if (!Initialize(&bindLive)
        || fx::physics::PrepareReplacement(&bindLive, &bindStaged)
            != SidecarStatus::Success)
    {
        return false;
    }

    dxBody liveBody{1u};
    const TokenResult liveBind =
        fx::physics::Bind(&bindLive, 2u, &liveBody);
    if (!liveBind
        || fx::physics::PublishReplacement(
               &bindLive, &bindStaged, &bindRollback)
            != SidecarStatus::TransactionProvenanceMismatch
        || !ResolvesTo(&bindLive, 2u, liveBind.token, &liveBody)
        || bindStaged.ActiveCount() != 0u
        || bindRollback.ActiveCount() != 0u)
    {
        return false;
    }

    BodySidecar resetLive{};
    BodySidecar resetStaged{};
    BodySidecar resetRollback{};
    AllowUndrainedFixture(&resetStaged);
    if (!Initialize(&resetLive)
        || fx::physics::PrepareReplacement(&resetLive, &resetStaged)
            != SidecarStatus::Success)
    {
        return false;
    }
    const std::uint64_t revisionBeforeReset =
        fx::physics::SidecarTestAccess::GetRevision(&resetLive);
    return fx::physics::ResetEmpty(&resetLive) == SidecarStatus::Success
        && fx::physics::SidecarTestAccess::GetRevision(&resetLive)
            == fx::physics::NextRevision(revisionBeforeReset)
        && fx::physics::PublishReplacement(
               &resetLive, &resetStaged, &resetRollback)
            == SidecarStatus::TransactionProvenanceMismatch
        && resetLive.ActiveCount() == 0u
        && resetStaged.ActiveCount() == 0u
        && resetRollback.ActiveCount() == 0u;
}

bool TestCrossStateBodyAliasingRejected()
{
    BodySidecar live{};
    BodySidecar staged{};
    BodySidecar rollback{};
    AllowUndrainedFixture(&staged);
    if (!Initialize(&live))
        return false;

    dxBody sharedBody{1u};
    const TokenResult liveBind =
        fx::physics::Bind(&live, 1u, &sharedBody);
    if (!liveBind
        || fx::physics::PrepareReplacement(&live, &staged)
            != SidecarStatus::Success)
    {
        return false;
    }
    const TokenResult stagedBind =
        fx::physics::Bind(&staged, 2u, &sharedBody);
    if (!stagedBind
        || fx::physics::ValidateDisjointOwnership(&live, &staged)
            != SidecarStatus::DuplicateBody
        || fx::physics::PublishReplacement(&live, &staged, &rollback)
            != SidecarStatus::DuplicateBody)
    {
        return false;
    }
    return ResolvesTo(&live, 1u, liveBind.token, &sharedBody)
        && ResolvesTo(&staged, 2u, stagedBind.token, &sharedBody)
        && rollback.ActiveCount() == 0u;
}

bool TestFailedRollbackIsNonMutating()
{
    BodySidecar live{};
    BodySidecar staged{};
    BodySidecar rollback{};
    BodySidecar discarded{};
    AllowUndrainedFixture(&rollback);
    if (!Initialize(&live))
        return false;

    dxBody oldBody{1u};
    dxBody newFirst{2u};
    dxBody newSecond{3u};
    const TokenResult oldBind = fx::physics::Bind(&live, 1u, &oldBody);
    if (!oldBind
        || fx::physics::PrepareReplacement(&live, &staged)
            != SidecarStatus::Success)
    {
        return false;
    }
    const TokenResult newFirstBind =
        fx::physics::Bind(&staged, 1u, &newFirst);
    const TokenResult newSecondBind =
        fx::physics::Bind(&staged, 8u, &newSecond);
    if (!newFirstBind || !newSecondBind
        || fx::physics::PublishReplacement(&live, &staged, &rollback)
            != SidecarStatus::Success)
    {
        return false;
    }

    dxBody postPublishBody{4u};
    const TokenResult postPublishBind =
        fx::physics::Bind(&live, 10u, &postPublishBody);
    if (!postPublishBind
        || fx::physics::ValidateReplacementRelation(&rollback, &live)
            != SidecarStatus::Success)
    {
        return false;
    }
    const BodySlot liveFirstBefore =
        fx::physics::SidecarTestAccess::GetSlot(&live, 1u);
    const BodySlot liveSecondBefore =
        fx::physics::SidecarTestAccess::GetSlot(&live, 8u);
    const BodySlot livePostPublishBefore =
        fx::physics::SidecarTestAccess::GetSlot(&live, 10u);
    const BodySlot rollbackBefore =
        fx::physics::SidecarTestAccess::GetSlot(&rollback, 1u);

    if (fx::physics::RollbackReplacement(
            &live, &rollback, &discarded)
            != SidecarStatus::TransactionProvenanceMismatch)
    {
        return false;
    }
    return live.ActiveCount() == 3u
        && rollback.ActiveCount() == 1u
        && discarded.ActiveCount() == 0u
        && SameSlot(
            liveFirstBefore,
            fx::physics::SidecarTestAccess::GetSlot(&live, 1u))
        && SameSlot(
            liveSecondBefore,
            fx::physics::SidecarTestAccess::GetSlot(&live, 8u))
        && SameSlot(
            livePostPublishBefore,
            fx::physics::SidecarTestAccess::GetSlot(&live, 10u))
        && SameSlot(
            rollbackBefore,
            fx::physics::SidecarTestAccess::GetSlot(&rollback, 1u))
        && ResolvesTo(&live, 1u, newFirstBind.token, &newFirst)
        && ResolvesTo(&live, 8u, newSecondBind.token, &newSecond)
        && ResolvesTo(
            &live, 10u, postPublishBind.token, &postPublishBody)
        && ResolvesTo(&rollback, 1u, oldBind.token, &oldBody);
}
} // namespace

int main()
{
    if (!TestInitializationAndLegacyTokenBits())
        return Fail("initialization or legacy token bit preservation");
    if (!TestVacantOwnerValidation())
        return Fail("vacant-owner validation");
    if (!TestPoolFreeAndSidecarPublicationTransaction())
        return Fail("pool/sidecar pre-publication transaction");
    if (!TestReturnValueBindResolveTakeAndRecycle())
        return Fail("return-value bind/resolve/take/recycle contract");
    if (!TestLifecycleDrainAndFinalize())
        return Fail("lifecycle drain/finalize contract");
    if (!TestCapacityAndUniqueOwnership())
        return Fail("capacity or unique-body ownership");
    if (!TestGenerationWrapAndResetInvalidation())
        return Fail("generation wrap or reset invalidation");
    if (!TestSemanticValidation())
        return Fail("semantic validation");
    if (!TestCorruptBindAndTakeAreNonMutating())
        return Fail("corrupt bind/take mutation rejection");
    if (!TestCorruptTransactionRoleIsRejected())
        return Fail("corrupt transaction role rejection");
    if (!TestReplacementPublicationAndRollbackEveryOwner())
        return Fail("replacement publication/rollback ownership");
    if (!TestSameTokenIndependentReplacementRejected())
        return Fail("same-token independent replacement admission");
    if (!TestPreparedSourceProvenanceAndReplayAreRejected())
        return Fail("prepared-source provenance or replay admission");
    if (!TestReconstructedSourceRejectsStalePreparation())
        return Fail("reconstructed-source transaction provenance");
    if (!TestChangedLiveAfterPrepareRejected())
        return Fail("changed-live replacement admission");
    if (!TestBindAndResetInvalidatePreparedSource())
        return Fail("bind/reset prepared-source invalidation");
    if (!TestCrossStateBodyAliasingRejected())
        return Fail("cross-state body aliasing");
    if (!TestFailedRollbackIsNonMutating())
        return Fail("failed rollback mutation");
    return 0;
}
