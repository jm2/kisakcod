#include <aim_assist/aim_assist_safety.h>

#include <cstdint>
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

void CheckEntityProfile(std::int32_t world, const char *profile)
{
    Check(
        !aim_assist::safety::IsOrdinaryEntityNumber(-1, world),
        profile);
    Check(
        aim_assist::safety::IsOrdinaryEntityNumber(0, world),
        profile);
    Check(
        aim_assist::safety::IsOrdinaryEntityNumber(world - 1, world),
        profile);
    Check(
        !aim_assist::safety::IsOrdinaryEntityNumber(world, world),
        profile);
    Check(
        !aim_assist::safety::IsOrdinaryEntityNumber(world + 1, world),
        profile);
}
} // namespace

int main()
{
    CheckEntityProfile(1022, "MP entity boundary");
    CheckEntityProfile(2174, "SP entity boundary");

    Check(
        aim_assist::safety::BoundedWeaponIndex(0, 128) == 0,
        "weapon zero remains the no-weapon sentinel");
    Check(
        aim_assist::safety::BoundedWeaponIndex(127, 128) == 127,
        "last registered weapon remains valid");
    Check(
        aim_assist::safety::BoundedWeaponIndex(128, 128) == 0,
        "weapon count is rejected");
    Check(
        aim_assist::safety::BoundedWeaponIndex(
            std::numeric_limits<std::uint32_t>::max(), 128) == 0,
        "negative signed weapon conversion is rejected");
    Check(
        aim_assist::safety::BoundedWeaponIndex(0, 0) == 0,
        "empty weapon registry remains safe");

    return failures == 0 ? 0 : 1;
}
