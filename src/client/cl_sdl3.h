#pragma once

// SDL3 client backend (KISAK_CLIENT_SDL3, docs/design/CLIENT.md): the game
// window and its event pump. It stands in for CreateWindowExA/MainWndProc so
// the same window and input path serves Windows, Linux and macOS. On Windows
// the D3D9 device still gets a real HWND; elsewhere the handle is the
// SDL_Window itself, which is what dxvk-native's SDL3 WSI expects.

struct GfxWindowParms;

// Creates the game window and stores its native handle in wndParms->hwnd.
// borderless is r_noborder for a windowed mode.
bool CL_SdlCreateWindow(GfxWindowParms *wndParms, bool borderless);
void CL_SdlShowWindow(void *nativeWindow);
void CL_SdlDestroyWindow(void *nativeWindow);

// Drains SDL's queue into Sys_QueEvent, IN_MouseEvent and VID_AppActivate.
// On Windows SDL also dispatches the thread's other windows (console, splash).
// Returns false while there is no game window, when SDL pumps nothing and the
// caller must run the platform's own message loop.
bool CL_SdlPumpEvents();

// Mouse state for cl_sdl3_input.cpp's IN_* layer.
bool CL_SdlHasFocus();
void CL_SdlRaiseWindow();
// Window-relative cursor position and the whole-pixel motion since the last
// call. In captured (relative) mode the cursor is hidden and confined, as the
// Win32 build's recentring does.
void CL_SdlTakeMouseMotion(int *x, int *y, int *dx, int *dy);
void CL_SdlSetMouseCaptured(bool captured);
void CL_SdlWarpMouse(int x, int y);
