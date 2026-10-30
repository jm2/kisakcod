// R_FindDisplayModeRefreshRate (gfx_d3d/r_display_modes.cpp): the refresh-rate
// lookup that R_ClosestRefreshRateForMode runs over dx.displayModes when the
// window is set up. The decompiled lookup read Height and RefreshRate through
// negative indexes into the pointer table that follows dx.displayModes. That
// only worked while pointers were 4 bytes, and it crashed the Win64 client in
// R_EnumDisplayModes. The modes here are sorted as R_CompareDisplayModes sorts
// them. R_ChooseFullscreenSize, which R_SetWndParms calls, picks the desktop
// mode for fullscreen entered with Alt+Enter.

#include <cstdio>

#include <gfx_d3d/r_display_modes.h>

namespace
{
int g_failures = 0;
int g_asserts = 0;

void Expect(int got, int want, const char *what)
{
    if (got != want)
    {
        std::printf("FAIL: %s: got %d, want %d\n", what, got, want);
        ++g_failures;
    }
}

_D3DDISPLAYMODE Mode(UINT width, UINT height, UINT refreshRate)
{
    _D3DDISPLAYMODE mode{};
    mode.Width = width;
    mode.Height = height;
    mode.RefreshRate = refreshRate;
    mode.Format = D3DFMT_X8R8G8B8;
    return mode;
}
}

void MyAssertHandler(const char *, int, int, const char *, ...)
{
    ++g_asserts;
}

int main()
{
    const _D3DDISPLAYMODE modes[] = {
        Mode(640, 480, 60),   Mode(640, 480, 75),   Mode(800, 600, 60),
        Mode(800, 600, 75),   Mode(1024, 768, 60),  Mode(1024, 768, 85),
        Mode(1920, 1080, 60), Mode(1920, 1080, 144),
    };
    const uint32_t count = sizeof(modes) / sizeof(modes[0]);

    Expect(R_FindDisplayModeRefreshRate(modes, count, 800, 600, 75), 75, "exact 800x600@75");
    Expect(R_FindDisplayModeRefreshRate(modes, count, 640, 480, 60), 60, "exact first mode");
    Expect(R_FindDisplayModeRefreshRate(modes, count, 1920, 1080, 144), 144, "exact last mode");
    Expect(R_FindDisplayModeRefreshRate(modes, count, 800, 600, 70), 60, "800x600@70 takes the fastest rate below");
    Expect(R_FindDisplayModeRefreshRate(modes, count, 1024, 768, 120), 85, "1024x768@120 takes the fastest rate below");
    Expect(R_FindDisplayModeRefreshRate(modes, count, 800, 600, 50), 60, "800x600@50 takes the slowest rate above");
    Expect(R_FindDisplayModeRefreshRate(modes, count, 640, 480, 30), 60, "first mode below its slowest rate");
    Expect(R_FindDisplayModeRefreshRate(modes, count, 1920, 1080, 240), 144, "last mode above its fastest rate");
    Expect(g_asserts, 0, "no asserts for listed resolutions");

    Expect(R_FindDisplayModeRefreshRate(modes, count, 1280, 720, 60), 60, "unlisted resolution keeps the request");
    Expect(g_asserts, 1, "an unlisted resolution asserts once");

    // R_ChooseFullscreenSize: R_SetWndParms starts from r_mode's size (here
    // 800x600) and passes the adapter's current desktop mode (#917).
    const auto Fullscreen = [&](bool atDesktop, UINT desktopWidth, UINT desktopHeight, int wantWidth, int wantHeight,
                                const char *what) {
        int width = 800;
        int height = 600;
        R_ChooseFullscreenSize(modes, count, atDesktop, Mode(desktopWidth, desktopHeight, 60), &width, &height);
        Expect(width, wantWidth, what);
        Expect(height, wantHeight, what);
    };
    Fullscreen(true, 1920, 1080, 1920, 1080, "Alt+Enter takes the desktop mode");
    Fullscreen(false, 1920, 1080, 800, 600, "menu fullscreen keeps r_mode's size");
    Fullscreen(true, 1280, 720, 800, 600, "an unlisted desktop mode keeps r_mode's size");
    Fullscreen(true, 0, 0, 800, 600, "an empty desktop mode keeps r_mode's size");

    // R_KeepFullscreenAtDesktop over the window restarts R_SetWndParms sees
    // (r_mode is an enum index). On yoga, "r_mode 1920x1200; vid_restart"
    // after Alt+Enter restarted at the desktop size (#950).
    bool atDesktop = false;
    int atDesktopMode = -1;
    const auto AltEnter = [&]() {
        atDesktop = true; // R_SetFullscreenAtDesktop(true)
        atDesktopMode = -1;
    };
    const auto Restart = [&](bool fullscreen, int mode) {
        atDesktop = R_KeepFullscreenAtDesktop(atDesktop, fullscreen, mode, &atDesktopMode);
        return atDesktop ? 1 : 0;
    };
    AltEnter();
    Expect(Restart(true, 4), 1, "Alt+Enter's restart takes the desktop");
    Expect(Restart(true, 4), 1, "a restart at the same r_mode keeps the desktop");
    Expect(Restart(true, 6), 0, "a new r_mode ends the desktop fullscreen");
    Expect(Restart(true, 4), 0, "and it stays ended");
    AltEnter();
    Expect(Restart(false, 4), 0, "a windowed restart ends it");
    Expect(Restart(true, 4), 0, "menu fullscreen after that keeps r_mode");
    AltEnter();
    Expect(Restart(true, 6), 1, "Alt+Enter after a windowed r_mode change takes the desktop");

    if (g_failures)
        return 1;
    std::printf("renderer-display-modes: all checks passed\n");
    return 0;
}
