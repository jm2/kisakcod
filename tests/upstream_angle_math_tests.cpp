#include <universal/com_angle.h>

#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
int failures = 0;

void Check(bool condition, const char *description)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++failures;
    }
}

void CheckNear(float actual, float expected, const char *description)
{
    Check(std::fabs(actual - expected) <= 0.0001f, description);
}
} // namespace

int main()
{
    CheckNear(AngleSubtract(0.0f, 0.0f), 0.0f, "zero delta");
    CheckNear(AngleSubtract(180.0f, 0.0f), -180.0f, "+180 tie");
    CheckNear(AngleSubtract(-180.0f, 0.0f), -180.0f, "-180 tie");
    CheckNear(AngleSubtract(540.0f, 0.0f), -180.0f, "+540 tie");
    CheckNear(AngleSubtract(-540.0f, 0.0f), -180.0f, "-540 tie");
    CheckNear(AngleSubtract(359.0f, 0.0f), -1.0f, "positive wrap");
    CheckNear(AngleSubtract(0.0f, 359.0f), 1.0f, "negative wrap");
    CheckNear(AngleSubtract(721.0f, 0.0f), 1.0f, "multiple turns");
    CheckNear(AngleSubtract(-721.0f, 0.0f), -1.0f, "negative turns");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    Check(std::isnan(AngleSubtract(nan, 0.0f)), "NaN propagates");
    Check(std::isnan(AngleSubtract(infinity, 0.0f)), "+infinity propagates");
    Check(std::isnan(AngleSubtract(-infinity, 0.0f)), "-infinity propagates");

    return failures == 0 ? 0 : 1;
}
