#include "EffectsCore/fx_physics_sidecar.h"

// Production includes intentionally do not opt in to
// KISAK_FX_PHYSICS_SIDECAR_TESTING. Defining the test helper's public name in
// another translation unit must therefore confer no access to BodySidecar's
// private ownership state.
namespace fx::physics
{
struct SidecarTestAccess
{
    static void Mutate(BodySidecar *const sidecar) noexcept
    {
        sidecar->activeCount_ = 1u;
        sidecar->initialized_ = true;
    }
};
} // namespace fx::physics
