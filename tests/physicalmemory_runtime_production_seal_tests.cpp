#include <universal/physicalmemory.h>

#include <type_traits>

namespace
{
using TestAccess = PhysicalMemoryGlobalStateTestAccess;

template <typename Access>
concept CanNameSnapshot = requires
{
    typename Access::Snapshot;
};

template <typename Access>
concept CanCaptureGlobalState = requires
{
    Access::Capture();
};

template <typename Access>
concept CanInstallCapturedGlobalState = requires
{
    Access::Install(Access::Capture());
};

static_assert(!std::is_constructible_v<TestAccess>);
static_assert(!CanNameSnapshot<TestAccess>);
static_assert(!CanCaptureGlobalState<TestAccess>);
static_assert(!CanInstallCapturedGlobalState<TestAccess>);
} // namespace

int main()
{
    return 0;
}
