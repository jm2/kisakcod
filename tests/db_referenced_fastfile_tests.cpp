#include "database/db_referenced_fastfile.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

namespace
{
struct ZoneFixture
{
    const char *name = "";
    bool modZone = false;
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
    zones[kDefaultZoneSlot] = {"slot0-default", false};
    zones[1] = {"slot1", false};
    zones[2] = {"localized_slot2", false};
    zones[31] = {"slot31", false};
    zones[32] = {"slot32", false};

    std::array<std::size_t, 3> visited{};
    std::size_t visitedCount = 0;
    ForEachReferencedFastFile(
        zones,
        IsLocalized,
        [&](const std::size_t slot, const ZoneFixture &)
        {
            if (visitedCount < visited.size())
                visited[visitedCount] = slot;
            ++visitedCount;
        });

    Expect(visitedCount == visited.size(), "only slots 1, 31, and 32 must match");
    Expect(visited[0] == 1, "slot 1 must be visited first");
    Expect(visited[1] == 31, "slot 31 must retain table order");
    Expect(visited[2] == 32, "the highest usable slot must be visited last");
}

void TestNameFormatting()
{
    using namespace db::referenced_fastfile;

    ZoneFixture zones[kZoneSlotCount]{};
    zones[kDefaultZoneSlot] = {"slot0-default", true};
    zones[31] = {"slot31", true};
    zones[32] = {"slot32", false};

    std::string output;
    const auto emit = [&](const char *const part)
    {
        output.append(part);
    };
    EmitReferencedFastFileNames(zones, "mods/example", IsLocalized, emit);
    Expect(
        output == "mods/example/slot31 slot32",
        "mod zones must use the requested directory prefix and preserve slot order");

    zones[32].name = "localized_slot32";
    output.clear();
    EmitReferencedFastFileNames(zones, "mods/example", IsLocalized, emit);
    Expect(
        output == "mods/example/slot31",
        "a localized fast file in slot 32 must be excluded");
}
} // namespace

int main()
{
    TestSlotWalk();
    TestNameFormatting();

    if (g_failures != 0)
        return 1;

    std::puts("referenced fast-file tests passed");
    return 0;
}
