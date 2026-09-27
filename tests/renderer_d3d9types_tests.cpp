// ABI contracts for gfx_d3d/r_d3d9types.h, the non-Windows stand-in for
// <d3d9.h> that the shared asset headers compile against (KPI K5,
// docs/design/PLATFORM_POSIX.md). GfxImageLoadDef.format is a disk field and
// r_image.h compares the depth formats, so the stand-in's enumerator values
// and 4-byte width are engine ABI: these checks fail if one drifts. On
// Windows the same declarations come from the D3D9 SDK and the same checks
// pin the SDK values.
#include <gfx_d3d/r_d3d9types.h>

#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace
{

int failures = 0;

void Check(const bool condition, const char *const expression, const int line)
{
    if (condition)
        return;
    std::fprintf(stderr, "line %d: check failed: %s\n", line, expression);
    ++failures;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

int RunChecks()
{
    // The enum is a 4-byte disk field (OFFSET_TO_GfxImageLoadDef_DATA).
    CHECK(sizeof(_D3DFORMAT) == 4);
    CHECK(alignof(_D3DFORMAT) == 4);
    CHECK(sizeof(_D3DCUBEMAP_FACES) == 4);
    CHECK(alignof(_D3DCUBEMAP_FACES) == 4);

    // Fixed D3D9 values (d3d9types.h) used by shared engine code.
    CHECK(D3DFMT_UNKNOWN == 0);
    CHECK(D3DFMT_A8R8G8B8 == 21);
    CHECK(D3DFMT_X8R8G8B8 == 22);
    CHECK(D3DFMT_A8 == 28);
    CHECK(D3DFMT_L8 == 50);
    CHECK(D3DFMT_A8L8 == 51);
    CHECK(D3DFMT_DXT1 == 0x31545844); // MAKEFOURCC('D','X','T','1')
    CHECK(D3DFMT_DXT3 == 0x33545844);
    CHECK(D3DFMT_DXT5 == 0x35545844);
    CHECK(D3DFMT_D16_LOCKABLE == 70);
    CHECK(D3DFMT_D32 == 71);
    // The depth formats Image_GetUsage discriminates.
    CHECK(D3DFMT_D24S8 == 75);
    CHECK(D3DFMT_D24X8 == 77);
    CHECK(D3DFMT_D16 == 80);
    CHECK(D3DFMT_R32F == 114);
    CHECK(static_cast<uint32_t>(D3DFMT_FORCE_DWORD) == 0xFFFFFFFFu);

    // The cubemap face r_image.h signatures take by value.
    CHECK(D3DCUBEMAP_FACE_POSITIVE_X == 0);
    CHECK(D3DCUBEMAP_FACE_NEGATIVE_X == 1);
    CHECK(D3DCUBEMAP_FACE_POSITIVE_Y == 2);
    CHECK(D3DCUBEMAP_FACE_NEGATIVE_Y == 3);
    CHECK(D3DCUBEMAP_FACE_POSITIVE_Z == 4);
    CHECK(D3DCUBEMAP_FACE_NEGATIVE_Z == 5);
    CHECK(static_cast<uint32_t>(D3DCUBEMAP_FACE_FORCE_DWORD) == 0xFFFFFFFFu);

    static_assert(std::is_enum_v<_D3DFORMAT>);
    static_assert(std::is_enum_v<_D3DCUBEMAP_FACES>);
    return failures;
}

} // namespace

int main()
{
    return RunChecks() == 0 ? 0 : 1;
}
