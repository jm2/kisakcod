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
static_assert(!std::is_copy_constructible_v<
    fx::physics::BodySidecarSnapshotScratch>);
static_assert(!std::is_copy_assignable_v<
    fx::physics::BodySidecarSnapshotScratch>);
static_assert(!std::is_move_constructible_v<
    fx::physics::BodySidecarSnapshotScratch>);
static_assert(!std::is_move_assignable_v<
    fx::physics::BodySidecarSnapshotScratch>);
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
static_assert(std::is_same_v<
              decltype(fx::physics::SnapshotOwnership(
                  std::declval<const fx::physics::BodySidecar *>(),
                  std::declval<fx::physics::OwnershipSnapshot *>())),
              fx::physics::SidecarStatus>);
static_assert(noexcept(fx::physics::SnapshotOwnership(
    std::declval<const fx::physics::BodySidecar *>(),
    std::declval<fx::physics::OwnershipSnapshot *>())));
static_assert(std::is_same_v<
              decltype(fx::physics::SnapshotOwnershipWithScratch(
                  std::declval<const fx::physics::BodySidecar *>(),
                  std::declval<fx::physics::OwnershipSnapshot *>(),
                  std::declval<
                      fx::physics::BodySidecarSnapshotScratch *>())),
              fx::physics::SidecarStatus>);
static_assert(
    noexcept(fx::physics::SnapshotOwnershipWithScratch(
        std::declval<const fx::physics::BodySidecar *>(),
        std::declval<fx::physics::OwnershipSnapshot *>(),
        std::declval<fx::physics::BodySidecarSnapshotScratch *>())));
static_assert(std::is_trivially_copyable_v<
    fx::physics::OwnershipRecord>);
static_assert(std::is_trivially_copyable_v<
    fx::physics::OwnershipSnapshot>);
static_assert(sizeof(fx::physics::OwnershipRecord)
    <= sizeof(dxBody *) + 2u * sizeof(std::uint32_t));
static_assert(std::tuple_size_v<decltype(
    fx::physics::OwnershipSnapshot::records)>
    == fx::physics::BODY_LIMIT);
static_assert(noexcept(fx::physics::Validate(
    std::declval<const fx::physics::BodySidecar *>())));
static_assert(noexcept(fx::physics::ValidateWithScratch(
    std::declval<const fx::physics::BodySidecar *>(),
    std::declval<fx::physics::BodySidecarValidationScratch *>())));
static_assert(noexcept(fx::physics::ResetEmptyWithScratch(
    std::declval<fx::physics::BodySidecar *>(),
    std::declval<fx::physics::BodySidecarValidationScratch *>())));
static_assert(noexcept(fx::physics::BindWithScratch(
    std::declval<fx::physics::BodySidecar *>(),
    std::declval<std::size_t>(),
    std::declval<dxBody *>(),
    std::declval<fx::physics::BodySidecarValidationScratch *>())));
static_assert(noexcept(fx::physics::TakeWithScratch(
    std::declval<fx::physics::BodySidecar *>(),
    std::declval<std::size_t>(),
    std::declval<fx::physics::BodyToken>(),
    std::declval<fx::physics::BodySidecarValidationScratch *>())));
static_assert(noexcept(fx::physics::TakeFirstWithScratch(
    std::declval<fx::physics::BodySidecar *>(),
    std::declval<fx::physics::BodySidecarValidationScratch *>())));
static_assert(noexcept(fx::physics::PrepareReplacementWithScratch(
    std::declval<const fx::physics::BodySidecar *>(),
    std::declval<fx::physics::BodySidecar *>(),
    std::declval<fx::physics::BodySidecarValidationScratch *>())));
static_assert(noexcept(fx::physics::PublishReplacementWithScratch(
    std::declval<fx::physics::BodySidecar *>(),
    std::declval<fx::physics::BodySidecar *>(),
    std::declval<fx::physics::BodySidecar *>(),
    std::declval<fx::physics::BodySidecarValidationScratch *>())));
static_assert(noexcept(fx::physics::RollbackReplacementWithScratch(
    std::declval<fx::physics::BodySidecar *>(),
    std::declval<fx::physics::BodySidecar *>(),
    std::declval<fx::physics::BodySidecar *>(),
    std::declval<fx::physics::BodySidecarValidationScratch *>())));
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
using fx::physics::BodySidecarSnapshotScratch;
using fx::physics::BodySidecarValidationScratch;
using fx::physics::BodySlot;
using fx::physics::BodyToken;
using fx::physics::IndexedBodyResult;
using fx::physics::OwnershipRecord;
using fx::physics::OwnershipSnapshot;
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

bool SameSidecarShape(
    const BodySidecar *const first,
    const BodySidecar *const second)
{
    if (!first || !second
        || first->ActiveCount() != second->ActiveCount()
        || first->IsInitialized() != second->IsInitialized()
        || fx::physics::SidecarTestAccess::GetRevision(first)
            != fx::physics::SidecarTestAccess::GetRevision(second)
        || fx::physics::SidecarTestAccess::GetTransactionRole(first)
            != fx::physics::SidecarTestAccess::GetTransactionRole(second))
    {
        return false;
    }

    for (std::size_t owner = 0; owner < MAX_ELEMS; ++owner)
    {
        const BodySlot firstSlot =
            fx::physics::SidecarTestAccess::GetSlot(first, owner);
        const BodySlot secondSlot =
            fx::physics::SidecarTestAccess::GetSlot(second, owner);
        if ((firstSlot.body != nullptr) != (secondSlot.body != nullptr)
            || firstSlot.generation != secondSlot.generation)
        {
            return false;
        }
    }
    return true;
}

bool SameOwnershipRecord(
    const OwnershipRecord &first,
    const OwnershipRecord &second)
{
    return first.body == second.body
        && first.token == second.token
        && first.ownerIndex == second.ownerIndex;
}

bool SameOwnershipSnapshot(
    const OwnershipSnapshot &first,
    const OwnershipSnapshot &second)
{
    if (first.count != second.count)
        return false;
    for (std::size_t index = 0; index < first.records.size(); ++index)
    {
        if (!SameOwnershipRecord(
                first.records[index], second.records[index]))
        {
            return false;
        }
    }
    return true;
}

OwnershipSnapshot MakeOwnershipSnapshotSentinel(dxBody *const body)
{
    OwnershipSnapshot snapshot{};
    snapshot.count = 173u;
    for (std::size_t index = 0; index < snapshot.records.size(); ++index)
    {
        snapshot.records[index] = {
            body,
            static_cast<BodyToken>(index + 1u),
            static_cast<std::uint16_t>(index % MAX_ELEMS),
        };
    }
    return snapshot;
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

bool TestScratchWrapperParityAndReuse()
{
    BodySidecarSnapshotScratch scratch{};

    BodySidecar wrapperLifecycle{};
    BodySidecar scratchLifecycle{};
    AllowUndrainedFixture(&wrapperLifecycle, &scratchLifecycle);
    if (fx::physics::ResetEmpty(&wrapperLifecycle)
            != SidecarStatus::Success
        || fx::physics::ResetEmptyWithScratch(
               &scratchLifecycle, &scratch)
            != SidecarStatus::Success
        || !SameSidecarShape(&wrapperLifecycle, &scratchLifecycle))
    {
        return false;
    }

    dxBody wrapperFirst{1u};
    dxBody wrapperSecond{2u};
    dxBody scratchFirst{1u};
    dxBody scratchSecond{2u};
    const TokenResult wrapperFirstBind =
        fx::physics::Bind(&wrapperLifecycle, 5u, &wrapperFirst);
    const TokenResult scratchFirstBind =
        fx::physics::BindWithScratch(
            &scratchLifecycle, 5u, &scratchFirst, &scratch);
    const TokenResult wrapperSecondBind =
        fx::physics::Bind(&wrapperLifecycle, 2u, &wrapperSecond);
    const TokenResult scratchSecondBind =
        fx::physics::BindWithScratch(
            &scratchLifecycle, 2u, &scratchSecond, &scratch);
    if (wrapperFirstBind.status != scratchFirstBind.status
        || wrapperFirstBind.token != scratchFirstBind.token
        || wrapperSecondBind.status != scratchSecondBind.status
        || wrapperSecondBind.token != scratchSecondBind.token
        || !SameSidecarShape(&wrapperLifecycle, &scratchLifecycle))
    {
        return false;
    }

    const BodyResult wrapperTaken = fx::physics::Take(
        &wrapperLifecycle, 2u, wrapperSecondBind.token);
    const BodyResult scratchTaken = fx::physics::TakeWithScratch(
        &scratchLifecycle, 2u, scratchSecondBind.token, &scratch);
    const IndexedBodyResult wrapperFirstTaken =
        fx::physics::TakeFirst(&wrapperLifecycle);
    const IndexedBodyResult scratchFirstTaken =
        fx::physics::TakeFirstWithScratch(&scratchLifecycle, &scratch);
    if (wrapperTaken.status != scratchTaken.status
        || !wrapperTaken.body || !scratchTaken.body
        || wrapperTaken.body->id != scratchTaken.body->id
        || wrapperFirstTaken.status != scratchFirstTaken.status
        || !wrapperFirstTaken.body || !scratchFirstTaken.body
        || wrapperFirstTaken.body->id != scratchFirstTaken.body->id
        || wrapperFirstTaken.ownerIndex != scratchFirstTaken.ownerIndex
        || wrapperFirstTaken.token != scratchFirstTaken.token
        || fx::physics::ResetEmpty(&wrapperLifecycle)
            != SidecarStatus::Success
        || fx::physics::ResetEmptyWithScratch(
               &scratchLifecycle, &scratch)
            != SidecarStatus::Success
        || !SameSidecarShape(&wrapperLifecycle, &scratchLifecycle))
    {
        return false;
    }

    const std::uint64_t lifecycleRevision =
        fx::physics::SidecarTestAccess::GetRevision(&scratchLifecycle);
    dxBody rejectedBody{99u};
    if (fx::physics::ValidateWithScratch(&scratchLifecycle, nullptr)
            != SidecarStatus::InvalidArgument
        || fx::physics::ResetEmptyWithScratch(
               &scratchLifecycle, nullptr)
            != SidecarStatus::InvalidArgument
        || fx::physics::BindWithScratch(
               &scratchLifecycle, 1u, &rejectedBody, nullptr).status
            != SidecarStatus::InvalidArgument
        || fx::physics::TakeWithScratch(
               &scratchLifecycle, 1u, 1u, nullptr).status
            != SidecarStatus::InvalidArgument
        || fx::physics::TakeFirstWithScratch(
               &scratchLifecycle, nullptr).status
            != SidecarStatus::InvalidArgument
        || scratchLifecycle.ActiveCount() != 0u
        || fx::physics::SidecarTestAccess::GetRevision(
               &scratchLifecycle) != lifecycleRevision)
    {
        return false;
    }

    BodySidecar wrapperLive{};
    BodySidecar wrapperStaged{};
    BodySidecar wrapperRollback{};
    BodySidecar wrapperDiscarded{};
    BodySidecar scratchLive{};
    BodySidecar scratchStaged{};
    BodySidecar scratchRollback{};
    BodySidecar scratchDiscarded{};
    BodySidecar scratchUnrelated{};
    AllowUndrainedFixture(
        &wrapperLive,
        &wrapperStaged,
        &wrapperRollback,
        &wrapperDiscarded,
        &scratchLive,
        &scratchStaged,
        &scratchRollback,
        &scratchDiscarded,
        &scratchUnrelated);
    if (!Initialize(&wrapperLive)
        || fx::physics::ResetEmptyWithScratch(&scratchLive, &scratch)
            != SidecarStatus::Success
        || fx::physics::ResetEmptyWithScratch(
               &scratchUnrelated, &scratch)
            != SidecarStatus::Success)
    {
        return false;
    }

    dxBody wrapperOldFirst{10u};
    dxBody wrapperOldSecond{11u};
    dxBody scratchOldFirst{10u};
    dxBody scratchOldSecond{11u};
    const TokenResult wrapperOldFirstBind =
        fx::physics::Bind(&wrapperLive, 1u, &wrapperOldFirst);
    const TokenResult wrapperOldSecondBind =
        fx::physics::Bind(&wrapperLive, 7u, &wrapperOldSecond);
    const TokenResult scratchOldFirstBind = fx::physics::BindWithScratch(
        &scratchLive, 1u, &scratchOldFirst, &scratch);
    const TokenResult scratchOldSecondBind = fx::physics::BindWithScratch(
        &scratchLive, 7u, &scratchOldSecond, &scratch);
    if (!wrapperOldFirstBind || !wrapperOldSecondBind
        || !scratchOldFirstBind || !scratchOldSecondBind
        || !SameSidecarShape(&wrapperLive, &scratchLive))
    {
        return false;
    }

    std::array<BodyToken, MAX_ELEMS> wrapperExpected{};
    std::array<BodyToken, MAX_ELEMS> scratchExpected{};
    wrapperExpected[1] = wrapperOldFirstBind.token;
    wrapperExpected[7] = wrapperOldSecondBind.token;
    scratchExpected[1] = scratchOldFirstBind.token;
    scratchExpected[7] = scratchOldSecondBind.token;
    if (fx::physics::ValidateSemanticOwnership(
            &wrapperLive, wrapperExpected)
            != fx::physics::ValidateSemanticOwnershipWithScratch(
                &scratchLive, scratchExpected, &scratch)
        || fx::physics::PrepareReplacement(
               &wrapperLive, &wrapperStaged)
            != fx::physics::PrepareReplacementWithScratch(
                &scratchLive, &scratchStaged, &scratch))
    {
        return false;
    }

    dxBody wrapperNewFirst{20u};
    dxBody wrapperNewSecond{21u};
    dxBody scratchNewFirst{20u};
    dxBody scratchNewSecond{21u};
    const TokenResult wrapperNewFirstBind =
        fx::physics::Bind(&wrapperStaged, 1u, &wrapperNewFirst);
    const TokenResult wrapperNewSecondBind =
        fx::physics::Bind(&wrapperStaged, 9u, &wrapperNewSecond);
    const TokenResult scratchNewFirstBind = fx::physics::BindWithScratch(
        &scratchStaged, 1u, &scratchNewFirst, &scratch);
    const TokenResult scratchNewSecondBind = fx::physics::BindWithScratch(
        &scratchStaged, 9u, &scratchNewSecond, &scratch);
    if (!wrapperNewFirstBind || !wrapperNewSecondBind
        || !scratchNewFirstBind || !scratchNewSecondBind
        || !SameSidecarShape(&wrapperStaged, &scratchStaged)
        || fx::physics::ValidateDisjointOwnership(
               &wrapperLive, &wrapperStaged)
            != fx::physics::ValidateDisjointOwnershipWithScratch(
                &scratchLive, &scratchStaged, &scratch)
        || fx::physics::ValidateReplacementRelation(
               &wrapperLive, &wrapperStaged)
            != fx::physics::ValidateReplacementRelationWithScratch(
                &scratchLive, &scratchStaged, &scratch))
    {
        return false;
    }

    const std::uint64_t stagedRevision =
        fx::physics::SidecarTestAccess::GetRevision(&scratchStaged);
    if (fx::physics::PublishReplacementWithScratch(
            &scratchUnrelated,
            &scratchStaged,
            &scratchRollback,
            &scratch) != SidecarStatus::TransactionProvenanceMismatch
        || scratchUnrelated.ActiveCount() != 0u
        || scratchStaged.ActiveCount() != 2u
        || scratchRollback.ActiveCount() != 0u
        || fx::physics::SidecarTestAccess::GetRevision(&scratchStaged)
            != stagedRevision)
    {
        return false;
    }

    if (fx::physics::PublishReplacement(
            &wrapperLive, &wrapperStaged, &wrapperRollback)
            != fx::physics::PublishReplacementWithScratch(
                &scratchLive,
                &scratchStaged,
                &scratchRollback,
                &scratch)
        || !SameSidecarShape(&wrapperLive, &scratchLive)
        || !SameSidecarShape(&wrapperStaged, &scratchStaged)
        || !SameSidecarShape(&wrapperRollback, &scratchRollback))
    {
        return false;
    }

    OwnershipSnapshot wrappedSnapshot{};
    OwnershipSnapshot scratchSnapshot{};
    if (fx::physics::SnapshotOwnership(&scratchLive, &wrappedSnapshot)
            != SidecarStatus::Success
        || fx::physics::SnapshotOwnershipWithScratch(
               &scratchLive, &scratchSnapshot, &scratch)
            != SidecarStatus::Success
        || !SameOwnershipSnapshot(wrappedSnapshot, scratchSnapshot)
        || fx::physics::RollbackReplacement(
               &wrapperLive, &wrapperRollback, &wrapperDiscarded)
            != fx::physics::RollbackReplacementWithScratch(
                &scratchLive,
                &scratchRollback,
                &scratchDiscarded,
                &scratch))
    {
        return false;
    }

    return SameSidecarShape(&wrapperLive, &scratchLive)
        && SameSidecarShape(&wrapperRollback, &scratchRollback)
        && SameSidecarShape(&wrapperDiscarded, &scratchDiscarded)
        && fx::physics::ValidateWithScratch(&scratchLive, &scratch)
            == SidecarStatus::Success;
}

bool TestOwnershipSnapshotExactRecords()
{
    BodySidecar sidecar{};
    if (!Initialize(&sidecar))
        return false;

    dxBody lowBody{1u};
    dxBody middleBody{2u};
    dxBody highBody{3u};
    constexpr std::size_t lowOwner = 1u;
    constexpr std::size_t middleOwner = 71u;
    constexpr std::size_t highOwner = MAX_ELEMS - 1u;
    const TokenResult highBind =
        fx::physics::Bind(&sidecar, highOwner, &highBody);
    const TokenResult lowBind =
        fx::physics::Bind(&sidecar, lowOwner, &lowBody);
    const TokenResult middleBind =
        fx::physics::Bind(&sidecar, middleOwner, &middleBody);
    if (!highBind || !lowBind || !middleBind)
        return false;

    const std::uint64_t revisionBefore =
        fx::physics::SidecarTestAccess::GetRevision(&sidecar);
    dxBody sentinelBody{99u};
    OwnershipSnapshot snapshot =
        MakeOwnershipSnapshotSentinel(&sentinelBody);
    if (fx::physics::SnapshotOwnership(&sidecar, &snapshot)
            != SidecarStatus::Success
        || snapshot.count != 3u)
    {
        return false;
    }

    const OwnershipRecord expected[] = {
        {&lowBody, lowBind.token, static_cast<std::uint16_t>(lowOwner)},
        {&middleBody, middleBind.token,
         static_cast<std::uint16_t>(middleOwner)},
        {&highBody, highBind.token, static_cast<std::uint16_t>(highOwner)},
    };
    for (std::size_t index = 0; index < std::size(expected); ++index)
    {
        if (!SameOwnershipRecord(snapshot.records[index], expected[index]))
            return false;
    }
    const OwnershipRecord emptyRecord{};
    for (std::size_t index = std::size(expected);
         index < snapshot.records.size();
         ++index)
    {
        if (!SameOwnershipRecord(snapshot.records[index], emptyRecord))
            return false;
    }

    if (sidecar.ActiveCount() != 3u
        || fx::physics::SidecarTestAccess::GetRevision(&sidecar)
            != revisionBefore
        || !ResolvesTo(&sidecar, lowOwner, lowBind.token, &lowBody)
        || !ResolvesTo(
            &sidecar, middleOwner, middleBind.token, &middleBody)
        || !ResolvesTo(&sidecar, highOwner, highBind.token, &highBody))
    {
        return false;
    }

    BodySidecar empty{};
    if (!Initialize(&empty))
        return false;
    snapshot = MakeOwnershipSnapshotSentinel(&sentinelBody);
    const OwnershipSnapshot expectedEmpty{};
    return fx::physics::SnapshotOwnership(&empty, &snapshot)
            == SidecarStatus::Success
        && SameOwnershipSnapshot(snapshot, expectedEmpty);
}

bool TestOwnershipSnapshotFullCapacity()
{
    BodySidecar sidecar{};
    if (!Initialize(&sidecar))
        return false;

    std::array<dxBody, fx::physics::BODY_LIMIT> bodies{};
    std::array<BodyToken, fx::physics::BODY_LIMIT> tokens{};
    for (std::size_t owner = 0; owner < bodies.size(); ++owner)
    {
        bodies[owner].id = static_cast<std::uint32_t>(owner + 1u);
        const TokenResult bound =
            fx::physics::Bind(&sidecar, owner, &bodies[owner]);
        if (!bound)
            return false;
        tokens[owner] = bound.token;
    }

    const std::uint64_t revisionBefore =
        fx::physics::SidecarTestAccess::GetRevision(&sidecar);
    OwnershipSnapshot snapshot{};
    OwnershipSnapshot scratchSnapshot{};
    BodySidecarSnapshotScratch scratch{};
    std::array<BodyToken, MAX_ELEMS> expectedTokens{};
    for (std::size_t owner = 0; owner < tokens.size(); ++owner)
        expectedTokens[owner] = tokens[owner];
    if (fx::physics::SnapshotOwnership(&sidecar, &snapshot)
            != SidecarStatus::Success
        || fx::physics::SnapshotOwnershipWithScratch(
               &sidecar, &scratchSnapshot, &scratch)
            != SidecarStatus::Success
        || !SameOwnershipSnapshot(snapshot, scratchSnapshot)
        || fx::physics::ValidateWithScratch(&sidecar, &scratch)
            != SidecarStatus::Success
        || fx::physics::ValidateSemanticOwnershipWithScratch(
               &sidecar, expectedTokens, &scratch)
            != SidecarStatus::Success
        || snapshot.count != fx::physics::BODY_LIMIT
        || sidecar.ActiveCount() != fx::physics::BODY_LIMIT
        || fx::physics::SidecarTestAccess::GetRevision(&sidecar)
            != revisionBefore)
    {
        return false;
    }
    for (std::size_t owner = 0; owner < bodies.size(); ++owner)
    {
        const OwnershipRecord &record = snapshot.records[owner];
        if (record.body != &bodies[owner]
            || record.token != tokens[owner]
            || record.ownerIndex != owner)
        {
            return false;
        }
    }
    return true;
}

bool TestOwnershipSnapshotFailuresAreTransactional()
{
    dxBody sentinelBody{99u};
    const OwnershipSnapshot sentinel =
        MakeOwnershipSnapshotSentinel(&sentinelBody);
    OwnershipSnapshot actual = sentinel;
    BodySidecarSnapshotScratch scratch{};
    const auto scratchRejectsWithoutPublishing =
        [&](const BodySidecar *const sidecar,
            const SidecarStatus expectedStatus)
    {
        actual = sentinel;
        return fx::physics::SnapshotOwnershipWithScratch(
                   sidecar, &actual, &scratch) == expectedStatus
            && SameOwnershipSnapshot(actual, sentinel);
    };
    if (fx::physics::SnapshotOwnership(nullptr, &actual)
            != SidecarStatus::InvalidArgument
        || !SameOwnershipSnapshot(actual, sentinel)
        || !scratchRejectsWithoutPublishing(
            nullptr, SidecarStatus::InvalidArgument))
    {
        return false;
    }

    BodySidecar uninitialized{};
    actual = sentinel;
    if (fx::physics::SnapshotOwnership(&uninitialized, &actual)
            != SidecarStatus::Uninitialized
        || !SameOwnershipSnapshot(actual, sentinel)
        || fx::physics::SnapshotOwnership(&uninitialized, nullptr)
            != SidecarStatus::InvalidArgument
        || fx::physics::SnapshotOwnershipWithScratch(
               &uninitialized, nullptr, &scratch)
            != SidecarStatus::InvalidArgument
        || !scratchRejectsWithoutPublishing(
            &uninitialized, SidecarStatus::Uninitialized))
    {
        return false;
    }
    actual = sentinel;
    if (fx::physics::SnapshotOwnershipWithScratch(
            &uninitialized, &actual, nullptr)
            != SidecarStatus::InvalidArgument
        || !SameOwnershipSnapshot(actual, sentinel))
    {
        return false;
    }

    dxBody countBody{1u};
    BodySidecar countCorrupt{};
    if (!Initialize(&countCorrupt)
        || !fx::physics::Bind(&countCorrupt, 1u, &countBody))
    {
        return false;
    }
    fx::physics::SidecarTestAccess::SetActiveCount(&countCorrupt, 2u);
    actual = sentinel;
    if (fx::physics::SnapshotOwnership(&countCorrupt, &actual)
            != SidecarStatus::ActiveCountCorrupt
        || !SameOwnershipSnapshot(actual, sentinel)
        || !scratchRejectsWithoutPublishing(
            &countCorrupt, SidecarStatus::ActiveCountCorrupt))
    {
        return false;
    }

    dxBody generationBody{2u};
    BodySidecar generationCorrupt{};
    if (!Initialize(&generationCorrupt)
        || !fx::physics::Bind(&generationCorrupt, 2u, &generationBody))
    {
        return false;
    }
    fx::physics::SidecarTestAccess::SetSlot(
        &generationCorrupt,
        2u,
        &generationBody,
        fx::physics::INVALID_BODY_TOKEN);
    actual = sentinel;
    if (fx::physics::SnapshotOwnership(&generationCorrupt, &actual)
            != SidecarStatus::CorruptGeneration
        || !SameOwnershipSnapshot(actual, sentinel)
        || !scratchRejectsWithoutPublishing(
            &generationCorrupt, SidecarStatus::CorruptGeneration))
    {
        return false;
    }

    dxBody duplicateBody{3u};
    BodySidecar duplicate{};
    const bool duplicateInitialized = Initialize(&duplicate);
    const TokenResult duplicateBind = duplicateInitialized
        ? fx::physics::Bind(&duplicate, 3u, &duplicateBody)
        : TokenResult{};
    if (!duplicateBind)
        return false;
    fx::physics::SidecarTestAccess::SetSlot(
        &duplicate, 4u, &duplicateBody, duplicateBind.token);
    fx::physics::SidecarTestAccess::SetActiveCount(&duplicate, 2u);
    actual = sentinel;
    if (fx::physics::SnapshotOwnership(&duplicate, &actual)
            != SidecarStatus::DuplicateBody
        || !SameOwnershipSnapshot(actual, sentinel)
        || !scratchRejectsWithoutPublishing(
            &duplicate, SidecarStatus::DuplicateBody))
    {
        return false;
    }

    BodySidecar provenanceCorrupt{};
    BodySidecar peer{};
    if (!Initialize(&provenanceCorrupt) || !Initialize(&peer))
        return false;
    fx::physics::SidecarTestAccess::SetTransactionState(
        &provenanceCorrupt,
        static_cast<TransactionRole>(0xFFu),
        &peer,
        fx::physics::SidecarTestAccess::GetRevision(&peer));
    actual = sentinel;
    return fx::physics::SnapshotOwnership(&provenanceCorrupt, &actual)
            == SidecarStatus::TransactionProvenanceMismatch
        && SameOwnershipSnapshot(actual, sentinel)
        && scratchRejectsWithoutPublishing(
            &provenanceCorrupt,
            SidecarStatus::TransactionProvenanceMismatch);
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

bool TestFullCapacityRetireRollbackAndRebind()
{
    constexpr std::size_t retiredCount = fx::physics::BODY_LIMIT;
    BodySidecar live{};
    BodySidecar staged{};
    BodySidecar rollback{};
    BodySidecar discarded{};
    BodySidecarSnapshotScratch scratch{};
    AllowUndrainedFixture(&live, &staged, &rollback, &discarded);
    if (!Initialize(&live))
        return false;

    std::array<dxBody, fx::physics::BODY_LIMIT> oldBodies{};
    std::array<dxBody, fx::physics::BODY_LIMIT> desiredBodies{};
    std::array<dxBody, retiredCount> reconstructedBodies{};
    std::array<BodyToken, fx::physics::BODY_LIMIT> oldTokens{};
    std::array<BodyToken, fx::physics::BODY_LIMIT> desiredTokens{};
    std::array<BodyToken, retiredCount> reconstructedTokens{};
    std::array<bool, retiredCount> retiredLedger{};
    std::array<bool, fx::physics::BODY_LIMIT> discardedLedger{};
    std::array<bool, retiredCount> reconstructedLedger{};
    for (std::size_t owner = 0; owner < fx::physics::BODY_LIMIT; ++owner)
    {
        oldBodies[owner].id = static_cast<std::uint32_t>(owner + 1u);
        desiredBodies[owner].id =
            static_cast<std::uint32_t>(owner + 1001u);
        const TokenResult bound = fx::physics::BindWithScratch(
            &live, owner, &oldBodies[owner], &scratch);
        if (!bound)
            return false;
        oldTokens[owner] = bound.token;
    }

    // Model the exact archive ordering: Take every selected live owner before
    // PrepareReplacement captures the post-retirement revision/generations.
    for (std::size_t owner = 0; owner < retiredCount; ++owner)
    {
        const BodyResult retired = fx::physics::TakeWithScratch(
            &live, owner, oldTokens[owner], &scratch);
        if (!retired || retired.body != &oldBodies[owner]
            || retiredLedger[owner])
        {
            return false;
        }
        retiredLedger[owner] = true;
    }
    if (live.ActiveCount() != 0u
        || fx::physics::PrepareReplacementWithScratch(
               &live, &staged, &scratch)
            != SidecarStatus::Success)
    {
        return false;
    }
    for (std::size_t owner = 0; owner < fx::physics::BODY_LIMIT; ++owner)
    {
        const TokenResult bound = fx::physics::BindWithScratch(
            &staged, owner, &desiredBodies[owner], &scratch);
        if (!bound)
            return false;
        desiredTokens[owner] = bound.token;
    }
    if (staged.ActiveCount() != fx::physics::BODY_LIMIT
        || fx::physics::PublishReplacementWithScratch(
               &live, &staged, &rollback, &scratch)
            != SidecarStatus::Success
        || rollback.ActiveCount() != 0u)
    {
        return false;
    }
    for (std::size_t owner = 0; owner < fx::physics::BODY_LIMIT; ++owner)
    {
        if (!ResolvesTo(
                &live,
                owner,
                desiredTokens[owner],
                &desiredBodies[owner]))
        {
            return false;
        }
    }
    if (fx::physics::RollbackReplacementWithScratch(
            &live, &rollback, &discarded, &scratch)
            != SidecarStatus::Success
        || live.ActiveCount() != 0u
        || discarded.ActiveCount() != fx::physics::BODY_LIMIT)
    {
        return false;
    }

    // Every desired registration must survive publication and rollback with
    // its exact owner, token, and body identity before cleanup transfers it.
    for (std::size_t owner = 0; owner < fx::physics::BODY_LIMIT; ++owner)
    {
        if (!ResolvesTo(
                &discarded,
                owner,
                desiredTokens[owner],
                &desiredBodies[owner]))
        {
            return false;
        }
    }
    for (std::size_t count = 0;
         count < fx::physics::BODY_LIMIT;
         ++count)
    {
        const IndexedBodyResult taken =
            fx::physics::TakeFirstWithScratch(&discarded, &scratch);
        if (!taken
            || taken.ownerIndex >= fx::physics::BODY_LIMIT
            || discardedLedger[taken.ownerIndex]
            || taken.body != &desiredBodies[taken.ownerIndex]
            || taken.token != desiredTokens[taken.ownerIndex])
        {
            return false;
        }
        discardedLedger[taken.ownerIndex] = true;
    }
    if (discarded.ActiveCount() != 0u
        || fx::physics::ResetEmptyWithScratch(&discarded, &scratch)
            != SidecarStatus::Success
        || fx::physics::ValidateVacantDestination(&discarded)
            != SidecarStatus::Success)
    {
        return false;
    }

    std::array<BodyToken, MAX_ELEMS> expected{};
    for (std::size_t owner = 0; owner < retiredCount; ++owner)
    {
        reconstructedBodies[owner].id =
            static_cast<std::uint32_t>(owner + 2001u);
        const TokenResult rebound = fx::physics::BindWithScratch(
            &live, owner, &reconstructedBodies[owner], &scratch);
        if (!rebound || rebound.token != desiredTokens[owner]
            || fx::physics::Resolve(&live, owner, oldTokens[owner]).status
                != SidecarStatus::StaleToken)
        {
            return false;
        }
        reconstructedTokens[owner] = rebound.token;
        expected[owner] = reconstructedTokens[owner];
    }

    for (std::size_t owner = 0; owner < retiredCount; ++owner)
    {
        if (!retiredLedger[owner]
            || !discardedLedger[owner]
            || !ResolvesTo(
                &live,
                owner,
                reconstructedTokens[owner],
                &reconstructedBodies[owner]))
        {
            return false;
        }
    }
    if (live.ActiveCount() != fx::physics::BODY_LIMIT
        || fx::physics::ValidateSemanticOwnershipWithScratch(
               &live, expected, &scratch)
            != SidecarStatus::Success
        || fx::physics::ValidateVacantDestination(&staged)
            != SidecarStatus::Success
        || fx::physics::ValidateVacantDestination(&rollback)
            != SidecarStatus::Success)
    {
        return false;
    }

    // Mirror the production destruction transfer and prove every restored
    // registration is transferred exactly once with the expected identity.
    for (std::size_t count = 0; count < retiredCount; ++count)
    {
        const IndexedBodyResult taken =
            fx::physics::TakeFirstWithScratch(&live, &scratch);
        if (!taken
            || taken.ownerIndex >= retiredCount
            || reconstructedLedger[taken.ownerIndex]
            || taken.body != &reconstructedBodies[taken.ownerIndex]
            || taken.token != reconstructedTokens[taken.ownerIndex])
        {
            return false;
        }
        reconstructedLedger[taken.ownerIndex] = true;
    }
    for (std::size_t owner = 0; owner < retiredCount; ++owner)
    {
        if (!reconstructedLedger[owner])
            return false;
    }
    return fx::physics::ResetEmptyWithScratch(&live, &scratch)
            == SidecarStatus::Success
        && fx::physics::ResetEmptyWithScratch(&staged, &scratch)
            == SidecarStatus::Success
        && fx::physics::ResetEmptyWithScratch(&rollback, &scratch)
            == SidecarStatus::Success
        && fx::physics::ValidateVacantDestination(&live)
            == SidecarStatus::Success
        && fx::physics::ValidateVacantDestination(&staged)
            == SidecarStatus::Success
        && fx::physics::ValidateVacantDestination(&rollback)
            == SidecarStatus::Success;
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
    if (!TestScratchWrapperParityAndReuse())
        return Fail("caller-owned sidecar scratch parity or reuse");
    if (!TestOwnershipSnapshotExactRecords())
        return Fail("ownership snapshot records or read-only contract");
    if (!TestOwnershipSnapshotFullCapacity())
        return Fail("full-capacity ownership snapshot");
    if (!TestOwnershipSnapshotFailuresAreTransactional())
        return Fail("transactional ownership snapshot failures");
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
    if (!TestFullCapacityRetireRollbackAndRebind())
        return Fail("full-capacity retirement/rollback/rebind sequence");
    return 0;
}
