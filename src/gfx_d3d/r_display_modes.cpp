#include "r_display_modes.h"

#include <universal/assertive.h>

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
        int comparison = (int)(modes[mid].Width - width);
        if (!comparison)
        {
            comparison = (int)(modes[mid].Height - height);
            if (!comparison)
            {
                comparison = (int)modes[mid].RefreshRate - refreshRate;
                if (!comparison)
                    return refreshRate;
            }
        }
        if (comparison >= 0)
            top = mid - 1;
        else
            bot = mid + 1;
    }
    if (top >= 0 && modes[top].Width == width && modes[top].Height == height)
        return (int)modes[top].RefreshRate;
    if (bot < (int)modeCount && modes[bot].Width == width && modes[bot].Height == height)
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
