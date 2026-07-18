#include <script/scr_string_transaction.h>

#include <cstdint>
#include <type_traits>

namespace
{
template <typename Lease>
concept HasUngatedBegin = requires(Lease *lease)
{
	MT_TryBeginValidationLease(lease);
};

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

template <typename Admission>
concept HasCanonicalTestingAdmission = requires
{
    Admission::ForTesting();
};

template <typename Admission>
concept HasInvalidTestingAdmission = requires
{
    Admission::InvalidForTesting();
};

template <typename Lease>
concept HasLeaseAuthenticationSetter = requires(Lease &lease)
{
    lease.SetAuthenticationFieldsForTesting(1, 0, 0);
};

template <typename Lease>
concept HasLeaseMutationSetter = requires(Lease &lease)
{
    lease.SetMutationCountForTesting(1);
};

template <typename Lease>
concept HasLeaseCanonicalInspection = requires(Lease &lease)
{
    lease.isCanonicalClearNoLock();
};

template <typename Lease>
concept HasLeaseActivationAuthority = requires(Lease &lease)
{
    lease.activateNoLock(1);
};

template <typename Lease>
concept HasLeasePoisonAuthority = requires(Lease &lease)
{
    lease.poisonNoLock();
};

template <typename Lease>
concept HasLeaseClearAuthority = requires(Lease &lease)
{
    lease.clearNoLock();
};

template <typename Batch>
concept HasBatchAuthenticationSetter = requires(Batch &batch)
{
    batch.SetAuthenticationFieldsForTesting(1, 0, 0);
};

template <typename Batch>
concept HasBatchLeaseAccessor = requires(Batch &batch)
{
    batch.MemoryTreeLeaseForTesting();
};

template <typename Batch>
concept HasBatchActivationSetter = requires(Batch &batch)
{
    batch.ActivateForTesting(1);
};

template <typename Batch>
concept HasBatchOperationSetter = requires(Batch &batch)
{
    batch.SetOperationCountForTesting(1);
};

template <typename Batch>
concept HasBatchMemoryMutationSetter = requires(Batch &batch)
{
    batch.SetMemoryTreeMutationCountForTesting(1);
};

static_assert(!std::is_default_constructible_v<MT_ValidationLeaseAdmission>);
static_assert(!std::is_copy_constructible_v<MT_ValidationLeaseAdmission>);
static_assert(!std::is_move_constructible_v<MT_ValidationLeaseAdmission>);
static_assert(!std::is_trivially_copyable_v<MT_ValidationLeaseAdmission>);
static_assert(!std::is_default_constructible_v<MT_ValidationLease>);
static_assert(!std::is_destructible_v<MT_ValidationLease>);
static_assert(!HasUngatedBegin<MT_ValidationLease>);
static_assert(!HasUngatedFinish<MT_ValidationLease>);
static_assert(!HasUngatedAllocation<MT_ValidationLease>);
static_assert(!HasUngatedQuery<MT_ValidationLease>);
static_assert(!HasUngatedFree<MT_ValidationLease>);
static_assert(!HasCanonicalTestingAdmission<MT_ValidationLeaseAdmission>);
static_assert(!HasInvalidTestingAdmission<MT_ValidationLeaseAdmission>);
static_assert(!HasLeaseAuthenticationSetter<MT_ValidationLease>);
static_assert(!HasLeaseMutationSetter<MT_ValidationLease>);
static_assert(!HasLeaseCanonicalInspection<MT_ValidationLease>);
static_assert(!HasLeaseActivationAuthority<MT_ValidationLease>);
static_assert(!HasLeasePoisonAuthority<MT_ValidationLease>);
static_assert(!HasLeaseClearAuthority<MT_ValidationLease>);
static_assert(std::is_default_constructible_v<script_string::OwnershipBatch>);
static_assert(std::is_destructible_v<script_string::OwnershipBatch>);
static_assert(std::is_standard_layout_v<script_string::OwnershipBatch>);
static_assert(
    !HasBatchAuthenticationSetter<script_string::OwnershipBatch>);
static_assert(!HasBatchLeaseAccessor<script_string::OwnershipBatch>);
static_assert(!HasBatchActivationSetter<script_string::OwnershipBatch>);
static_assert(!HasBatchOperationSetter<script_string::OwnershipBatch>);
static_assert(
    !HasBatchMemoryMutationSetter<script_string::OwnershipBatch>);
} // namespace
