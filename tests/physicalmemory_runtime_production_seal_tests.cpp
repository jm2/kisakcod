#include <universal/physicalmemory.h>
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

int main()
{
    return 0;
}
