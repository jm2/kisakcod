// IN_* mouse layer for the SDL3 client backend (KISAK_CLIENT_SDL3). It takes
// the place of win32/win_input.cpp with the same entry points and the same
// order of decisions, so Windows and the POSIX client share one input path.
// Motion still enters the engine only through CL_MouseEvent
// (docs/design/CLIENT.md, usercmd invariance).

#include "cl_sdl3.h"

#include <SDL3/SDL.h>

#include <client/client.h>
#include <qcommon/qcommon.h>
#include <qcommon/sys_local.h>
#include <ui/keycodes.h>

#ifdef KISAK_MP
#include <client_mp/client_mp.h>
#endif

#ifdef _WIN32
#include <win32/win_local.h>
#endif

int CL_MouseEvent(int x, int y, int dx, int dy);

const dvar_t *in_mouse;

namespace
{
constexpr int kMouseButtons = 5;

bool s_mouseInitialized;
bool s_mouseActive;
bool s_appActive;
int s_oldButtonState;

void IN_StartupMouse()
{
    s_mouseInitialized = in_mouse->current.enabled;
    if (!s_mouseInitialized)
        Com_Printf(16, "Mouse control not active.\n");
}

void IN_MouseMove()
{
    if (!CL_SdlHasFocus())
        return;
    int x;
    int y;
    int dx;
    int dy;
    CL_SdlTakeMouseMotion(&x, &y, &dx, &dy);
    // Nonzero means the game, not a menu, owns the mouse: Win32 then recentres
    // the cursor each move, which relative mode does for us.
    const int captured = CL_MouseEvent(x, y, dx, dy);
#ifdef _WIN32
    g_wv.recenterMouse = captured;
#endif
    CL_SdlSetMouseCaptured(captured && s_mouseActive);
}
} // namespace

void IN_Init()
{
    in_mouse = Dvar_RegisterBool("in_mouse", 1, DVAR_ARCHIVE | DVAR_LATCH, "Initialize the mouse drivers");
    IN_StartupMouse();
    Dvar_ClearModified((dvar_s *)in_mouse);
}

void IN_DeactivateMouse()
{
    if (s_mouseInitialized && s_mouseActive)
    {
        s_mouseActive = false;
        CL_SdlSetMouseCaptured(false);
    }
}

void IN_Shutdown()
{
    IN_DeactivateMouse();
}

bool IN_IsForegroundWindow()
{
    return CL_SdlHasFocus();
}

void IN_SetForegroundWindow()
{
    CL_SdlRaiseWindow();
}

void IN_ActivateMouse(qboolean force)
{
    if (!s_mouseInitialized)
        return;
    if (!in_mouse->current.enabled)
        s_mouseActive = false;
    else if (force || !s_mouseActive)
        s_mouseActive = IN_IsForegroundWindow();
}

void IN_Activate(qboolean active)
{
    s_appActive = active != 0;
    if (s_appActive)
        IN_ActivateMouse(1);
    else
        IN_DeactivateMouse();
}

void IN_MouseEvent(int mstate)
{
    if (!s_mouseInitialized || mstate == s_oldButtonState)
        return;
    const int changed = s_oldButtonState ^ mstate;
    for (int button = 0; button < kMouseButtons; ++button)
    {
        if (changed & (1 << button))
            Sys_QueEvent(0, SE_KEY, K_MOUSE1 + button, (mstate & (1 << button)) != 0, 0, nullptr);
    }
    s_oldButtonState = mstate;
}

void IN_Frame()
{
    // win_input.cpp posts a left-button-down message for this dvar.
    if (Dvar_GetBool("ClickToContinue"))
        IN_MouseEvent(1);
    if (!s_mouseInitialized)
        return;
    if (s_appActive)
    {
        IN_ActivateMouse(0);
        IN_MouseMove();
    }
    else
    {
        IN_DeactivateMouse();
    }
}

void IN_ShowSystemCursor(int show)
{
    if (show)
        SDL_ShowCursor();
    else
        SDL_HideCursor();
}

void IN_RecenterMouse()
{
    // Only MainWndProc's WM_DISPLAYCHANGE calls this, and relative mode
    // already keeps the cursor still.
}

#if defined(_WIN32) || defined(KISAK_DXVK_NATIVE)
// dxvk-native's windows_base.h supplies POINT off Windows.
void IN_SetCursorPos(POINT pos)
{
    CL_SdlWarpMouse(pos.x, pos.y);
}
#endif
