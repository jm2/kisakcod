// Headless seam for the IDirect3D* reach in shared database code (KPI K5,
// docs/design/PLATFORM_POSIX.md). db_load.cpp's shader-load failure paths
// call ->Release() on opaque COM pointers; behind KISAK_DEDI_HEADLESS that
// call is guarded out because the headless build never creates COM objects.
// This test exercises the seam pattern: a null-check + null-assignment
// cleanup on an opaque COM pointer compiles and runs without the D3D9 SDK.
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

// Mirrors MaterialVertexShaderProgram.prog.vs / MaterialPixelShaderProgram.prog.ps
// in gfx_d3d/r_material.h: an opaque COM pointer held by a shared asset record.
struct ShaderProgram
{
    IDirect3DVertexShader9 *vs;
    IDirect3DPixelShader9 *ps;
};

// The headless cleanup path from db_load.cpp's Load_MaterialVertexShaderPtr
// and Load_MaterialPixelShaderPtr: when DB_CompleteObject fails, the shader
// pointer is released and nulled. Behind KISAK_DEDI_HEADLESS the Release()
// call is guarded out (no COM objects exist headless), but the null-check
// and null-assignment remain.
void CleanupShaderProgram(ShaderProgram *prog)
{
    if (prog->vs)
    {
#ifndef KISAK_DEDI_HEADLESS
        prog->vs->Release();
#endif
        prog->vs = nullptr;
    }
    if (prog->ps)
    {
#ifndef KISAK_DEDI_HEADLESS
        prog->ps->Release();
#endif
        prog->ps = nullptr;
    }
}

int RunChecks()
{
    // Opaque COM pointers are pointer-sized on every platform.
    CHECK(sizeof(IDirect3DVertexShader9 *) == sizeof(void *));
    CHECK(sizeof(IDirect3DPixelShader9 *) == sizeof(void *));

    // Cleanup nulls both pointers when they are non-null.
    ShaderProgram prog{};
    // Fabricate non-null opaque pointers without constructing COM objects.
    prog.vs = reinterpret_cast<IDirect3DVertexShader9 *>(uintptr_t{1});
    prog.ps = reinterpret_cast<IDirect3DPixelShader9 *>(uintptr_t{1});
    CHECK(prog.vs != nullptr);
    CHECK(prog.ps != nullptr);
    CleanupShaderProgram(&prog);
    CHECK(prog.vs == nullptr);
    CHECK(prog.ps == nullptr);

    // Cleanup is idempotent on already-null pointers.
    CleanupShaderProgram(&prog);
    CHECK(prog.vs == nullptr);
    CHECK(prog.ps == nullptr);

    // A mixed state cleans each pointer independently.
    prog.vs = reinterpret_cast<IDirect3DVertexShader9 *>(uintptr_t{1});
    CHECK(prog.vs != nullptr);
    CHECK(prog.ps == nullptr);
    CleanupShaderProgram(&prog);
    CHECK(prog.vs == nullptr);
    CHECK(prog.ps == nullptr);

    return failures;
}

} // namespace

int main()
{
    return RunChecks() == 0 ? 0 : 1;
}
