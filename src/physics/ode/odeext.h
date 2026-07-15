#pragma once

// LWSS ADD - Custom COD4
#include <physics/ode/collision_kernel.h>
#include <universal/pool_allocator.h>

#include "joint.h"

#define ODE_GEOM_POOL_COUNT 2048

dxUserGeom *__cdecl Phys_GetWorldGeom();
void __cdecl ODE_LeakCheck();

bool __cdecl ODE_Init();

enum class odebodycleanupstatus_t : std::uint8_t
{
    Success,
    InvalidArgument,
    GeometryCleanupFailed,
    BodyPoolStateInvalid,
};

// Caller holds CRITSECT_PHYSICS for these operations (the lower layer cannot
// acquire it without making rollback paths recursively lock). They preserve
// normal ODE body-list and joint-detach semantics for KisakCOD's fixed
// hinge/ball/AMotor stores and contact groups, but never report allocator or
// topology failures. Other ODE in-place joint stores are deliberately rejected
// by the silent preflight.
[[nodiscard]] poolmutationstatus_t ODE_TryBodyCreateNoReport(
    dxWorld *world,
    dxBody **outBody) noexcept;
[[nodiscard]] bool ODE_TryValidateBodyDestroyNoReport(
    dxBody *body) noexcept;
[[nodiscard]] odebodycleanupstatus_t ODE_TryBodyDestroyNoReport(
    dxBody *body) noexcept;

// These descriptors carry the out-of-line ownership controls paired with the
// ABI-frozen ODE pool metadata. All production users must share these exact
// descriptors rather than rebuilding an extent without its control.
poolstorage_t ODE_BodyPoolStorage() noexcept;
poolstorage_t ODE_GeomPoolStorage() noexcept;

struct odeGlob_t // sizeof=0x2C64E0
{                                       // ...
    dxWorld world[3];
    dxSimpleSpace space[3];             // ...
    dxJointGroup contactsGroup[3];      // ...
    // padding byte
    // padding byte
    // padding byte
    // padding byte
    // padding byte
    // padding byte
    // padding byte
    // padding byte
    dxBody bodies[512];                 // ...
    pooldata_t bodyPool;                // ...
    //unsigned int geoms[425984];      // ...
    alignas(dxGeomTransform)
        char geoms[sizeof(dxGeomTransform) * ODE_GEOM_POOL_COUNT];
    pooldata_t geomPool;                // ...
    dxUserGeom worldGeom;               // ...
    // padding byte
    // padding byte
    // padding byte
    // padding byte
};

extern odeGlob_t odeGlob;

// LWSS END
