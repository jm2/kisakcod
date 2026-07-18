#include "EffectsCore/fx_physics_sidecar.h"

// Production includes intentionally do not opt in to
// KISAK_FX_PHYSICS_SIDECAR_TESTING. Recreating the test helper's public name
// must therefore confer no access to BodySidecar's private ownership state.
// Dependent requires-expressions turn access denial into a positive compile
// contract: restoring the old friendship makes either static_assert fail.
namespace fx::physics
{
struct SidecarTestAccess
{
    template <typename Sidecar>
    static constexpr bool CanMutateActiveCount = requires(
        Sidecar *const sidecar)
    {
        sidecar->activeCount_ = 1u;
    };

    template <typename Sidecar>
    static constexpr bool CanMutateInitialized = requires(
        Sidecar *const sidecar)
    {
        sidecar->initialized_ = true;
    };
};

static_assert(
    !SidecarTestAccess::CanMutateActiveCount<BodySidecar>,
    "production must not grant same-name access to private ownership state");
static_assert(
    !SidecarTestAccess::CanMutateInitialized<BodySidecar>,
    "production must not grant same-name access to private lifecycle state");
} // namespace fx::physics

int main()
{
    return 0;
}
