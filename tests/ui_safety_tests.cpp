#include <ui/ui_safety.h>

#include <array>
#include <cstdio>

namespace
{
int failures = 0;

void Check(const bool condition, const char *const description)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++failures;
    }
}

std::array<int, ui_safety::kSavegameCapacity> IdentityDisplayMap()
{
    std::array<int, ui_safety::kSavegameCapacity> displaySavegames{};
    for (std::size_t index = 0; index < displaySavegames.size(); ++index)
        displaySavegames[index] = static_cast<int>(index);
    return displaySavegames;
}

void TestSavegameCountCapacity()
{
    Check(
        ui_safety::IsSavegameCountValid(255),
        "255 savegames must fit the display map");
    Check(
        ui_safety::IsSavegameCountValid(256),
        "256 savegames must fit the display map exactly");
    Check(
        !ui_safety::IsSavegameCountValid(257),
        "257 savegames must fail closed");
    Check(
        !ui_safety::IsSavegameCountValid(512),
        "the backing-list size must not become the live count limit");

    Check(
        ui_safety::GetFailClosedSavegameCount(255) == 255,
        "a valid count must be preserved");
    Check(
        ui_safety::GetFailClosedSavegameCount(256) == 256,
        "the exact display capacity must be preserved");
    Check(
        ui_safety::GetFailClosedSavegameCount(257) == 0,
        "a one-past-capacity count must expose no entries");
    Check(
        ui_safety::GetFailClosedSavegameCount(512) == 0,
        "the storage-layout count must expose no entries");
    Check(
        ui_safety::GetFailClosedSavegameCount(-1) == 0,
        "a negative count must expose no entries");

    Check(
        ui_safety::CanAppendSavegame(255),
        "the final display-map entry must remain appendable");
    Check(
        !ui_safety::CanAppendSavegame(256),
        "the full display map must reject another append");
    Check(
        !ui_safety::CanAppendSavegame(257),
        "an already-invalid count must reject appends");
    Check(
        !ui_safety::CanAppendSavegame(512),
        "the backing-list size must reject appends");
}

void TestSavegameSlotResolution()
{
    auto displaySavegames = IdentityDisplayMap();
    int slotIndex = -1;

    Check(
        ui_safety::TryResolveSavegameSlot(
            displaySavegames.data(), 255, 254, &slotIndex)
            && slotIndex == 254,
        "the final entry in a 255-save list must resolve");
    Check(
        ui_safety::TryResolveSavegameSlot(
            displaySavegames.data(), 256, 255, &slotIndex)
            && slotIndex == 255,
        "the final entry in a full display map must resolve");

    slotIndex = 7;
    Check(
        !ui_safety::TryResolveSavegameSlot(
            displaySavegames.data(), 257, 0, &slotIndex)
            && slotIndex == -1,
        "a 257-save count must fail before reading the display map");
    slotIndex = 7;
    Check(
        !ui_safety::TryResolveSavegameSlot(
            displaySavegames.data(), 512, 0, &slotIndex)
            && slotIndex == -1,
        "a 512-save count must fail before reading the display map");

    slotIndex = 7;
    Check(
        !ui_safety::TryResolveSavegameSlot(
            displaySavegames.data(), 256, -1, &slotIndex)
            && slotIndex == -1,
        "a negative display index must fail closed");
    slotIndex = 7;
    Check(
        !ui_safety::TryResolveSavegameSlot(
            displaySavegames.data(), 256, 256, &slotIndex)
            && slotIndex == -1,
        "a one-past-end display index must fail closed");

    displaySavegames[4] = -1;
    slotIndex = 7;
    Check(
        !ui_safety::TryResolveSavegameSlot(
            displaySavegames.data(), 256, 4, &slotIndex)
            && slotIndex == -1,
        "a negative mapped slot must fail closed");

    displaySavegames[4] = 256;
    slotIndex = 7;
    Check(
        !ui_safety::TryResolveSavegameSlot(
            displaySavegames.data(), 256, 4, &slotIndex)
            && slotIndex == -1,
        "a mapped slot outside the live count must fail closed");

    displaySavegames[4] = 255;
    slotIndex = 7;
    Check(
        !ui_safety::TryResolveSavegameSlot(
            displaySavegames.data(), 255, 4, &slotIndex)
            && slotIndex == -1,
        "a mapped slot equal to a shorter live count must fail closed");

    slotIndex = 7;
    Check(
        !ui_safety::TryResolveSavegameSlot(
            nullptr, 256, 0, &slotIndex)
            && slotIndex == -1,
        "a null display map must fail closed");
    Check(
        !ui_safety::TryResolveSavegameSlot(
            displaySavegames.data(), 256, 0, nullptr),
        "a null result pointer must fail closed");
}
}

int main()
{
    TestSavegameCountCapacity();
    TestSavegameSlotResolution();
    return failures == 0 ? 0 : 1;
}
