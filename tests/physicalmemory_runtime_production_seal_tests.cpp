#include <universal/physicalmemory.h>
#include <universal/physicalmemory_checked.h>
#include <universal/physicalmemory_runtime.h>

#include <cstddef>
#include <type_traits>

// A production include leaves the complete helper declaration absent. Defining
// the same global name is a positive compile seal: exposing even an opaque
// declaration from physicalmemory.h makes this fixture ill-formed.
class PhysicalMemoryGlobalStateTestAccess final
{
};

static_assert(std::is_empty_v<PhysicalMemoryGlobalStateTestAccess>);
static_assert(std::is_trivially_copyable_v<
    PhysicalMemoryGlobalStateTestAccess>);
static_assert(std::is_standard_layout_v<pmem_runtime::AllocationResult>);
static_assert(std::is_trivially_copyable_v<pmem_runtime::AllocationResult>);
static_assert(std::is_standard_layout_v<pmem_runtime::DiagnosticEntry>);
static_assert(std::is_trivially_copyable_v<pmem_runtime::DiagnosticEntry>);
static_assert(std::is_standard_layout_v<pmem_runtime::DiagnosticSnapshot>);
static_assert(std::is_trivially_copyable_v<pmem_runtime::DiagnosticSnapshot>);
static_assert(sizeof(pmem_runtime::AllocationStatus) == 1);
static_assert(sizeof(pmem_runtime::InitializationPhase) == 1);
static_assert(sizeof(pmem_runtime::InitializationStatus) == 1);
static_assert(sizeof(pmem_runtime::ProcessInitAllocationStatus) == 1);
static_assert(sizeof(pmem_runtime::AllocationReceiptPhase) == 1);
static_assert(sizeof(pmem_runtime::AllocationReceiptStatus) == 1);
static_assert(sizeof(pmem_runtime::DiagnosticEntryKind) == 1);
static_assert(sizeof(pmem_runtime::DiagnosticSnapshotStatus) == 1);
static_assert(sizeof(pmem_runtime::DiagnosticEntry) == 0x18);
static_assert(offsetof(pmem_runtime::DiagnosticEntry, bytes) == 0x0);
static_assert(offsetof(pmem_runtime::DiagnosticEntry, name) == 0x4);
static_assert(offsetof(pmem_runtime::DiagnosticEntry, kind) == 0x17);
static_assert(sizeof(pmem_runtime::DiagnosticSnapshot) == 0x610);
static_assert(offsetof(pmem_runtime::DiagnosticSnapshot, high) == 0x0);
static_assert(offsetof(pmem_runtime::DiagnosticSnapshot, low) == 0x300);
static_assert(offsetof(pmem_runtime::DiagnosticSnapshot, highCount) == 0x600);
static_assert(offsetof(pmem_runtime::DiagnosticSnapshot, lowCount) == 0x604);
static_assert(offsetof(pmem_runtime::DiagnosticSnapshot, freeBytes) == 0x608);
static_assert(offsetof(pmem_runtime::DiagnosticSnapshot, status) == 0x60C);
static_assert(offsetof(pmem_runtime::DiagnosticSnapshot, reserved) == 0x60D);
static_assert(noexcept(pmem_runtime::TryInitialize()));
static_assert(noexcept(pmem_runtime::TryAllocate(1, 1, 0, 0)));
static_assert(noexcept(pmem_runtime::TryCaptureDiagnosticSnapshot()));
static_assert(noexcept(pmem_runtime::StorageIsOutsideManagedMemory(
    nullptr, 0)));
static_assert(std::is_same_v<
    decltype(pmem_runtime::StorageIsOutsideManagedMemory(nullptr, 0)),
    bool>);
static_assert(noexcept(pmem_runtime::TryBeginAllocationReceipt(
    nullptr, 0, nullptr)));
static_assert(noexcept(pmem_runtime::TryEndAllocationReceipt(nullptr)));
static_assert(noexcept(pmem_runtime::TryFreeAllocationReceipt(nullptr)));
static_assert(noexcept(pmem_runtime::TryAuthenticateAllocationReceipt(
    nullptr, 0, pmem_runtime::AllocationReceiptPhase::Pristine)));
static_assert(noexcept(pmem_runtime::TryAuthenticateAllocationRange(
    nullptr, 0, nullptr, 0,
    pmem_runtime::AllocationReceiptPhase::Begun)));
static_assert(std::is_same_v<
    decltype(pmem_runtime::TryBeginAllocationReceipt(nullptr, 0, nullptr)),
    pmem_runtime::AllocationReceiptStatus>);
static_assert(std::is_same_v<
    decltype(pmem_runtime::TryAuthenticateAllocationRange(
        nullptr, 0, nullptr, 0,
        pmem_runtime::AllocationReceiptPhase::Begun)),
    pmem_runtime::AllocationReceiptStatus>);
static_assert(noexcept(pmem_runtime::TryBeginProcessInitAllocation()));
static_assert(noexcept(pmem_runtime::TryEndProcessInitAllocation()));

int main()
{
    return 0;
}
