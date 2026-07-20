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
static_assert(sizeof(pmem_runtime::AllocationStatus) == 1);
static_assert(sizeof(pmem_runtime::InitializationPhase) == 1);
static_assert(sizeof(pmem_runtime::InitializationStatus) == 1);
static_assert(noexcept(pmem_runtime::TryInitialize()));
static_assert(noexcept(pmem_runtime::TryAllocate(1, 1, 0, 0)));

int main()
{
    return 0;
}
