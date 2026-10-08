#include "r_display_modes.h"

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
