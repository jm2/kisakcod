#include "EffectsCore/fx_archive_capacity.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <limits>

namespace
{
int failures = 0;

using PhysicsRetirementPlan = fx::archive::PhysicsRetirementPlan;
using PhysicsRetirementPlanStatus =
    fx::archive::PhysicsRetirementPlanStatus;

void Check(const bool condition, const char *const message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

PhysicsRetirementPlan MakeSentinelPlan()
{
    PhysicsRetirementPlan plan{};
    for (std::size_t index = 0; index < plan.entryIndices.size(); ++index)
    {
        plan.entryIndices[index] =
            (std::numeric_limits<std::size_t>::max)() - index;
    }
    plan.count = 91;
    plan.released = {7, 8, 9};
    return plan;
}

bool PlansEqual(
    const PhysicsRetirementPlan &left,
    const PhysicsRetirementPlan &right)
{
    return left.entryIndices == right.entryIndices
        && left.count == right.count
        && left.released.bodies == right.released.bodies
        && left.released.userData == right.released.userData
        && left.released.geoms == right.released.geoms;
}

void CheckTransactionalFailure(
    const PhysicsRetirementPlanStatus status,
    const PhysicsRetirementPlanStatus expectedStatus,
    const PhysicsRetirementPlan &plan,
    const PhysicsRetirementPlan &sentinel,
    const char *const statusMessage,
    const char *const transactionMessage)
{
    Check(status == expectedStatus, statusMessage);
    Check(PlansEqual(plan, sentinel), transactionMessage);
}

void TestNoRetirementNeeded()
{
    fx::archive::PhysicsRetirementPlan plan{};
    plan.count = 17;
    const auto status = fx::archive::BuildPhysicsRetirementPlan(
        {12, 12, 24}, {12, 12, 24}, nullptr, 0, &plan);
    Check(
        status == fx::archive::PhysicsRetirementPlanStatus::Success,
        "sufficient free capacity should produce a plan");
    Check(plan.count == 0, "sufficient capacity should retire nothing");
}

void TestFullPoolReplacement()
{
    fx::archive::PhysicsRetirementCandidate candidates[12]{};
    for (std::size_t index = 0; index < 12; ++index)
    {
        candidates[index] = {index, 100 + index, 2};
    }
    fx::archive::PhysicsRetirementPlan plan{};
    const auto status = fx::archive::BuildPhysicsRetirementPlan(
        {0, 0, 0}, {12, 12, 24}, candidates, 12, &plan);
    Check(
        status == fx::archive::PhysicsRetirementPlanStatus::Success,
        "500 non-FX plus 12 old FX bodies should permit 12-body replacement");
    Check(plan.count == 12, "a full body pool must retire all 12 old bodies");
    Check(
        plan.released.bodies == 12 && plan.released.userData == 12
            && plan.released.geoms == 24,
        "full replacement must account for every released resource");
}

void TestInsufficientFinalCapacityIsTransactional()
{
    const fx::archive::PhysicsRetirementCandidate candidates[] = {
        {0, 10, 1},
        {1, 11, 1},
    };
    const PhysicsRetirementPlan sentinel = MakeSentinelPlan();
    PhysicsRetirementPlan plan = sentinel;
    const auto status = fx::archive::BuildPhysicsRetirementPlan(
        {0, 0, 0}, {3, 3, 3}, candidates, 2, &plan);
    CheckTransactionalFailure(
        status,
        PhysicsRetirementPlanStatus::InsufficientCapacity,
        plan,
        sentinel,
        "replacement larger than free plus owned capacity must fail",
        "insufficient capacity must preserve every plan output field");
}

void TestGeometryChoosesMinimumDeterministically()
{
    const fx::archive::PhysicsRetirementCandidate candidates[] = {
        {4, 40, 2},
        {2, 20, 5},
        {3, 10, 5},
        {1, 30, 1},
    };
    fx::archive::PhysicsRetirementPlan plan{};
    const auto status = fx::archive::BuildPhysicsRetirementPlan(
        {4, 4, 0}, {4, 4, 9}, candidates, 4, &plan);
    Check(
        status == fx::archive::PhysicsRetirementPlanStatus::Success,
        "geometry deficit should be satisfiable independently of body count");
    Check(plan.count == 2, "two five-geom bodies are the minimum retirement");
    Check(
        plan.entryIndices[0] == 3 && plan.entryIndices[1] == 2,
        "equal geom candidates must be ordered by owner index");
}

void TestBodyDeficitStillPrefersExpensiveGeoms()
{
    const fx::archive::PhysicsRetirementCandidate candidates[] = {
        {0, 4, 1},
        {1, 3, 8},
        {2, 2, 3},
    };
    fx::archive::PhysicsRetirementPlan plan{};
    const auto status = fx::archive::BuildPhysicsRetirementPlan(
        {1, 1, 100}, {3, 3, 0}, candidates, 3, &plan);
    Check(
        status == fx::archive::PhysicsRetirementPlanStatus::Success,
        "body deficit should be satisfiable");
    Check(plan.count == 2, "two missing body slots require two retirements");
    Check(
        plan.entryIndices[0] == 1 && plan.entryIndices[1] == 2,
        "body retirement should preserve the high-geom deterministic order");
}

void TestAsymmetricFreeCountsUseTheLargerDeficit()
{
    const fx::archive::PhysicsRetirementCandidate candidates[] = {
        {0, 0, 0},
        {1, 1, 0},
        {2, 2, 0},
    };
    fx::archive::PhysicsRetirementPlan plan{};
    const auto status = fx::archive::BuildPhysicsRetirementPlan(
        {1, 3, 0}, {3, 3, 0}, candidates, 3, &plan);
    Check(
        status == fx::archive::PhysicsRetirementPlanStatus::Success,
        "asymmetric body and user-data capacity should be plannable");
    Check(
        plan.count == 2 && plan.released.bodies == 2
            && plan.released.userData == 2,
        "the larger body/user-data deficit must determine retirement count");
}

void TestMalformedCandidatesAreRejected()
{
    const PhysicsRetirementPlan sentinel = MakeSentinelPlan();
    PhysicsRetirementPlan plan = sentinel;

    const fx::archive::PhysicsRetirementCandidate invalidEntry{
        fx::physics::BODY_LIMIT, 0, 0};
    auto status = fx::archive::BuildPhysicsRetirementPlan(
        {0, 0, 0}, {0, 0, 0}, &invalidEntry, 1, &plan);
    CheckTransactionalFailure(
        status,
        PhysicsRetirementPlanStatus::InvalidCandidate,
        plan,
        sentinel,
        "entry indices at the global body limit must be rejected",
        "invalid entry bounds must preserve every plan output field");

    plan = sentinel;
    const fx::archive::PhysicsRetirementCandidate invalidOwner{
        0, MAX_ELEMS, 0};
    status = fx::archive::BuildPhysicsRetirementPlan(
        {0, 0, 0}, {0, 0, 0}, &invalidOwner, 1, &plan);
    CheckTransactionalFailure(
        status,
        PhysicsRetirementPlanStatus::InvalidCandidate,
        plan,
        sentinel,
        "owner indices at the FX element limit must be rejected",
        "invalid owner bounds must preserve every plan output field");

    const fx::archive::PhysicsRetirementCandidate duplicateOwners[] = {
        {0, 7, 1},
        {1, 7, 2},
    };
    plan = sentinel;
    status = fx::archive::BuildPhysicsRetirementPlan(
        {0, 0, 0}, {1, 1, 1}, duplicateOwners, 2, &plan);
    CheckTransactionalFailure(
        status,
        PhysicsRetirementPlanStatus::InvalidCandidate,
        plan,
        sentinel,
        "duplicate owners must be rejected",
        "duplicate owners must preserve every plan output field");

    const fx::archive::PhysicsRetirementCandidate duplicateEntries[] = {
        {3, 7, 1},
        {3, 8, 2},
    };
    plan = sentinel;
    status = fx::archive::BuildPhysicsRetirementPlan(
        {0, 0, 0}, {1, 1, 1}, duplicateEntries, 2, &plan);
    CheckTransactionalFailure(
        status,
        PhysicsRetirementPlanStatus::InvalidCandidate,
        plan,
        sentinel,
        "duplicate entry indices must be rejected",
        "duplicate entries must preserve every plan output field");

    const fx::archive::PhysicsRetirementCandidate overflowing[] = {
        {0, 0, (std::numeric_limits<std::size_t>::max)()},
        {1, 1, 1},
    };
    plan = sentinel;
    status = fx::archive::BuildPhysicsRetirementPlan(
        {0, 0, 0}, {1, 1, 1}, overflowing, 2, &plan);
    CheckTransactionalFailure(
        status,
        PhysicsRetirementPlanStatus::InvalidCandidate,
        plan,
        sentinel,
        "candidate resource overflow must be rejected",
        "candidate overflow must preserve every plan output field");
}

void TestInvalidArgumentsAndCapacityOverflowAreTransactional()
{
    const fx::archive::PhysicsRetirementCandidate candidate{0, 0, 1};
    const PhysicsRetirementPlan sentinel = MakeSentinelPlan();
    PhysicsRetirementPlan plan = sentinel;

    auto status = fx::archive::BuildPhysicsRetirementPlan(
        {0, 0, 0}, {1, 1, 1}, nullptr, 1, &plan);
    CheckTransactionalFailure(
        status,
        PhysicsRetirementPlanStatus::InvalidArgument,
        plan,
        sentinel,
        "nonzero candidate count requires candidate storage",
        "missing candidate storage must preserve every plan output field");

    plan = sentinel;
    status = fx::archive::BuildPhysicsRetirementPlan(
        {0, 0, 0}, {1, 2, 1}, &candidate, 1, &plan);
    CheckTransactionalFailure(
        status,
        PhysicsRetirementPlanStatus::InvalidArgument,
        plan,
        sentinel,
        "desired bodies and user-data records must remain paired",
        "mismatched desired counts must preserve every plan output field");

    plan = sentinel;
    status = fx::archive::BuildPhysicsRetirementPlan(
        {0, 0, 0},
        {fx::physics::BODY_LIMIT + 1, fx::physics::BODY_LIMIT + 1, 0},
        &candidate,
        1,
        &plan);
    CheckTransactionalFailure(
        status,
        PhysicsRetirementPlanStatus::InvalidArgument,
        plan,
        sentinel,
        "desired body count must respect the global limit",
        "oversized desired counts must preserve every plan output field");

    plan = sentinel;
    status = fx::archive::BuildPhysicsRetirementPlan(
        {0, 0, 0},
        {0, 0, 0},
        &candidate,
        fx::physics::BODY_LIMIT + 1,
        &plan);
    CheckTransactionalFailure(
        status,
        PhysicsRetirementPlanStatus::InvalidArgument,
        plan,
        sentinel,
        "candidate count must respect the global limit",
        "oversized candidate counts must preserve every plan output field");

    const std::size_t maximum =
        (std::numeric_limits<std::size_t>::max)();
    plan = sentinel;
    status = fx::archive::BuildPhysicsRetirementPlan(
        {maximum, 0, 0}, {0, 0, 0}, &candidate, 1, &plan);
    CheckTransactionalFailure(
        status,
        PhysicsRetirementPlanStatus::InvalidArgument,
        plan,
        sentinel,
        "free plus retirable body capacity overflow must be rejected",
        "body-capacity overflow must preserve every plan output field");

    plan = sentinel;
    status = fx::archive::BuildPhysicsRetirementPlan(
        {0, maximum, 0}, {0, 0, 0}, &candidate, 1, &plan);
    CheckTransactionalFailure(
        status,
        PhysicsRetirementPlanStatus::InvalidArgument,
        plan,
        sentinel,
        "free plus retirable user-data capacity overflow must be rejected",
        "user-data overflow must preserve every plan output field");

    plan = sentinel;
    status = fx::archive::BuildPhysicsRetirementPlan(
        {0, 0, maximum}, {0, 0, 0}, &candidate, 1, &plan);
    CheckTransactionalFailure(
        status,
        PhysicsRetirementPlanStatus::InvalidArgument,
        plan,
        sentinel,
        "free plus retirable geom capacity overflow must be rejected",
        "geom-capacity overflow must preserve every plan output field");

    Check(
        fx::archive::BuildPhysicsRetirementPlan(
            {0, 0, 0}, {0, 0, 0}, nullptr, 0, nullptr)
            == PhysicsRetirementPlanStatus::InvalidArgument,
        "output storage is mandatory");
}

void TestGlobalBodyLimitBoundary()
{
    constexpr std::size_t limit = fx::physics::BODY_LIMIT;
    std::array<fx::archive::PhysicsRetirementCandidate,
               fx::physics::BODY_LIMIT>
        candidates{};
    for (std::size_t index = 0; index < candidates.size(); ++index)
        candidates[index] = {index, limit - 1 - index, 1};

    fx::archive::PhysicsRetirementPlan plan{};
    const auto status = fx::archive::BuildPhysicsRetirementPlan(
        {0, 0, 0},
        {limit, limit, limit},
        candidates.data(),
        candidates.size(),
        &plan);
    Check(
        status == PhysicsRetirementPlanStatus::Success,
        "the exact 512-body boundary must remain representable");
    Check(
        plan.count == limit,
        "a completely full pool requires 512 retirements");
    bool orderValid = true;
    for (std::size_t index = 0; index < limit; ++index)
    {
        if (plan.entryIndices[index] != limit - 1 - index)
        {
            orderValid = false;
            break;
        }
    }
    Check(
        orderValid,
        "all 512 candidates must retain deterministic owner ordering");
    Check(
        plan.released.bodies == limit
            && plan.released.userData == limit
            && plan.released.geoms == limit,
        "the 512-entry plan must report every released resource");
}
} // namespace

int main()
{
    TestNoRetirementNeeded();
    TestFullPoolReplacement();
    TestInsufficientFinalCapacityIsTransactional();
    TestGeometryChoosesMinimumDeterministically();
    TestBodyDeficitStillPrefersExpensiveGeoms();
    TestAsymmetricFreeCountsUseTheLargerDeficit();
    TestMalformedCandidatesAreRejected();
    TestInvalidArgumentsAndCapacityOverflowAreTransactional();
    TestGlobalBodyLimitBoundary();

    if (failures != 0)
    {
        std::fprintf(stderr, "%d capacity planner test(s) failed\n", failures);
        return 1;
    }
    std::puts("FX archive capacity planner tests passed");
    return 0;
}
