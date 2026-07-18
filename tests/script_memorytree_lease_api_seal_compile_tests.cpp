#include <script/scr_string_transaction.h>

#include <cstdint>
#include <type_traits>

namespace
{
template <typename Lease>
concept HasUngatedFinish = requires(Lease *lease)
{
    MT_FinishValidationLease(lease);
};

template <typename Lease>
concept HasUngatedAllocation = requires(Lease &lease, std::uint16_t *outIndex)
{
    MT_TryAllocIndexLeased(lease, 1, 1, outIndex);
};

template <typename Lease>
concept HasUngatedQuery = requires(Lease &lease, MT_AllocationInfo *outInfo)
{
    MT_TryGetAllocationInfoLeased(lease, 1, outInfo);
};

template <typename Lease>
concept HasUngatedFree = requires(Lease &lease)
{
    MT_TryFreeIndexLeased(lease, 1, 1);
};

static_assert(!std::is_default_constructible_v<MT_ValidationLeaseAdmission>);
static_assert(!std::is_copy_constructible_v<MT_ValidationLeaseAdmission>);
static_assert(!std::is_move_constructible_v<MT_ValidationLeaseAdmission>);
static_assert(!std::is_trivially_copyable_v<MT_ValidationLeaseAdmission>);
static_assert(!std::is_default_constructible_v<MT_ValidationLease>);
static_assert(!std::is_destructible_v<MT_ValidationLease>);
static_assert(!HasUngatedFinish<MT_ValidationLease>);
static_assert(!HasUngatedAllocation<MT_ValidationLease>);
static_assert(!HasUngatedQuery<MT_ValidationLease>);
static_assert(!HasUngatedFree<MT_ValidationLease>);
static_assert(std::is_default_constructible_v<script_string::OwnershipBatch>);
static_assert(std::is_destructible_v<script_string::OwnershipBatch>);
static_assert(std::is_standard_layout_v<script_string::OwnershipBatch>);
} // namespace
