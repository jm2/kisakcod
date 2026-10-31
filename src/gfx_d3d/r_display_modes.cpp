#include "r_display_modes.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <string_view>

#include <universal/assertive.h>

namespace
{
// Orders a mode against the request as R_CompareDisplayModes orders modes:
// width, then height, then refresh rate.
int CompareModeToRequest(const _D3DDISPLAYMODE &mode, uint32_t width, uint32_t height, int refreshRate)
{
    if (mode.Width != width)
        return (int)(mode.Width - width);
    if (mode.Height != height)
        return (int)(mode.Height - height);
    return (int)mode.RefreshRate - refreshRate;
}

bool ModeHasResolution(const _D3DDISPLAYMODE *modes, uint32_t modeCount, int index, uint32_t width, uint32_t height)
{
    return index >= 0 && index < (int)modeCount && modes[index].Width == width && modes[index].Height == height;
}
}

int R_FindDisplayModeRefreshRate(
    const _D3DDISPLAYMODE *modes,
    uint32_t modeCount,
    uint32_t width,
    uint32_t height,
    int refreshRate)
{
    int bot = 0;
    int top = (int)modeCount - 1;
    while (bot <= top)
    {
        const int mid = (bot + top) / 2;
        const int comparison = CompareModeToRequest(modes[mid], width, height, refreshRate);
        if (!comparison)
            return refreshRate;
        if (comparison > 0)
            top = mid - 1;
        else
            bot = mid + 1;
    }
    if (ModeHasResolution(modes, modeCount, top, width, height))
        return (int)modes[top].RefreshRate;
    if (ModeHasResolution(modes, modeCount, bot, width, height))
        return (int)modes[bot].RefreshRate;
    MyAssertHandler(
        ".\\r_init.cpp",
        1706,
        0,
        "%s\n\twant (%u %u) among %u modes",
        "dx.displayModes[bot].Width == width && dx.displayModes[bot].Height == height",
        width,
        height,
        modeCount);
    return refreshRate;
}

void R_ChooseFullscreenSize(
    const _D3DDISPLAYMODE *modes,
    uint32_t modeCount,
    bool atDesktop,
    const _D3DDISPLAYMODE &desktop,
    int *width,
    int *height)
{
    if (!atDesktop)
        return;
    for (uint32_t modeIndex = 0; modeIndex < modeCount; ++modeIndex)
    {
        if (modes[modeIndex].Width == desktop.Width && modes[modeIndex].Height == desktop.Height)
        {
            *width = (int)desktop.Width;
            *height = (int)desktop.Height;
            return;
        }
    }
}

bool R_KeepFullscreenAtDesktop(bool atDesktop, bool fullscreen, int mode, int *atDesktopMode)
{
    if (!atDesktop || !fullscreen)
        return false;
    if (*atDesktopMode < 0)
        *atDesktopMode = mode;
    return *atDesktopMode == mode;
}

const char *R_FirstFullRestartDvar(const char *const *latchedNames, uint32_t count)
{
    static constexpr std::string_view displayModeDvars[] = {
        "r_mode", "r_fullscreen", "r_displayRefresh", "r_aspectRatio", "r_customMode", "r_vsync"};
    const auto sameName = [](std::string_view a, std::string_view b) {
        return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
            return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
        });
    };
    const char *const *other = std::find_if(latchedNames, latchedNames + count, [&](const char *name) {
        return std::none_of(std::begin(displayModeDvars), std::end(displayModeDvars),
                            [&](std::string_view dvar) { return sameName(dvar, name); });
    });
    return other == latchedNames + count ? nullptr : *other;
}

bool R_CanSwitchModeInPlace(const char *const *latchedNames, uint32_t count, bool fullscreenChanged)
{
    if (!count && !fullscreenChanged)
        return false;
    return !R_FirstFullRestartDvar(latchedNames, count);
}

bool R_IsWindowedMove(bool fullscreen, bool windowFullscreen)
{
    return !fullscreen && !windowFullscreen;
}
