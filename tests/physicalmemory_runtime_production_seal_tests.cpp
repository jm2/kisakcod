#include <universal/physicalmemory.h>

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

int main()
{
    return 0;
}
