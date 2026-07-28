#include <bgame/bg_weapon_input_safety.h>

#include <cstdint>
#include <cstdio>
#include <limits>

namespace
{
int Fail(const char *const message)
{
    std::fprintf(stderr, "weapon-input safety test failed: %s\n", message);
    return 1;
}
}

int main()
{
    using bg::weapon_input::IsAttackSuppressed;
    using bg::weapon_input::kFriendlyFireSuppressed;
    using bg::weapon_input::kWeaponsDisabled;

    if (IsAttackSuppressed(0))
        return Fail("zero flags");
    if (IsAttackSuppressed(0x01) || IsAttackSuppressed(0x02))
        return Fail("unrelated flags");
    if (!IsAttackSuppressed(kFriendlyFireSuppressed))
        return Fail("friendly-fire suppression");
    if (!IsAttackSuppressed(kWeaponsDisabled))
        return Fail("disableWeapons suppression");
    if (!IsAttackSuppressed(
            kFriendlyFireSuppressed | kWeaponsDisabled))
    {
        return Fail("combined suppression flags");
    }
    if (!IsAttackSuppressed((std::numeric_limits<std::int32_t>::max)()))
        return Fail("suppression among high unrelated bits");

    return 0;
}
