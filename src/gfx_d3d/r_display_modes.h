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

// Whether Alt+Enter's desktop fullscreen still holds for this window restart.
// A windowed restart ends it. The first fullscreen restart after Alt+Enter
// records r_mode in *atDesktopMode (-1 until then); a later restart with a
// different r_mode, from the options menu or the console, ends it (#950).
bool R_KeepFullscreenAtDesktop(bool atDesktop, bool fullscreen, int mode, int *atDesktopMode);

// Whether a vid_restart can switch the display mode in place instead of
// restarting the renderer, cgame and the map (#953). latchedNames lists every
// dvar whose latched value differs from its current one. Alt+Enter sets
// r_fullscreen directly, so nothing is latched; fullscreenChanged says
// r_fullscreen differs from the window's mode. The switch applies when there
// is a change and each latched dvar sets the display mode: r_mode,
// r_fullscreen, r_displayRefresh, r_aspectRatio, r_customMode or r_vsync.
bool R_CanSwitchModeInPlace(const char *const *latchedNames, uint32_t count, bool fullscreenChanged);
