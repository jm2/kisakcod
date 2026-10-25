// r_staticmodelcache_tests.cpp: helpers of the production
// gfx_d3d/r_staticmodelcache.cpp. SetupTransformUnitVec and
// LocalTransformUnitVec take the fixed-point axis as int (*)[3]; the
// decompiled code reached rows 1-2 as (*fixedMtx)[3..8]. SMC_GetLeaf reached
// any tree's leaf as leafs[0][cacheIndex - 1], past tree 0's 32 leaves.

#include <gfx_d3d/r_staticmodelcache.h>

#include <cstdio>

void __cdecl SetupTransformUnitVec(const float4 *mtx, int (*fixedMtx)[3]);
PackedUnitVec __cdecl LocalTransformUnitVec(PackedUnitVec in, const int (*fixedMtx)[3]);

namespace
{
int g_failures = 0;

void Check(const bool ok, const char *const what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

void TestIdentityAxis()
{
    float4 axis[3]{};
    axis[0].v[0] = 1.0f;
    axis[1].v[1] = 1.0f;
    axis[2].v[2] = 1.0f;
    int fixedMtx[3][3] = {};
    SetupTransformUnitVec(axis, fixedMtx);
    bool diag = true;
    for (int r = 0; r < 3; ++r)
    {
        for (int c = 0; c < 3; ++c)
            diag = diag && fixedMtx[r][c] == (r == c ? 32768 : 0);
    }
    Check(diag, "SetupTransformUnitVec writes all three rows");

    PackedUnitVec in{};
    in.array[0] = 200;
    in.array[1] = 127;
    in.array[2] = 60;
    in.array[3] = 64;
    const PackedUnitVec out = LocalTransformUnitVec(in, fixedMtx);
    Check(out.array[0] == 200 && out.array[1] == 127 && out.array[2] == 60 && out.array[3] == 64,
        "LocalTransformUnitVec keeps a vector under the identity axis");
}

void TestLeafAcrossTrees()
{
    Check(SMC_GetLeaf(33) - SMC_GetLeaf(1) == 32, "leaf 33 is tree 1's first leaf");
    Check(SMC_GetLeaf(2) - SMC_GetLeaf(1) == 1, "leaves within a tree are adjacent");
}
}

int main()
{
    TestIdentityAxis();
    TestLeafAcrossTrees();
    if (g_failures)
        return 1;
    std::puts("r_staticmodelcache: ok");
    return 0;
}
