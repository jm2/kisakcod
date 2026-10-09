// fx_update_util_tests.cpp: the production per-element update helpers
// (EffectsCore/fx_update_util.cpp) at native width: spawn origins and element
// orientations from an FxSpatialFrame, velocity from converted FxElemVelStateSamples,
// frustum culling, and the FxSystem visibility-blocker double buffer. The engine's
// MyAssertHandler aborts here (com_math_test_stubs.cpp), so an engine assert
// fails the test.

#include <EffectsCore/fx_system.h>
#include <EffectsCore/fx_visibility_atomic.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>

namespace
{
int g_failures = 0;
int g_warnings = 0;

void Check(const bool ok, const char *const what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

bool Near(const double a, const double b)
{
    return std::fabs(a - b) < 1e-4;
}

bool NearVec(const float *const a, const float x, const float y, const float z)
{
    return Near(a[0], x) && Near(a[1], y) && Near(a[2], z);
}

double Dot(const float *const a, const float *const b)
{
    return static_cast<double>(a[0]) * b[0] + static_cast<double>(a[1]) * b[1]
        + static_cast<double>(a[2]) * b[2];
}

bool Orthonormal(const float (*const axis)[3])
{
    return Near(Dot(axis[0], axis[0]), 1.0) && Near(Dot(axis[1], axis[1]), 1.0) && Near(Dot(axis[2], axis[2]), 1.0)
        && Near(Dot(axis[0], axis[1]), 0.0) && Near(Dot(axis[0], axis[2]), 0.0) && Near(Dot(axis[1], axis[2]), 0.0);
}

// A frame at (10, 20, 30) turned 90 degrees about z.
FxSpatialFrame YawedFrame()
{
    const float half = std::sqrt(0.5f);
    return FxSpatialFrame{{0.0f, 0.0f, half, half}, {10.0f, 20.0f, 30.0f}};
}

void TestOrientation()
{
    const FxSpatialFrame spawn = YawedFrame();
    const FxSpatialFrame now{{0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 2.0f, 3.0f}};
    FxElemDef elemDef{};
    orientation_t orient{};

    FX_GetOrientation(&elemDef, &spawn, &now, 0, &orient);
    Check(NearVec(orient.origin, 0, 0, 0) && NearVec(orient.axis[0], 1, 0, 0) && NearVec(orient.axis[2], 0, 0, 1),
        "run relative to world is the identity");

    elemDef.flags = 0x40; // relative to the spawn frame
    FX_GetOrientation(&elemDef, &spawn, &now, 0, &orient);
    Check(NearVec(orient.origin, 10, 20, 30) && Orthonormal(orient.axis) && Near(orient.axis[0][2], 0.0)
            && Near(orient.axis[2][2], 1.0),
        "run relative to spawn takes the spawn frame");
    const float local[3] = {1.0f, 2.0f, 3.0f};
    float world[3] = {};
    float back[3] = {};
    FX_OrientationPosToWorldPos(&orient, local, world);
    FX_OrientationPosFromWorldPos(&orient, world, back);
    Check(NearVec(back, 1, 2, 3) && Near(world[2], 33.0), "local/world round trip");

    elemDef.flags = 0x80; // relative to the current frame
    FX_GetOrientation(&elemDef, &spawn, &now, 0, &orient);
    Check(NearVec(orient.origin, 1, 2, 3) && NearVec(orient.axis[1], 0, 1, 0), "run relative to effect takes now");

    // Relative to the spawn offset: axis 0 points along the offset.
    for (const int32_t offsetShape : {0x10, 0x20})
    {
        elemDef.flags = 0xC0 | offsetShape;
        elemDef.spawnOffsetRadius.base = 5.0f;
        for (int32_t seed = 0; seed < 64; seed += 7)
        {
            FX_GetOrientation(&elemDef, &spawn, &now, seed, &orient);
            const float offset[3] = {orient.origin[0] - 10.0f, orient.origin[1] - 20.0f, orient.origin[2] - 30.0f};
            Check(Orthonormal(orient.axis) && Near(Dot(offset, orient.axis[0]), 5.0),
                "run relative to offset faces the offset");
        }
    }
}

void TestTrailOrigin()
{
    const FxSpatialFrame frame = YawedFrame();
    FxElemDef elemDef{};
    elemDef.spawnOrigin[0].base = 1.0f;
    elemDef.spawnOrigin[2].base = -2.0f;
    float origin[3] = {};
    float right[3] = {};
    float up[3] = {};
    FX_GetOriginForTrailElem(nullptr, &elemDef, &frame, 0, origin, right, up);
    Check(NearVec(origin, 11, 20, 28), "world-space spawn origin");
    Check(Near(Dot(right, right), 1.0) && Near(Dot(up, up), 1.0) && NearVec(up, 0, 0, 1), "trail right/up axes");

    elemDef.flags = 2; // the spawn origin is in the frame's space
    FX_GetOriginForTrailElem(nullptr, &elemDef, &frame, 0, origin, right, up);
    Check(Near(origin[2], 28.0) && Near(std::hypot(origin[0] - 10.0f, origin[1] - 20.0f), 1.0),
        "local-space spawn origin turns with the frame");
}

void TestVelocity()
{
    FxElemVelStateSample samples[3]{};
    samples[0].world.velocity.base[0] = 0.002f;
    samples[1].world.velocity.base[0] = 0.004f;
    samples[0].local.velocity.base[1] = 0.001f;
    samples[1].local.velocity.base[1] = 0.001f;
    FxElemDef elemDef{};
    elemDef.velSamples = samples;
    elemDef.velIntervalCount = 2;
    elemDef.flags = 0x2000000; // world graph only

    const FxSpatialFrame frame = YawedFrame();
    orientation_t orient{};
    FX_SpatialFrameToOrientation(&frame, &orient);
    const float baseVel[3] = {0.0f, 0.0f, 1.0f};
    float velocity[3] = {};
    // A quarter of the life is halfway through interval 0: weights 1 and 1.
    FX_GetVelocityAtTime(&elemDef, 0, 1000.0f, 250.0f, &orient, baseVel, velocity);
    Check(NearVec(velocity, 6.0f, 0.0f, 1.0f), "world velocity between samples");

    elemDef.flags = 0x1000000; // local graph only, turned by the orientation
    FX_GetVelocityAtTime(&elemDef, 0, 1000.0f, 0.0f, &orient, baseVel, velocity);
    Check(Near(std::hypot(velocity[0], velocity[1]), 2.0) && Near(velocity[2], 1.0) && Near(velocity[1], 0.0),
        "local velocity turned into world space");
}

void TestElemAxis()
{
    FxElemDef elemDef{};
    orientation_t identity{};
    identity.axis[0][0] = identity.axis[1][1] = identity.axis[2][2] = 1.0f;
    mat3x3 axis{};
    FX_GetElemAxis(&elemDef, 0, &identity, 0.0f, axis);
    Check(NearVec(axis[0], 1, 0, 0) && NearVec(axis[1], 0, 1, 0) && NearVec(axis[2], 0, 0, 1), "zero angles");

    elemDef.angularVelocity[1].base = 0.0015707963f; // 90 degrees of yaw per second
    FX_GetElemAxis(&elemDef, 0, &identity, 1000.0f, axis);
    Check(NearVec(axis[0], 0, 1, 0) && NearVec(axis[1], -1, 0, 0) && NearVec(axis[2], 0, 0, 1), "yaw after a second");
}

void TestCull()
{
    FxCamera camera{};
    camera.isValid = 1;
    camera.frustumPlaneCount = 1;
    camera.frustum[0][0] = 1.0f; // keep x >= 2
    camera.frustum[0][3] = 2.0f;
    const float behind[3] = {-10.0f, 0.0f, 0.0f};
    const float straddling[3] = {0.0f, 0.0f, 0.0f};
    Check(FX_CullSphere(&camera, 1, behind, 5.0f) == 1, "sphere behind the plane is culled");
    Check(FX_CullSphere(&camera, 1, straddling, 5.0f) == 0, "sphere crossing the plane is kept");
}

void TestVisBlockers()
{
    auto system = std::make_unique<FxSystem>();
    auto front = std::make_unique<FxVisState>();
    auto back = std::make_unique<FxVisState>();
    system->visStateBufferWrite = front.get();
    system->visStateBufferRead = back.get();
    system->frameCount = 7;
    system->localClientNum = 0;
    back->blockerCount = 3;

    const float pos[3] = {1.0f, 2.0f, 3.0f};
    FX_AddVisBlocker(system.get(), pos, 10.0f, 0.25f);
    Check(front->blockerCount == 1 && NearVec(front->blocker[0].origin, 1, 2, 3)
            && front->blocker[0].radius == 160 && front->blocker[0].visibility == 49152,
        "blocker packed into the write buffer");

    for (std::uint32_t i = 1; i < fx::visibility::kBlockerCapacity; ++i)
        FX_AddVisBlocker(system.get(), pos, 1.0f, 0.5f);
    Check(front->blockerCount == static_cast<int32_t>(fx::visibility::kBlockerCapacity) && g_warnings == 0,
        "write buffer fills");
    FX_AddVisBlocker(system.get(), pos, 1.0f, 0.5f);
    FX_AddVisBlocker(system.get(), pos, 1.0f, 0.5f);
    Check(front->blockerCount == static_cast<int32_t>(fx::visibility::kBlockerCapacity) && g_warnings == 1,
        "overflow drops the blocker and warns once a frame");

    fx_serverVisClient = -1;
    FX_ToggleVisBlockerFrame(system.get());
    Check(system->visStateBufferRead == front.get() && system->visStateBufferWrite == back.get()
            && back->blockerCount == 0 && fx_serverVisClient == 0,
        "toggle swaps the buffers and clears the new write side");
}
} // namespace

bool __cdecl FX_CurrentThreadOwnsCooperativeIterator(const FxSystem *)
{
    return true;
}

void Com_PrintWarning(int, const char *, ...)
{
    ++g_warnings;
}

int32_t fx_serverVisClient;

int main()
{
    TestOrientation();
    TestTrailOrigin();
    TestVelocity();
    TestElemAxis();
    TestCull();
    TestVisBlockers();
    if (g_failures == 0)
        std::puts("fx update util contracts passed");
    return g_failures == 0 ? 0 : 1;
}
