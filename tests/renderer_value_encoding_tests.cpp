#include <gfx_d3d/r_image_dimensions.h>
#include <gfx_d3d/r_pretess_encoding.h>

#include <cstdint>
#include <cstdio>
#include <limits>

namespace
{
int Fail(const char *const message)
{
    std::fprintf(stderr, "renderer value-encoding test failed: %s\n", message);
    return 1;
}

bool TestPreTessPacking()
{
    using gfx::pretess_encoding::TryPackSurface;

    std::uint32_t packed = 0xA5A5A5A5u;
    if (!TryPackSurface(0, 0, 0, &packed) || packed != 0)
        return false;
    if (!TryPackSurface(1, 2, 0x1234u, &packed)
        || packed != 0x12340201u)
    {
        return false;
    }
    if (!TryPackSurface(255, 255, 0xFFFFu, &packed)
        || packed != std::numeric_limits<std::uint32_t>::max())
    {
        return false;
    }

    packed = 0xA5A5A5A5u;
    return !TryPackSurface(-1, 0, 0, &packed)
        && packed == 0xA5A5A5A5u
        && !TryPackSurface(256, 0, 0, &packed)
        && packed == 0xA5A5A5A5u
        && !TryPackSurface(0, 256, 0, &packed)
        && packed == 0xA5A5A5A5u
        && !TryPackSurface(0, 0, 0, nullptr)
        && packed == 0xA5A5A5A5u;
}

bool ExpectResolution(
    const int width,
    const int height,
    const int mip,
    const std::uint16_t expectedWidth,
    const std::uint16_t expectedHeight)
{
    gfx::image_dimensions::MipmapResolution resolution{};
    return gfx::image_dimensions::TryGetMipmapResolution(
               width, height, mip, &resolution)
        && resolution.width == expectedWidth
        && resolution.height == expectedHeight;
}

bool TestMipmapResolution()
{
    using gfx::image_dimensions::MipmapResolution;
    using gfx::image_dimensions::TryGetMipmapResolution;

    if (!ExpectResolution(1, 1, 0, 1, 1)
        || !ExpectResolution(1920, 1080, 0, 1920, 1080)
        || !ExpectResolution(1920, 1080, 1, 960, 540)
        || !ExpectResolution(1920, 1080, 31, 1, 1)
        || !ExpectResolution(7, 5, 2, 1, 1)
        || !ExpectResolution(65535, 65535, 1, 32767, 32767))
    {
        return false;
    }

    MipmapResolution unchanged{123u, 456u};
    return !TryGetMipmapResolution(0, 1, 0, &unchanged)
        && unchanged.width == 123u
        && unchanged.height == 456u
        && !TryGetMipmapResolution(-1, 1, 0, &unchanged)
        && !TryGetMipmapResolution(65536, 1, 0, &unchanged)
        && !TryGetMipmapResolution(1, 0, 0, &unchanged)
        && !TryGetMipmapResolution(1, 65536, 0, &unchanged)
        && !TryGetMipmapResolution(1, 1, -1, &unchanged)
        && !TryGetMipmapResolution(1, 1, 32, &unchanged)
        && !TryGetMipmapResolution(
            1, 1, std::numeric_limits<int>::max(), &unchanged)
        && !TryGetMipmapResolution(1, 1, 0, nullptr)
        && unchanged.width == 123u
        && unchanged.height == 456u;
}
} // namespace

int main()
{
    if (!TestPreTessPacking())
        return Fail("pre-tess packing or failure atomicity");
    if (!TestMipmapResolution())
        return Fail("mipmap validation, clamping, or failure atomicity");
    return 0;
}
