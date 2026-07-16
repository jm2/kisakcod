#include "database/db_referenced_fastfile.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>

namespace
{
struct ZoneFixture
{
    const char *name = "";
    bool modZone = false;
    std::int32_t fileSize = 0;
};

int g_failures;

void Expect(const bool condition, const char *const message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++g_failures;
    }
}

bool IsLocalized(const char *const name)
{
    return std::string_view(name).starts_with("localized_");
}

void TestSlotWalk()
{
    using namespace db::referenced_fastfile;

    ZoneFixture zones[kZoneSlotCount]{};
    zones[kDefaultZoneSlot] = {"slot0-default", false, -100};
    zones[1] = {"slot1", false, -1};
    zones[2] = {"localized_slot2", false, -2};
    zones[31] = {"slot31", false, 31};
    zones[32] = {"slot32", false, 32};

    std::array<std::size_t, 3> visited{};
    std::array<std::int32_t, 3> visitedFileSizes{};
    std::size_t visitedCount = 0;
    ForEachReferencedFastFile(
        zones,
        IsLocalized,
        [&](const std::size_t slot, const ZoneFixture &zone)
        {
            if (visitedCount < visited.size())
            {
                visited[visitedCount] = slot;
                visitedFileSizes[visitedCount] = zone.fileSize;
            }
            ++visitedCount;
        });

    Expect(visitedCount == visited.size(), "only slots 1, 31, and 32 must match");
    Expect(visited[0] == 1, "slot 1 must be visited first");
    Expect(visited[1] == 31, "slot 31 must retain table order");
    Expect(visited[2] == 32, "the highest usable slot must be visited last");
    Expect(
        visitedFileSizes == std::array<std::int32_t, 3>{-1, 31, 32},
        "checksum inputs must have the same filtering and slot order as names");
}

void TestSignedDecimalFormatting()
{
    using db::referenced_fastfile::FormatSignedDecimal;

    std::array<char, 12> exact{};
    Expect(
        FormatSignedDecimal(
            (std::numeric_limits<std::int32_t>::min)(),
            exact.data(),
            exact.size()),
        "INT32_MIN must fit its exact signed-decimal buffer");
    Expect(
        std::strcmp(exact.data(), "-2147483648") == 0,
        "signed decimal formatting must preserve the negative sign");
    Expect(exact.back() == '\0', "signed decimal output must be explicitly terminated");

    std::array<char, 11> tooSmall{};
    Expect(
        !FormatSignedDecimal(
            (std::numeric_limits<std::int32_t>::min)(),
            tooSmall.data(),
            tooSmall.size()),
        "signed decimal formatting must reject a buffer without NUL capacity");
}

void TestNameFormatting()
{
    using namespace db::referenced_fastfile;

    ZoneFixture zones[kZoneSlotCount]{};
    zones[kDefaultZoneSlot] = {"slot0-default", true};
    zones[31] = {"slot31", true};
    zones[32] = {"slot32", false};

    constexpr char expected[] = "mods/example/slot31 slot32";
    std::array<char, sizeof(expected)> exact{};
    Expect(
        FormatReferencedFastFileNames(
            zones,
            "mods/example",
            IsLocalized,
            exact.data(),
            exact.size()),
        "an exact-fit name list must succeed");
    Expect(
        std::strcmp(exact.data(), expected) == 0,
        "mod zones must use the requested directory prefix and preserve slot order");

    std::array<char, sizeof(expected) - 1> tooSmall{};
    tooSmall.fill('#');
    const auto unchanged = tooSmall;
    Expect(
        !FormatReferencedFastFileNames(
            zones,
            "mods/example",
            IsLocalized,
            tooSmall.data(),
            tooSmall.size()),
        "a name list without NUL capacity must fail");
    Expect(tooSmall == unchanged, "a failed name list must leave output unchanged");

    std::array<char, sizeof(expected) - 1> emptyOutput{};
    Expect(
        !FormatReferencedFastFileNames(
            zones,
            "mods/example",
            IsLocalized,
            emptyOutput.data(),
            emptyOutput.size()),
        "the production-equivalent empty destination must reject overflow");
    Expect(emptyOutput[0] == '\0', "failed production output must remain empty");

    zones[32].name = "localized_slot32";
    std::array<char, 128> localizedOutput{};
    Expect(
        FormatReferencedFastFileNames(
            zones,
            "mods/example",
            IsLocalized,
            localizedOutput.data(),
            localizedOutput.size()),
        "a filtered name list must fit");
    Expect(
        std::strcmp(localizedOutput.data(), "mods/example/slot31") == 0,
        "a localized fast file in slot 32 must be excluded");
}

void TestExpandedSystemInfoCapacity()
{
    using namespace db::referenced_fastfile;

    constexpr std::size_t kLegacyCapacity = 2080;
    constexpr std::size_t kSystemInfoCapacity = 8192;
    constexpr std::size_t kZoneNameLength = 63;
    constexpr std::size_t kModPrefixLength = sizeof("mods/example/") - 1;
    constexpr std::size_t kExpectedLength =
        kLiveFastFileZoneCount * (kModPrefixLength + kZoneNameLength)
        + (kLiveFastFileZoneCount - 1);
    static_assert(kExpectedLength >= kLegacyCapacity);
    static_assert(kExpectedLength < kSystemInfoCapacity);

    ZoneFixture zones[kZoneSlotCount]{};
    std::array<std::array<char, kZoneNameLength + 1>, kLiveFastFileZoneCount>
        zoneNames{};
    for (std::size_t index = 0; index < zoneNames.size(); ++index)
    {
        zoneNames[index].fill(static_cast<char>('a' + index % 26));
        zoneNames[index].back() = '\0';
        zones[index + kFirstFastFileZoneSlot] = {
            zoneNames[index].data(),
            true,
            static_cast<std::int32_t>(index),
        };
    }

    std::array<char, kLegacyCapacity> legacyOutput{};
    legacyOutput.fill('#');
    const auto unchangedLegacyOutput = legacyOutput;
    Expect(
        !FormatReferencedFastFileNames(
            zones,
            "mods/example",
            IsLocalized,
            legacyOutput.data(),
            legacyOutput.size()),
        "the complete 32-zone mod list must exceed the legacy 2080-byte buffer");
    Expect(
        legacyOutput == unchangedLegacyOutput,
        "legacy-capacity failure must not publish a partial list");

    std::array<char, kSystemInfoCapacity> systemInfoOutput{};
    Expect(
        FormatReferencedFastFileNames(
            zones,
            "mods/example",
            IsLocalized,
            systemInfoOutput.data(),
            systemInfoOutput.size()),
        "the complete 32-zone mod list must fit the SYSTEMINFO value buffer");
    Expect(
        std::strlen(systemInfoOutput.data()) == kExpectedLength,
        "the complete 32-zone mod list must retain every byte");
    Expect(
        std::strncmp(
            systemInfoOutput.data(),
            "mods/example/",
            kModPrefixLength) == 0,
        "the expanded list must begin with the requested mod prefix");
    Expect(
        systemInfoOutput[kExpectedLength] == '\0',
        "the expanded list must be explicitly terminated");
}
} // namespace

int main()
{
    TestSlotWalk();
    TestSignedDecimalFormatting();
    TestNameFormatting();
    TestExpandedSystemInfoCapacity();

    if (g_failures != 0)
        return 1;

    std::puts("referenced fast-file tests passed");
    return 0;
}
