// com_math_quat_mat_tests.cpp: ConvertQuatToMat and ConvertQuatToInverseMat
// (production universal/com_math.cpp) on a known rotation. Each writes its
// axis rows; the decompiled code reached rows 1-3 as (*axis)[3..11], past
// row 0's three floats.

#include <cmath>
#include <cstdio>

#include <universal/com_math.h>

// xanim/dobj.h's DObjAnimMat, member for member (that header needs
// -fms-extensions, which this portable test does not use).
struct DObjAnimMat
{
    float quat[4];
    float trans[3];
    float transWeight;
};
static_assert(sizeof(DObjAnimMat) == 32);

namespace
{
int g_failures = 0;
void Check(bool ok, const char *what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}
bool Row(const float (&row)[3], float x, float y, float z)
{
    return std::fabs(row[0] - x) < 1e-5f && std::fabs(row[1] - y) < 1e-5f && std::fabs(row[2] - z) < 1e-5f;
}
} // namespace

int main()
{
    // 90 degrees about z, translated by (1, 2, 3).
    DObjAnimMat mat{};
    const float half = std::sqrt(0.5f);
    mat.quat[2] = half;
    mat.quat[3] = half;
    mat.trans[0] = 1;
    mat.trans[1] = 2;
    mat.trans[2] = 3;
    mat.transWeight = 2; // 2 / |q|^2

    float axis[3][3] = {};
    ConvertQuatToMat(&mat, axis);
    Check(Row(axis[0], 0, 1, 0) && Row(axis[1], -1, 0, 0) && Row(axis[2], 0, 0, 1), "the rotation's axis rows");

    float inverse[4][3] = {};
    ConvertQuatToInverseMat(&mat, inverse);
    Check(Row(inverse[0], 0, -1, 0) && Row(inverse[1], 1, 0, 0) && Row(inverse[2], 0, 0, 1),
          "the inverse rotation is the transpose");
    Check(Row(inverse[3], -2, 1, -3), "the inverse translation undoes the rotated translation");

    if (!g_failures)
        std::printf("quat to mat: all checks passed\n");
    return g_failures ? 1 : 0;
}
