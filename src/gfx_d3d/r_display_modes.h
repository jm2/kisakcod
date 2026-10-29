#pragma once
#include <cstdint>

// _D3DDISPLAYMODE comes from the Windows SDK's d3d9.h.
#include "r_d3d9types.h"

// The refresh rate to use for width x height, searching modes sorted by
// R_CompareDisplayModes (width, then height, then refresh rate). An exact
// match returns refreshRate. Otherwise the result is the fastest listed rate
// below it, or, if no rate is below it, the slowest rate above it. The mode
// list must contain width x height.
int R_FindDisplayModeRefreshRate(
    const _D3DDISPLAYMODE *modes,
    uint32_t modeCount,
    uint32_t width,
    uint32_t height,
    int refreshRate);

// The size R_SetWndParms gives a fullscreen window (#917). Fullscreen entered
// with Alt+Enter (atDesktop) takes the desktop mode when the mode list has it;
// otherwise r_mode's size stands. r_mode is never rewritten, so leaving
// fullscreen restores the windowed size.
void R_ChooseFullscreenSize(
    const _D3DDISPLAYMODE *modes,
    uint32_t modeCount,
    bool atDesktop,
    const _D3DDISPLAYMODE &desktop,
    int *width,
    int *height);
