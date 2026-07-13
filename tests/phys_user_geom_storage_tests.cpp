#include <physics/ode/user_geom_storage.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>

namespace
{
union NativeBrushReference
{
    std::uint16_t brushModel;
    const void *brush;
};

struct NativeBrushInfo
{
    NativeBrushReference reference;
    float centerOfMass[3];
};

static_assert(physics::ode::UserGeomClassDataMatches<NativeBrushInfo>);

struct GuardedStorage
{
    std::array<unsigned char, 16> prefix{};
    alignas(physics::ode::kUserGeomClassDataAlignment)
        std::array<unsigned char, physics::ode::kUserGeomClassDataBytes>
            payload{};
    std::array<unsigned char, 16> suffix{};
};
} // namespace

int main()
{
    GuardedStorage storage{};
    storage.prefix.fill(0xA5u);
    storage.payload.fill(0u);
    storage.suffix.fill(0x5Au);

    int brushSentinel = 0;
    auto *const info = ::new (storage.payload.data()) NativeBrushInfo{};
    info->reference.brush = &brushSentinel;
    info->centerOfMass[0] = 1.25f;
    info->centerOfMass[1] = -2.5f;
    info->centerOfMass[2] = 5.0f;

    const bool payloadRoundTrips =
        info->reference.brush == &brushSentinel
        && info->centerOfMass[0] == 1.25f
        && info->centerOfMass[1] == -2.5f
        && info->centerOfMass[2] == 5.0f;
    const bool guardsIntact =
        storage.prefix
            == std::array<unsigned char, 16>{
                0xA5u, 0xA5u, 0xA5u, 0xA5u,
                0xA5u, 0xA5u, 0xA5u, 0xA5u,
                0xA5u, 0xA5u, 0xA5u, 0xA5u,
                0xA5u, 0xA5u, 0xA5u, 0xA5u}
        && storage.suffix
            == std::array<unsigned char, 16>{
                0x5Au, 0x5Au, 0x5Au, 0x5Au,
                0x5Au, 0x5Au, 0x5Au, 0x5Au,
                0x5Au, 0x5Au, 0x5Au, 0x5Au,
                0x5Au, 0x5Au, 0x5Au, 0x5Au};

    if (!payloadRoundTrips || !guardsIntact)
    {
        std::fprintf(
            stderr,
            "native brush class data exceeded ODE user-geom storage\n");
        return 1;
    }

    std::puts("native-width ODE user-geom storage passed");
    return 0;
}
