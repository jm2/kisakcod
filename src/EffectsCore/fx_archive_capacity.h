#pragma once

#include "fx_physics_sidecar.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace fx::archive
{
struct PhysicsResourceCount
{
    std::size_t bodies = 0;
    std::size_t userData = 0;
    std::size_t geoms = 0;
};

struct PhysicsRetirementCandidate
{
    std::size_t entryIndex = physics::BODY_LIMIT;
    std::size_t ownerIndex = MAX_ELEMS;
    std::size_t geomCount = 0;
};

enum class PhysicsRetirementPlanStatus : std::uint8_t
{
    Success,
    InvalidArgument,
    InvalidCandidate,
    InsufficientCapacity,
};

struct PhysicsRetirementPlan
{
    std::array<std::size_t, physics::BODY_LIMIT> entryIndices{};
    std::size_t count = 0;
    PhysicsResourceCount released{};
};

[[nodiscard]] inline bool PhysicsResourceCountCanAdd(
    const std::size_t left,
    const std::size_t right) noexcept
{
    return left <= (std::numeric_limits<std::size_t>::max)() - right;
}

[[nodiscard]] inline std::size_t PhysicsResourceDeficit(
    const std::size_t required,
    const std::size_t available) noexcept
{
    return required > available ? required - available : 0;
}

// Selects the smallest number of old FX-owned bodies whose destruction makes
// the desired replacement fit. Every candidate releases one body slot, one
// user-data slot, and its exact number of geometry slots. Sorting by descending
// geometry cost makes the minimum-count choice deterministic; owner and entry
// indices provide stable tie breaks. The output remains unchanged on failure.
[[nodiscard]] inline PhysicsRetirementPlanStatus
BuildPhysicsRetirementPlan(
    const PhysicsResourceCount &freeCapacity,
    const PhysicsResourceCount &desired,
    const PhysicsRetirementCandidate *const candidates,
    const std::size_t candidateCount,
    PhysicsRetirementPlan *const outPlan) noexcept
{
    if (!outPlan || (candidateCount != 0 && !candidates)
        || candidateCount > physics::BODY_LIMIT
        || desired.bodies > physics::BODY_LIMIT
        || desired.userData > physics::BODY_LIMIT
        || desired.bodies != desired.userData)
    {
        return PhysicsRetirementPlanStatus::InvalidArgument;
    }

    std::array<PhysicsRetirementCandidate, physics::BODY_LIMIT> ordered{};
    PhysicsResourceCount totalRetirable{};
    for (std::size_t index = 0; index < candidateCount; ++index)
    {
        const PhysicsRetirementCandidate &candidate = candidates[index];
        if (candidate.entryIndex >= physics::BODY_LIMIT
            || candidate.ownerIndex >= MAX_ELEMS)
        {
            return PhysicsRetirementPlanStatus::InvalidCandidate;
        }
        for (std::size_t prior = 0; prior < index; ++prior)
        {
            if (candidates[prior].entryIndex == candidate.entryIndex
                || candidates[prior].ownerIndex == candidate.ownerIndex)
            {
                return PhysicsRetirementPlanStatus::InvalidCandidate;
            }
        }
        if (!PhysicsResourceCountCanAdd(
                totalRetirable.bodies, 1)
            || !PhysicsResourceCountCanAdd(
                totalRetirable.userData, 1)
            || !PhysicsResourceCountCanAdd(
                totalRetirable.geoms, candidate.geomCount))
        {
            return PhysicsRetirementPlanStatus::InvalidCandidate;
        }
        ordered[index] = candidate;
        ++totalRetirable.bodies;
        ++totalRetirable.userData;
        totalRetirable.geoms += candidate.geomCount;
    }

    if (!PhysicsResourceCountCanAdd(
            freeCapacity.bodies, totalRetirable.bodies)
        || !PhysicsResourceCountCanAdd(
            freeCapacity.userData, totalRetirable.userData)
        || !PhysicsResourceCountCanAdd(
            freeCapacity.geoms, totalRetirable.geoms))
    {
        return PhysicsRetirementPlanStatus::InvalidArgument;
    }
    if (freeCapacity.bodies + totalRetirable.bodies < desired.bodies
        || freeCapacity.userData + totalRetirable.userData < desired.userData
        || freeCapacity.geoms + totalRetirable.geoms < desired.geoms)
    {
        return PhysicsRetirementPlanStatus::InsufficientCapacity;
    }

    std::sort(
        ordered.begin(),
        ordered.begin() + candidateCount,
        [](const PhysicsRetirementCandidate &left,
           const PhysicsRetirementCandidate &right) noexcept {
            if (left.geomCount != right.geomCount)
                return left.geomCount > right.geomCount;
            if (left.ownerIndex != right.ownerIndex)
                return left.ownerIndex < right.ownerIndex;
            return left.entryIndex < right.entryIndex;
        });

    const std::size_t bodyDeficit = PhysicsResourceDeficit(
        desired.bodies, freeCapacity.bodies);
    const std::size_t userDataDeficit = PhysicsResourceDeficit(
        desired.userData, freeCapacity.userData);
    const std::size_t geomDeficit = PhysicsResourceDeficit(
        desired.geoms, freeCapacity.geoms);
    const std::size_t countDeficit = (std::max)(
        bodyDeficit, userDataDeficit);

    PhysicsRetirementPlan plan{};
    while (plan.count < candidateCount
        && (plan.count < countDeficit
            || plan.released.geoms < geomDeficit))
    {
        const PhysicsRetirementCandidate &candidate = ordered[plan.count];
        plan.entryIndices[plan.count] = candidate.entryIndex;
        ++plan.count;
        ++plan.released.bodies;
        ++plan.released.userData;
        plan.released.geoms += candidate.geomCount;
    }
    if (plan.count < countDeficit || plan.released.geoms < geomDeficit)
        return PhysicsRetirementPlanStatus::InsufficientCapacity;

    *outPlan = plan;
    return PhysicsRetirementPlanStatus::Success;
}
} // namespace fx::archive
