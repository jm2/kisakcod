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
