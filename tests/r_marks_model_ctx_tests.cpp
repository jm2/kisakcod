// r_marks_model_ctx_tests.cpp: R_MarkModelCoreCallback_0_ (production
// gfx_d3d/r_marks.cpp), the triangle visitor for marks on skinned models. It
// gets a MarkModelCoreContext through void *; the decompiled code read its
// fields at the x86 offsets 0/4/12/16/24, which at 64-bit land mid-pointer.

#include <gfx_d3d/r_marks.h>
#include <gfx_d3d/r_gfx.h>

#include <cstdio>
#include <cstdlib>

// Only the fragment path unpacks normals; a rejected triangle never gets there.
void __cdecl Vec3UnpackUnitVec(PackedUnitVec, float *)
{
    std::abort();
}

bool __cdecl R_MarkModelCoreCallback_0_(
    void *contextAsVoid,
    const GfxPackedVertex **triVerts0,
    const GfxPackedVertex **triVerts1);

namespace
{
GfxPackedVertex g_verts[3];
GfxMarkContext g_markContext;
const float kOrigin[3] = {0, 0, 0};
}

int main()
{
    // A triangle facing -z; the mark points along +z, so it is rejected
    // before any fragment work, after reading markDir.
    g_verts[1].xyz[0] = 1.0f;
    g_verts[2].xyz[1] = 1.0f;
    const GfxPackedVertex *tri[3] = {&g_verts[0], &g_verts[1], &g_verts[2]};
    const float markDir[3] = {0, 0, 1};

    MarkModelCoreContext context{};
    context.markContext = &g_markContext;
    context.markOrigin = kOrigin;
    context.markDir = markDir;
    const bool rejected = R_MarkModelCoreCallback_0_(&context, tri, tri);
    if (!rejected)
    {
        std::fputs("FAIL a back-facing triangle is rejected through the context's markDir\n", stderr);
        return 1;
    }
    std::puts("r_marks model context: ok");
    return 0;
}
