// Headless seam for the IDirect3D* reach in shared database code (KPI K5,
// docs/design/NATIVE64.md). The material shader load failure paths in
// db_load.cpp release and null opaque COM shader pointers; behind
// KISAK_DEDI_HEADLESS the Release() call is compiled out because the headless
// build never creates COM objects.
//
// This test compiles the engine's seam TU (src/database/db_shader_release.cpp
// — the helper db_load.cpp's failure paths call) and drives it through its
// production declaration, so the code under test is the engine's cleanup, not
// a re-implementation of it. Removing the seam guard makes that TU fail to
// compile in this KISAK_DEDI_HEADLESS build (Release() on the opaque forward
// declaration), which fails this test instead of leaving it green.
#include <database/db_shader_release.h>
#include <gfx_d3d/r_d3d9types.h>

#include <cstdint>
#include <cstdio>

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
    // Opaque COM pointers are pointer-sized on every platform.
    CHECK(sizeof(IDirect3DVertexShader9 *) == sizeof(void *));
    CHECK(sizeof(IDirect3DPixelShader9 *) == sizeof(void *));

    // Fabricate non-null opaque pointers without constructing COM objects.
    // This test target compiles the engine TU with KISAK_DEDI_HEADLESS, where
    // the seam compiles the Release() call out, so no COM method is invoked
    // on these sentinels.
    IDirect3DVertexShader9 *vs =
        reinterpret_cast<IDirect3DVertexShader9 *>(uintptr_t{1});
    IDirect3DPixelShader9 *ps =
        reinterpret_cast<IDirect3DPixelShader9 *>(uintptr_t{1});
    CHECK(vs != nullptr);
    CHECK(ps != nullptr);

    // The engine's seam release nulls each slot.
    DB_ReleaseVertexShader(&vs);
    DB_ReleasePixelShader(&ps);
    CHECK(vs == nullptr);
    CHECK(ps == nullptr);

    // Releasing an already-null slot is a no-op.
    DB_ReleaseVertexShader(&vs);
    DB_ReleasePixelShader(&ps);
    CHECK(vs == nullptr);
    CHECK(ps == nullptr);

    // A mixed state cleans each slot independently.
    vs = reinterpret_cast<IDirect3DVertexShader9 *>(uintptr_t{1});
    CHECK(vs != nullptr);
    CHECK(ps == nullptr);
    DB_ReleaseVertexShader(&vs);
    CHECK(vs == nullptr);
    CHECK(ps == nullptr);

    return failures;
}

} // namespace

int main()
{
    return RunChecks() == 0 ? 0 : 1;
}
