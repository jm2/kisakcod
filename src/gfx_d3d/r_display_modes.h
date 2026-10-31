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

// Whether a move of the game window is the windowed position that
// MainWndProc's WM_MOVE, and the SDL3 window-moved event, keep in vid_xpos
// and vid_ypos for the next window (#946). windowFullscreen is the mode the
// renderer last gave the window (vidConfig.isFullscreen). Retail checks only
// r_fullscreen, but Alt+Enter clears it before the vid_restart that makes the
// fullscreen window a window, and that window's moves to (0, 0) in between
// put the next window there.
bool R_IsWindowedMove(bool fullscreen, bool windowFullscreen);

// Whether the frame being built is the one an in-place display mode switch
// left behind, clearing *dropFrame. The switch resets the device after
// R_ToggleSmpFrame gave that frame its buffers, and the reset releases them:
// its pretess index buffer is null until the next frame (#981).
// R_IssueRenderCommands drops it, as it drops the frame R_CheckLostDevice
// recovers a lost device in.
bool R_TakeFrameToDrop(bool *dropFrame);

// The first of latchedNames that doesn't set the display mode, so the restart
// must be the full one; null when every latched dvar sets it. vid_restart
// logs it when it falls back.
const char *R_FirstFullRestartDvar(const char *const *latchedNames, uint32_t count);
