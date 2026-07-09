#include <qcommon/identity.h>

#include <cstdint>
#include <iostream>
#include <limits>

namespace
{
int failures = 0;

void Expect(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
}

int main()
{
    Expect(kisak::identity::IsHexGuid("0123456789abcdef0123456789ABCDEF"), "valid mixed-case GUID");
    Expect(!kisak::identity::IsHexGuid(nullptr), "null GUID rejected");
    Expect(!kisak::identity::IsHexGuid("0123456789abcdef"), "short GUID rejected");
    Expect(!kisak::identity::IsHexGuid("0123456789abcdef0123456789abcdeg"), "non-hex GUID rejected");

    std::uint64_t steamId = 0;
    Expect(kisak::identity::ParseSteamId("76561198000000000", &steamId), "valid SteamID64");
    Expect(steamId == UINT64_C(76561198000000000), "SteamID64 value preserved");
    Expect(!kisak::identity::ParseSteamId("0", &steamId), "zero Steam identity rejected");
    Expect(!kisak::identity::ParseSteamId("76561198abcdef", &steamId), "non-decimal Steam identity rejected");
    Expect(!kisak::identity::ParseSteamId("18446744073709551616", &steamId), "overflowing Steam identity rejected");
    Expect(kisak::identity::ParseSteamId("18446744073709551615", &steamId), "maximum uint64 identity accepted");
    Expect(steamId == std::numeric_limits<std::uint64_t>::max(), "maximum uint64 value preserved");
    Expect(!kisak::identity::ParseSteamId("123456789012345678901", &steamId), "overlong decimal identity rejected");
    Expect(!kisak::identity::ParseSteamId("76561198000000000", nullptr), "null output rejected");

    return failures == 0 ? 0 : 1;
}
